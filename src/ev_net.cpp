#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <raids/ev_net.h>
#include <raids/ev_service.h>
#include <raids/ev_client.h>
#include <raids/ev_http.h>
#include <raids/ev_nats.h>
#include <raids/ev_publish.h>

using namespace rai;
using namespace ds;
using namespace kv;

int
EvPoll::init( int numfds,  bool prefetch,  bool single )
{
  size_t sz = sizeof( this->ev[ 0 ] ) * numfds;

  if ( prefetch )
    this->prefetch_queue = EvPrefetchQueue::create();
  this->single_thread = single;

  if ( (this->efd = ::epoll_create( numfds )) < 0 ) {
    perror( "epoll" );
    return -1;
  }
  this->nfds = numfds;
  this->ev   = (struct epoll_event *) aligned_malloc( sz );
  if ( this->ev == NULL ) {
    perror( "malloc" );
    return -1;
  }
  return 0;
}

int
EvPoll::wait( int ms )
{
  int n = ::epoll_wait( this->efd, this->ev, this->nfds, ms );
  if ( n < 0 ) {
    if ( errno == EINTR )
      return 0;
    perror( "epoll_wait" );
    return -1;
  }
  for ( int i = 0; i < n; i++ ) {
    EvSocket *s = this->sock[ this->ev[ i ].data.fd ];
    if ( ( this->ev[ i ].events & ( EPOLLIN | EPOLLRDHUP ) ) != 0 )
      s->idle_push( s->type != EV_LISTEN_SOCK ? EV_READ : EV_READ_HI );
    if ( ( this->ev[ i ].events & ( EPOLLOUT ) ) != 0 )
      s->idle_push( EV_WRITE );
  }
  return n;
}

bool
EvPoll::dispatch( void )
{
  EvSocket *s;
  for ( uint64_t start = this->prio_tick++; start + 1000 > this->prio_tick;
        this->prio_tick++ ) {
    if ( this->quit )
      this->process_quit();
    if ( this->queue.is_empty() )
      return true;
    s = this->queue.pop();
    switch ( __builtin_ffs( s->state ) - 1 ) {
      case EV_READ:
      case EV_READ_LO:
      case EV_READ_HI:
        switch ( s->type ) {
          case EV_REDIS_SOCK:  ((EvRedisService *) s)->read(); break;
          case EV_HTTP_SOCK:   ((EvHttpService *) s)->read(); break;
          case EV_LISTEN_SOCK: ((EvListen *) s)->accept(); break;
          case EV_CLIENT_SOCK: ((EvClient *) s)->read(); break;
          case EV_TERMINAL:    ((EvTerminal *) s)->read(); break;
          case EV_NATS_SOCK:   ((EvNatsService *) s)->read(); break;
        }
        break;
      case EV_PROCESS:
        switch ( s->type ) {
          case EV_REDIS_SOCK:  ((EvRedisService *) s)->process( false ); break;
          case EV_HTTP_SOCK:   ((EvHttpService *) s)->process( false ); break;
          case EV_LISTEN_SOCK: break;
          case EV_CLIENT_SOCK: ((EvClient *) s)->process(); break;
          case EV_TERMINAL:    ((EvTerminal *) s)->process(); break;
          case EV_NATS_SOCK:   ((EvNatsService *) s)->process( false ); break;
        }
        break;
      case EV_WRITE:
      case EV_WRITE_HI:
        switch ( s->type ) {
          case EV_REDIS_SOCK:  ((EvRedisService *) s)->write(); break;
          case EV_HTTP_SOCK:   ((EvHttpService *) s)->write(); break;
          case EV_LISTEN_SOCK: break;
          case EV_CLIENT_SOCK: ((EvClient *) s)->write(); break;
          case EV_TERMINAL:    ((EvTerminal *) s)->write(); break;
          case EV_NATS_SOCK:   ((EvNatsService *) s)->write(); break;
        }
        break;
      case EV_CLOSE:
        s->popall();
        this->remove_sock( s );
        switch ( s->type ) {
          case EV_REDIS_SOCK:  ((EvRedisService *) s)->process_close(); break;
          case EV_HTTP_SOCK:   ((EvHttpService *) s)->process_close(); break;
          case EV_LISTEN_SOCK: break;
          case EV_CLIENT_SOCK: ((EvClient *) s)->process_close(); break;
          case EV_TERMINAL:    ((EvTerminal *) s)->process_close(); break;
          case EV_NATS_SOCK:   ((EvNatsService *) s)->process_close(); break;
        }
        break;
    }
    if ( s->state != 0 ) {
      s->prio_cnt = this->prio_tick;
      this->queue.push( s );
    }
  }
  return false;
#if 0
    use_pref = ( this->prefetch_queue != NULL &&
                 this->queue[ EV_PROCESS ].cnt > 1 );
    if ( this->prefetch_queue != NULL && ! this->prefetch_queue->is_empty() )
      this->drain_prefetch( *this->prefetch_queue );
#endif
}

bool
RoutePublish::publish( EvPublish &pub )
{
  EvPoll & poll = static_cast<EvPoll &>( *this );
  EvSocket *s;
  bool flow_good = true;
  for ( uint32_t i = 0; i < pub.rcount; i++ ) {
    if ( pub.routes[ i ] <= (uint32_t) poll.maxfd &&
         (s = poll.sock[ pub.routes[ i ] ]) != NULL ) {
      switch ( s->type ) {
        case EV_REDIS_SOCK:
          flow_good &= ((EvRedisService *) s)->publish( pub );
          break;
        case EV_HTTP_SOCK:
          flow_good &= ((EvHttpService *) s)->publish( pub );
          break;
	case EV_LISTEN_SOCK:  break;
        case EV_CLIENT_SOCK:
          flow_good &= ((EvClient *) s)->publish( pub );
          break;
        case EV_TERMINAL:
          flow_good &= ((EvTerminal *) s)->publish( pub );
          break;
        case EV_NATS_SOCK:
          flow_good &= ((EvNatsService *) s)->publish( pub );
          break;
      }
    }
  }
  return flow_good;
}

bool
RoutePublish::hash_to_sub( uint32_t r,  uint32_t h,  char *key,
                           size_t &keylen )
{
  EvPoll & poll = static_cast<EvPoll &>( *this );
  EvSocket *s;
  if ( r <= (uint32_t) poll.maxfd && (s = poll.sock[ r ]) != NULL ) {
    switch ( s->type ) {
      case EV_REDIS_SOCK:
        return ((EvRedisService *) s)->hash_to_sub( h, key, keylen );
      case EV_HTTP_SOCK:
        return ((EvHttpService *) s)->hash_to_sub( h, key, keylen );
      case EV_LISTEN_SOCK:  break;
      case EV_CLIENT_SOCK:
        return ((EvClient *) s)->hash_to_sub( h, key, keylen );
      case EV_TERMINAL:
        return ((EvTerminal *) s)->hash_to_sub( h, key, keylen );
      case EV_NATS_SOCK:
        return ((EvNatsService *) s)->hash_to_sub( h, key, keylen );
    }
  }
  return false;
}

bool
EvSocket::publish( EvPublish & )
{
  fprintf( stderr, "no publish defined for type %d\n", this->type );
  return false;
}

bool
EvSocket::hash_to_sub( uint32_t,  char *,  size_t & )
{
  fprintf( stderr, "no hash_to_sub defined for type %d\n", this->type );
  return false;
}

void
EvPoll::drain_prefetch( EvPrefetchQueue &q )
{
  RedisKeyCtx * ctx[ PREFETCH_SIZE ];
  EvSocket    * svc;
  size_t i, j, sz, cnt = 0;

  sz = PREFETCH_SIZE;
  if ( sz > q.count() )
    sz = q.count();
  this->prefetch_cnt[ sz ]++;
  for ( i = 0; i < sz; i++ ) {
    ctx[ i ] = q.pop();
    ctx[ i ]->prefetch();
  }
  i &= ( PREFETCH_SIZE - 1 );
  for ( j = 0; ; ) {
    switch ( ctx[ j ]->run( svc ) ) {
      default:
      case EXEC_SUCCESS:  /* transaction complete, all keys done */
        switch ( svc->type ) {
          case EV_REDIS_SOCK:  ((EvRedisService *) svc)->process( true ); break;
          case EV_HTTP_SOCK:   ((EvHttpService *) svc)->process( true ); break;
          case EV_LISTEN_SOCK: break;
          case EV_CLIENT_SOCK: break;
          case EV_TERMINAL:    break;
          case EV_NATS_SOCK:   ((EvNatsService *) svc)->process( true ); break;
        }
        break;
      case EXEC_DEPENDS:   /* incomplete, depends on another key */
        q.push( ctx[ j ] );
        break;
      case EXEC_CONTINUE:  /* key complete, more keys to go */
        break;
    }
    cnt++;
    if ( --sz == 0 && q.is_empty() ) {
      this->prefetch_cnt[ 0 ] += cnt;
      return;
    }
    j = ( j + 1 ) & ( PREFETCH_SIZE - 1 );
    if ( ! q.is_empty() ) {
      do {
        ctx[ i ] = q.pop();
        ctx[ i ]->prefetch();
        i = ( i + 1 ) & ( PREFETCH_SIZE - 1 );
      } while ( ++sz < PREFETCH_SIZE && ! q.is_empty() );
    }
  }
}

void
EvPoll::process_quit( void )
{
  if ( this->quit ) {
    EvSocket *s = this->active_list.hd;
    if ( s == NULL ) { /* no more sockets open */
      this->quit = 5;
      return;
    }
    /* wait for socks to flush data for up to 5 interations */
    do {
      if ( ! s->test( EV_WRITE ) || this->quit >= 5 ) {
        if ( s->state != 0 ) {
          this->queue.remove( s ); /* close state */
          s->popall();
          s->push( EV_CLOSE );
          this->queue.push( s );
        }
      }
    } while ( (s = s->next) != NULL );
    this->quit++;
  }
}

int
EvPoll::add_sock( EvSocket *s )
{
  /* make enough space for fd */
  if ( s->fd > this->maxfd ) {
    int xfd = align<int>( s->fd + 1, EvPoll::ALLOC_INCR );
    EvSocket **tmp;
    if ( xfd < this->nfds )
      xfd = this->nfds;
  try_again:;
    tmp = (EvSocket **)
          ::realloc( this->sock, xfd * sizeof( this->sock[ 0 ] ) );
    if ( tmp == NULL ) {
      perror( "realloc" );
      xfd /= 2;
      if ( xfd > s->fd )
        goto try_again;
      return -1;
    }
    for ( int i = this->maxfd + 1; i < xfd; i++ )
      tmp[ i ] = NULL;
    this->sock  = tmp;
    this->maxfd = xfd - 1;
  }
  /* add to poll set */
  struct epoll_event event;
  ::memset( &event, 0, sizeof( struct epoll_event ) );
  event.data.fd = s->fd;
  event.events  = EPOLLIN | EPOLLRDHUP | EPOLLET;
  if ( ::epoll_ctl( this->efd, EPOLL_CTL_ADD, s->fd, &event ) < 0 ) {
    perror( "epoll_ctl" );
    return -1;
  }
  this->sock[ s->fd ] = s;
  this->fdcnt++;
  /* add to active list */
  s->listfl = IN_ACTIVE_LIST;
  this->active_list.push_tl( s );
  /* if sock starts in write mode, add it to the queue */
  s->prio_cnt = this->prio_tick;
  if ( s->state != 0 )
    this->queue.push( s );
  return 0;
}

void
EvPoll::remove_sock( EvSocket *s )
{
  struct epoll_event event;
  /* remove poll set */
  if ( s->fd <= this->maxfd && this->sock[ s->fd ] == s ) {
    ::memset( &event, 0, sizeof( struct epoll_event ) );
    event.data.fd = s->fd;
    event.events  = 0;
    if ( ::epoll_ctl( this->efd, EPOLL_CTL_DEL, s->fd, &event ) < 0 )
      perror( "epoll_ctl" );
    this->sock[ s->fd ] = NULL;
    this->fdcnt--;
  }
  ::close( s->fd );
  if ( s->listfl == IN_ACTIVE_LIST ) {
    s->listfl = IN_NO_LIST;
    this->active_list.pop( s );
  }
  /* release memory buffers */
  if ( s->type != EV_LISTEN_SOCK )
    ((EvConnection *) s)->release();
}

int
EvTerminal::start( int sock )
{
  this->fd = sock; /* usually stdin fd */
  ::fcntl( sock, F_SETFL, O_NONBLOCK | ::fcntl( sock, F_GETFL ) );
  return this->poll.add_sock( this );
}

bool
EvConnection::read( void )
{
  this->adjust_recv();
  for (;;) {
    if ( this->len < this->recv_size ) {
      ssize_t nbytes = ::read( this->fd, &this->recv[ this->len ],
                               this->recv_size - this->len );
      if ( nbytes > 0 ) {
        this->len += nbytes;
        this->nbytes_recv += nbytes;
        this->push( EV_PROCESS );
        /* if buf almost full, switch to low priority read */
        if ( this->len >= this->recv_highwater )
          this->pushpop( EV_READ_LO, EV_READ );
        else
          this->pushpop( EV_READ, EV_READ_LO );
        return true;
      }
      else { /* wait for epoll() to set EV_READ again */
        this->pop3( EV_READ, EV_READ_LO, EV_READ_HI );
        if ( nbytes < 0 ) {
          if ( errno != EINTR ) {
            if ( errno != EAGAIN ) {
              if ( errno != ECONNRESET )
                perror( "read" );
              this->popall();
              this->push( EV_CLOSE );
            }
          }
        }
        else if ( nbytes == 0 )
          this->push( EV_CLOSE );
        else if ( this->test( EV_WRITE ) )
          this->pushpop( EV_WRITE_HI, EV_WRITE );
      }
      return false;
    }
    /* allow draining of existing buf before resizing */
    if ( this->test( EV_READ ) ) {
      this->pushpop( EV_READ_LO, EV_READ );
      return false;
    }
    if ( ! this->resize_recv_buf() )
      return false;
  }
}

bool
EvConnection::resize_recv_buf( void )
{
  size_t newsz = this->recv_size * 2;
  if ( newsz != (size_t) (uint32_t) newsz )
    return false;
  void * ex_recv_buf = aligned_malloc( newsz );
  if ( ex_recv_buf == NULL )
    return false;
  ::memcpy( ex_recv_buf, &this->recv[ this->off ], this->len );
  this->len -= this->off;
  this->off  = 0;
  if ( this->recv != this->recv_buf )
    ::free( this->recv );
  this->recv = (char *) ex_recv_buf;
  this->recv_size = newsz;
  return true;
}

bool
EvConnection::try_read( void )
{
  /* XXX: check of write side is full and return false */
  this->adjust_recv();
  if ( this->len + 1024 >= this->recv_size )
    this->resize_recv_buf();
  return this->read();
}

size_t
EvConnection::write( void )
{
  struct msghdr h;
  ssize_t nbytes;
  size_t nb = 0;
  StreamBuf & strm = *this;
  if ( strm.sz > 0 )
    strm.flush();
  ::memset( &h, 0, sizeof( h ) );
  h.msg_iov    = &strm.iov[ strm.woff ];
  h.msg_iovlen = strm.idx - strm.woff;
  if ( h.msg_iovlen == 1 ) {
    nbytes = ::send( this->fd, h.msg_iov[ 0 ].iov_base,
                     h.msg_iov[ 0 ].iov_len, 0 );
  }
  else {
    nbytes = ::sendmsg( this->fd, &h, 0 );
    while ( nbytes < 0 && errno == EMSGSIZE ) {
      if ( (h.msg_iovlen /= 2) == 0 )
        break;
      nbytes = ::sendmsg( this->fd, &h, 0 );
    }
  }
  if ( nbytes > 0 ) {
    strm.wr_pending -= nbytes;
    this->nbytes_sent += nbytes;
    nb += nbytes;
    if ( strm.wr_pending == 0 ) {
      strm.reset();
      this->pop2( EV_WRITE, EV_WRITE_HI );
    }
    else {
      for (;;) {
        if ( (size_t) nbytes >= strm.iov[ strm.woff ].iov_len ) {
	  nbytes -= strm.iov[ strm.woff ].iov_len;
	  strm.woff++;
	  if ( nbytes == 0 )
	    break;
	}
	else {
	  char *base = (char *) strm.iov[ strm.woff ].iov_base;
	  strm.iov[ strm.woff ].iov_len -= nbytes;
	  strm.iov[ strm.woff ].iov_base = &base[ nbytes ];
	  break;
	}
      }
    }
    return nb;
  }
  if ( errno != EAGAIN && errno != EINTR ) {
    this->popall();
    this->push( EV_CLOSE );
    if ( errno != ECONNRESET )
      perror( "sendmsg" );
  }
  return 0;
}

size_t
EvConnection::try_write( void )
{
  StreamBuf & strm = *this;
  size_t nb = 0;
  if ( strm.woff < strm.idx )
    nb = this->write();
  if ( strm.woff > strm.vlen / 2 ) {
    uint32_t i = 0;
    while ( strm.woff < strm.vlen )
      strm.iov[ i++ ] = strm.iov[ strm.woff++ ];
    strm.woff = 0;
    strm.idx  = i;
  }
  return nb;
}

void
EvConnection::close_alloc_error( void )
{
  fprintf( stderr, "Allocation failed! Closing connection\n" );
  this->popall();
  this->push( EV_CLOSE );
}


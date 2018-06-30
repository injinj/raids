#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <raids/ev_unix.h>
#include <raids/ev_service.h>

using namespace rai;
using namespace ds;

int
EvUnixListen::listen( const char *path )
{
  static int on = 1;
  int sock;
  struct sockaddr_un sunaddr;
  struct stat statbuf;

  sock = ::socket( PF_LOCAL, SOCK_STREAM, 0 );
  if ( sock < 0 ) {
    perror( "error: socket" );
    return -1;
  }
  sunaddr.sun_family = AF_LOCAL;
  if ( ::stat( path, &statbuf ) == 0 &&
       statbuf.st_size == 0 ) { /* make sure it's empty */
    ::unlink( path );
  }
  ::strncpy( sunaddr.sun_path, path, sizeof( sunaddr.sun_path ) );

  if ( ::setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof( on ) ) != 0 )
    perror( "warning: SO_REUSEADDR" );
  if ( ::bind( sock, (struct sockaddr *) &sunaddr, sizeof( sunaddr ) ) != 0 ) {
    perror( "error: bind" );
    goto fail;
  }
  if ( ::listen( sock, 128 ) != 0 ) {
    perror( "error: listen" );
    goto fail;
  }
  this->fd = sock;
  ::fcntl( sock, F_SETFL, O_NONBLOCK | ::fcntl( sock, F_GETFL ) );
  if ( this->add_poll() < 0 )
    goto fail;
  return 0;
fail:;
  ::close( sock );
  this->fd = -1;
  return -1;
}

void
EvUnixListen::accept( void )
{
  struct sockaddr_un sunaddr;
  socklen_t addrlen = sizeof( sunaddr );
  int sock = ::accept( this->fd, (struct sockaddr *) &sunaddr, &addrlen );
  if ( sock < 0 ) {
    if ( errno != EINTR ) {
      if ( errno != EAGAIN )
	perror( "accept" );
      this->pop( EV_READ );
    }
    return;
  }
  EvService * c;
  if ( (c = (EvService *) this->poll.free_svc) != NULL )
    c->pop_free_list();
  else {
    void * m = aligned_malloc( sizeof( EvService ) * EvPoll::ALLOC_INCR );
    if ( m == NULL ) {
      perror( "accept: no memory" );
      ::close( sock );
      return;
    }
    c = new ( m ) EvService( this->poll );
    for ( int i = EvPoll::ALLOC_INCR - 1; i >= 1; i-- ) {
      new ( (void *) &c[ i ] ) EvService( this->poll );
      c[ i ].push_free_list();
    }
  }
  ::fcntl( sock, F_SETFL, O_NONBLOCK | ::fcntl( sock, F_GETFL ) );
  c->fd = sock;
  if ( c->add_poll() < 0 ) {
    ::close( sock );
    c->push_free_list();
  }
}

int
EvUnixClient::connect( const char *path )
{
  int sock;
  struct sockaddr_un sunaddr;

  sock = ::socket( PF_LOCAL, SOCK_STREAM, 0 );
  if ( sock < 0 ) {
    perror( "error: socket" );
    return -1;
  }
  sunaddr.sun_family = AF_LOCAL;
  ::strncpy( sunaddr.sun_path, path, sizeof( sunaddr.sun_path ) );
  if ( ::connect( sock, (struct sockaddr *) &sunaddr,
                  sizeof( sunaddr ) ) != 0 ) {
    perror( "error: connect" );
    goto fail;
  }
  this->fd = sock;
  ::fcntl( sock, F_SETFL, O_NONBLOCK | ::fcntl( sock, F_GETFL ) );
  if ( this->add_poll() < 0 )
    goto fail;
  return 0;
fail:;
  this->fd = -1;
  ::close( sock );
  return -1;
}


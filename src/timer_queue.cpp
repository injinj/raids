#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <raids/timer_queue.h>
#include <raikv/util.h>

using namespace rai;
using namespace ds;
using namespace kv;

EvTimerQueue *
EvTimerQueue::create_timer_queue( EvPoll &p )
{
  int tfd;

  tfd = ::timerfd_create( CLOCK_MONOTONIC, TFD_NONBLOCK );
  if ( tfd == -1 ) {
    perror( "timerfd_create() failed" );
    return NULL;
  } 
  
  void * m = aligned_malloc( sizeof( EvTimerQueue ) );
  if ( m == NULL ) {
    perror( "alloc timer queue" );
    ::close( tfd );
    return NULL;
  }
  EvTimerQueue * q = new ( m ) EvTimerQueue( p );
  q->fd = tfd;
  q->last  = current_monotonic_time_ns();
  q->now   = q->last;
  q->delta = MAX_DELTA;
  if ( p.add_sock( q ) < 0 ) {
    printf( "failed to add timer %d\n", tfd );
    ::close( tfd );
    delete q;
    return NULL;
  }
  return q;
}

static const uint32_t to_ns[] = { 1000 * 1000 * 1000, 1000 * 1000, 1000, 1 };

bool
EvTimerQueue::add_timer_units( int id,  uint32_t ival,  TimerUnits u,
                               uint64_t timer_id,  uint64_t event_id )
{
  EvTimerEvent el;
  el.id          = id;
  el.ival        = ( ival << 2 ) | (uint32_t) u;
  el.timer_id    = timer_id;
  el.next_expire = current_monotonic_time_ns() +
                   ( (uint64_t) ival * (uint64_t) to_ns[ u ] );
  el.event_id    = event_id;
  if ( ! this->queue.push( el ) )
    return false;
  this->idle_push( EV_PROCESS );
  return true;
}

bool
EvTimerQueue::remove_timer( int id,  uint64_t timer_id,  uint64_t event_id )
{
  EvTimerEvent el;
  el.id          = id;
  el.ival        = 0;
  el.timer_id    = timer_id;
  el.next_expire = 0;
  el.event_id    = event_id;
  return this->queue.remove( el );
}

void
EvTimerQueue::repost( void )
{
  EvTimerEvent el = this->queue.pop();
  do {
    el.next_expire += (uint64_t) ( el.ival >> 2 ) *
                      (uint64_t) to_ns[ el.ival & 3 ];
  } while ( el.next_expire <= this->now );
  this->queue.push( el );
}

bool
EvTimerQueue::read( void )
{
  uint8_t buf[ 1024 ];
  size_t  total = 0;
  ssize_t n;
  for (;;) {
    n = ::read( this->fd, buf, sizeof( buf ) );
    if ( n < 0 ) {
      if ( errno != EINTR ) {
        if ( errno != EAGAIN ) {
          perror( "raed timer" );
          this->popall();
          this->push( EV_CLOSE );
        }
        else {
          this->pop3( EV_READ, EV_READ_LO, EV_READ_HI );
          this->push( EV_PROCESS );
        }
      }
      break;
    }
    total += n;
  }
  return total > 0;
}

bool
EvTimerQueue::set_timer( void )
{
  struct itimerspec ts;
  ts.it_interval.tv_sec = 0;
  ts.it_interval.tv_nsec = 0;
  ts.it_value.tv_sec  = this->delta / (uint64_t) 1000000000;
  ts.it_value.tv_nsec = this->delta % (uint64_t) 1000000000;

  if ( timerfd_settime( this->fd, 0, &ts, NULL ) < 0 ) {
    perror( "set timer" );
    return false;
  }
  return true;
}

void
EvTimerQueue::process( void )
{
  this->last = this->now;
  this->now  = current_monotonic_time_ns();

  while ( ! this->queue.is_empty() ) {
    EvTimerEvent &ev = this->queue.heap[ 0 ];
    if ( ev.next_expire <= this->now ) { /* timers are ready to fire */
      if ( this->poll.timer_expire( ev ) )
        this->repost();    /* next timer interval */
      else
        this->queue.pop(); /* remove timer */
    }
    else {
      this->delta = ev.next_expire - this->now;
      if ( ! this->set_timer() )
        return; /* probably need to exit, this retries later */
      break;
    }
  }
  this->pop( EV_PROCESS ); /* all timers that expired are processed */
}
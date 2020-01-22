#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <raids/ev_net.h>

using namespace rai;
using namespace ds;

int
EvUdp::listen( const char *ip,  int port,  const char *k )
{
  static int on = 1, off = 0;
  int  status = 0,
       sock;
  char svc[ 16 ];
  struct addrinfo hints, * ai = NULL, * p = NULL;

  ::snprintf( svc, sizeof( svc ), "%d", port );
  ::memset( &hints, 0, sizeof( struct addrinfo ) );
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags    = AI_PASSIVE;

  status = ::getaddrinfo( ip, svc, &hints, &ai );
  if ( status != 0 ) {
    perror( "getaddrinfo" );
    return -1;
  }
  sock = -1;
  /* try inet6 first, since it can listen to both ip stacks */
  for ( int fam = AF_INET6; ; ) {
    for ( p = ai; p != NULL; p = p->ai_next ) {
      if ( fam == p->ai_family ) {
	sock = ::socket( p->ai_family, SOCK_DGRAM, IPPROTO_UDP );
	if ( sock < 0 )
	  continue;
        if ( fam == AF_INET6 ) {
	  if ( ::setsockopt( sock, IPPROTO_IPV6, IPV6_V6ONLY, &off,
	                     sizeof( off ) ) != 0 )
	    perror( "warning: IPV6_V6ONLY" );
        }
	if ( ::setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, &on,
                           sizeof( on ) ) != 0 )
          perror( "warning: SO_REUSEADDR" );
	if ( ::setsockopt( sock, SOL_SOCKET, SO_REUSEPORT, &on,
                           sizeof( on ) ) != 0 )
          perror( "warning: SO_REUSEPORT" );
	status = ::bind( sock, p->ai_addr, p->ai_addrlen );
	if ( status == 0 )
	  goto break_loop;
	::close( sock );
        sock = -1;
      }
    }
    if ( fam == AF_INET ) /* tried both */
      break;
    fam = AF_INET;
  }
break_loop:;
  if ( status != 0 ) {
    perror( "error: bind" );
    goto fail;
  }
  if ( sock == -1 ) {
    fprintf( stderr, "error: failed to create a socket\n" );
    status = -1;
    goto fail;
  }
  this->PeerData::init_peer( sock, p->ai_addr, k );
  ::fcntl( sock, F_SETFL, O_NONBLOCK | ::fcntl( sock, F_GETFL ) );
  if ( (status = this->poll.add_sock( this )) < 0 ) {
    this->fd = -1;
fail:;
    if ( sock != -1 )
      ::close( sock );
  }
  if ( ai != NULL )
    ::freeaddrinfo( ai );
  return status;
}

int
EvUdp::connect( const char *ip,  int port )
{
  static int off = 0;
  int  status = 0,
       sock;
  char svc[ 16 ];
  struct addrinfo hints, * ai = NULL, * p = NULL;

  ::snprintf( svc, sizeof( svc ), "%d", port );
  ::memset( &hints, 0, sizeof( struct addrinfo ) );
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags    = AI_PASSIVE;

  status = ::getaddrinfo( ip, svc, &hints, &ai );
  if ( status != 0 ) {
    perror( "getaddrinfo" );
    return -1;
  }
  sock = -1;
  /* try inet6 first, since it can listen to both ip stacks */
  for ( int fam = AF_INET6; ; ) {
    for ( p = ai; p != NULL; p = p->ai_next ) {
      if ( fam == p->ai_family ) {
	sock = ::socket( p->ai_family, SOCK_DGRAM, IPPROTO_UDP );
	if ( sock < 0 )
	  continue;
        if ( fam == AF_INET6 ) {
	  if ( ::setsockopt( sock, IPPROTO_IPV6, IPV6_V6ONLY, &off,
	                     sizeof( off ) ) != 0 )
	    perror( "warning: IPV6_V6ONLY" );
        }
	status = ::connect( sock, p->ai_addr, p->ai_addrlen );
	if ( status == 0 )
	  goto break_loop;
	::close( sock );
        sock = -1;
      }
    }
    if ( fam == AF_INET ) /* tried both */
      break;
    fam = AF_INET;
  }
break_loop:;
  if ( status != 0 ) {
    perror( "error: connect" );
    goto fail;
  }
  if ( sock == -1 ) {
    fprintf( stderr, "error: failed to create a socket\n" );
    status = -1;
    goto fail;
  }
  this->PeerData::init_peer( sock, p->ai_addr, "udp_client" );
  ::fcntl( sock, F_SETFL, O_NONBLOCK | ::fcntl( sock, F_GETFL ) );
  if ( (status = this->poll.add_sock( this )) < 0 ) {
    this->fd = -1;
fail:;
    if ( sock != -1 )
      ::close( sock );
  }
  if ( ai != NULL )
    ::freeaddrinfo( ai );
  return status;
}


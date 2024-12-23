#ifndef __rai_raids__ev_http_h__
#define __rai_raids__ev_http_h__

#include <raikv/ev_tcp.h>
#include <raids/ev_tcp_ssl.h>
#include <raids/redis_exec.h>
#include <raids/term.h>
#include <raids/ws_frame.h>

namespace rai {
namespace ds {

struct EvHttpListen : public kv::EvTcpListen {
  kv::RoutePublish & sub_route;
  SSL_Context        ssl_ctx;
  void * operator new( size_t, void *ptr ) { return ptr; }
  EvHttpListen( kv::EvPoll &p, kv::RoutePublish &sr ) noexcept;
  EvHttpListen( kv::EvPoll &p ) noexcept;
  virtual EvSocket *accept( void ) noexcept;
  virtual int listen( const char *ip,  int port,  int opts ) noexcept {
    return this->kv::EvTcpListen::listen2( ip, port, opts, "http_listen",
                                           this->sub_route.route_id );
  }
};

struct EvPrefetchQueue;

struct HttpReq {
  static const size_t STR_SZ = 128;
  char         wsver[ STR_SZ ], wskey[ STR_SZ ], wspro[ STR_SZ ],
               content_type[ STR_SZ ];
  size_t       wskeylen,
               content_length,
               method_len,
               path_len,
               authorize_len;
  const char * method,
             * path,
             * data,
             * authorize;
  int          opts; /* parse the hdrs that are useful */

  enum Opts {
    HTTP_1_1   = 1, /* HTTP/1.1 found */
    UPGRADE    = 2, /* Connection: upgrade */
    KEEP_ALIVE = 4, /* Connection: keep-alive */
    CLOSE      = 8, /* Connection: close */
    WEBSOCKET  = 16 /* Upgrade: websocket */
  };

  HttpReq() : wskeylen( 0 ), content_length( 0 ), method_len( 0 ), path_len( 0 ),
              authorize_len( 0 ), method( 0 ), path( 0 ), data( 0 ),
              authorize( 0 ), opts( 0 ) {
    this->wsver[ 0 ] = this->wskey[ 0 ] = this->wspro[ 0 ] =
    this->content_type[ 0 ] = '\0';
  }
  bool parse_version( const char *line,  size_t len ) noexcept;
  void parse_header( const char *line,  size_t len ) noexcept;
  static size_t decode_uri( const char *s,  const char *e,  char *q,
                            size_t qlen ) noexcept;
};

struct HttpOut {
  const char * hdr[ 16 ]; /* calculate size of hdrs for alloc */
  size_t       len[ 16 ],
               off,
               size;
  void push( const char *s,  size_t l ) {
    this->hdr[ this->off ] = s;
    this->len[ this->off++ ] = l;
    this->size += l;
  }
  size_t cat( char *s ) {
    const char *t = s;
    for ( size_t i = 0; ; ) {
      size_t j = this->len[ i ];
      ::memcpy( s, this->hdr[ i++ ], j );
      s = &s[ j ];
      if ( i == this->off )
        return s - t;
    }
  }
};

struct WSMsg {
  char * inptr;
  size_t inoff,
         inlen,
         msgcnt,
         nlcnt;
};

struct HttpServerNonce;
struct HtDigestDB;
struct HttpDigestAuth;

struct EvHttpConnection : public SSL_Connection {
  char            * wsbuf;   /* decoded websocket frames */
  size_t            wsoff,   /* start offset of wsbuf */
                    wslen,   /* length of wsbuf used */
                    wsalloc, /* sizeof wsbuf alloc */
                    wsmsgcnt;
  uint64_t          websock_off;/* on output pointer that frames msgs with ws */
  HttpServerNonce * svr_nonce;
  HtDigestDB      * digest_db;
  int               term_int;
  bool              is_using_term;
  Term              term;

  EvHttpConnection( kv::EvPoll &p,  const uint8_t t )
    : SSL_Connection( p, t ), wsbuf( 0 ), wsoff( 0 ), wslen( 0 ),
      websock_off( 0 ), svr_nonce( 0 ), digest_db( 0 ), term_int( 0 ),
      is_using_term( false ) {}
  void initialize_state( HttpServerNonce *svr = NULL,  HtDigestDB *db = NULL ) {
    this->wsbuf         = NULL;
    this->wsoff         = 0;
    this->wslen         = 0;
    this->wsalloc       = 0;
    this->wsmsgcnt      = 0;
    this->websock_off   = 0;
    this->term_int      = 0;
    this->is_using_term = false;
    this->svr_nonce     = svr;
    this->digest_db     = db;
    this->term.zero();
  }
  void init_http_response( const HttpReq &hreq,  HttpOut &hout,
                           int opts,  int code ) noexcept;
  void send_404_not_found( const HttpReq &hreq,  int opts ) noexcept;
  void send_404_bad_type( const HttpReq &hreq ) noexcept;
  void send_201_created( const HttpReq &hreq ) noexcept;
  void send_401_unauthorized( const HttpReq &hreq,
                              HttpDigestAuth &auth ) noexcept;
  bool process_websock( void ) noexcept;
  bool process_http( void ) noexcept;
  bool flush_term( void ) noexcept;
  bool frame_websock( void ) noexcept;
  virtual bool frame_websock2( void ) noexcept;
  bool send_ws_upgrade( const HttpReq &wshdr ) noexcept;
  bool send_ws_pong( const char *payload,  size_t pay_len ) noexcept;
  size_t recv_wsframe( char *start,  char *end ) noexcept;
  static const char *get_mime_type( const char *path, size_t len,
                                    size_t &mlen,  bool &is_gzip ) noexcept;
  virtual bool process_get( const HttpReq &hreq ) noexcept;
  virtual bool process_get_file( const char *path,  size_t path_len ) noexcept;
  virtual void process_wsmsg( WSMsg &wmsg ) noexcept = 0;
  virtual bool process_post( const HttpReq &hreq ) noexcept;
  /* EvSocket */
  virtual void write( void ) noexcept;
  virtual void process( void ) noexcept;
  virtual void release( void ) noexcept;
};

struct EvHttpService : public EvHttpConnection, public RedisExec {
  void * operator new( size_t, void *ptr ) { return ptr; }

  EvHttpService( kv::EvPoll &p,  const uint8_t t,  kv::RoutePublish &sr )
    : EvHttpConnection( p, t ),
      RedisExec( *sr.map, sr.ctx_id, sr.dbx_id, *this, sr, *this, p.timer ) {}

  virtual bool process_get( const HttpReq &hreq ) noexcept;
  virtual void process_wsmsg( WSMsg &wmsg ) noexcept;
  virtual bool process_post( const HttpReq &hreq ) noexcept;
  virtual bool frame_websock2( void ) noexcept;
  /* EvSocket */
  virtual void process_close( void ) noexcept;
  virtual void release( void ) noexcept;
  virtual bool timer_expire( uint64_t tid, uint64_t eid ) noexcept;
  virtual bool hash_to_sub( uint32_t h, char *k, size_t &klen ) noexcept;
  virtual bool on_msg( kv::EvPublish &pub ) noexcept;
  virtual uint8_t is_subscribed( const kv::NotifySub &sub ) noexcept;
  virtual uint8_t is_psubscribed( const kv::NotifyPattern &pat ) noexcept;
  virtual void key_prefetch( kv::EvKeyCtx &ctx ) noexcept;
  virtual int  key_continue( kv::EvKeyCtx &ctx ) noexcept;
  /* PeerData */
  virtual int client_list( char *buf,  size_t buflen ) noexcept;
  virtual bool match( kv::PeerMatchArgs &ka ) noexcept;
};

}
}
#endif

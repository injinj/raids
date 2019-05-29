#ifndef __rai_raids__pattern_cvt_h__
#define __rai_raids__pattern_cvt_h__

namespace rai {
namespace ds {

struct PatternCvt {
  static const size_t MAX_PREFIX_LEN = 63; /* uint64_t bit mask */
  char * out;          /* utf8 or utf32 char classes */
  size_t off,          /* off will be > maxlen on buf space failure */
         prefixlen;    /* size of literal prefix */
  const size_t maxlen; /* max size of output */

  PatternCvt( char *o, size_t len )
    : out( o ), off( 0 ), prefixlen( 0 ), maxlen( len ) {}

  void char_out( char c ) {
    if ( ++this->off <= this->maxlen )
      this->out[ off - 1 ] = c;
  }

  void str_out( const char *s,  size_t len ) {
    size_t i = this->off;
    if ( (this->off += len) <= this->maxlen ) {
      for (;;) {
        this->out[ i++ ] = (char) (uint8_t) *s++;
        if ( --len == 0 )
          break;
      }
    }
  }
  /* return 0 on success or -1 on failure
   * normal glob rules:
   *  * = zero or more chars
   *  ? = zero or one char
   *  [xyz] = x | y | z
   *  [^xyz] = ! ( x | y | z ) */
  int convert_glob( const char *pattern,  size_t patlen ) {
    size_t k, j = 0;
    bool   inside_bracket,
           anchor_end = true;

    this->off = 0;
    if ( patlen > 0 ) {
      this->str_out( "(?s)\\A", 6 ); /* match nl & anchor start */
      if ( pattern[ patlen - 1 ] == '*' ) {
        if ( patlen == 1 || pattern[ patlen - 2 ] != '\\' ) {
          patlen -= 1; /* no need to match trail */
          anchor_end = false;
        }
      }
      j = patlen;
      inside_bracket = false;
      for ( k = 0; k < patlen; k++ ) {
        if ( pattern[ k ] == '\\' ) {
          if ( k + 1 < patlen ) {
            switch ( pattern[ ++k ] ) {
              case '\\': case '?': case '*': case '[': case ']': case '.':
                /* leave escape in these cases, chars have special meaning */
                this->char_out( '\\' );
                /* FALLTHRU */
              default:
                /* strip escape on others: \w \s \d are special to pcre */
                this->char_out( pattern[ k ] );
                break;
            }
          }
        }
        else if ( ! inside_bracket ) {
          switch ( pattern[ k ] ) {
            case '*':
              if ( j > k ) j = k;
              if ( k > 0 && pattern[ k - 1 ] == '*' )
                k++; /* skip duplicate '*' */
              else
                this->str_out( ".*?", 3 ); /* commit and star */
              break;
            case '?':
              if ( j > k ) j = k;
              this->char_out( '.' );
              break;
            case '.':
            case '+':
            case '(':
            case ')':
            case '{':
            case '}':
              this->char_out( '\\' );
              this->char_out( pattern[ k ] );
              break;
            case '[':
              if ( j > k ) j = k;
              inside_bracket = true;
              break;
            default:
              this->char_out( pattern[ k ] );
              break;
          }
        }
        else {
          if ( pattern[ k ] == ']' )
            inside_bracket = false;
          this->char_out( pattern[ k ] );
        }
      }
      if ( anchor_end )
        this->str_out( "\\z", 2 ); /* anchor at end */
    }
    this->prefixlen = j;
    if ( this->prefixlen > MAX_PREFIX_LEN )
      this->prefixlen = MAX_PREFIX_LEN;
    if ( this->off > this->maxlen )
      return -1;
    return 0;
  }

  /* rv/nats style wildcard:
   * * = [^.]+[.]  / match one segment
   * > = .+ / match one or more segments
   */
  int convert_nats( const char *pattern,  size_t patlen ) {
    size_t k, j = 0;
    bool   anchor_end = true;

    this->off = 0;
    if ( patlen > 0 ) {
      this->str_out( "(?s)\\A", 6 ); /* match nl & anchor start */
      if ( pattern[ patlen - 1 ] == '>' ) {
        if ( patlen == 1 || pattern[ patlen - 2 ] == '.' ) {
          patlen -= 1; /* no need to match trail */
          anchor_end = false;
        }
      }
      j = patlen;
      for ( k = 0; k < patlen; k++ ) {
        if ( pattern[ k ] == '*' ) {
          if ( j > k ) j = k;
          this->str_out( "[^.]+", 5 );
        }
        else if ( pattern[ k ] == '>' ) {
          if ( j > k ) j = k;
          this->str_out( ".+", 2 );
        }
        else if ( pattern[ k ] == '.' || pattern[ k ] == '?' ||
                  pattern[ k ] == '[' || pattern[ k ] == ']' ||
                  pattern[ k ] == '\\' || pattern[ k ] == '+' ) {
          this->char_out( '\\' );
          this->char_out( pattern[ k ] );
        }
        else {
          this->char_out( pattern[ k ] );
        }
      }
      if ( anchor_end )
        this->str_out( "\\z", 2 ); /* anchor at end */
    }
    this->prefixlen = j;
    if ( this->prefixlen > MAX_PREFIX_LEN )
      this->prefixlen = MAX_PREFIX_LEN;
    if ( this->off > this->maxlen )
      return -1;
    return 0;
  }
};

}
}

#endif
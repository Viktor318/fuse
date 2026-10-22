/* basic_text.c: Load and save BASIC programs as plain text
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

/* The text format follows zmakebas, so that files can be shared with it:

     10 PRINT "Hello"        one BASIC line per text line
     # comment               lines starting with # are ignored
     \                       a backslash at the end joins the next line

   Escapes, usable anywhere in a line:

     \\  backslash           \@  @                 \*  copyright sign
     \a .. \u  UDG A to U    \{n}  character code n (decimal, or 0x hex)
     \XY block graphics, X and Y one of space ' . : (left and right half)

   Keywords are recognised in capitals only (as LIST shows them), so
   variable names like `to' or `in' are kept. If no line has a number,
   the lines are numbered 10, 20, 30, ... Accented letters are replaced by
   the plain letter, as the Spectrum character set has none. */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "basic_text.h"
#include "fuse.h"
#include "keyboard.h"
#include "machine.h"
#include "memory_pages.h"
#include "ui/ui.h"
#include "utils.h"
#include "z80/z80.h"

/* ------------------------------------------------------------------------
   Keywords
   ------------------------------------------------------------------------ */

typedef struct keyword_t {
  const char *text;		/* A space matches any number of spaces */
  libspectrum_byte code;
} keyword_t;

static const keyword_t keywords[] = {
  { "SPECTRUM", 0xa3 }, { "PLAY", 0xa4 },
  { "RND", 0xa5 }, { "INKEY$", 0xa6 }, { "PI", 0xa7 }, { "FN", 0xa8 },
  { "POINT", 0xa9 }, { "SCREEN$", 0xaa }, { "ATTR", 0xab }, { "AT", 0xac },
  { "TAB", 0xad }, { "VAL$", 0xae }, { "CODE", 0xaf }, { "VAL", 0xb0 },
  { "LEN", 0xb1 }, { "SIN", 0xb2 }, { "COS", 0xb3 }, { "TAN", 0xb4 },
  { "ASN", 0xb5 }, { "ACS", 0xb6 }, { "ATN", 0xb7 }, { "LN", 0xb8 },
  { "EXP", 0xb9 }, { "INT", 0xba }, { "SQR", 0xbb }, { "SGN", 0xbc },
  { "ABS", 0xbd }, { "PEEK", 0xbe }, { "IN", 0xbf }, { "USR", 0xc0 },
  { "STR$", 0xc1 }, { "CHR$", 0xc2 }, { "NOT", 0xc3 }, { "BIN", 0xc4 },
  { "OR", 0xc5 }, { "AND", 0xc6 }, { "<=", 0xc7 }, { ">=", 0xc8 },
  { "<>", 0xc9 }, { "LINE", 0xca }, { "THEN", 0xcb }, { "TO", 0xcc },
  { "STEP", 0xcd }, { "DEF FN", 0xce }, { "CAT", 0xcf }, { "FORMAT", 0xd0 },
  { "MOVE", 0xd1 }, { "ERASE", 0xd2 }, { "OPEN #", 0xd3 },
  { "CLOSE #", 0xd4 }, { "MERGE", 0xd5 }, { "VERIFY", 0xd6 },
  { "BEEP", 0xd7 }, { "CIRCLE", 0xd8 }, { "INK", 0xd9 }, { "PAPER", 0xda },
  { "FLASH", 0xdb }, { "BRIGHT", 0xdc }, { "INVERSE", 0xdd },
  { "OVER", 0xde }, { "OUT", 0xdf }, { "LPRINT", 0xe0 }, { "LLIST", 0xe1 },
  { "STOP", 0xe2 }, { "READ", 0xe3 }, { "DATA", 0xe4 },
  { "RESTORE", 0xe5 }, { "NEW", 0xe6 }, { "BORDER", 0xe7 },
  { "CONTINUE", 0xe8 }, { "DIM", 0xe9 }, { "REM", 0xea }, { "FOR", 0xeb },
  { "GO TO", 0xec }, { "GO SUB", 0xed }, { "INPUT", 0xee }, { "LOAD", 0xef },
  { "LIST", 0xf0 }, { "LET", 0xf1 }, { "PAUSE", 0xf2 }, { "NEXT", 0xf3 },
  { "POKE", 0xf4 }, { "PRINT", 0xf5 }, { "PLOT", 0xf6 }, { "RUN", 0xf7 },
  { "SAVE", 0xf8 }, { "RANDOMIZE", 0xf9 }, { "IF", 0xfa }, { "CLS", 0xfb },
  { "DRAW", 0xfc }, { "CLEAR", 0xfd }, { "RETURN", 0xfe }, { "COPY", 0xff },
};

#define KEYWORD_COUNT ( sizeof( keywords ) / sizeof( keywords[0] ) )

#define TOKEN_REM    0xea
#define TOKEN_BIN    0xc4
#define TOKEN_DEF_FN 0xce
#define TOKEN_FIRST_48 0xa5	/* RND */
#define TOKEN_FIRST_128 0xa3	/* SPECTRUM */

#define NUMBER_MARKER 0x0e

static const char*
keyword_text( libspectrum_byte code )
{
  size_t i;

  for( i = 0; i < KEYWORD_COUNT; i++ )
    if( keywords[i].code == code ) return keywords[i].text;

  return NULL;
}

/* ------------------------------------------------------------------------
   Growable byte buffer
   ------------------------------------------------------------------------ */

typedef struct buffer_t {
  libspectrum_byte *data;
  size_t length, allocated;
} buffer_t;

static void
buffer_add( buffer_t *buffer, libspectrum_byte b )
{
  if( buffer->length == buffer->allocated ) {
    buffer->allocated = buffer->allocated ? buffer->allocated * 2 : 256;
    buffer->data = libspectrum_renew( libspectrum_byte, buffer->data,
                                      buffer->allocated );
  }
  buffer->data[ buffer->length++ ] = b;
}

static void
buffer_add_string( buffer_t *buffer, const char *s )
{
  while( *s ) buffer_add( buffer, (libspectrum_byte)*s++ );
}

/* ------------------------------------------------------------------------
   Character conversion
   ------------------------------------------------------------------------ */

/* Windows-1250 (Central European, used by Hungarian Windows), 0x80-0xff */
static const unsigned short cp1250[128] = {
  0x20ac, 0x0000, 0x201a, 0x0000, 0x201e, 0x2026, 0x2020, 0x2021,
  0x0000, 0x2030, 0x0160, 0x2039, 0x015a, 0x0164, 0x017d, 0x0179,
  0x0000, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
  0x0000, 0x2122, 0x0161, 0x203a, 0x015b, 0x0165, 0x017e, 0x017a,
  0x00a0, 0x02c7, 0x02d8, 0x0141, 0x00a4, 0x0104, 0x00a6, 0x00a7,
  0x00a8, 0x00a9, 0x015e, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x017b,
  0x00b0, 0x00b1, 0x02db, 0x0142, 0x00b4, 0x00b5, 0x00b6, 0x00b7,
  0x00b8, 0x0105, 0x015f, 0x00bb, 0x013d, 0x02dd, 0x013e, 0x017c,
  0x0154, 0x00c1, 0x00c2, 0x0102, 0x00c4, 0x0139, 0x0106, 0x00c7,
  0x010c, 0x00c9, 0x0118, 0x00cb, 0x011a, 0x00cd, 0x00ce, 0x010e,
  0x0110, 0x0143, 0x0147, 0x00d3, 0x00d4, 0x0150, 0x00d6, 0x00d7,
  0x0158, 0x016e, 0x00da, 0x0170, 0x00dc, 0x00dd, 0x0162, 0x00df,
  0x0155, 0x00e1, 0x00e2, 0x0103, 0x00e4, 0x013a, 0x0107, 0x00e7,
  0x010d, 0x00e9, 0x0119, 0x00eb, 0x011b, 0x00ed, 0x00ee, 0x010f,
  0x0111, 0x0144, 0x0148, 0x00f3, 0x00f4, 0x0151, 0x00f6, 0x00f7,
  0x0159, 0x016f, 0x00fa, 0x0171, 0x00fc, 0x00fd, 0x0163, 0x02d9,
};

/* Plain letters for U+00C0 - U+00FF and U+0100 - U+017F; '?' means none */
static const char latin1_letters[] =
  "AAAAAAACEEEEIIIIDNOOOOO?OUUUUYTsaaaaaaaceeeeiiiidnooooo?ouuuuyty";
static const char latin_extended_a_letters[] =
  "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlLlLl"
  "NnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";

/* Decode UTF-8 (or Windows-1250 if the text is not valid UTF-8) */
static unsigned int*
decode_text( const char *text, size_t length, size_t *count )
{
  const unsigned char *s = (const unsigned char*)text;
  unsigned int *result;
  size_t i, n = 0;
  int utf8 = 1;

  /* Skip a byte order mark */
  if( length >= 3 && s[0] == 0xef && s[1] == 0xbb && s[2] == 0xbf ) {
    s += 3; length -= 3;
  }

  /* Valid UTF-8? */
  for( i = 0; i < length && utf8; ) {
    size_t extra, j;
    if( s[i] < 0x80 ) { i++; continue; }
    else if( ( s[i] & 0xe0 ) == 0xc0 && s[i] >= 0xc2 ) extra = 1;
    else if( ( s[i] & 0xf0 ) == 0xe0 ) extra = 2;
    else if( ( s[i] & 0xf8 ) == 0xf0 && s[i] <= 0xf4 ) extra = 3;
    else { utf8 = 0; break; }
    if( i + extra >= length ) { utf8 = 0; break; }
    for( j = 1; j <= extra; j++ )
      if( ( s[i + j] & 0xc0 ) != 0x80 ) utf8 = 0;
    i += extra + 1;
  }

  result = libspectrum_new( unsigned int, length + 1 );

  for( i = 0; i < length; ) {
    unsigned int c = s[i];

    if( c < 0x80 ) {
      i++;
    } else if( !utf8 ) {
      c = cp1250[ c - 0x80 ]; if( !c ) c = 0xfffd;
      i++;
    } else if( c < 0xe0 ) {
      c = ( ( c & 0x1f ) << 6 ) | ( s[i + 1] & 0x3f ); i += 2;
    } else if( c < 0xf0 ) {
      c = ( ( c & 0x0f ) << 12 ) | ( ( s[i + 1] & 0x3f ) << 6 ) |
          ( s[i + 2] & 0x3f );
      i += 3;
    } else {
      c = ( ( c & 0x07 ) << 18 ) | ( ( s[i + 1] & 0x3f ) << 12 ) |
          ( ( s[i + 2] & 0x3f ) << 6 ) | ( s[i + 3] & 0x3f );
      i += 4;
    }

    result[ n++ ] = c;
  }

  *count = n;
  return result;
}

/* Spectrum block graphics for the Unicode quadrant characters U+2596 -
   U+259F, and the halves and full block */
static int
block_graphic( unsigned int c )
{
  static const libspectrum_byte quadrants[10] = {
    0x88, 0x84, 0x82, 0x8e, 0x86, 0x8b, 0x87, 0x81, 0x89, 0x8d
  };

  switch( c ) {
  case 0x2580: return 0x83;	/* upper half */
  case 0x2584: return 0x8c;	/* lower half */
  case 0x2588: return 0x8f;	/* full block */
  case 0x258c: return 0x8a;	/* left half */
  case 0x2590: return 0x85;	/* right half */
  }
  if( c >= 0x2596 && c <= 0x259f ) return quadrants[ c - 0x2596 ];

  return -1;
}

/* What a host character becomes on the Spectrum. Sets *replaced when it
   is only similar (é -> e) and *unknown when there is no equivalent */
static int
spectrum_character( unsigned int c, int *replaced, int *unknown )
{
  int block;

  *replaced = *unknown = 0;

  if( c == '\t' ) return ' ';
  if( c >= 0x20 && c < 0x7f ) return c;
  if( c == 0x00a3 ) return 0x60;	/* pound sign */
  if( c == 0x00a9 ) return 0x7f;	/* copyright sign */

  block = block_graphic( c );
  if( block >= 0 ) return block;

  *replaced = 1;

  if( c >= 0x00c0 && c <= 0x00ff && latin1_letters[ c - 0xc0 ] != '?' )
    return latin1_letters[ c - 0xc0 ];
  if( c >= 0x0100 && c <= 0x017f )
    return latin_extended_a_letters[ c - 0x100 ];

  switch( c ) {
  case 0x00a0: return ' ';				/* no-break space */
  case 0x2018: case 0x2019: case 0x201a: return '\'';
  case 0x201c: case 0x201d: case 0x201e:
  case 0x00ab: case 0x00bb: return '"';
  case 0x2013: case 0x2014: return '-';
  case 0x2026: return '.';
  case 0x00d7: return '*';
  case 0x00f7: return '/';
  }

  *replaced = 0; *unknown = 1;
  return '?';
}

/* ------------------------------------------------------------------------
   Text to tokens
   ------------------------------------------------------------------------ */

/* One character of a source line after escapes have been resolved. Raw
   characters came from an escape and are never part of a keyword or
   number */
typedef struct unit_t {
  int c;
  int raw;
} unit_t;

typedef struct tokenizer_t {
  basic_text_info *info;
  char *error;
  size_t error_length;
  size_t source_line;		/* For error messages */
  int basic128;
} tokenizer_t;

static int
tokenizer_error( tokenizer_t *t, const char *message )
{
  snprintf( t->error, t->error_length, "Text line %lu: %s",
            (unsigned long)t->source_line, message );
  return 1;
}

static int
block_half( unsigned int c, int top, int bottom )
{
  switch( c ) {
  case ' ': return 0;
  case '\'': return top;
  case '.': return bottom;
  case ':': return top | bottom;
  }
  return -1;
}

/* Resolve escapes and convert characters. Replacements are counted, but
   only reported once the BASIC line number is known */
static int
make_units( tokenizer_t *t, const unsigned int *text, size_t length,
            unit_t *units, size_t *count, size_t *replaced,
            size_t *unknown )
{
  size_t i, n = 0;

  *replaced = *unknown = 0;

  for( i = 0; i < length; i++ ) {
    unsigned int c = text[i];
    int r, u;

    if( c != '\\' ) {
      units[n].c = spectrum_character( c, &r, &u );
      units[n].raw = units[n].c >= 0x80;
      *replaced += r; *unknown += u;
      n++;
      continue;
    }

    if( ++i == length ) return tokenizer_error( t, "backslash at end" );
    c = text[i];

    if( c == '\\' || c == '@' ) {
      units[n].c = c; units[n].raw = 0; n++;
    } else if( c == '*' ) {
      units[n].c = 0x7f; units[n].raw = 1; n++;
    } else if( ( c >= 'a' && c <= 'u' ) || ( c >= 'A' && c <= 'U' ) ) {
      units[n].c = 0x90 + ( ( c | 0x20 ) - 'a' ); units[n].raw = 1; n++;
    } else if( block_half( c, 2, 8 ) >= 0 ) {
      int right;
      if( i + 1 == length ||
          ( right = block_half( text[ i + 1 ], 1, 4 ) ) < 0 )
        return tokenizer_error( t, "incomplete block graphics escape" );
      units[n].c = 0x80 | block_half( c, 2, 8 ) | right;
      units[n].raw = 1; n++;
      i++;
    } else if( c == '{' ) {
      unsigned long value = 0;
      int digits = 0, base = 10;
      i++;
      if( i + 1 < length && text[i] == '0' &&
          ( text[ i + 1 ] == 'x' || text[ i + 1 ] == 'X' ) ) {
        base = 16; i += 2;
      }
      for( ; i < length && text[i] != '}'; i++ ) {
        unsigned int d = text[i];
        if( d >= '0' && d <= '9' ) d -= '0';
        else if( base == 16 && d >= 'a' && d <= 'f' ) d -= 'a' - 10;
        else if( base == 16 && d >= 'A' && d <= 'F' ) d -= 'A' - 10;
        else return tokenizer_error( t, "bad number in \\{...} escape" );
        value = value * base + d;
        if( value > 255 )
          return tokenizer_error( t, "\\{...} escape is above 255" );
        digits++;
      }
      if( i == length || !digits )
        return tokenizer_error( t, "bad \\{...} escape" );
      units[n].c = value; units[n].raw = 1; n++;
    } else {
      return tokenizer_error( t, "unknown escape after backslash" );
    }
  }

  *count = n;
  return 0;
}

static int
is_upper( const unit_t *u )
{
  return !u->raw && u->c >= 'A' && u->c <= 'Z';
}

static int
is_letter( const unit_t *u )
{
  return !u->raw && ( ( u->c >= 'A' && u->c <= 'Z' ) ||
                      ( u->c >= 'a' && u->c <= 'z' ) );
}

static int
is_digit( const unit_t *u )
{
  return !u->raw && u->c >= '0' && u->c <= '9';
}

/* Find the longest keyword at units[pos]. Returns its length in units, or
   0 if there is none */
static size_t
match_keyword( const unit_t *units, size_t pos, size_t count, int basic128,
               libspectrum_byte *code )
{
  size_t best = 0, best_text = 0, k;

  /* A keyword written in letters can't continue a word in capitals */
  int after_capital = pos > 0 && is_upper( &units[ pos - 1 ] );

  for( k = 0; k < KEYWORD_COUNT; k++ ) {
    const char *text = keywords[k].text;
    size_t i = pos, j = 0, text_length = strlen( text );

    if( keywords[k].code < TOKEN_FIRST_48 && !basic128 ) continue;
    if( text_length <= best_text ) continue;
    if( after_capital && text[0] >= 'A' && text[0] <= 'Z' ) continue;

    while( text[j] ) {
      if( text[j] == ' ' ) {
        while( i < count && !units[i].raw && units[i].c == ' ' ) i++;
        j++;
      } else if( i < count && !units[i].raw && units[i].c == text[j] ) {
        i++; j++;
      } else {
        break;
      }
    }
    if( text[j] ) continue;

    /* ...nor be followed by one */
    if( text[ text_length - 1 ] >= 'A' && text[ text_length - 1 ] <= 'Z' &&
        i < count && is_upper( &units[i] ) )
      continue;

    best = i - pos; best_text = text_length; *code = keywords[k].code;
  }

  return best;
}

/* The five byte form the ROM stores after every number in a line */
static int
number_bytes( double value, libspectrum_byte *bytes )
{
  int exponent;
  double mantissa;
  libspectrum_dword m;

  if( value == floor( value ) && value >= 0 && value <= 65535 ) {
    unsigned int v = value;
    bytes[0] = 0; bytes[1] = 0;
    bytes[2] = v & 0xff; bytes[3] = v >> 8; bytes[4] = 0;
    return 0;
  }

  mantissa = frexp( value, &exponent );		/* 0.5 <= mantissa < 1 */
  m = (libspectrum_dword)floor( ldexp( mantissa, 32 ) + 0.5 );
  if( m == 0 && mantissa > 0 ) {		/* rounded up to 1 */
    m = 0x80000000UL; exponent++;
  }
  if( exponent + 128 < 1 ) {			/* too small: zero */
    memset( bytes, 0, 5 );
    return 0;
  }
  if( exponent + 128 > 255 ) return 1;

  bytes[0] = exponent + 128;
  bytes[1] = ( m >> 24 ) & 0x7f;		/* top bit is the sign */
  bytes[2] = ( m >> 16 ) & 0xff;
  bytes[3] = ( m >> 8 ) & 0xff;
  bytes[4] = m & 0xff;
  return 0;
}

static int
add_number( tokenizer_t *t, buffer_t *out, double value )
{
  libspectrum_byte bytes[5];
  size_t i;

  if( number_bytes( value, bytes ) )
    return tokenizer_error( t, "number too big" );

  buffer_add( out, NUMBER_MARKER );
  for( i = 0; i < 5; i++ ) buffer_add( out, bytes[i] );
  return 0;
}

/* Remove spaces just written before a keyword: LIST puts them back */
static void
drop_spaces( buffer_t *out, size_t code_spaces_from )
{
  while( out->length > code_spaces_from &&
         out->data[ out->length - 1 ] == ' ' )
    out->length--;
}

/* Tokenise the text of one line (after its line number) */
static int
tokenize_line( tokenizer_t *t, const unit_t *u, size_t count, buffer_t *out )
{
  size_t i = 0;
  size_t code_spaces_from = out->length;
  int in_string = 0, in_rem = 0, identifier = 0;
  int def_fn = 0;		/* 1: after DEF FN, 2: in its parameter list */

  while( i < count ) {
    libspectrum_byte code = 0;
    size_t length;

    if( in_rem || in_string ) {
      if( in_string && !u[i].raw && u[i].c == '"' ) in_string = 0;
      buffer_add( out, u[i].c );
      i++;
      code_spaces_from = out->length;
      continue;
    }

    if( !u[i].raw && u[i].c == '"' ) {
      in_string = 1; identifier = 0;
      buffer_add( out, '"' );
      i++;
      continue;
    }

    /* Keyword? */
    if( !identifier || !is_letter( &u[i] ) ) {
      length = match_keyword( u, i, count, t->basic128, &code );
      if( length ) {
        drop_spaces( out, code_spaces_from );
        buffer_add( out, code );
        i += length;
        identifier = 0;

        if( code == TOKEN_REM ) {
          if( i < count && !u[i].raw && u[i].c == ' ' ) i++;
          in_rem = 1;
          continue;
        }

        while( i < count && !u[i].raw && u[i].c == ' ' ) i++;
        code_spaces_from = out->length;

        if( code == TOKEN_DEF_FN ) def_fn = 1;

        if( code == TOKEN_BIN ) {
          double value = 0;
          while( i < count && !u[i].raw && ( u[i].c == '0' || u[i].c == '1' ) ) {
            value = value * 2 + ( u[i].c - '0' );
            buffer_add( out, u[i].c );
            i++;
          }
          if( add_number( t, out, value ) ) return 1;
          code_spaces_from = out->length;
        }
        continue;
      }
    }

    /* Number? */
    if( !identifier &&
        ( is_digit( &u[i] ) ||
          ( !u[i].raw && u[i].c == '.' && i + 1 < count &&
            is_digit( &u[ i + 1 ] ) ) ) ) {
      char number[64];
      size_t n = 0;

      while( i < count && is_digit( &u[i] ) && n < 60 )
        number[ n++ ] = u[ i++ ].c;
      if( i < count && !u[i].raw && u[i].c == '.' && n < 60 ) {
        number[ n++ ] = u[ i++ ].c;
        while( i < count && is_digit( &u[i] ) && n < 60 )
          number[ n++ ] = u[ i++ ].c;
      }
      if( i < count && !u[i].raw && ( u[i].c == 'e' || u[i].c == 'E' ) ) {
        size_t j = i + 1;
        if( j < count && !u[j].raw && ( u[j].c == '+' || u[j].c == '-' ) )
          j++;
        if( j < count && is_digit( &u[j] ) ) {
          while( i < j ) number[ n++ ] = u[ i++ ].c;
          while( i < count && is_digit( &u[i] ) && n < 60 )
            number[ n++ ] = u[ i++ ].c;
        }
      }
      number[n] = '\0';
      if( n >= 60 ) return tokenizer_error( t, "number too long" );

      buffer_add_string( out, number );
      if( add_number( t, out, strtod( number, NULL ) ) ) return 1;
      code_spaces_from = out->length;
      continue;
    }

    /* Anything else is stored as it is */
    buffer_add( out, u[i].c );

    if( u[i].raw || u[i].c != ' ' ) code_spaces_from = out->length;

    if( is_letter( &u[i] ) ) {
      identifier = 1;
      if( def_fn == 2 ) {
        /* Each parameter is followed by room for its value */
        if( i + 1 < count && !u[ i + 1 ].raw && u[ i + 1 ].c == '$' ) {
          buffer_add( out, '$' ); i++;
        }
        if( add_number( t, out, 0 ) ) return 1;
        code_spaces_from = out->length;
        identifier = 0;
      }
    } else if( !is_digit( &u[i] ) ) {
      identifier = 0;
      if( !u[i].raw && u[i].c == '(' && def_fn == 1 ) def_fn = 2;
      else if( !u[i].raw && u[i].c == ')' && def_fn == 2 ) def_fn = 0;
    }

    i++;
  }

  /* Trailing spaces outside strings and REM are not kept */
  if( !in_string && !in_rem ) drop_spaces( out, code_spaces_from );

  return 0;
}

typedef struct line_t {
  unsigned int number;
  size_t source_line;
  libspectrum_byte *data;
  size_t length;
} line_t;

static int
compare_lines( const void *a, const void *b )
{
  const line_t *x = a, *y = b;
  return x->number < y->number ? -1 : x->number > y->number ? 1 : 0;
}

static void
note_replacements( basic_text_info *info, unsigned int line,
                   size_t replaced, size_t unknown )
{
  if( !replaced && !unknown ) return;

  info->replaced += replaced;
  info->unknown += unknown;

  if( info->replaced_line_count < BASIC_TEXT_MAX_REPORTED_LINES )
    info->replaced_lines[ info->replaced_line_count ] = line;
  info->replaced_line_count++;
}

int
basic_text_tokenize( const char *text, size_t length, int basic128,
                     libspectrum_byte **program, size_t *program_length,
                     basic_text_info *info, char *error, size_t error_length )
{
  tokenizer_t t;
  unsigned int *chars;
  unit_t *units;
  line_t *lines = NULL;
  size_t char_count, line_count = 0, lines_allocated = 0, i, start;
  size_t source_line = 0;
  int numbered = -1;		/* -1: not known yet */
  int result = 1;
  unsigned int auto_number = 0;
  buffer_t out = { NULL, 0, 0 };

  memset( info, 0, sizeof( *info ) );
  t.info = info; t.error = error; t.error_length = error_length;
  t.basic128 = basic128;
  if( error_length ) error[0] = '\0';

  chars = decode_text( text, length, &char_count );
  units = libspectrum_new( unit_t, char_count + 1 );

  for( start = 0; start < char_count; ) {
    unsigned int *line_chars;
    size_t line_length = 0, end, unit_count, replaced, unknown, pos = 0;
    size_t first_source_line = source_line + 1;
    unsigned int number;
    buffer_t line = { NULL, 0, 0 };

    /* Join lines ending in an odd number of backslashes */
    line_chars = libspectrum_new( unsigned int, char_count - start + 1 );
    for( end = start; ; ) {
      size_t e = end, backslashes = 0;
      while( e < char_count && chars[e] != '\n' ) e++;
      source_line++;
      memcpy( line_chars + line_length, chars + end,
              ( e - end ) * sizeof( *chars ) );
      line_length += e - end;
      if( line_length && line_chars[ line_length - 1 ] == '\r' )
        line_length--;
      while( backslashes < line_length &&
             line_chars[ line_length - 1 - backslashes ] == '\\' )
        backslashes++;
      end = e < char_count ? e + 1 : e;
      if( backslashes % 2 == 0 || end >= char_count ) break;
      line_length--;
    }
    start = end;
    t.source_line = first_source_line;

    /* Blank lines and comments */
    while( pos < line_length && ( line_chars[pos] == ' ' ||
                                  line_chars[pos] == '\t' ) )
      pos++;
    if( pos == line_length || line_chars[pos] == '#' ) {
      libspectrum_free( line_chars );
      continue;
    }

    if( make_units( &t, line_chars + pos, line_length - pos, units,
                    &unit_count, &replaced, &unknown ) ) {
      libspectrum_free( line_chars );
      goto end;
    }
    libspectrum_free( line_chars );

    /* Line number */
    pos = 0;
    if( numbered == -1 ) numbered = is_digit( &units[0] );

    if( numbered ) {
      unsigned long n = 0;
      if( !is_digit( &units[0] ) ) {
        tokenizer_error( &t, "line number missing (either number all lines "
                             "or none)" );
        goto end;
      }
      while( pos < unit_count && is_digit( &units[pos] ) ) {
        n = n * 10 + ( units[pos].c - '0' );
        if( n > 9999 ) {
          tokenizer_error( &t, "line number above 9999" );
          goto end;
        }
        pos++;
      }
      number = n;
    } else {
      if( is_digit( &units[0] ) ) {
        tokenizer_error( &t, "line number found, but earlier lines had "
                             "none (either number all lines or none)" );
        goto end;
      }
      auto_number += 10;
      if( auto_number > 9999 ) {
        tokenizer_error( &t, "too many lines to number automatically" );
        goto end;
      }
      number = auto_number;
    }

    while( pos < unit_count && !units[pos].raw && units[pos].c == ' ' ) pos++;
    if( pos == unit_count ) {
      tokenizer_error( &t, "line has nothing after its number" );
      goto end;
    }

    if( tokenize_line( &t, units + pos, unit_count - pos, &line ) ) {
      libspectrum_free( line.data );
      goto end;
    }

    note_replacements( info, number, replaced, unknown );

    if( line_count == lines_allocated ) {
      lines_allocated = lines_allocated ? lines_allocated * 2 : 64;
      lines = libspectrum_renew( line_t, lines, lines_allocated );
    }
    lines[ line_count ].number = number;
    lines[ line_count ].source_line = first_source_line;
    lines[ line_count ].data = line.data;
    lines[ line_count ].length = line.length;
    line_count++;
  }

  if( !line_count ) {
    snprintf( error, error_length, "The file has no BASIC lines" );
    goto end;
  }

  qsort( lines, line_count, sizeof( *lines ), compare_lines );

  for( i = 0; i < line_count; i++ ) {
    size_t length = lines[i].length + 1;

    if( i && lines[i].number == lines[ i - 1 ].number ) {
      t.source_line = lines[i].source_line;
      snprintf( error, error_length, "Line number %u is used twice",
                lines[i].number );
      goto end;
    }

    buffer_add( &out, lines[i].number >> 8 );
    buffer_add( &out, lines[i].number & 0xff );
    buffer_add( &out, length & 0xff );
    buffer_add( &out, length >> 8 );
    {
      size_t j;
      for( j = 0; j < lines[i].length; j++ )
        buffer_add( &out, lines[i].data[j] );
    }
    buffer_add( &out, 0x0d );
  }

  info->lines = line_count;
  *program = out.data; out.data = NULL;
  *program_length = out.length;
  result = 0;

end:
  for( i = 0; i < line_count; i++ ) libspectrum_free( lines[i].data );
  libspectrum_free( lines );
  libspectrum_free( out.data );
  libspectrum_free( units );
  libspectrum_free( chars );
  return result;
}

/* ------------------------------------------------------------------------
   Tokens to text
   ------------------------------------------------------------------------ */

static void
add_escape( buffer_t *out, libspectrum_byte b )
{
  char text[16];
  snprintf( text, sizeof( text ), "\\{%d}", b );
  buffer_add_string( out, text );
}

/* Append a character that is not a keyword */
static void
add_character( buffer_t *out, libspectrum_byte b, int basic128 )
{
  static const char halves[] = " '.:";

  if( b >= 0x20 && b < 0x7f ) {
    if( b == '\\' ) buffer_add_string( out, "\\\\" );
    else if( b == 0x60 ) buffer_add_string( out, "\xc2\xa3" );	/* £ */
    else buffer_add( out, b );
  } else if( b == 0x7f ) {
    buffer_add_string( out, "\xc2\xa9" );			/* © */
  } else if( b >= 0x80 && b <= 0x8f ) {
    buffer_add( out, '\\' );
    buffer_add( out, halves[ ( ( b & 2 ) ? 1 : 0 ) | ( ( b & 8 ) ? 2 : 0 ) ] );
    buffer_add( out, halves[ ( ( b & 1 ) ? 1 : 0 ) | ( ( b & 4 ) ? 2 : 0 ) ] );
  } else if( b >= 0x90 && b < ( basic128 ? TOKEN_FIRST_128 : TOKEN_FIRST_48 ) ) {
    buffer_add( out, '\\' );
    buffer_add( out, 'a' + ( b - 0x90 ) );
  } else {
    add_escape( out, b );
  }
}

static int
ends_with_space( const buffer_t *out, size_t line_start )
{
  return out->length == line_start || out->data[ out->length - 1 ] == ' ';
}

static void
add_keyword( buffer_t *out, libspectrum_byte code, size_t line_start )
{
  const char *text = keyword_text( code );
  int leading, trailing;

  /* Spacing as LIST shows it: functions have no space in front, the
     comparison operators none at all */
  if( code >= 0xc7 && code <= 0xc9 ) {
    leading = trailing = 0;
  } else if( code >= 0xa5 && code <= 0xc4 ) {
    leading = 0;
    trailing = code > 0xa7;	/* not RND, INKEY$, PI */
  } else {
    leading = 1;
    trailing = code != 0xd3 && code != 0xd4;	/* OPEN #4, CLOSE #4 */
  }

  if( leading && !ends_with_space( out, line_start ) ) buffer_add( out, ' ' );
  buffer_add_string( out, text );
  if( trailing ) buffer_add( out, ' ' );
}

char*
basic_text_detokenize( const libspectrum_byte *program, size_t length,
                       int basic128, const char *newline )
{
  buffer_t out = { NULL, 0, 0 };
  size_t pos = 0;
  int first_token = basic128 ? TOKEN_FIRST_128 : TOKEN_FIRST_48;

  while( pos + 4 <= length ) {
    unsigned int number = ( program[pos] << 8 ) | program[ pos + 1 ];
    size_t line_length = program[ pos + 2 ] | ( program[ pos + 3 ] << 8 );
    size_t i, end, line_start;
    int in_string = 0, in_rem = 0;
    char text[16];

    if( number > 16383 ) break;	/* variables follow */
    if( pos + 4 + line_length > length || !line_length ) {
      libspectrum_free( out.data );
      return NULL;
    }

    snprintf( text, sizeof( text ), "%u ", number );
    buffer_add_string( &out, text );
    line_start = out.length;

    i = pos + 4;
    end = i + line_length;
    if( program[ end - 1 ] == 0x0d ) end--;

    while( i < end ) {
      libspectrum_byte b = program[i++];

      if( in_rem || in_string ) {
        if( b == '"' && in_string ) in_string = 0;
        if( b >= first_token ) add_escape( &out, b );
        else add_character( &out, b, basic128 );
      } else if( b == NUMBER_MARKER ) {
        i += 5;
      } else if( b >= first_token ) {
        add_keyword( &out, b, line_start );
        if( b == TOKEN_REM ) in_rem = 1;
      } else {
        if( b == '"' ) in_string = 1;
        add_character( &out, b, basic128 );
      }

      /* Colour control codes are followed by their parameters */
      if( b >= 0x10 && b <= 0x17 ) {
        int params = b >= 0x16 ? 2 : 1;
        while( params-- && i < end ) add_escape( &out, program[i++] );
      }
    }

    /* Trailing space after the last keyword */
    if( !in_rem && !in_string )
      while( out.length > line_start && out.data[ out.length - 1 ] == ' ' )
        out.length--;

    buffer_add_string( &out, newline );
    pos += 4 + line_length;
  }

  buffer_add( &out, '\0' );
  return (char*)out.data;
}

/* ------------------------------------------------------------------------
   The emulated machine
   ------------------------------------------------------------------------ */

/* System variables */
#define SV_FLAGS  0x5c3b
#define SV_TV_FLAG 0x5c3c
#define SV_E_PPC  0x5c49
#define SV_VARS   0x5c4b
#define SV_PROG   0x5c53
#define SV_NXTLIN 0x5c55
#define SV_DATADD 0x5c57
#define SV_E_LINE 0x5c59
#define SV_K_CUR  0x5c5b
#define SV_CH_ADD 0x5c5d
#define SV_X_PTR  0x5c5f
#define SV_WORKSP 0x5c61
#define SV_STKBOT 0x5c63
#define SV_STKEND 0x5c65
#define SV_S_TOP  0x5c6c
#define SV_FLAGX  0x5c71
#define SV_RAMTOP 0x5cb2
#define SV_FLAGS3 0x5b66	/* 128K: bit 0 set while BASIC runs */

/* 128K editor flags in RAM page 7 */
#define ED_MODE   0x2c0c	/* 0xec0c: 1 = 128 BASIC */
#define ED_FLAGS  0x2c0d	/* 0xec0d: bit 1 set while a menu is shown */

/* Space left for the stacks above the program */
#define STACK_ROOM 256

static libspectrum_word
read_word( libspectrum_word address )
{
  return readbyte_internal( address ) |
         ( readbyte_internal( address + 1 ) << 8 );
}

static void
write_word( libspectrum_word address, libspectrum_word value )
{
  writebyte_internal( address, value & 0xff );
  writebyte_internal( address + 1, value >> 8 );
}

static int
has_128_editor( void )
{
  return machine_current->capabilities &
         ( LIBSPECTRUM_MACHINE_CAPABILITY_128_MEMORY |
           LIBSPECTRUM_MACHINE_CAPABILITY_PLUS3_MEMORY );
}

static libspectrum_byte
page7_byte( libspectrum_word offset )
{
  return memory_map_ram[ 7 * MEMORY_PAGES_IN_16K +
                         ( offset >> MEMORY_PAGE_SIZE_LOGARITHM ) ].
    page[ offset & MEMORY_PAGE_SIZE_MASK ];
}

/* Where the CPU is running. Menu actions are run just after the end of
   frame interrupt has been accepted (IM 1), so then the interrupted
   address is the one on top of the stack */
static void
cpu_position( libspectrum_word *pc, libspectrum_word *sp )
{
  *pc = z80.pc.w; *sp = z80.sp.w;

  if( *pc == 0x0038 && !z80.iff1 && z80.im == 1 ) {
    *pc = read_word( *sp );
    *sp += 2;
  }
}

/* Is paging locked in 48K mode? */
static int
locked_48( void )
{
  return ( machine_current->ram.last_byte & 0x20 ) != 0;
}

/* Is the CPU in the 128K editor's loop waiting for a key
   (LD HL,FLAGS / BIT 5,(HL) / JR Z,$-2)? */
static int
waiting_for_key_128( void )
{
  libspectrum_word pc, sp;

  cpu_position( &pc, &sp );

  if( memory_map_read[0].source != memory_source_rom ||
      memory_map_read[0].page_num != 0 || pc >= 0x4000 || pc < 2 )
    return 0;

  if( readbyte_internal( pc ) == 0xcb ) pc += 2;
  return readbyte_internal( pc - 2 ) == 0xcb &&
         readbyte_internal( pc - 1 ) == 0x6e &&
         readbyte_internal( pc ) == 0x28 &&
         readbyte_internal( pc + 1 ) == 0xfc &&
         z80.hl.w == SV_FLAGS;
}

static int
menu_128_shown( void )
{
  return has_128_editor() && !locked_48() && waiting_for_key_128() &&
         ( page7_byte( ED_FLAGS ) & 0x02 );
}

static int
editor_128_idle( void )
{
  return has_128_editor() && !locked_48() && waiting_for_key_128() &&
         page7_byte( ED_MODE ) == 1 &&
         !( page7_byte( ED_FLAGS ) & 0x02 ) &&
         !( readbyte_internal( SV_FLAGS3 ) & 0x01 );
}

/* Is the 48K BASIC editor waiting for a key? The ROM then runs WAIT-KEY,
   called from ED-LOOP (return address 0x0f3b) */
static int
editor_48_idle( void )
{
  libspectrum_word pc, sp;
  int i;

  cpu_position( &pc, &sp );

  if( memory_map_read[0].source != memory_source_rom ) return 0;

  /* The 48K BASIC ROM is paged in */
  if( readbyte_internal( 0x0f38 ) != 0xcd ||
      readbyte_internal( 0x0f39 ) != 0xd4 ||
      readbyte_internal( 0x0f3a ) != 0x15 )
    return 0;

  /* In KEY-INPUT, WAIT-KEY or CALL-SUB */
  if( !( ( pc >= 0x10a8 && pc < 0x111d ) || ( pc >= 0x15d4 && pc < 0x1610 ) ||
         ( pc >= 0x162c && pc < 0x1634 ) ) )
    return 0;

  /* Not an INPUT statement */
  if( readbyte_internal( SV_FLAGX ) & 0x20 ) return 0;

  /* Not 128 BASIC running a program */
  if( has_128_editor() && !locked_48() &&
      ( readbyte_internal( SV_FLAGS3 ) & 0x01 ) )
    return 0;

  for( i = 0; i < 12; i++ )
    if( read_word( sp + 2 * i ) == 0x0f3b ) return 1;

  return 0;
}

/* Which editor is waiting: 0 none, 48 or 128 */
static int
editor_idle( void )
{
  if( editor_128_idle() ) return 128;
  if( editor_48_idle() ) return 48;
  return 0;
}

/* Is a 128 BASIC (with PLAY and SPECTRUM) program in memory? */
static int
memory_is_basic128( void )
{
  if( !has_128_editor() || locked_48() ) return 0;
  return page7_byte( ED_MODE ) == 1;
}

/* Put the program in memory, as LOAD would */
static int
inject_program( const libspectrum_byte *program, size_t length )
{
  libspectrum_word prog = read_word( SV_PROG );
  libspectrum_word ramtop = read_word( SV_RAMTOP );
  libspectrum_word vars, e_line;
  size_t i;

  if( prog < 0x5c00 ||
      (size_t)prog + length + 3 + STACK_ROOM > ramtop ) {
    ui_error( UI_ERROR_ERROR, "The BASIC program does not fit in memory "
              "(%lu bytes)", (unsigned long)length );
    return 1;
  }

  for( i = 0; i < length; i++ ) writebyte_internal( prog + i, program[i] );

  vars = prog + length;
  writebyte_internal( vars, 0x80 );		/* no variables */
  e_line = vars + 1;
  writebyte_internal( e_line, 0x0d );		/* empty edit line */
  writebyte_internal( e_line + 1, 0x80 );

  write_word( SV_VARS, vars );
  write_word( SV_E_LINE, e_line );
  write_word( SV_K_CUR, e_line );
  write_word( SV_CH_ADD, e_line );
  write_word( SV_X_PTR, 0 );
  write_word( SV_WORKSP, e_line + 2 );
  write_word( SV_STKBOT, e_line + 2 );
  write_word( SV_STKEND, e_line + 2 );
  write_word( SV_NXTLIN, prog );
  write_word( SV_DATADD, prog - 1 );
  write_word( SV_E_PPC, 0 );
  write_word( SV_S_TOP, 0 );

  /* Have the 48K editor print the (now empty) edit line again, unless a
     message is shown there which the next key clears anyway */
  if( !( readbyte_internal( SV_TV_FLAG ) & 0x20 ) )
    writebyte_internal( SV_TV_FLAG, readbyte_internal( SV_TV_FLAG ) | 0x08 );

  return 0;
}

/* ------------------------------------------------------------------------
   Loading

   48K BASIC: the program is put in memory while the editor waits for a
   key.

   128K BASIC: the editor keeps its own copy of the lines on screen, so the
   program is put in memory while the main menu is shown, and 128 BASIC is
   then selected: the editor starts with the new program listed, just as
   after the Tape Loader. From the editor, the main menu is reached with
   EDIT and Exit.

   If a program is running, the machine is reset first.
   ------------------------------------------------------------------------ */

typedef enum load_state_t {
  LOAD_IDLE,
  LOAD_WAIT_BOOT,		/* reset: waiting for the 48K editor or menu */
  LOAD_EDIT_KEY,		/* 128K editor: pressing EDIT */
  LOAD_WAIT_EDIT_MENU,		/* waiting for the editor's menu */
  LOAD_EDIT_MENU_KEY,		/* pressing a key in the editor's menu */
  LOAD_WAIT_MAIN_MENU,		/* waiting for the main menu */
  LOAD_MAIN_MENU_KEY,		/* pressing a key in the main menu */
  LOAD_WAIT_EDITOR,		/* waiting for 128 BASIC to start */
  LOAD_LIST_KEY,		/* 48K: pressing Enter to list the program */
  LOAD_REPORT,			/* done: tell the user */
} load_state_t;

/* Menu items */
#define EDIT_MENU_EXIT 4
#define MAIN_MENU_BASIC128 1

/* Frames to wait for the machine before giving up */
#define LOAD_TIMEOUT 1000
/* Frames a key is held down, and then released */
#define KEY_FRAMES 4

static load_state_t load_state = LOAD_IDLE;
static load_state_t after_key_state;
static unsigned long key_frames, load_frames;

static libspectrum_byte *pending_program = NULL;
static size_t pending_length;
static basic_text_info pending_info;
static char pending_name[ 128 ];

static void
load_finish( void )
{
  libspectrum_free( pending_program );
  pending_program = NULL;
  load_state = LOAD_IDLE;
  keyboard_synthetic_release_all( KEYBOARD_SYNTHETIC_BASIC_TEXT );
}

/* Tell the user the program is in memory, and what had to be replaced */
static void
report_loaded( const basic_text_info *info )
{
  char lines[ BASIC_TEXT_MAX_REPORTED_LINES * 8 + 8 ];
  char replaced[ 256 ];
  size_t i, used = 0;

  lines[0] = '\0';
  for( i = 0; i < info->replaced_line_count &&
              i < BASIC_TEXT_MAX_REPORTED_LINES; i++ )
    used += snprintf( lines + used, sizeof( lines ) - used, "%s%d",
                      i ? ", " : "", info->replaced_lines[i] );
  if( info->replaced_line_count > BASIC_TEXT_MAX_REPORTED_LINES )
    snprintf( lines + used, sizeof( lines ) - used, ", ..." );

  if( info->unknown )
    snprintf( replaced, sizeof( replaced ), "\n\n%lu character(s) were "
              "replaced by a plain letter and %lu with no Spectrum "
              "equivalent by '?'.\nLines: %s",
              (unsigned long)info->replaced, (unsigned long)info->unknown,
              lines );
  else if( info->replaced )
    snprintf( replaced, sizeof( replaced ), "\n\n%lu accented character(s) "
              "were replaced by the plain letter, as the Spectrum has "
              "none.\nLines: %s", (unsigned long)info->replaced, lines );
  else
    replaced[0] = '\0';

  ui_error( UI_ERROR_INFO, "The BASIC program \"%s\" was loaded "
            "successfully (%lu lines).%s", pending_name,
            (unsigned long)info->lines, replaced );
}

/* Put the pending program in memory. Returns 0 on success */
static int
load_inject( void )
{
  if( inject_program( pending_program, pending_length ) ) {
    load_finish();
    return 1;
  }
  return 0;
}

/* Press `key' (with Caps Shift if `caps'), then go to `next' */
static void
press_key( keyboard_key_name key, int caps, load_state_t state,
           load_state_t next )
{
  if( caps )
    keyboard_synthetic_press( KEYBOARD_SYNTHETIC_BASIC_TEXT, KEYBOARD_Caps );
  keyboard_synthetic_press( KEYBOARD_SYNTHETIC_BASIC_TEXT, key );
  key_frames = 0;
  load_state = state;
  after_key_state = next;
}

/* 48K: Enter on the empty edit line lists the program */
static void
list_program( void )
{
  libspectrum_free( pending_program );
  pending_program = NULL;
  press_key( KEYBOARD_Enter, 0, LOAD_LIST_KEY, LOAD_REPORT );
}

/* At the main menu: put the program in memory, then choose 128 BASIC */
static void
main_menu( void )
{
  if( pending_program && load_inject() ) return;

  /* Keep the program for the report; it is no longer needed in memory */
  libspectrum_free( pending_program );
  pending_program = NULL;

  if( page7_byte( ED_MODE ) == MAIN_MENU_BASIC128 )
    press_key( KEYBOARD_Enter, 0, LOAD_MAIN_MENU_KEY, LOAD_WAIT_EDITOR );
  else
    press_key( KEYBOARD_6, 1, LOAD_MAIN_MENU_KEY, LOAD_WAIT_MAIN_MENU );
}

static int
target_mode( int editor )
{
  if( editor ) return editor;
  return has_128_editor() ? 128 : 48;
}

int
basic_text_load( const char *filename )
{
  utils_file file;
  char error[256];
  int editor, mode;

  if( load_state != LOAD_IDLE ) load_finish();

  if( utils_read_file( filename, &file ) ) return 1;

  {
    const char *name = filename, *p;
    for( p = filename; *p; p++ )
      if( *p == '/' || *p == '\\' ) name = p + 1;
    snprintf( pending_name, sizeof( pending_name ), "%s", name );
  }

  editor = editor_idle();
  mode = target_mode( editor );

  /* Check the text before anything is changed */
  if( basic_text_tokenize( (const char*)file.buffer, file.length,
                           mode == 128, &pending_program, &pending_length,
                           &pending_info, error, sizeof( error ) ) ) {
    ui_error( UI_ERROR_ERROR, "%s", error );
    utils_close_file( &file );
    return 1;
  }
  utils_close_file( &file );

  load_frames = 0;

  if( editor == 48 ) {
    if( load_inject() ) return 1;
    list_program();
    return 0;
  }

  if( editor == 128 ) {
    press_key( KEYBOARD_1, 1, LOAD_EDIT_KEY, LOAD_WAIT_EDIT_MENU );
    return 0;
  }

  if( !ui_query( "The BASIC editor is not active, so the Spectrum will be "
                 "reset before the program is loaded (anything running "
                 "will be stopped). Continue?" ) ) {
    load_finish();
    return 1;
  }

  machine_reset( 0 );
  load_state = LOAD_WAIT_BOOT;
  return 0;
}

void
basic_text_frame( void )
{
  if( load_state == LOAD_IDLE ) return;

  if( ++load_frames > LOAD_TIMEOUT ) {
    load_finish();
    ui_error( UI_ERROR_ERROR, "The BASIC editor did not respond, so the "
              "program could not be loaded" );
    return;
  }

  switch( load_state ) {

  case LOAD_WAIT_BOOT:
    if( editor_48_idle() ) {
      if( load_inject() ) return;
      list_program();
    } else if( menu_128_shown() ) {
      main_menu();
    }
    break;

  case LOAD_EDIT_KEY:
  case LOAD_EDIT_MENU_KEY:
  case LOAD_MAIN_MENU_KEY:
  case LOAD_LIST_KEY:
    if( ++key_frames == KEY_FRAMES ) {
      keyboard_synthetic_release_all( KEYBOARD_SYNTHETIC_BASIC_TEXT );
    } else if( key_frames >= 2 * KEY_FRAMES ) {
      load_state = after_key_state;
    }
    break;

  case LOAD_WAIT_EDIT_MENU:
    if( menu_128_shown() ) {
      if( page7_byte( ED_MODE ) == EDIT_MENU_EXIT )
        press_key( KEYBOARD_Enter, 0, LOAD_EDIT_MENU_KEY,
                   LOAD_WAIT_MAIN_MENU );
      else
        press_key( KEYBOARD_6, 1, LOAD_EDIT_MENU_KEY, LOAD_WAIT_EDIT_MENU );
    }
    break;

  case LOAD_WAIT_MAIN_MENU:
    /* The main menu starts on its first item, not on Exit */
    if( menu_128_shown() && page7_byte( ED_MODE ) != EDIT_MENU_EXIT )
      main_menu();
    break;

  case LOAD_WAIT_EDITOR:
    /* The 128K editor starts with the program listed */
    if( editor_128_idle() ) load_state = LOAD_REPORT;
    break;

  case LOAD_REPORT:
    load_finish();
    report_loaded( &pending_info );
    break;

  case LOAD_IDLE:
    break;

  }
}

/* ------------------------------------------------------------------------
   Saving
   ------------------------------------------------------------------------ */

int
basic_text_save( const char *filename )
{
  libspectrum_word prog = read_word( SV_PROG );
  libspectrum_word vars = read_word( SV_VARS );
  libspectrum_byte *program;
  size_t length, i;
  char *text;
  int error;

#ifdef WIN32
  const char *newline = "\r\n";
#else
  const char *newline = "\n";
#endif

  if( prog < 0x5c00 || vars < prog ) {
    ui_error( UI_ERROR_ERROR, "There is no BASIC program in memory" );
    return 1;
  }

  length = vars - prog;
  if( !length ) {
    ui_error( UI_ERROR_ERROR, "There is no BASIC program in memory" );
    return 1;
  }

  program = libspectrum_new( libspectrum_byte, length );
  for( i = 0; i < length; i++ ) program[i] = readbyte_internal( prog + i );

  text = basic_text_detokenize( program, length, memory_is_basic128(),
                                newline );
  libspectrum_free( program );

  if( !text ) {
    ui_error( UI_ERROR_ERROR, "The BASIC program in memory is damaged" );
    return 1;
  }

  error = utils_write_file( filename, (unsigned char*)text, strlen( text ) );
  libspectrum_free( text );

  return error;
}

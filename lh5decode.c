/* lh5decode.c: Decompressing LHA -lh5- data
   Copyright (c) 2026 Viktor (GitHub: Viktor318)
   The method as in lhasa (https://fragglet.github.io/lhasa/), Copyright
   (c) 2011, 2012 Simon Howard (ISC license), and the ar002 archiver of
   Haruhiko Okumura.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

/* The data is a series of blocks, each with its own Huffman codes: one
   code for the literal bytes and the match lengths together, one for the
   match offsets (their bit counts), and one that the lengths of the
   first code are coded with. A match copies earlier output: at most 256
   bytes from at most 8 KB back. */

#include "config.h"

#include <string.h>

#include "lh5decode.h"

#define MAX_MATCH 256
#define THRESHOLD 3			/* the shortest match */
#define NC ( 255 + MAX_MATCH + 2 - THRESHOLD )	/* literals and lengths */
#define CBIT 9				/* bits of the number of codes */
#define NP 14				/* offset bit counts (-lh5-) */
#define PBIT 4
#define NT 19				/* the code of the code lengths */
#define TBIT 5
#define NPT ( NT > NP ? NT : NP )
#define CODE_BITS 16

typedef struct decoder {
  const unsigned char *in;
  size_t in_length, in_position;
  unsigned int bit_buffer;	/* the next 16 bits, at the top */
  unsigned int sub_buffer;	/* bits read from the input, not used yet */
  int bit_count;		/* bits in sub_buffer */
  int overrun;			/* bytes read beyond the input */

  unsigned short left[ 2 * NC - 1 ], right[ 2 * NC - 1 ];
  unsigned char c_length[ NC ], pt_length[ NPT ];
  unsigned short c_table[ 4096 ], pt_table[ 256 ];
  unsigned int block_left;
} decoder;

static void
fill_buffer( decoder *d, int n )
{
  d->bit_buffer = ( d->bit_buffer << n ) & 0xffff;
  while( n > d->bit_count ) {
    n -= d->bit_count;
    d->bit_buffer |= ( d->sub_buffer << n ) & 0xffff;
    if( d->in_position < d->in_length ) {
      d->sub_buffer = d->in[ d->in_position++ ];
    } else {
      d->sub_buffer = 0;
      d->overrun++;
    }
    d->bit_count = 8;
  }
  d->bit_count -= n;
  d->bit_buffer |= ( d->sub_buffer >> d->bit_count ) & 0xffff;
}

static unsigned int
get_bits( decoder *d, int n )
{
  unsigned int x;

  if( n == 0 ) return 0;
  x = d->bit_buffer >> ( CODE_BITS - n );
  fill_buffer( d, n );
  return x;
}

/* The lookup table of a code from its lengths: codes up to `table_bits'
   long are found at once, longer ones through a tree. 0 if the lengths
   are not a code */
static int
make_table( decoder *d, int count_chars, const unsigned char *lengths,
            int table_bits, unsigned short *table )
{
  unsigned int count[ 17 ], weight[ 17 ], start[ 18 ];
  unsigned int i, k, length, jut_bits, avail, next_code, mask;
  unsigned short *p;
  int ch;

  for( i = 1; i <= 16; i++ ) count[i] = 0;
  for( ch = 0; ch < count_chars; ch++ ) {
    if( lengths[ ch ] > 16 ) return 0;
    count[ lengths[ ch ] ]++;
  }

  start[1] = 0;
  for( i = 1; i <= 16; i++ ) start[ i + 1 ] = start[i] + ( count[i] << ( 16 - i ) );
  if( start[17] != 0x10000 ) return 0;

  jut_bits = 16 - table_bits;
  for( i = 1; i <= (unsigned int)table_bits; i++ ) {
    start[i] >>= jut_bits;
    weight[i] = 1u << ( table_bits - i );
  }
  while( i <= 16 ) {
    weight[i] = 1u << ( 16 - i );
    i++;
  }

  i = start[ table_bits + 1 ] >> jut_bits;
  if( i != 0x10000 ) {
    k = 1u << table_bits;
    while( i != k ) table[ i++ ] = 0;
  }

  avail = count_chars;
  mask = 1u << ( 15 - table_bits );
  for( ch = 0; ch < count_chars; ch++ ) {
    length = lengths[ ch ];
    if( length == 0 ) continue;
    next_code = start[ length ] + weight[ length ];
    if( length <= (unsigned int)table_bits ) {
      for( i = start[ length ]; i < next_code; i++ ) table[i] = ch;
    } else {
      k = start[ length ];
      p = &table[ k >> jut_bits ];
      i = length - table_bits;
      while( i != 0 ) {
        if( *p == 0 ) {
          if( avail >= 2 * NC - 1 ) return 0;
          d->right[ avail ] = d->left[ avail ] = 0;
          *p = avail++;
        }
        p = k & mask ? &d->right[ *p ] : &d->left[ *p ];
        k <<= 1;
        i--;
      }
      *p = ch;
    }
    start[ length ] = next_code;
  }
  return 1;
}

/* The lengths of the code of the code lengths (`special': after so many,
   2 bits tell how many zero lengths follow), or of the offset code */
static int
read_pt_length( decoder *d, int count_chars, int bits, int special )
{
  int i, c, n = get_bits( d, bits );

  if( n > count_chars ) return 0;
  if( n == 0 ) {
    c = get_bits( d, bits );
    if( c >= count_chars ) return 0;
    for( i = 0; i < count_chars; i++ ) d->pt_length[i] = 0;
    for( i = 0; i < 256; i++ ) d->pt_table[i] = c;
    return 1;
  }

  i = 0;
  while( i < n ) {
    c = d->bit_buffer >> ( CODE_BITS - 3 );
    if( c == 7 ) {
      unsigned int mask = 1u << ( CODE_BITS - 4 );
      while( mask & d->bit_buffer ) {
        mask >>= 1;
        c++;
        if( c > 16 ) return 0;
      }
    }
    fill_buffer( d, c < 7 ? 3 : c - 3 );
    d->pt_length[ i++ ] = c;
    if( i == special ) {
      c = get_bits( d, 2 );
      while( --c >= 0 && i < count_chars ) d->pt_length[ i++ ] = 0;
    }
  }
  while( i < count_chars ) d->pt_length[ i++ ] = 0;
  return make_table( d, count_chars, d->pt_length, 8, d->pt_table );
}

/* A code through the lookup table and the tree */
static unsigned int
tree_walk( decoder *d, unsigned int j, int limit, unsigned int mask )
{
  while( j >= (unsigned int)limit ) {
    if( j >= 2 * NC - 1 || !mask ) return 0;
    j = d->bit_buffer & mask ? d->right[j] : d->left[j];
    mask >>= 1;
  }
  return j;
}

static int
read_c_length( decoder *d )
{
  int i, c, n = get_bits( d, CBIT );

  if( n > NC ) return 0;
  if( n == 0 ) {
    c = get_bits( d, CBIT );
    if( c >= NC ) return 0;
    for( i = 0; i < NC; i++ ) d->c_length[i] = 0;
    for( i = 0; i < 4096; i++ ) d->c_table[i] = c;
    return 1;
  }

  i = 0;
  while( i < n ) {
    c = d->pt_table[ d->bit_buffer >> 8 ];
    if( c >= NT ) c = tree_walk( d, c, NT, 1u << 7 );
    fill_buffer( d, d->pt_length[c] );
    if( c <= 2 ) {
      if( c == 0 ) c = 1;
      else if( c == 1 ) c = get_bits( d, 4 ) + 3;
      else c = get_bits( d, CBIT ) + 20;
      while( --c >= 0 && i < NC ) d->c_length[ i++ ] = 0;
    } else {
      d->c_length[ i++ ] = c - 2;
    }
  }
  while( i < NC ) d->c_length[ i++ ] = 0;
  return make_table( d, NC, d->c_length, 12, d->c_table );
}

/* The next literal or length; -1 if the data is damaged */
static int
decode_c( decoder *d )
{
  unsigned int j;

  if( d->block_left == 0 ) {
    d->block_left = get_bits( d, 16 );
    if( d->block_left == 0 ) return -1;
    if( !read_pt_length( d, NT, TBIT, 3 ) || !read_c_length( d ) ||
        !read_pt_length( d, NP, PBIT, -1 ) )
      return -1;
  }
  d->block_left--;

  j = d->c_table[ d->bit_buffer >> 4 ];
  if( j >= NC ) j = tree_walk( d, j, NC, 1u << 3 );
  fill_buffer( d, d->c_length[j] );
  return j;
}

/* The offset of a match */
static unsigned int
decode_p( decoder *d )
{
  unsigned int j = d->pt_table[ d->bit_buffer >> 8 ];

  if( j >= NP ) j = tree_walk( d, j, NP, 1u << 7 );
  fill_buffer( d, d->pt_length[j] );
  if( j != 0 ) j = ( 1u << ( j - 1 ) ) + get_bits( d, j - 1 );
  return j;
}

int
lh5_decode( const unsigned char *in, size_t in_length,
            unsigned char *out, size_t out_length, size_t *used )
{
  static decoder d;		/* large; one at a time */
  size_t position = 0;

  memset( &d, 0, sizeof( d ) );
  d.in = in;
  d.in_length = in_length;
  fill_buffer( &d, CODE_BITS );

  while( position < out_length ) {
    int c = decode_c( &d );

    if( c < 0 || d.overrun > 4 ) return 1;
    if( c < 256 ) {
      out[ position++ ] = c;
    } else {
      size_t length = c - 256 + THRESHOLD;
      size_t offset = decode_p( &d ) + 1;

      if( offset > position ) return 1;
      if( length > out_length - position ) length = out_length - position;
      while( length-- ) {
        out[ position ] = out[ position - offset ];
        position++;
      }
    }
  }

  if( used ) {
    /* The bytes read ahead into the bit buffers are not used */
    size_t ahead = ( d.bit_count + CODE_BITS ) / 8;
    *used = d.in_position > ahead ? d.in_position - ahead : 0;
  }
  return d.overrun > 4;
}

unsigned int
lh5_crc16( const unsigned char *data, size_t length )
{
  unsigned int crc = 0;
  size_t i;
  int bit;

  for( i = 0; i < length; i++ ) {
    crc ^= data[i];
    for( bit = 0; bit < 8; bit++ )
      crc = crc & 1 ? ( crc >> 1 ) ^ 0xa001 : crc >> 1;
  }
  return crc & 0xffff;
}

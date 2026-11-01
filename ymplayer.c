/* ymplayer.c: YM (ST-Sound) and VTX (Vortex) register dump player
   Copyright (c) 2026 Viktor (GitHub: Viktor318)
   The formats as ZXTune (https://zxtune.bitbucket.io/), Copyright (c)
   Vitamin/CAIG <vitamin.caig@gmail.com>, reads them
   (src/formats/chiptune/aym/ym_vtx.cpp, src/module/players/aym/ymvtx.cpp).

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

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "aysource.h"
#include "lh5decode.h"
#include "ymplayer.h"

#define COLUMNS 14			/* registers kept of each frame */
#define STRING_LENGTH 255
#define AY_CLOCK ( AYSOURCE_CLOCK / 2 )
#define ATARI_CLOCK 2000000
#define FRAME_RATE 50			/* of the Spectrum: the player's */

#define CLOCK_MIN 100000
#define CLOCK_MAX 10000000
#define RATE_MIN 25
#define RATE_MAX 100
#define DUMP_MAX ( 16L * RATE_MAX * 30 * 60 )	/* 30 minutes */

/* Register 13 holding this was not written in the frame */
#define NOT_WRITTEN 0xff

struct ymplayer_module {
  unsigned char *rows;		/* COLUMNS registers of each frame */
  long rows_count, loop_row;
  unsigned long clock;
  unsigned int rate;		/* frames a second */
  int layout;			/* VTX: the stereo layout, 0-6; YM: -1 */
  char title[ STRING_LENGTH + 1 ], author[ STRING_LENGTH + 1 ];
  char comment[ STRING_LENGTH + 1 ];

  /* Playing */
  long next_row;
  unsigned int rate_left;	/* frames of the file owed, times 50 */
  unsigned char current[ YMPLAYER_REGISTERS ];
};

static unsigned long
big_endian( const unsigned char *p, int bytes )
{
  unsigned long value = 0;
  while( bytes-- ) value = value << 8 | *p++;
  return value;
}

static unsigned long
little_endian( const unsigned char *p, int bytes )
{
  unsigned long value = 0;
  while( bytes-- ) value = value << 8 | p[ bytes ];
  return value;
}

/* A zero-terminated string at *position (at most STRING_LENGTH bytes kept);
   0 if the data ends first */
static int
read_string( const unsigned char *data, size_t length, size_t *position,
             char *text )
{
  size_t i = 0;

  while( *position < length && data[ *position ] ) {
    if( i < STRING_LENGTH ) text[ i++ ] = data[ *position ];
    ( *position )++;
  }
  text[i] = '\0';
  if( *position >= length ) return 0;
  ( *position )++;
  return 1;
}

static ymplayer_module *
new_module( long rows )
{
  ymplayer_module *m;

  if( rows <= 0 || rows > DUMP_MAX / COLUMNS ) return NULL;
  m = calloc( 1, sizeof( *m ) );
  if( !m ) return NULL;
  m->rows = calloc( rows, COLUMNS );
  if( !m->rows ) {
    free( m );
    return NULL;
  }
  m->rows_count = rows;
  m->clock = ATARI_CLOCK;
  m->rate = FRAME_RATE;
  m->layout = -1;
  return m;
}

/* The registers stored register by register (all the frames of register 0,
   then of register 1...), with `columns' registers of `rows' frames */
static void
read_interleaved( ymplayer_module *m, const unsigned char *dump,
                  size_t length, long rows, int columns )
{
  long row;
  int column;

  for( column = 0; column < columns && column < COLUMNS; column++ )
    for( row = 0; row < m->rows_count; row++ ) {
      size_t at = (size_t)column * rows + row;
      m->rows[ row * COLUMNS + column ] = at < length ? dump[ at ] : 0;
    }
}

/* The registers stored frame by frame */
static void
read_frames( ymplayer_module *m, const unsigned char *dump, size_t length,
             int columns )
{
  long row;
  int column;

  for( row = 0; row < m->rows_count; row++ )
    for( column = 0; column < columns && column < COLUMNS; column++ ) {
      size_t at = (size_t)row * columns + column;
      m->rows[ row * COLUMNS + column ] = at < length ? dump[ at ] : 0;
    }
}

/* YM2!, YM3!, YM3b: 14 registers, interleaved; YM3b ends with the loop
   frame */
static ymplayer_module *
load_ym3( const unsigned char *data, size_t length )
{
  int with_loop = data[3] == 'b';
  size_t dump = length - 4 - ( with_loop ? 4 : 0 );
  long rows = dump / COLUMNS;
  ymplayer_module *m;

  if( length < 4 + ( with_loop ? 4 : 0 ) + COLUMNS ) return NULL;
  m = new_module( rows );
  if( !m ) return NULL;
  read_interleaved( m, data + 4, rows * COLUMNS, rows, COLUMNS );
  if( with_loop ) {
    /* Stored either way round: the smaller one is right */
    unsigned long be = big_endian( data + length - 4, 4 );
    unsigned long le = little_endian( data + length - 4, 4 );
    m->loop_row = be < le ? be : le;
  }
  return m;
}

/* YM5!, YM6!: a header, the digital drums (skipped), title, author and
   comment, then 16 registers of each frame */
static ymplayer_module *
load_ym5( const unsigned char *data, size_t length )
{
  unsigned long rows, attributes, clock, loop, size;
  unsigned int drums, rate;
  size_t position;
  ymplayer_module *m;

  if( length < 34 ) return NULL;
  rows = big_endian( data + 12, 4 );
  attributes = big_endian( data + 16, 4 );
  drums = big_endian( data + 20, 2 );
  clock = big_endian( data + 22, 4 );
  rate = big_endian( data + 26, 2 );
  loop = big_endian( data + 28, 4 );
  position = 34 + big_endian( data + 32, 2 );

  while( drums-- ) {
    if( position + 4 > length ) return NULL;
    size = big_endian( data + position, 4 );
    if( size > length - position - 4 ) return NULL;
    position += 4 + size;
  }

  m = new_module( rows );
  if( !m ) return NULL;
  if( !read_string( data, length, &position, m->title ) ||
      !read_string( data, length, &position, m->author ) ||
      !read_string( data, length, &position, m->comment ) ) {
    ymplayer_free( m );
    return NULL;
  }

  if( attributes & 1 )
    read_interleaved( m, data + position, length - position, rows, 16 );
  else
    read_frames( m, data + position, length - position, 16 );

  if( clock >= CLOCK_MIN && clock <= CLOCK_MAX ) m->clock = clock;
  if( rate >= RATE_MIN && rate <= RATE_MAX ) m->rate = rate;
  m->loop_row = loop;
  return m;
}

static int
is_ym( const unsigned char *data, size_t length )
{
  if( length < 4 || data[0] != 'Y' || data[1] != 'M' ) return 0;
  if( !memcmp( data + 2, "2!", 2 ) || !memcmp( data + 2, "3!", 2 ) ||
      !memcmp( data + 2, "3b", 2 ) )
    return 1;
  return length >= 12 && ( !memcmp( data + 2, "5!LeOnArD!", 10 ) ||
                           !memcmp( data + 2, "6!LeOnArD!", 10 ) );
}

static ymplayer_module *
load_ym( const unsigned char *data, size_t length )
{
  return data[2] == '5' || data[2] == '6' ? load_ym5( data, length ) :
                                            load_ym3( data, length );
}

/* An LHA archive (level 0 header) holding a YM file: -lh5- packed or
   -lh0- stored */
static int
is_packed_ym( const unsigned char *data, size_t length )
{
  size_t start;

  if( length < 22 || data[20] != 0 ) return 0;
  if( memcmp( data + 2, "-lh5-", 5 ) && memcmp( data + 2, "-lh0-", 5 ) )
    return 0;
  start = 2 + data[0];
  return data[0] >= 20 && start + little_endian( data + 7, 4 ) <= length;
}

static ymplayer_module *
load_packed_ym( const unsigned char *data )
{
  size_t start = 2 + data[0];
  unsigned long packed = little_endian( data + 7, 4 );
  unsigned long original = little_endian( data + 11, 4 );
  unsigned char *unpacked;
  ymplayer_module *m = NULL;

  if( original < 4 || original > DUMP_MAX + 0x10000 ) return NULL;
  unpacked = malloc( original );
  if( !unpacked ) return NULL;

  if( !memcmp( data + 2, "-lh0-", 5 ) ) {
    if( packed >= original ) memcpy( unpacked, data + start, original );
    else original = 0;
  } else if( lh5_decode( data + start, packed, unpacked, original, NULL ) ) {
    original = 0;
  }

  if( original && is_ym( unpacked, original ) )
    m = load_ym( unpacked, original );
  free( unpacked );
  return m;
}

/* VTX: "ay"/"ym" (or "AY"/"YM" in the old version), the stereo layout,
   loop frame, chip clock, frame rate, (year), the unpacked length, title,
   author, (program, editor, comment), then the registers packed with
   -lh5-, interleaved */
static int
vtx_header( const unsigned char *data, size_t length, int *new_version,
            unsigned long *unpacked )
{
  unsigned int type, rate;
  unsigned long clock;

  if( length < 16 ) return 0;
  type = little_endian( data, 2 );
  if( type == 0x7961 || type == 0x6d79 ) *new_version = 1;
  else if( type == 0x5941 || type == 0x4d59 ) *new_version = 0;
  else return 0;

  clock = little_endian( data + 5, 4 );
  rate = data[9];
  *unpacked = little_endian( data + ( *new_version ? 12 : 10 ), 4 );
  return ( data[2] & 7 ) <= 6 && rate >= RATE_MIN && rate <= RATE_MAX &&
         clock >= CLOCK_MIN && clock <= CLOCK_MAX &&
         *unpacked >= (unsigned long)COLUMNS * RATE_MIN &&
         *unpacked <= (unsigned long)COLUMNS * RATE_MAX * 30 * 60 &&
         *unpacked % COLUMNS == 0;
}

static ymplayer_module *
load_vtx( const unsigned char *data, size_t length )
{
  int new_version;
  unsigned long unpacked_length;
  size_t position;
  unsigned char *unpacked;
  char ignored[ STRING_LENGTH + 1 ];
  ymplayer_module *m;

  if( !vtx_header( data, length, &new_version, &unpacked_length ) )
    return NULL;
  m = new_module( unpacked_length / COLUMNS );
  if( !m ) return NULL;
  m->loop_row = little_endian( data + 3, 2 );
  m->layout = data[2] & 7;
  m->clock = little_endian( data + 5, 4 );
  m->rate = data[9];

  position = new_version ? 16 : 14;
  if( !read_string( data, length, &position, m->title ) ||
      !read_string( data, length, &position, m->author ) ||
      ( new_version &&
        ( !read_string( data, length, &position, ignored ) ||
          !read_string( data, length, &position, ignored ) ||
          !read_string( data, length, &position, m->comment ) ) ) ) {
    ymplayer_free( m );
    return NULL;
  }

  unpacked = malloc( unpacked_length );
  if( !unpacked || lh5_decode( data + position, length - position, unpacked,
                               unpacked_length, NULL ) ) {
    free( unpacked );
    ymplayer_free( m );
    return NULL;
  }
  read_interleaved( m, unpacked, unpacked_length, m->rows_count, COLUMNS );
  free( unpacked );
  return m;
}

int
ymplayer_check( const unsigned char *data, size_t length )
{
  int new_version;
  unsigned long unpacked;

  return is_ym( data, length ) || is_packed_ym( data, length ) ||
         vtx_header( data, length, &new_version, &unpacked );
}

ymplayer_module *
ymplayer_load( const unsigned char *data, size_t length )
{
  ymplayer_module *m;

  if( is_ym( data, length ) ) m = load_ym( data, length );
  else if( is_packed_ym( data, length ) ) m = load_packed_ym( data );
  else m = load_vtx( data, length );

  if( !m ) return NULL;
  if( m->loop_row < 0 || m->loop_row >= m->rows_count ) m->loop_row = 0;
  ymplayer_reset( m );
  return m;
}

void
ymplayer_free( ymplayer_module *module )
{
  if( !module ) return;
  free( module->rows );
  free( module );
}

/* Frames of the file to frames of the player */
static long
to_player_frames( const ymplayer_module *m, long rows )
{
  return ( rows * FRAME_RATE + m->rate - 1 ) / m->rate;
}

long
ymplayer_frames( const ymplayer_module *module )
{
  return to_player_frames( module, module->rows_count );
}

long
ymplayer_loop( const ymplayer_module *module )
{
  return to_player_frames( module, module->loop_row );
}

const char *ymplayer_title( const ymplayer_module *m ) { return m->title; }
const char *ymplayer_author( const ymplayer_module *m ) { return m->author; }
const char *ymplayer_comment( const ymplayer_module *m ) { return m->comment; }

const char *
ymplayer_stereo( const ymplayer_module *module )
{
  static const char * const names[] =
    { "Mono", "ABC", "ACB", "BAC", "BCA", "CAB", "CBA" };
  return module->layout >= 0 && module->layout <= 6 ?
         names[ module->layout ] : "";
}

void
ymplayer_reset( ymplayer_module *module )
{
  module->next_row = 0;
  /* The first frame of the player plays the first one of the file */
  module->rate_left = FRAME_RATE - module->rate;
  memset( module->current, 0, sizeof( module->current ) );
}

/* A period of the file's chip for the Spectrum's AY */
static unsigned long
scale( const ymplayer_module *m, unsigned long period, unsigned long max )
{
  /* The Spectrum's clock (within 0.1%): as it is */
  if( m->clock > AY_CLOCK - 2000 && m->clock < AY_CLOCK + 2000 )
    return period;
  period = ( period * AY_CLOCK + m->clock / 2 ) / m->clock;
  return period > max ? max : period;
}

void
ymplayer_frame( ymplayer_module *m,
                unsigned char registers[ YMPLAYER_REGISTERS ],
                unsigned int *mask )
{
  unsigned char next[ YMPLAYER_REGISTERS ];
  int envelope = -1, reg, channel;

  *mask = 0;

  /* The frames of the file in this one of the player (none, one or more,
     when the rates differ): the last one counts, the envelope shape is
     written if any of them wrote it */
  m->rate_left += m->rate;
  if( m->rate_left < FRAME_RATE ) return;
  memcpy( next, m->current, sizeof( next ) );
  while( m->rate_left >= FRAME_RATE ) {
    const unsigned char *row = m->rows + m->next_row * COLUMNS;

    m->rate_left -= FRAME_RATE;
    memcpy( next, row, 13 );
    if( row[13] != NOT_WRITTEN ) envelope = row[13] & 0x0f;
    if( ++m->next_row >= m->rows_count ) m->next_row = m->loop_row;
  }

  /* The bits the chip has, and the periods for its clock */
  for( channel = 0; channel < 3; channel++ ) {
    unsigned long period = next[ channel * 2 ] |
                           ( next[ channel * 2 + 1 ] & 0x0f ) << 8;
    period = scale( m, period, 0x0fff );
    next[ channel * 2 ] = period & 0xff;
    next[ channel * 2 + 1 ] = period >> 8;
    next[ 8 + channel ] &= 0x1f;
  }
  next[6] = scale( m, next[6] & 0x1f, 0x1f );
  {
    unsigned long period = scale( m, next[11] | next[12] << 8, 0xffff );
    next[11] = period & 0xff;
    next[12] = period >> 8;
  }

  for( reg = 0; reg < 13; reg++ ) {
    if( next[ reg ] == m->current[ reg ] ) continue;
    m->current[ reg ] = registers[ reg ] = next[ reg ];
    *mask |= 1u << reg;
  }
  if( envelope >= 0 ) {
    m->current[13] = registers[13] = envelope;
    *mask |= 1u << 13;
  }
}

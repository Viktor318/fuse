/* psgplayer.c: PSG (Programmable Sound Generator) register dump player
   Copyright (c) 2026 Viktor (GitHub: Viktor318)
   The format as ZXTune (https://zxtune.bitbucket.io/), Copyright (c)
   Vitamin/CAIG <vitamin.caig@gmail.com>, reads it
   (src/formats/chiptune/aym/psg.cpp, src/module/players/aym/psg.cpp).

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

#include "psgplayer.h"

#define HEADER_LENGTH 16
#define VERSION_OFFSET 4

#define FRAME 0xff		/* a new frame */
#define SKIP 0xfe		/* 4 * n frames */
#define END 0xfd		/* the end of the music */

struct psgplayer_module {
  unsigned char *data;
  size_t length;
  size_t start;			/* the first byte after the header */
  long frames;

  /* Playing */
  size_t position;		/* the next byte to read */
  long empty;			/* frames of a skip still to come */
  int ended;
};

int
psgplayer_check( const unsigned char *data, size_t length )
{
  return length > HEADER_LENGTH && !memcmp( data, "PSG\x1a", 4 );
}

/* Some emulators write the frames right after the "PSG", 1Ah: their
   "version" is the first FFh */
static size_t
data_start( const unsigned char *data )
{
  return data[ VERSION_OFFSET ] == FRAME ? VERSION_OFFSET : HEADER_LENGTH;
}

/* Skip the register writes at *position, up to the next frame mark; 0 if
   the data ends or is damaged first (a register over 15 ends it, as in
   ZXTune) */
static int
skip_writes( const unsigned char *data, size_t length, size_t *position )
{
  while( *position < length ) {
    unsigned char reg = data[ *position ];
    if( reg == FRAME || reg == SKIP ) return 1;
    if( reg > 15 || *position + 1 >= length ) return 0;
    *position += 2;
  }
  return 0;
}

/* The frames of the file: every FFh one, every FEh n 4 * n; the writes
   before the first mark belong to no frame */
static long
count_frames( const unsigned char *data, size_t length, size_t start )
{
  size_t position = start;
  long frames = 0;

  if( !skip_writes( data, length, &position ) ) return 0;
  while( position < length ) {
    if( data[ position ] == FRAME ) {
      frames++;
      position++;
    } else if( data[ position ] == SKIP ) {
      if( position + 1 >= length ) break;
      frames += 4L * data[ position + 1 ];
      position += 2;
    } else {
      break;
    }
    if( !skip_writes( data, length, &position ) ) break;
  }
  return frames;
}

psgplayer_module *
psgplayer_load( const unsigned char *data, size_t length )
{
  psgplayer_module *module;
  size_t start;
  long frames;

  if( !psgplayer_check( data, length ) ) return NULL;
  start = data_start( data );
  frames = count_frames( data, length, start );
  if( frames <= 0 ) return NULL;

  module = calloc( 1, sizeof( *module ) );
  if( !module ) return NULL;
  module->data = malloc( length );
  if( !module->data ) {
    free( module );
    return NULL;
  }
  memcpy( module->data, data, length );
  module->length = length;
  module->start = start;
  module->frames = frames;
  psgplayer_reset( module );
  return module;
}

void
psgplayer_free( psgplayer_module *module )
{
  if( !module ) return;
  free( module->data );
  free( module );
}

long
psgplayer_frames( const psgplayer_module *module )
{
  return module->frames;
}

void
psgplayer_reset( psgplayer_module *module )
{
  module->position = module->start;
  module->empty = 0;
  module->ended = 0;
  /* To the first frame mark */
  if( !skip_writes( module->data, module->length, &module->position ) )
    module->ended = 1;
}

/* The register writes of a frame, up to the next mark */
static void
read_writes( psgplayer_module *m, unsigned char *registers,
             unsigned int *mask )
{
  while( m->position < m->length ) {
    unsigned char reg = m->data[ m->position ];

    if( reg == FRAME || reg == SKIP ) return;
    if( reg > 15 || m->position + 1 >= m->length ) break;
    if( reg < PSGPLAYER_REGISTERS ) {
      registers[ reg ] = m->data[ m->position + 1 ];
      *mask |= 1u << reg;
    }
    m->position += 2;
  }
  /* The end mark, the end of the data or damaged data */
  m->position = m->length;
}

void
psgplayer_frame( psgplayer_module *m,
                 unsigned char registers[ PSGPLAYER_REGISTERS ],
                 unsigned int *mask )
{
  *mask = 0;

  /* A skip: its frames write nothing but the last one, which gets the
     writes after it */
  if( m->empty > 0 ) {
    if( --m->empty == 0 ) read_writes( m, registers, mask );
    return;
  }

  while( !m->ended ) {
    unsigned char mark;

    if( m->position >= m->length ) {
      m->ended = 1;
      return;
    }
    mark = m->data[ m->position ];
    if( mark == FRAME ) {
      m->position++;
      read_writes( m, registers, mask );
      return;
    }
    if( mark == SKIP && m->position + 1 < m->length ) {
      long frames = 4L * m->data[ m->position + 1 ];
      m->position += 2;
      /* FEh 0: no frames, the writes go on in the frame before */
      if( frames == 0 ) {
        read_writes( m, registers, mask );
        continue;
      }
      m->empty = frames - 1;
      if( m->empty == 0 ) read_writes( m, registers, mask );
      return;
    }
    m->ended = 1;
  }
}

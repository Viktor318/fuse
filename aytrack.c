/* aytrack.c: What the AY tracker players share: the patterns, the track
   cursor and the building of the registers of a frame
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   After ZXTune (https://zxtune.bitbucket.io/), Copyright (c) Vitamin/CAIG
   <vitamin.caig@gmail.com>, GNU LGPL version 3: the track model and cursor
   (src/module/players/track_model.h, tracking.h) and the track builder
   (src/module/players/aym/aym_base_track.cpp).

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

#include <stdlib.h>
#include <string.h>

#include "aytrack.h"

/* ------------------------------------------------------------------------
   The patterns
   ------------------------------------------------------------------------ */

int
aytrack_init( aytrack *t, int pattern_count )
{
  memset( t, 0, sizeof( *t ) );
  t->patterns = calloc( pattern_count, sizeof( *t->patterns ) );
  if( !t->patterns ) return 0;
  t->pattern_count = pattern_count;
  return 1;
}

void
aytrack_free( aytrack *t )
{
  int i;

  if( !t->patterns ) return;
  for( i = 0; i < t->pattern_count; i++ ) free( t->patterns[i].lines );
  free( t->patterns );
  t->patterns = NULL;
}

/* Room for `count' lines in a pattern (new ones empty) */
static int
grow( aytrack_pattern *p, int count )
{
  aytrack_line *lines;
  int allocated = p->allocated ? p->allocated : 16;

  if( count <= p->allocated && p->lines ) return 1;
  while( allocated < count ) allocated *= 2;
  if( allocated > AYTRACK_MAX_LINES ) allocated = AYTRACK_MAX_LINES;
  lines = realloc( p->lines, allocated * sizeof( *lines ) );
  if( !lines ) return 0;
  memset( lines + p->allocated, 0,
          ( allocated - p->allocated ) * sizeof( *lines ) );
  p->lines = lines;
  p->allocated = allocated;
  return 1;
}

int
aytrack_pattern_lines( aytrack *t, int pattern )
{
  if( pattern < 0 || pattern >= t->pattern_count ) return 0;
  return grow( &t->patterns[ pattern ], 1 );
}

void
aytrack_pattern_size( aytrack *t, int pattern, int size )
{
  if( pattern < 0 || pattern >= t->pattern_count ) return;
  if( size > AYTRACK_MAX_LINES ) size = AYTRACK_MAX_LINES;
  /* As ZXTune: never smaller than the lines there are */
  if( size > t->patterns[ pattern ].size ) t->patterns[ pattern ].size = size;
}

aytrack_line *
aytrack_start_line( aytrack *t, int pattern, int line )
{
  aytrack_pattern *p;

  if( pattern < 0 || pattern >= t->pattern_count || line < 0 ||
      line >= AYTRACK_MAX_LINES )
    return NULL;
  p = &t->patterns[ pattern ];
  if( !grow( p, line + 1 ) ) return NULL;
  p->lines[ line ].present = 1;
  if( line + 1 > p->size ) p->size = line + 1;
  return &p->lines[ line ];
}

void
aytrack_add_command( aytrack_cell *cell, int type, int p1, int p2, int p3 )
{
  aytrack_command *c;

  if( cell->command_count >= AYTRACK_MAX_COMMANDS ) return;
  c = &cell->commands[ cell->command_count++ ];
  c->type = type;
  c->param1 = p1;
  c->param2 = p2;
  c->param3 = p3;
}

aytrack_command *
aytrack_find_command( aytrack_cell *cell, int type )
{
  int i;

  for( i = 0; i < cell->command_count; i++ )
    if( cell->commands[i].type == type ) return &cell->commands[i];
  return NULL;
}

int
aytrack_positions_valid( const aytrack *t )
{
  int i;

  if( t->position_count <= 0 ) return 0;
  for( i = 0; i < t->position_count; i++ ) {
    int p = t->positions[i].pattern;
    if( p < 0 || p >= t->pattern_count || !t->patterns[p].lines ) return 0;
  }
  return 1;
}

/* ------------------------------------------------------------------------
   The cursor
   ------------------------------------------------------------------------ */

static const aytrack_line *
current_line( const aytrack *t )
{
  const aytrack_pattern *p;

  if( t->pattern < 0 || t->pattern >= t->pattern_count ) return NULL;
  p = &t->patterns[ t->pattern ];
  if( !p->lines || t->line >= p->size || t->line >= p->allocated )
    return NULL;
  return p->lines[ t->line ].present ? &p->lines[ t->line ] : NULL;
}

static int
line_valid( const aytrack *t )
{
  if( t->pattern < 0 || t->pattern >= t->pattern_count ) return 0;
  return t->patterns[ t->pattern ].lines &&
         t->line < t->patterns[ t->pattern ].size;
}

/* A line starts: its tempo, if it sets one */
static void
line_loaded( aytrack *t )
{
  const aytrack_line *line = current_line( t );

  if( line && line->tempo ) t->tempo = line->tempo;
}

static void
set_position( aytrack *t, int position )
{
  t->position = position;
  t->pattern = position < t->position_count ?
               t->positions[ position ].pattern : 0;
  t->line = 0;
  t->quirk = 0;
  line_loaded( t );
}

void
aytrack_reset( aytrack *t )
{
  t->tempo = t->initial_tempo;
  t->loops = 0;
  set_position( t, 0 );
}

static int
advance_line( aytrack *t )
{
  t->line++;
  t->quirk = 0;
  line_loaded( t );
  return line_valid( t );
}

static int
next_position( aytrack *t )
{
  set_position( t, t->position + 1 );
  return t->position < t->position_count;
}

const aytrack_line *
aytrack_new_line( const aytrack *t )
{
  return t->quirk == 0 ? current_line( t ) : NULL;
}

int
aytrack_transposition( const aytrack *t )
{
  return t->position < t->position_count ?
         t->positions[ t->position ].transposition : 0;
}

void
aytrack_next_frame( aytrack *t )
{
  if( ++t->quirk < t->tempo ) return;
  if( advance_line( t ) ) return;
  if( next_position( t ) ) return;

  /* The end: from the start to the loop position, so that the tempo is
     what it is there */
  t->tempo = t->initial_tempo;
  set_position( t, 0 );
  while( t->position < t->position_count &&
         t->position != t->loop_position ) {
    if( !advance_line( t ) ) next_position( t );
  }
  t->loops++;
}

/* Two hours at most */
#define MAX_LENGTH ( 2L * 60 * 60 * 50 )

long
aytrack_length( aytrack *t, long *loop )
{
  long frames = 0;

  *loop = 0;
  aytrack_reset( t );
  while( t->loops == 0 && frames < MAX_LENGTH ) {
    if( t->position == t->loop_position && t->line == 0 && t->quirk == 0 &&
        !*loop )
      *loop = frames;
    aytrack_next_frame( t );
    frames++;
  }
  /* A loop position past the last one: as ZXTune, the loop is the end */
  if( t->loop_position >= t->position_count ) *loop = frames;
  aytrack_reset( t );
  return frames;
}

/* ------------------------------------------------------------------------
   The registers of a frame
   ------------------------------------------------------------------------ */

void
aytrack_registers_start( aytrack_registers *regs,
                         const unsigned short *table )
{
  memset( regs->r, 0, sizeof( regs->r ) );
  regs->mask = 1u << 7;		/* the mixer is always written */
  regs->table = table;
}

static int
clamp( int value, int low, int high )
{
  return value < low ? low : value > high ? high : value;
}

unsigned int
aytrack_frequency( const aytrack_registers *regs, int halftone )
{
  return regs->table[ clamp( halftone, 0, AYTABLE_NOTES - 1 ) ];
}

int
aytrack_sliding_difference( const aytrack_registers *regs, int from, int to )
{
  return (int)aytrack_frequency( regs, to ) -
         (int)aytrack_frequency( regs, from );
}

void
aytrack_set_tone_period( aytrack_registers *regs, int channel,
                         unsigned int period )
{
  regs->r[ channel * 2 ] = period & 0xff;
  regs->r[ channel * 2 + 1 ] = ( period >> 8 ) & 0xff;
  regs->mask |= 3u << ( channel * 2 );
}

void
aytrack_set_tone( aytrack_registers *regs, int channel, int halftones,
                  int offset )
{
  aytrack_set_tone_period( regs, channel,
                           ( aytrack_frequency( regs, halftones ) + offset ) &
                           0xfff );
}

void
aytrack_set_level( aytrack_registers *regs, int channel, int level )
{
  regs->r[ 8 + channel ] = clamp( level, 0, 15 );
  regs->mask |= 1u << ( 8 + channel );
}

void
aytrack_disable_tone( aytrack_registers *regs, int channel )
{
  regs->r[7] |= 1 << channel;
}

void
aytrack_disable_noise( aytrack_registers *regs, int channel )
{
  regs->r[7] |= 8 << channel;
}

/* As ZXTune: the envelope bit goes on whatever the level register holds
   (set or not) */
void
aytrack_enable_envelope( aytrack_registers *regs, int channel )
{
  regs->r[ 8 + channel ] |= 16;
  regs->mask |= 1u << ( 8 + channel );
}

void
aytrack_set_noise( aytrack_registers *regs, unsigned int noise )
{
  regs->r[6] = noise & 31;
  regs->mask |= 1u << 6;
}

void
aytrack_set_envelope_type( aytrack_registers *regs, unsigned int type )
{
  regs->r[13] = type & 0xff;
  regs->mask |= 1u << 13;
}

void
aytrack_set_envelope_tone( aytrack_registers *regs, unsigned int tone )
{
  regs->r[11] = tone & 0xff;
  regs->r[12] = ( tone >> 8 ) & 0xff;
  regs->mask |= 3u << 11;
}

void
aytrack_registers_end( const aytrack_registers *regs,
                       unsigned char *registers, unsigned int *mask )
{
  int reg;

  for( reg = 0; reg < AYTRACK_REGISTERS; reg++ )
    if( regs->mask & ( 1u << reg ) ) registers[ reg ] = regs->r[ reg ];
  *mask = regs->mask;
}

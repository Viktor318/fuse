/* aytrack.h: What the AY tracker players share: the patterns, the track
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

#ifndef FUSE_AYTRACK_H
#define FUSE_AYTRACK_H

#include "aytables.h"

#define AYTRACK_CHANNELS 3
#define AYTRACK_MAX_COMMANDS 8
#define AYTRACK_MAX_POSITIONS 256
#define AYTRACK_MAX_LINES 256
#define AYTRACK_REGISTERS 14

/* ------------------------------------------------------------------------
   The patterns
   ------------------------------------------------------------------------ */

/* A command of a cell: its meaning is the player's */
typedef struct aytrack_command {
  int type;
  int param1, param2, param3;
} aytrack_command;

/* What a pattern line sets on a channel */
typedef struct aytrack_cell {
  int has_enabled, enabled;
  int has_note; int note;
  int has_sample; int sample;
  int has_ornament; int ornament;
  int has_volume; int volume;
  int command_count;
  aytrack_command commands[ AYTRACK_MAX_COMMANDS ];
} aytrack_cell;

typedef struct aytrack_line {
  int present;			/* the line has data */
  int tempo;			/* 0: no change */
  aytrack_cell cells[ AYTRACK_CHANNELS ];
} aytrack_line;

typedef struct aytrack_pattern {
  int size;			/* lines; no lines: no such pattern */
  aytrack_line *lines;
} aytrack_pattern;

typedef struct aytrack_position {
  int pattern;
  int transposition;
} aytrack_position;

typedef struct aytrack {
  int initial_tempo;
  int position_count, loop_position;
  aytrack_position positions[ AYTRACK_MAX_POSITIONS ];
  int pattern_count;
  aytrack_pattern *patterns;

  /* The cursor */
  int position, pattern, line, quirk, tempo;
  int loops;
} aytrack;

/* 0 if out of memory */
int aytrack_init( aytrack *t, int pattern_count );
void aytrack_free( aytrack *t );

/* The lines of a pattern, made for `lines' lines (the size is the last
   line used + 1 until set with aytrack_pattern_size); NULL if it cannot be */
aytrack_line *aytrack_pattern_lines( aytrack *t, int pattern );
void aytrack_pattern_size( aytrack *t, int pattern, int size );
/* Line `line' of a pattern, marked present (the size grows to it); NULL if
   out of range */
aytrack_line *aytrack_start_line( aytrack *t, int pattern, int line );

void aytrack_add_command( aytrack_cell *cell, int type, int p1, int p2,
                          int p3 );
aytrack_command *aytrack_find_command( aytrack_cell *cell, int type );

/* Whether all the positions have their patterns */
int aytrack_positions_valid( const aytrack *t );

/* ------------------------------------------------------------------------
   The cursor
   ------------------------------------------------------------------------ */

void aytrack_reset( aytrack *t );

/* The line starting in this frame (NULL: none, or not a new line) */
const aytrack_line *aytrack_new_line( const aytrack *t );
int aytrack_transposition( const aytrack *t );
/* After the frame: on to the next one (and line, and position; after the
   last one to the loop position) */
void aytrack_next_frame( aytrack *t );

/* The length of the module in frames, up to the end of its last position,
   and the frame of the loop position (*loop); as ZXTune tells them. The
   cursor is left at the start */
long aytrack_length( aytrack *t, long *loop );

/* ------------------------------------------------------------------------
   The registers of a frame (AYM::TrackBuilder)
   ------------------------------------------------------------------------ */

typedef struct aytrack_registers {
  unsigned char r[ AYTRACK_REGISTERS ];
  unsigned int mask;		/* those set */
  const unsigned short *table;	/* note periods */
} aytrack_registers;

/* A new frame: the mixer 0 (all on), nothing else set */
void aytrack_registers_start( aytrack_registers *regs,
                              const unsigned short *table );
void aytrack_set_tone( aytrack_registers *regs, int channel, int halftones,
                       int offset );
void aytrack_set_tone_period( aytrack_registers *regs, int channel,
                              unsigned int period );
void aytrack_set_level( aytrack_registers *regs, int channel, int level );
void aytrack_disable_tone( aytrack_registers *regs, int channel );
void aytrack_disable_noise( aytrack_registers *regs, int channel );
void aytrack_enable_envelope( aytrack_registers *regs, int channel );
void aytrack_set_noise( aytrack_registers *regs, unsigned int noise );
void aytrack_set_envelope_type( aytrack_registers *regs, unsigned int type );
void aytrack_set_envelope_tone( aytrack_registers *regs, unsigned int tone );
/* The period of a note (clamped to the table) */
unsigned int aytrack_frequency( const aytrack_registers *regs, int halftone );
int aytrack_sliding_difference( const aytrack_registers *regs, int from,
                                int to );

/* Copy the registers set to `registers' and their mask */
void aytrack_registers_end( const aytrack_registers *regs,
                            unsigned char *registers, unsigned int *mask );

#endif			/* #ifndef FUSE_AYTRACK_H */

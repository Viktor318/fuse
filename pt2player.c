/* pt2player.c: Pro Tracker 2 (.pt2) module player
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   Based on ZXTune (https://zxtune.bitbucket.io/), Copyright (c)
   Vitamin/CAIG <vitamin.caig@gmail.com>, GNU LGPL version 3: the module
   parser (src/formats/chiptune/aym/protracker2.cpp) and the player
   (src/module/players/aym/protracker2.cpp).

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

/* The module: a header (tempo, length, loop position, the offsets of 32
   samples and 16 ornaments, the offset of the patterns, 30 characters of
   title), the positions (pattern numbers) up to 0xff, the patterns (the
   offsets of the 3 channels' data, 6 bytes each) right after them, then
   the patterns' data, the samples (size, loop, 3 bytes a line) and the
   ornaments (size, loop, a halftone offset a line) */

#include <stdlib.h>
#include <string.h>

#include "aytrack.h"
#include "pt2player.h"

#define MIN_SIZE 100
#define MAX_SIZE 0x3800
#define HEADER_SIZE 132		/* up to the first position */
#define FORMAT_SIZE 133		/* the bytes ZXTune's format pattern checks */
#define MAX_POSITIONS 255
#define MIN_PATTERN_SIZE 5
#define MAX_PATTERN_SIZE 64
#define MAX_PATTERNS 32
#define MAX_SAMPLES 32
#define MAX_ORNAMENTS 16
#define MAX_LINES 256
#define PATTERN_ENTRY 6
#define POS_END_MARKER 0xff
#define DEFAULT_SAMPLE 1
#define DEFAULT_ORNAMENT 0

/* Offsets in the header */
#define OFFSET_TEMPO 0
#define OFFSET_LOOP 2
#define OFFSET_SAMPLES 3
#define OFFSET_ORNAMENTS 67
#define OFFSET_PATTERNS 99
#define OFFSET_NAME 101
#define NAME_SIZE 30
#define OFFSET_POSITIONS 131

/* The target note of no glissando to a note */
#define LIMITER -1

/* Commands of the cells */
enum { CMD_ENVELOPE = 1, CMD_NOENVELOPE, CMD_GLISS, CMD_GLISS_NOTE,
       CMD_NOGLISS, CMD_NOISE_ADD };

typedef struct sample_line {
  int level;			/* 0-15 */
  int noise;			/* 0-31 */
  int tone_mask, noise_mask;
  int vibrato;
} sample_line;

/* Lines beyond those there are read as this (ZXTune's stub line) */
static const sample_line stub_line = { 0, 0, 1, 1, 0 };

/* No lines: a missing sample or ornament (ZXTune's stub objects) */
typedef struct sample {
  int size, loop;
  sample_line lines[ MAX_LINES ];
} sample;

typedef struct ornament {
  int size, loop;
  int lines[ MAX_LINES ];
} ornament;

typedef struct channel {
  int enabled, envelope;
  int note;
  int sample, position_in_sample;
  int ornament, position_in_ornament;
  int volume;
  int noise_add;
  int sliding, sliding_target_note, glissade;
} channel;

struct pt2_module {
  char title[ NAME_SIZE + 1 ];
  aytrack track;
  sample samples[ MAX_SAMPLES ];
  ornament ornaments[ MAX_ORNAMENTS ];

  channel channels[3];
};

/* ------------------------------------------------------------------------
   Reading the module
   ------------------------------------------------------------------------ */

static unsigned int
word( const unsigned char *p )
{
  return p[0] | p[1] << 8;
}

/* The size of the header up to and with the end marker of the positions,
   if all the positions are patterns there can be; 0 if not */
static size_t
header_size( const unsigned char *data, size_t length )
{
  size_t i;

  if( length < HEADER_SIZE ) return 0;
  for( i = OFFSET_POSITIONS; i < length; i++ ) {
    if( data[i] == POS_END_MARKER ) return i + 1;
    if( data[i] >= MAX_PATTERNS ) return 0;
  }
  return 0;
}

/* ZXTune's format pattern */
static int
format_matches( const unsigned char *data, size_t length )
{
  int i;

  if( length < FORMAT_SIZE ) return 0;
  if( data[0] < 2 || data[1] < 1 || data[2] > 0xfe ) return 0;
  /* The high bytes of the offsets of the samples and ornaments */
  for( i = 0; i < MAX_SAMPLES + MAX_ORNAMENTS; i++ )
    if( data[ OFFSET_SAMPLES + i * 2 + 1 ] > 0x36 ) return 0;
  if( data[ OFFSET_PATTERNS + 1 ] > 0x01 ) return 0;
  if( data[ OFFSET_POSITIONS ] > 0x1f ) return 0;
  if( data[ OFFSET_POSITIONS + 1 ] != POS_END_MARKER &&
      data[ OFFSET_POSITIONS + 1 ] > 0x1f )
    return 0;
  return 1;
}

/* ZXTune's FastCheck: the positions, and the patterns right after them
   (ZXTune's areas of the header, the patterns and the end) */
static int
fast_check( const unsigned char *data, size_t length )
{
  size_t size = header_size( data, length ), patterns;

  if( size < HEADER_SIZE + 1 || size > HEADER_SIZE + MAX_POSITIONS )
    return 0;
  patterns = word( data + OFFSET_PATTERNS );
  return patterns == size && patterns < length;
}

int
pt2_check( const unsigned char *data, size_t length )
{
  if( length > MAX_SIZE ) length = MAX_SIZE;
  return format_matches( data, length ) && fast_check( data, length );
}

typedef struct parser {
  const unsigned char *data;
  size_t length;
  size_t used;			/* the end of the areas used */
  int error;
  int used_samples[ MAX_SAMPLES ];
  int used_ornaments[ MAX_ORNAMENTS ];
} parser;

/* An area of the module: it must be in the data */
static void
add_range( parser *p, size_t offset, size_t size )
{
  if( offset + size > p->length ) {
    p->error = 1;
    return;
  }
  if( offset + size > p->used ) p->used = offset + size;
}

static int
byte_at( parser *p, size_t offset )
{
  if( offset >= p->length ) {
    p->error = 1;
    return 0;
  }
  return p->data[ offset ];
}

static unsigned int
word_at( parser *p, size_t offset )
{
  if( offset + 2 > p->length ) {
    p->error = 1;
    return 0;
  }
  return word( p->data + offset );
}

typedef struct channel_cursor {
  size_t offset;
  int period, counter;
} channel_cursor;

/* A command of a cell. All of them only set the state of the channel, so
   a later command of a type makes an earlier one of it no matter: that
   one is dropped (so that a cell has at most one command of each type).
   ZXTune puts the note after a glissando to a note into the first such
   command: if that one has been dropped, the note is lost (*lost) */
static void
add_command( aytrack_cell *cell, int *lost, int type, int param1,
             int param2 )
{
  int i;

  for( i = 0; i < cell->command_count; i++ ) {
    if( cell->commands[i].type != type ) continue;
    if( type == CMD_GLISS_NOTE ) *lost = 1;
    memmove( &cell->commands[i], &cell->commands[ i + 1 ],
             ( cell->command_count - i - 1 ) * sizeof( cell->commands[0] ) );
    cell->command_count--;
    break;
  }
  aytrack_add_command( cell, type, param1, param2, 0 );
}

static void
parse_channel( parser *p, channel_cursor *c, aytrack_line *line,
               aytrack_cell *cell )
{
  int lost = 0;

  while( c->offset < p->length && !p->error ) {
    int cmd = byte_at( p, c->offset++ );

    if( cmd == 0 ) {
      continue;
    } else if( cmd >= 0xe1 ) {	/* sample */
      cell->has_sample = 1;
      cell->sample = cmd - 0xe0;
      p->used_samples[ cmd - 0xe0 ] = 1;
    } else if( cmd == 0xe0 ) {	/* rest */
      cell->has_enabled = 1;
      cell->enabled = 0;
      break;
    } else if( cmd >= 0x80 ) {	/* note */
      aytrack_command *gliss;
      cell->has_enabled = 1;
      cell->enabled = 1;
      gliss = aytrack_find_command( cell, CMD_GLISS_NOTE );
      if( lost ) {
        /* into the glissando dropped */
      } else if( gliss ) {
        gliss->param2 = cmd - 0x80;
      } else {
        cell->has_note = 1;
        cell->note = cmd - 0x80;
      }
      break;
    } else if( cmd == 0x7f ) {	/* envelope off */
      add_command( cell, &lost, CMD_NOENVELOPE, 0, 0 );
    } else if( cmd >= 0x71 ) {	/* envelope */
      unsigned int tone = word_at( p, c->offset );
      c->offset += 2;
      add_command( cell, &lost, CMD_ENVELOPE, cmd - 0x70, tone );
    } else if( cmd == 0x70 ) {	/* end of the cell */
      break;
    } else if( cmd >= 0x60 ) {	/* ornament */
      cell->has_ornament = 1;
      cell->ornament = cmd - 0x60;
      p->used_ornaments[ cmd - 0x60 ] = 1;
    } else if( cmd >= 0x20 ) {	/* lines to skip */
      c->period = cmd - 0x20;
    } else if( cmd >= 0x10 ) {	/* volume */
      cell->has_volume = 1;
      cell->volume = cmd - 0x10;
    } else if( cmd == 0x0f ) {	/* tempo, not checked */
      line->tempo = byte_at( p, c->offset++ );
    } else if( cmd == 0x0e ) {	/* glissando */
      int step = (signed char)byte_at( p, c->offset++ );
      add_command( cell, &lost, CMD_GLISS, step, 0 );
    } else if( cmd == 0x0d ) {	/* glissando to a note */
      int step = (signed char)byte_at( p, c->offset++ );
      unsigned int limit = word_at( p, c->offset );
      c->offset += 2;
      add_command( cell, &lost, CMD_GLISS_NOTE, step, limit );
    } else if( cmd == 0x0c ) {	/* glissando off */
      add_command( cell, &lost, CMD_NOGLISS, 0, 0 );
    } else {			/* noise offset */
      int value = (signed char)byte_at( p, c->offset++ );
      add_command( cell, &lost, CMD_NOISE_ADD, value, 0 );
    }
  }
}

static int
has_line( parser *p, const channel_cursor *c )
{
  int chan;

  for( chan = 0; chan < 3; chan++ ) {
    if( c[ chan ].counter ) continue;
    if( c[ chan ].offset >= p->length ||
        ( chan == 0 && p->data[ c[ chan ].offset ] == 0x00 ) )
      return 0;
  }
  return 1;
}

/* Whether the pattern has enough lines; an error if its data is not
   where it can be */
static int
parse_pattern( parser *p, aytrack *t, int index, size_t patterns,
               size_t min_offset )
{
  size_t entry = patterns + index * PATTERN_ENTRY, starts[3];
  channel_cursor c[3];
  int line, chan;

  add_range( p, entry, PATTERN_ENTRY );
  if( p->error ) return 0;
  memset( c, 0, sizeof( c ) );
  for( chan = 0; chan < 3; chan++ ) {
    starts[ chan ] = word( p->data + entry + chan * 2 );
    if( starts[ chan ] < min_offset || starts[ chan ] > p->length - 1 ) {
      p->error = 1;
      return 0;
    }
    c[ chan ].offset = starts[ chan ];
  }
  if( !aytrack_pattern_lines( t, index ) ) {
    p->error = 1;
    return 0;
  }

  for( line = 0; line < MAX_PATTERN_SIZE && !p->error; line++ ) {
    int skip = c[0].counter;
    aytrack_line *l;

    for( chan = 1; chan < 3; chan++ )
      if( c[ chan ].counter < skip ) skip = c[ chan ].counter;
    if( skip ) {
      for( chan = 0; chan < 3; chan++ ) c[ chan ].counter -= skip;
      line += skip;
    }
    if( !has_line( p, c ) ) {
      aytrack_pattern_size( t, index,
                            line > MIN_PATTERN_SIZE ? line : MIN_PATTERN_SIZE );
      break;
    }
    l = aytrack_start_line( t, index, line );
    if( !l ) {
      p->error = 1;
      return 0;
    }
    for( chan = 0; chan < 3; chan++ ) {
      if( c[ chan ].counter ) {
        c[ chan ].counter--;
        continue;
      }
      parse_channel( p, &c[ chan ], l, &l->cells[ chan ] );
      c[ chan ].counter = c[ chan ].period;
    }
  }
  if( p->error ) return 0;

  for( chan = 0; chan < 3; chan++ ) {
    size_t stop = c[ chan ].offset + 1;
    if( stop > p->length ) stop = p->length;
    add_range( p, starts[ chan ], stop - starts[ chan ] );
  }
  return line >= MIN_PATTERN_SIZE;
}

/* nnnnnsTN aaaaHHHH LLLLLLLL: noise, vibrato sign, tone and noise off,
   level, vibrato */
static void
parse_sample_line( sample_line *line, const unsigned char *src )
{
  int vibrato = ( src[1] & 0x0f ) << 8 | src[2];

  line->noise_mask = ( src[0] & 1 ) != 0;
  line->tone_mask = ( src[0] & 2 ) != 0;
  line->noise = src[0] >> 3;
  line->level = src[1] >> 4;
  line->vibrato = src[0] & 4 ? vibrato : -vibrato;
}

/* `lines' lines of the sample at `src' (of its size, the rest stubs); as
   ZXTune, the lines are found by 8-bit offsets */
static void
parse_sample( sample *s, const unsigned char *src, int lines )
{
  int i;

  s->size = src[0];
  for( i = 0; i < s->size; i++ ) s->lines[i] = stub_line;
  for( i = 0; i < lines; i++ ) {
    unsigned char offset = (unsigned char)( i * 3 ), line[3];
    line[0] = src[ 2 + offset++ ];
    line[1] = src[ 2 + offset++ ];
    line[2] = src[ 2 + offset ];
    parse_sample_line( &s->lines[i], line );
  }
  s->loop = src[1] < s->size ? src[1] : s->size;
}

static void
parse_ornament( ornament *o, const unsigned char *src, int lines )
{
  int i;

  o->size = src[0];
  for( i = 0; i < o->size; i++ ) o->lines[i] = 0;
  for( i = 0; i < lines; i++ )
    o->lines[i] = (signed char)src[ 2 + (unsigned char)i ];
  o->loop = src[1] < o->size ? src[1] : o->size;
}

/* The samples used; at least one of them must be there, whole or not */
static void
parse_samples( parser *p, pt2_module *m )
{
  int i, any = 0;

  for( i = 0; i < MAX_SAMPLES; i++ ) {
    size_t offset = word( p->data + OFFSET_SAMPLES + i * 2 );
    sample *s = &m->samples[i];

    if( !p->used_samples[i] ) continue;
    s->size = s->loop = 0;
    if( !offset ) {
      /* As ZXTune: a line read from the start of the module */
      parse_sample_line( &s->lines[0], p->data );
      s->size = 1;
    } else if( offset + 2 <= p->length ) {
      size_t avail = p->length - offset, used;
      used = p->data[ offset ] * 3;
      used = 2 + ( used < 256 ? used : 256 );
      if( used <= avail ) {
        add_range( p, offset, used );
        parse_sample( s, p->data + offset, p->data[ offset ] );
      } else {
        add_range( p, offset, avail );
        parse_sample( s, p->data + offset, ( avail - 2 ) / 3 );
      }
      any = 1;
    }
    /* else a stub */
  }
  if( !any ) p->error = 1;
}

static void
parse_ornaments( parser *p, pt2_module *m )
{
  int i;

  for( i = 0; i < MAX_ORNAMENTS; i++ ) {
    size_t offset = word( p->data + OFFSET_ORNAMENTS + i * 2 );
    ornament *o = &m->ornaments[i];

    if( !p->used_ornaments[i] ) continue;
    o->size = o->loop = 0;
    if( !offset ) {
      /* As ZXTune: a line read from the start of the module */
      o->lines[0] = (signed char)p->data[0];
      o->size = 1;
    } else if( offset + 2 <= p->length ) {
      size_t avail = p->length - offset, used = 2 + p->data[ offset ];
      if( used <= avail ) {
        add_range( p, offset, used );
        parse_ornament( o, p->data + offset, p->data[ offset ] );
      } else {
        add_range( p, offset, avail );
        parse_ornament( o, p->data + offset, avail - 2 );
      }
    }
    /* else a stub */
  }
}

/* The title as ZXTune shows it: without what is not printable ASCII (or
   spaces) at its ends, and with '?' for that inside */
static void
read_title( pt2_module *m, const unsigned char *data )
{
  const unsigned char *name = data + OFFSET_NAME;
  int start = 0, end = NAME_SIZE, i;

  while( start < end && ( name[ start ] <= ' ' || name[ start ] > 0x7e ) )
    start++;
  while( end > start && ( name[ end - 1 ] <= ' ' || name[ end - 1 ] > 0x7e ) )
    end--;
  for( i = start; i < end; i++ )
    m->title[ i - start ] = name[i] < ' ' || name[i] > 0x7e ? '?' : name[i];
  m->title[ end - start ] = '\0';
}

pt2_module *
pt2_load( const unsigned char *data, size_t length )
{
  parser p;
  pt2_module *m;
  int i, count, used_patterns[ MAX_PATTERNS ], last_pattern = -1;
  int any_pattern = 0;
  size_t size, patterns, min_offset;

  if( length > MAX_SIZE ) length = MAX_SIZE;
  if( !pt2_check( data, length ) ) return NULL;
  /* The tempo (as the format pattern wants it) */
  if( data[ OFFSET_TEMPO ] < 2 ) return NULL;

  size = header_size( data, length );
  memset( &p, 0, sizeof( p ) );
  p.data = data;
  p.length = length;
  p.used = size;

  m = calloc( 1, sizeof( *m ) );
  if( !m ) return NULL;
  if( !aytrack_init( &m->track, MAX_PATTERNS ) ) {
    free( m );
    return NULL;
  }
  m->track.initial_tempo = data[ OFFSET_TEMPO ];
  read_title( m, data );

  /* Positions: up to the end marker (all of them patterns there can be);
     the loop position as it is */
  count = size - 1 - OFFSET_POSITIONS;
  memset( used_patterns, 0, sizeof( used_patterns ) );
  for( i = 0; i < count; i++ ) {
    int pattern = data[ OFFSET_POSITIONS + i ];
    m->track.positions[i].pattern = pattern;
    m->track.positions[i].transposition = 0;
    used_patterns[ pattern ] = 1;
    if( pattern > last_pattern ) last_pattern = pattern;
  }
  m->track.position_count = count;
  m->track.loop_position = data[ OFFSET_LOOP ];

  /* Patterns: the used ones; their data after the entry of the last one
     of them, and one with enough lines */
  patterns = word( data + OFFSET_PATTERNS );
  min_offset = patterns + last_pattern * PATTERN_ENTRY;
  p.used_samples[ DEFAULT_SAMPLE ] = 1;
  p.used_ornaments[ DEFAULT_ORNAMENT ] = 1;
  for( i = 0; i < MAX_PATTERNS && !p.error; i++ ) {
    if( !used_patterns[i] ) continue;
    if( parse_pattern( &p, &m->track, i, patterns, min_offset ) )
      any_pattern = 1;
  }
  if( !any_pattern ) p.error = 1;

  if( !p.error ) parse_samples( &p, m );
  if( !p.error ) parse_ornaments( &p, m );

  if( p.error || p.used < MIN_SIZE ||
      !aytrack_positions_valid( &m->track ) ) {
    pt2_free( m );
    return NULL;
  }

  pt2_reset( m );
  return m;
}

void
pt2_free( pt2_module *m )
{
  if( !m ) return;
  aytrack_free( &m->track );
  free( m );
}

const char *
pt2_title( const pt2_module *m )
{
  return m->title;
}

long
pt2_length( pt2_module *m, long *loop )
{
  /* A loop position after the last one (the header's loop is not
     checked): the loop frame is the end, as aytrack_length gives it */
  long frames = aytrack_length( &m->track, loop );

  pt2_reset( m );
  return frames;
}

/* ------------------------------------------------------------------------
   The player
   ------------------------------------------------------------------------ */

void
pt2_reset( pt2_module *m )
{
  int i;

  for( i = 0; i < 3; i++ ) {
    channel *c = &m->channels[i];
    memset( c, 0, sizeof( *c ) );
    c->sample = DEFAULT_SAMPLE;
    c->ornament = DEFAULT_ORNAMENT;
    c->volume = 15;
    c->sliding_target_note = LIMITER;
  }
  aytrack_reset( &m->track );
}

static const sample *
sample_of( const pt2_module *m, int index )
{
  static const sample stub;

  return index >= 0 && index < MAX_SAMPLES ? &m->samples[ index ] : &stub;
}

static const ornament *
ornament_of( const pt2_module *m, int index )
{
  static const ornament stub;

  return index >= 0 && index < MAX_ORNAMENTS ? &m->ornaments[ index ] :
                                               &stub;
}

static unsigned int
volume_of( unsigned int volume, unsigned int level )
{
  return ( volume * 17 + ( volume > 7 ? 1 : 0 ) ) * level / 256;
}

static void
new_state( channel *c, const aytrack_cell *cell, aytrack_registers *regs )
{
  int i;

  if( cell->has_enabled ) {
    c->enabled = cell->enabled;
    if( !c->enabled ) {
      c->sliding = c->glissade = 0;
      c->sliding_target_note = LIMITER;
    }
    c->position_in_sample = c->position_in_ornament = 0;
  }
  if( cell->has_note ) {
    c->note = cell->note;
    c->sliding = c->glissade = 0;
    c->sliding_target_note = LIMITER;
  }
  if( cell->has_sample ) {
    c->sample = cell->sample;
    c->position_in_sample = 0;
  }
  if( cell->has_ornament ) {
    c->ornament = cell->ornament;
    c->position_in_ornament = 0;
  }
  if( cell->has_volume ) c->volume = cell->volume;
  for( i = 0; i < cell->command_count; i++ ) {
    const aytrack_command *cmd = &cell->commands[i];
    switch( cmd->type ) {
    case CMD_ENVELOPE:
      aytrack_set_envelope_type( regs, cmd->param1 );
      aytrack_set_envelope_tone( regs, cmd->param2 );
      c->envelope = 1;
      break;
    case CMD_NOENVELOPE:
      c->envelope = 0;
      break;
    case CMD_NOISE_ADD:
      c->noise_add = cmd->param1;
      break;
    case CMD_GLISS_NOTE:
      c->sliding = 0;
      c->glissade = cmd->param1;
      c->sliding_target_note = cmd->param2;
      break;
    case CMD_GLISS:
      c->glissade = cmd->param1;
      c->sliding_target_note = LIMITER;
      break;
    case CMD_NOGLISS:
      c->glissade = 0;
      break;
    }
  }
}

static void
synthesize( const pt2_module *m, channel *c, int chan,
            aytrack_registers *regs )
{
  const sample *s;
  const sample_line *line;
  const ornament *o;
  int halftones;

  if( !c->enabled ) {
    aytrack_set_level( regs, chan, 0 );
    return;
  }

  s = sample_of( m, c->sample );
  line = c->position_in_sample < s->size ?
         &s->lines[ c->position_in_sample ] : &stub_line;
  o = ornament_of( m, c->ornament );

  /* The tone */
  halftones = c->note + ( c->position_in_ornament < o->size ?
                          o->lines[ c->position_in_ornament ] : 0 );
  aytrack_set_tone( regs, chan, halftones, c->sliding + line->vibrato );
  if( line->tone_mask ) aytrack_disable_tone( regs, chan );
  /* The level and the envelope */
  aytrack_set_level( regs, chan, volume_of( c->volume, line->level ) );
  if( c->envelope ) aytrack_enable_envelope( regs, chan );
  /* The noise */
  if( !line->noise_mask )
    aytrack_set_noise( regs, (unsigned int)( line->noise + c->noise_add ) );
  else
    aytrack_disable_noise( regs, chan );

  /* The glissando to a note: there when it has gone that far */
  if( c->sliding_target_note != LIMITER ) {
    int range = aytrack_sliding_difference( regs, c->note,
                                            c->sliding_target_note );
    int real_range = range - ( c->sliding + c->glissade );

    if( ( c->glissade > 0 && real_range <= 0 ) ||
        ( c->glissade < 0 && real_range >= 0 ) ) {
      c->note = c->sliding_target_note;
      c->sliding_target_note = LIMITER;
      c->sliding = c->glissade = 0;
    }
  }
  c->sliding += c->glissade;

  if( ++c->position_in_sample >= s->size )
    c->position_in_sample = s->loop;
  if( ++c->position_in_ornament >= o->size )
    c->position_in_ornament = o->loop;
}

void
pt2_frame( pt2_module *m, unsigned char *registers, unsigned int *mask )
{
  aytrack_registers regs;
  const aytrack_line *line = aytrack_new_line( &m->track );
  int chan;

  aytrack_registers_start( &regs, aytable_protracker2 );
  if( line )
    for( chan = 0; chan < 3; chan++ )
      new_state( &m->channels[ chan ], &line->cells[ chan ], &regs );
  for( chan = 0; chan < 3; chan++ )
    synthesize( m, &m->channels[ chan ], chan, &regs );
  aytrack_registers_end( &regs, registers, mask );

  aytrack_next_frame( &m->track );
}

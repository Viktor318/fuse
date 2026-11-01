/* stcplayer.c: Sound Tracker (compiled, .stc) module player
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   Based on ZXTune (https://zxtune.bitbucket.io/), Copyright (c)
   Vitamin/CAIG <vitamin.caig@gmail.com>, GNU LGPL version 3: the module
   parser (src/formats/chiptune/aym/soundtracker_compiled.cpp) and the
   player (src/module/players/aym/soundtracker.cpp).

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

/* The module: a header (tempo, the offsets of the positions, ornaments
   and patterns, 18 characters of title or program name, size), up to 16
   samples of 99 bytes from offset 27 (number, 32 lines of 3 bytes, loop,
   loop size), the positions (count - 1, then pattern and transposition),
   up to 16 ornaments of 33 bytes (number, 32 halftone offsets) and up to
   32 pattern entries of 7 bytes (number, the offsets of the 3 channels'
   data) */

#include <stdlib.h>
#include <string.h>

#include "aytrack.h"
#include "stcplayer.h"

#define MIN_SIZE 128
#define MAX_SIZE 0x2600
#define HEADER_SIZE 27
#define SAMPLE_SIZE 32
#define SAMPLE_ENTRY ( 1 + SAMPLE_SIZE * 3 + 2 )
#define ORNAMENT_SIZE 32
#define ORNAMENT_ENTRY ( 1 + ORNAMENT_SIZE )
#define PATTERN_ENTRY 7
#define MIN_PATTERN_SIZE 5
#define MAX_PATTERN_SIZE 64
#define MAX_PATTERNS 32
#define MAX_SAMPLES 16
#define MAX_ORNAMENTS 16

/* Commands of the cells */
enum { CMD_ENVELOPE = 1, CMD_NOENVELOPE };

typedef struct sample_line {
  int level, noise, noise_mask, envelope_mask, effect;
} sample_line;

typedef struct sample {
  int present;
  int loop, loop_limit;
  sample_line lines[ SAMPLE_SIZE ];
} sample;

typedef struct ornament {
  int present;
  signed char lines[ ORNAMENT_SIZE ];
} ornament;

typedef struct channel {
  int note;
  int count_down, position;	/* the sample cursor; count_down -1: off */
  int sample, ornament;
  int envelope;			/* 0 off, 1 just set, 2 on */
} channel;

struct stc_module {
  char title[ 19 ];
  aytrack track;
  sample samples[ MAX_SAMPLES ];
  ornament ornaments[ MAX_ORNAMENTS ];

  channel channels[3];
  int envelope_type, envelope_tone;
};

/* ------------------------------------------------------------------------
   Reading the module
   ------------------------------------------------------------------------ */

static unsigned int
word( const unsigned char *p )
{
  return p[0] | p[1] << 8;
}

/* The size of an area: up to the nearest area after it (ZXTune's
   AreaController); -1 if none */
enum { AREA_HEADER, AREA_SAMPLES, AREA_POSITIONS, AREA_ORNAMENTS,
       AREA_PATTERNS, AREA_END, AREAS };
#define UNDEFINED ( (size_t)-1 )

static size_t
area_size( const size_t *areas, int area )
{
  size_t begin = areas[ area ], result = UNDEFINED;
  int i;

  for( i = 0; i < AREAS; i++ ) {
    if( i == area || areas[i] < begin ) continue;
    if( result == UNDEFINED || areas[i] - begin < result )
      result = areas[i] - begin;
  }
  return result;
}

/* A first ornament the samples or positions run into */
static int
overlaps_first_ornament( const size_t *areas, int area, size_t min_size )
{
  size_t start = areas[ area ], orn = areas[ AREA_ORNAMENTS ];
  return start < orn && start + min_size > orn &&
         start + min_size < orn + ORNAMENT_ENTRY;
}

int
stc_check( const unsigned char *data, size_t length )
{
  size_t areas[ AREAS ], size, required;

  if( length > MAX_SIZE ) length = MAX_SIZE;
  if( length < MIN_SIZE ) return 0;

  /* The bytes of typical modules */
  if( data[0] < 1 || data[0] > 0x20 || data[2] > 7 || data[4] > 7 ||
      data[6] > 8 || data[27] > 15 )
    return 0;

  areas[ AREA_HEADER ] = 0;
  areas[ AREA_SAMPLES ] = HEADER_SIZE;
  areas[ AREA_POSITIONS ] = word( data + 1 );
  areas[ AREA_ORNAMENTS ] = word( data + 3 );
  areas[ AREA_PATTERNS ] = word( data + 5 );
  areas[ AREA_END ] = length;

  if( area_size( areas, AREA_HEADER ) != HEADER_SIZE ||
      area_size( areas, AREA_END ) != UNDEFINED )
    return 0;

  size = area_size( areas, AREA_SAMPLES );
  if( size == UNDEFINED ) return 0;
  if( !( size && size % SAMPLE_ENTRY == 0 ) &&
      !overlaps_first_ornament( areas, AREA_SAMPLES, SAMPLE_ENTRY ) )
    return 0;

  size = area_size( areas, AREA_ORNAMENTS );
  if( !size || size == UNDEFINED || size % ORNAMENT_ENTRY ) return 0;

  if( areas[ AREA_POSITIONS ] >= length ) return 0;
  size = area_size( areas, AREA_POSITIONS );
  if( size == UNDEFINED ) return 0;
  required = 1 + ( data[ areas[ AREA_POSITIONS ] ] + 1 ) * 2;
  if( !( size && size == required ) &&
      !overlaps_first_ornament( areas, AREA_POSITIONS, required ) )
    return 0;

  return length >= areas[ AREA_ORNAMENTS ] + ORNAMENT_ENTRY &&
         length >= areas[ AREA_PATTERNS ] + PATTERN_ENTRY;
}

typedef struct parser {
  const unsigned char *data;
  size_t length;
  size_t used;			/* the end of the data read */
  int error;
  int used_samples[ MAX_SAMPLES ];
  int used_ornaments[ MAX_ORNAMENTS ];
  int any_sample, non_empty_pattern;
} parser;

static int
byte_at( parser *p, size_t offset )
{
  if( offset >= p->length ) {
    p->error = 1;
    return 0;
  }
  if( offset + 1 > p->used ) p->used = offset + 1;
  return p->data[ offset ];
}

/* An object of `size' bytes at `offset': it must be in the data */
static const unsigned char *
object_at( parser *p, size_t offset, size_t size )
{
  if( offset + size > p->length ) {
    p->error = 1;
    return NULL;
  }
  if( offset + size > p->used ) p->used = offset + size;
  return p->data + offset;
}

typedef struct channel_cursor {
  size_t offset;
  int period, counter;
} channel_cursor;

static void
parse_channel( parser *p, channel_cursor *c, aytrack_cell *cell )
{
  while( c->offset < p->length ) {
    int cmd = byte_at( p, c->offset++ );

    if( cmd <= 0x5f ) {		/* note */
      cell->has_enabled = 1;
      cell->enabled = 1;
      cell->has_note = 1;
      cell->note = cmd;
      p->non_empty_pattern = 1;
      break;
    } else if( cmd <= 0x6f ) {	/* sample */
      cell->has_sample = 1;
      cell->sample = cmd - 0x60;
      p->used_samples[ cmd - 0x60 ] = 1;
      p->any_sample = 1;
      if( cmd != 0x60 ) p->non_empty_pattern = 1;
    } else if( cmd <= 0x7f ) {	/* ornament, without envelope */
      aytrack_add_command( cell, CMD_NOENVELOPE, 0, 0, 0 );
      p->non_empty_pattern = 1;
      cell->has_ornament = 1;
      cell->ornament = cmd - 0x70;
      p->used_ornaments[ cmd - 0x70 ] = 1;
    } else if( cmd == 0x80 ) {	/* rest */
      cell->has_enabled = 1;
      cell->enabled = 0;
      break;
    } else if( cmd == 0x81 ) {	/* empty */
      break;
    } else if( cmd == 0x82 ) {	/* ornament 0 without envelope */
      cell->has_ornament = 1;
      cell->ornament = 0;
      aytrack_add_command( cell, CMD_NOENVELOPE, 0, 0, 0 );
      p->non_empty_pattern = 1;
    } else if( cmd <= 0x8e ) {	/* ornament 0 with an envelope */
      int period;
      cell->has_ornament = 1;
      cell->ornament = 0;
      period = byte_at( p, c->offset++ );
      aytrack_add_command( cell, CMD_ENVELOPE, cmd - 0x80, period, 0 );
      p->non_empty_pattern = 1;
    } else {			/* lines to skip */
      c->period = ( cmd + 0x100 - 0xa1 ) & 0xff;
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
        ( chan == 0 && p->data[ c[ chan ].offset ] == 0xff ) )
      return 0;
  }
  return 1;
}

static void
parse_pattern( parser *p, aytrack *t, int index, const unsigned char *entry )
{
  channel_cursor c[3];
  int line, chan;

  memset( c, 0, sizeof( c ) );
  for( chan = 0; chan < 3; chan++ ) c[ chan ].offset = word( entry + 1 + chan * 2 );
  if( !aytrack_pattern_lines( t, index ) ) {
    p->error = 1;
    return;
  }

  for( line = 0; line < MAX_PATTERN_SIZE; line++ ) {
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
    if( !l ) break;
    for( chan = 0; chan < 3; chan++ ) {
      if( c[ chan ].counter ) {
        c[ chan ].counter--;
        continue;
      }
      parse_channel( p, &c[ chan ], &l->cells[ chan ] );
      c[ chan ].counter = c[ chan ].period;
    }
  }
}

static void
parse_sample( sample *s, const unsigned char *src )
{
  int i, loop = src[ 1 + SAMPLE_SIZE * 3 ];
  int loop_size = src[ 2 + SAMPLE_SIZE * 3 ];

  s->present = 1;
  for( i = 0; i < SAMPLE_SIZE; i++ ) {
    /* EEEEaaaa NESnnnnn eeeeeeee: effect, level, noise mask, envelope
       mask, effect sign, noise */
    const unsigned char *l = src + 1 + i * 3;
    int effect = ( l[0] & 0xf0 ) * 16 + l[2];
    s->lines[i].level = l[0] & 15;
    s->lines[i].effect = l[1] & 32 ? effect : -effect;
    s->lines[i].noise = l[1] & 31;
    s->lines[i].noise_mask = ( l[1] & 128 ) != 0;
    s->lines[i].envelope_mask = ( l[1] & 64 ) != 0;
  }
  s->loop = loop < SAMPLE_SIZE ? loop : SAMPLE_SIZE;
  s->loop_limit = loop + loop_size < SAMPLE_SIZE ? loop + loop_size :
                                                   SAMPLE_SIZE;
}

/* Whether a sample is heard: an envelope or a level */
static int
sample_sounds( const sample *s )
{
  int i;

  for( i = 0; i < SAMPLE_SIZE; i++ )
    if( s->lines[i].envelope_mask || s->lines[i].level ) return 1;
  return 0;
}

/* The title, unless it is the name of the program that made the module */
static void
read_title( stc_module *m, const unsigned char *data )
{
  static const char * const programs[] = {
    "SONG BY ST COMPIL", "SONG BY MB COMPIL", "SONG BY ST-COMPIL",
    "SONG BY S.T.COMP", "SONG ST BY COMPILE", "SOUND TRACKER",
    "S.T.FULL EDITION", "S.W.COMPILE V2.0", "STU SONG COMPILER", NULL
  };
  char id[ 19 ];
  int i, start = 0, end = 18;

  memcpy( id, data + 7, 18 );
  id[18] = '\0';
  for( i = 0; programs[i]; i++ )
    if( !strncmp( id, programs[i], strlen( programs[i] ) ) ) return;
  for( i = 0; i < 18; i++ )
    if( (unsigned char)id[i] < 32 ) id[i] = ' ';
  while( start < end && id[ start ] == ' ' ) start++;
  while( end > start && id[ end - 1 ] == ' ' ) end--;
  memcpy( m->title, id + start, end - start );
  m->title[ end - start ] = '\0';
}

stc_module *
stc_load( const unsigned char *data, size_t length )
{
  parser p;
  stc_module *m;
  int i, count, any_pattern = 0, any_sound = 0;
  size_t positions, patterns, ornaments;
  int used_patterns[ MAX_PATTERNS ], used_count = 0, done[ MAX_PATTERNS ];

  if( length > MAX_SIZE ) length = MAX_SIZE;
  if( !stc_check( data, length ) ) return NULL;

  memset( &p, 0, sizeof( p ) );
  p.data = data;
  p.length = length;
  p.used = HEADER_SIZE;

  m = calloc( 1, sizeof( *m ) );
  if( !m ) return NULL;
  if( !aytrack_init( &m->track, MAX_PATTERNS ) ) {
    free( m );
    return NULL;
  }
  m->track.initial_tempo = data[0];
  read_title( m, data );

  /* Positions */
  positions = word( data + 1 );
  count = byte_at( &p, positions ) + 1;
  memset( used_patterns, 0, sizeof( used_patterns ) );
  for( i = 0; i < count && !p.error; i++ ) {
    const unsigned char *e = object_at( &p, positions + 1 + i * 2, 2 );
    if( !e || e[0] < 1 || e[0] > MAX_PATTERNS ) {
      p.error = 1;
      break;
    }
    m->track.positions[i].pattern = e[0] - 1;
    m->track.positions[i].transposition = (signed char)e[1];
    if( !used_patterns[ e[0] - 1 ] ) used_count++;
    used_patterns[ e[0] - 1 ] = 1;
  }
  m->track.position_count = count;
  m->track.loop_position = 0;

  /* Patterns: the used ones; missing ones are empty */
  patterns = word( data + 5 );
  memset( done, 0, sizeof( done ) );
  for( i = 0; i < MAX_PATTERNS && !p.error; i++ ) {
    const unsigned char *e = object_at( &p, patterns + i * PATTERN_ENTRY,
                                        PATTERN_ENTRY );
    int index;
    if( !e || e[0] < 1 || e[0] > MAX_PATTERNS ) break;
    index = e[0] - 1;
    if( !used_patterns[ index ] || done[ index ] ) continue;
    done[ index ] = 1;
    any_pattern = 1;
    parse_pattern( &p, &m->track, index, e );
    used_count--;
    if( !used_count ) break;
  }
  for( i = 0; i < MAX_PATTERNS; i++ )
    if( used_patterns[i] && !done[i] )
      aytrack_pattern_lines( &m->track, i );

  /* Samples: the used ones, up to the last one of them (as ZXTune, the
     table must be there that far); missing ones are silent */
  for( count = 0, i = 0; i < MAX_SAMPLES; i++ ) count += p.used_samples[i];
  for( i = 0; i < MAX_SAMPLES && !p.error && count; i++ ) {
    const unsigned char *s = object_at( &p, HEADER_SIZE + i * SAMPLE_ENTRY,
                                        SAMPLE_ENTRY );
    int index;
    if( !s ) break;
    index = s[0];
    if( index >= MAX_SAMPLES || !p.used_samples[ index ] ||
        m->samples[ index ].present )
      continue;
    parse_sample( &m->samples[ index ], s );
    if( sample_sounds( &m->samples[ index ] ) ) any_sound = 1;
    count--;
  }

  /* Ornaments: the used ones (0 always), the same way */
  p.used_ornaments[0] = 1;
  for( count = 0, i = 0; i < MAX_ORNAMENTS; i++ )
    count += p.used_ornaments[i];
  ornaments = word( data + 3 );
  for( i = 0; i < MAX_ORNAMENTS && !p.error && count; i++ ) {
    const unsigned char *o = object_at( &p, ornaments + i * ORNAMENT_ENTRY,
                                        ORNAMENT_ENTRY );
    if( !o ) break;
    if( o[0] >= MAX_ORNAMENTS || !p.used_ornaments[ o[0] ] ||
        m->ornaments[ o[0] ].present )
      continue;
    m->ornaments[ o[0] ].present = 1;
    memcpy( m->ornaments[ o[0] ].lines, o + 1, ORNAMENT_SIZE );
    count--;
  }

  if( p.error || !any_pattern || !p.non_empty_pattern || !p.any_sample || !any_sound ||
      p.used < MIN_SIZE || !aytrack_positions_valid( &m->track ) ) {
    stc_free( m );
    return NULL;
  }

  stc_reset( m );
  return m;
}

void
stc_free( stc_module *m )
{
  if( !m ) return;
  aytrack_free( &m->track );
  free( m );
}

const char *
stc_title( const stc_module *m )
{
  return m->title;
}

long
stc_length( stc_module *m, long *loop )
{
  long frames = aytrack_length( &m->track, loop );
  stc_reset( m );
  return frames;
}

/* ------------------------------------------------------------------------
   The player
   ------------------------------------------------------------------------ */

void
stc_reset( stc_module *m )
{
  int i;

  for( i = 0; i < 3; i++ ) {
    memset( &m->channels[i], 0, sizeof( m->channels[i] ) );
    m->channels[i].count_down = -1;
  }
  m->envelope_type = m->envelope_tone = 0;
  aytrack_reset( &m->track );
}

static const sample_line *
line_of( const stc_module *m, int sample, int position )
{
  static const sample_line stub = { 0, 0, 1, 1, 0 };

  if( sample < 0 || sample >= MAX_SAMPLES || !m->samples[ sample ].present )
    return &stub;
  return &m->samples[ sample ].lines[ position & 31 ];
}

static int
ornament_of( const stc_module *m, int ornament, int position )
{
  if( ornament < 0 || ornament >= MAX_ORNAMENTS ||
      !m->ornaments[ ornament ].present )
    return 0;
  return m->ornaments[ ornament ].lines[ position & 31 ];
}

/* The sample cursor one line on */
static void
cursor_next( const stc_module *m, int sample_index, int *count_down,
             int *position )
{
  const sample *s;
  int loop = 0, limit = 0;

  if( *count_down < 0 ) return;
  if( sample_index >= 0 && sample_index < MAX_SAMPLES &&
      m->samples[ sample_index ].present ) {
    s = &m->samples[ sample_index ];
    loop = s->loop;
    limit = s->loop_limit;
  }
  --*count_down;
  *position = ( *position + 1 ) & 0x1f;
  if( *count_down == 0 ) {
    if( loop ) {
      *position = loop & 0x1f;
      *count_down = limit - loop + 1;
    } else {
      *count_down = -1;
    }
  }
}

static void
new_state( stc_module *m, channel *c, const aytrack_cell *cell )
{
  int i;

  if( cell->has_enabled ) c->count_down = cell->enabled ? 32 : -1;
  if( cell->has_note ) {
    c->note = cell->note;
    c->position = 0;
  }
  if( cell->has_sample ) c->sample = cell->sample;
  if( cell->has_ornament ) c->ornament = cell->ornament;
  for( i = 0; i < cell->command_count; i++ ) {
    const aytrack_command *cmd = &cell->commands[i];
    if( cmd->type == CMD_ENVELOPE ) {
      m->envelope_type = cmd->param1;
      m->envelope_tone = cmd->param2;
      c->envelope = 1;
    } else if( cmd->type == CMD_NOENVELOPE ) {
      c->envelope = 0;
    }
  }
}

static void
synthesize( const stc_module *m, const channel *c, int chan,
            int transposition, aytrack_registers *regs )
{
  int count_down = c->count_down, position = c->position, next;
  const sample_line *line;

  cursor_next( m, c->sample, &count_down, &position );
  if( count_down < 0 ) {
    aytrack_set_level( regs, chan, 0 );
    return;
  }

  next = ( position - 1 ) & 0x1f;
  line = line_of( m, c->sample, next );
  aytrack_set_level( regs, chan, line->level );
  aytrack_set_tone( regs, chan,
                    c->note + ornament_of( m, c->ornament, next ) +
                    transposition,
                    line->effect );
  if( line->envelope_mask ) aytrack_disable_tone( regs, chan );
  if( !line->noise_mask ) aytrack_set_noise( regs, line->noise );
  else aytrack_disable_noise( regs, chan );
  if( c->envelope ) aytrack_enable_envelope( regs, chan );
}

void
stc_frame( stc_module *m, unsigned char *registers, unsigned int *mask )
{
  aytrack_registers regs;
  const aytrack_line *line = aytrack_new_line( &m->track );
  int chan, transposition;

  if( line )
    for( chan = 0; chan < 3; chan++ )
      new_state( m, &m->channels[ chan ], &line->cells[ chan ] );

  aytrack_registers_start( &regs, aytable_soundtracker );
  transposition = aytrack_transposition( &m->track );
  for( chan = 0; chan < 3; chan++ )
    synthesize( m, &m->channels[ chan ], chan, transposition, &regs );
  if( m->envelope_type ) {
    aytrack_set_envelope_type( &regs, m->envelope_type );
    aytrack_set_envelope_tone( &regs, m->envelope_tone );
  }
  aytrack_registers_end( &regs, registers, mask );

  /* On to the next frame */
  for( chan = 0; chan < 3; chan++ ) {
    channel *c = &m->channels[ chan ];
    cursor_next( m, c->sample, &c->count_down, &c->position );
    if( c->count_down >= 0 ) {
      if( c->envelope == 2 ) m->envelope_type = 0;
      else if( c->envelope == 1 ) {
        c->envelope = 2;
        m->envelope_type = 0;
      }
    }
  }
  aytrack_next_frame( &m->track );
}

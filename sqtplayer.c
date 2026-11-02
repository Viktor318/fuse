/* sqtplayer.c: SQ-Tracker (compiled, .sqt) module player
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   Based on ZXTune (https://zxtune.bitbucket.io/), Copyright (c)
   Vitamin/CAIG <vitamin.caig@gmail.com>, GNU LGPL version 3: the module
   parser (src/formats/chiptune/aym/sqtracker_compiled.cpp) and the player
   (src/module/players/aym/sqtracker.cpp).

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

/* The module: a header (the size, then the addresses of the tables of the
   samples, ornaments and patterns, of the positions and of the loop
   position), as the module was compiled at an address: the tables are
   1-based, so the first sample's entry is 2 bytes after the address of
   the samples. A sample is a loop, a loop size and 32 lines of 3 bytes,
   an ornament a loop, a loop size and 32 halftone offsets; a pattern its
   size and the data of one channel. A position is the pattern,
   transposition and attenuation of channels C, B and A, then the tempo;
   the positions end with pattern 0.

   As ZXTune, the channels' patterns of each different position are made
   into one pattern of three channels (the tempo and its changes on its
   lines, the effects of the channels where they are not enabled
   dropped), so that the track can be played as the other trackers'. */

#include <stdlib.h>
#include <string.h>

#include "aytrack.h"
#include "sqtplayer.h"

#define MIN_SIZE 256
#define MAX_SIZE 0x3600
#define HEADER_SIZE 12
#define FORMAT_SIZE 16		/* the bytes ZXTune's format pattern checks */
#define MIN_PATTERN_SIZE 11
#define MAX_PATTERN_SIZE 64
#define MAX_PATTERNS 99
#define MAX_SAMPLES 26
#define MAX_ORNAMENTS 26
#define SAMPLE_SIZE 32
#define ORNAMENT_SIZE 32
#define RAW_SAMPLE_SIZE ( 2 + SAMPLE_SIZE * 3 )
#define RAW_ORNAMENT_SIZE ( 2 + ORNAMENT_SIZE )
#define POSITION_SIZE 7
#define POSITION_CHANNEL_SIZE 2
/* The offset of the loop position's address in the header: the address
   of the samples is this much after the compile address */
#define MIN_SAMPLES_ADDRESS 10

/* Offsets in the header */
#define OFFSET_SAMPLES 2
#define OFFSET_ORNAMENTS 4
#define OFFSET_PATTERNS 6
#define OFFSET_POSITIONS 8
#define OFFSET_LOOP 10

/* Commands of the cells */
enum { CMD_TEMPO_ADDON = 1, CMD_ENVELOPE, CMD_GLISS, CMD_ATTENUATION,
       CMD_ATTENUATION_ADDON };

typedef struct sample_line {
  int level;			/* 0-15 */
  int noise;			/* 0-31 */
  int tone_deviation;
  int enable_noise, enable_tone;
} sample_line;

/* Lines beyond the 32 there are read as this (ZXTune's stub line) */
static const sample_line stub_line = { 0, 0, 0, 0, 0 };

typedef struct sample {
  unsigned int loop, loop_limit;
  sample_line lines[ SAMPLE_SIZE ];
} sample;

typedef struct ornament {
  unsigned int loop, loop_limit;
  int lines[ ORNAMENT_SIZE ];
} ornament;

typedef struct position_channel {
  int pattern;
  int transposition;
  unsigned int attenuation;
  int effects;			/* the effects are enabled */
} position_channel;

/* The channels are A, B, C here */
typedef struct position {
  int tempo;
  position_channel channels[3];
} position;

typedef struct channel {
  unsigned int note;
  unsigned int attenuation;
  int transposition;
  int sample;			/* 0: none, the channel is off */
  unsigned int sample_tick, sample_pos;
  int ornament;			/* 0: none */
  unsigned int ornament_tick, ornament_pos;
  int envelope;
  unsigned int sliding;		/* as ZXTune's int, without overflows */
  int glissade;
} channel;

struct sqt_module {
  aytrack track;
  int position_count, loop_position;
  position positions[ AYTRACK_MAX_POSITIONS ];
  sample samples[ MAX_SAMPLES + 1 ];
  ornament ornaments[ MAX_ORNAMENTS + 1 ];

  channel channels[3];
};

/* ------------------------------------------------------------------------
   Checking the module
   ------------------------------------------------------------------------ */

static unsigned int
word( const unsigned char *p )
{
  return p[0] | p[1] << 8;
}

/* ZXTune's format pattern (and the minimal size it is made with) */
static int
format_matches( const unsigned char *data, size_t length )
{
  if( length < MIN_SIZE || length < FORMAT_SIZE ) return 0;
  if( data[1] < 0x01 || data[1] > 0x30 ) return 0;
  if( data[3] && ( data[3] < 0x60 || data[3] > 0xfb ) ) return 0;
  if( data[5] && ( data[5] < 0x60 || data[5] > 0xfb ) ) return 0;
  if( data[7] && ( data[7] < 0x60 || data[7] > 0xfb ) ) return 0;
  if( !( ( data[9] >= 0x01 && data[9] <= 0x2d ) || data[9] >= 0x61 ) )
    return 0;
  if( !( ( data[11] >= 0x01 && data[11] <= 0x30 ) || data[11] >= 0x61 ) )
    return 0;
  if( data[13] && ( data[13] < 0x60 || data[13] > 0xfb ) ) return 0;
  if( data[15] > 0x01 && ( data[15] < 0x60 || data[15] > 0xfc ) ) return 0;
  return 1;
}

/* ZXTune's areas of the module: where its parts start, as offsets in the
   data (computed as ZXTune does, wrapping as its size_t) */
enum { AREA_HEADER, AREA_SAMPLES_OFFSETS, AREA_ORNAMENTS_OFFSETS,
       AREA_PATTERNS_OFFSETS, AREA_SAMPLES, AREA_ORNAMENTS, AREA_PATTERNS,
       AREA_POSITIONS, AREA_LOOPED_POSITION, AREA_END, AREAS };

#define UNDEFINED ( (size_t)-1 )

/* From an area to the next one (one that starts at or after it): the
   size of the area; 0 for an area not there */
static size_t
area_size( const size_t *areas, int key )
{
  size_t begin = areas[ key ], result = UNDEFINED;
  int i;

  if( begin == UNDEFINED ) return 0;
  for( i = 0; i < AREAS; i++ ) {
    if( areas[i] == UNDEFINED || areas[i] < begin || i == key ) continue;
    if( result == UNDEFINED || areas[i] - begin < result )
      result = areas[i] - begin;
  }
  return result;
}

static int
check_header( const size_t *areas )
{
  return area_size( areas, AREA_HEADER ) == HEADER_SIZE &&
         area_size( areas, AREA_END ) == UNDEFINED;
}

static int
check_offsets( const size_t *areas, int key )
{
  size_t size = area_size( areas, key );
  return size != UNDEFINED && size >= 2;
}

static int
check_positions( const size_t *areas )
{
  size_t size = area_size( areas, AREA_POSITIONS );

  if( areas[ AREA_LOOPED_POSITION ] != UNDEFINED ) {
    size_t rest;
    if( size == UNDEFINED || size % POSITION_SIZE ) return 0;
    rest = area_size( areas, AREA_LOOPED_POSITION );
    return rest != UNDEFINED && rest >= POSITION_SIZE + POSITION_CHANNEL_SIZE;
  }
  return size != UNDEFINED && size >= POSITION_SIZE + POSITION_CHANNEL_SIZE;
}

/* ZXTune's FastCheck: the order of the addresses in the header and the
   sizes of the areas between them */
static int
fast_check( const unsigned char *data, size_t length )
{
  size_t areas[ AREAS ], samples, ornaments, patterns, positions, loop;
  size_t delta, size;
  int i;

  if( length < HEADER_SIZE ) return 0;
  samples = word( data + OFFSET_SAMPLES );
  ornaments = word( data + OFFSET_ORNAMENTS );
  patterns = word( data + OFFSET_PATTERNS );
  positions = word( data + OFFSET_POSITIONS );
  loop = word( data + OFFSET_LOOP );
  if( samples < MIN_SAMPLES_ADDRESS || samples >= ornaments ||
      ornaments > patterns || patterns >= positions || positions > loop )
    return 0;
  delta = samples - MIN_SAMPLES_ADDRESS;

  for( i = 0; i < AREAS; i++ ) areas[i] = UNDEFINED;
  areas[ AREA_HEADER ] = 0;
  areas[ AREA_SAMPLES_OFFSETS ] = samples + 2 - delta;
  if( ornaments != patterns )
    areas[ AREA_ORNAMENTS_OFFSETS ] = ornaments + 2 - delta;
  areas[ AREA_PATTERNS_OFFSETS ] = patterns + 2 - delta;
  areas[ AREA_POSITIONS ] = positions - delta;
  if( positions != loop ) areas[ AREA_LOOPED_POSITION ] = loop - delta;
  areas[ AREA_END ] = length;
  if( check_header( areas ) ) {
    /* All the areas are in the data, so are the first entries */
    if( check_offsets( areas, AREA_SAMPLES_OFFSETS ) )
      areas[ AREA_SAMPLES ] =
        (size_t)word( data + areas[ AREA_SAMPLES_OFFSETS ] ) - delta;
    if( check_offsets( areas, AREA_ORNAMENTS_OFFSETS ) )
      areas[ AREA_ORNAMENTS ] =
        (size_t)word( data + areas[ AREA_ORNAMENTS_OFFSETS ] ) - delta;
  }

  if( !check_header( areas ) ) return 0;
  /* The samples */
  size = area_size( areas, AREA_SAMPLES );
  if( size == UNDEFINED || size < RAW_SAMPLE_SIZE ) return 0;
  /* The ornaments, if there is a table of them */
  if( areas[ AREA_ORNAMENTS_OFFSETS ] != UNDEFINED ) {
    if( !check_offsets( areas, AREA_ORNAMENTS_OFFSETS ) ) return 0;
    size = area_size( areas, AREA_ORNAMENTS );
    if( size == UNDEFINED || size < RAW_ORNAMENT_SIZE ) return 0;
  }
  /* The patterns and the positions */
  if( !check_offsets( areas, AREA_PATTERNS_OFFSETS ) ) return 0;
  return check_positions( areas );
}

int
sqt_check( const unsigned char *data, size_t length )
{
  if( length > MAX_SIZE ) length = MAX_SIZE;
  return format_matches( data, length ) && fast_check( data, length );
}

/* ------------------------------------------------------------------------
   Reading the module
   ------------------------------------------------------------------------ */

/* A line of a channel's pattern */
typedef struct raw_line {
  int present;
  int tempo;			/* 0: none */
  aytrack_cell cell;
} raw_line;

typedef struct raw_pattern {
  int size;
  raw_line lines[ MAX_PATTERN_SIZE ];
} raw_pattern;

typedef struct parser {
  const unsigned char *data;
  size_t length;
  size_t used;			/* the end of the areas used */
  size_t delta;			/* the compile address */
  int error;
  int used_patterns[ MAX_PATTERNS + 1 ];
  int used_samples[ MAX_SAMPLES + 1 ];
  int used_ornaments[ MAX_ORNAMENTS + 1 ];
  raw_pattern *patterns[ MAX_PATTERNS + 1 ];
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

static unsigned int
byte_at( parser *p, size_t offset )
{
  if( offset >= p->length ) {
    p->error = 1;
    return 0;
  }
  return p->data[ offset ];
}

/* The entry `index' of the table at the address in the header at
   `header', as an offset in the data */
static size_t
table_entry( parser *p, int header, int index )
{
  size_t entry = word( p->data + header ) + index * 2, address;

  if( entry < p->delta ) {
    p->error = 1;
    return 0;
  }
  entry -= p->delta;
  add_range( p, entry, 2 );
  if( p->error ) return 0;
  address = word( p->data + entry );
  if( address < p->delta ) {
    p->error = 1;
    return 0;
  }
  return address - p->delta;
}

/* What the parser sets on a cell. As ZXTune, only samples 1-26 and
   ornaments 1-26 can be used */
static void
set_sample( parser *p, aytrack_cell *cell, unsigned int sample )
{
  if( sample < 1 || sample > MAX_SAMPLES ) {
    p->error = 1;
    return;
  }
  p->used_samples[ sample ] = 1;
  cell->has_enabled = 1;
  cell->enabled = 1;
  cell->has_sample = 1;
  cell->sample = sample;
}

static void
set_ornament( parser *p, aytrack_cell *cell, unsigned int ornament )
{
  if( ornament < 1 || ornament > MAX_ORNAMENTS ) {
    p->error = 1;
    return;
  }
  p->used_ornaments[ ornament ] = 1;
  cell->has_ornament = 1;
  cell->ornament = ornament;
}

static void
set_rest( aytrack_cell *cell )
{
  cell->has_enabled = 1;
  cell->enabled = 0;
}

/* As ZXTune's unsigned note (it may have wrapped) */
static void
set_note( aytrack_cell *cell, unsigned int note )
{
  cell->has_note = 1;
  cell->note = (int)note;
}

/* An effect: there is one at most in a cell (so the cell's commands are
   always enough) */
static void
parse_effect( raw_line *line, unsigned int code, unsigned int param )
{
  aytrack_cell *cell = &line->cell;

  switch( code - 1 ) {
  case 0:
    aytrack_add_command( cell, CMD_ATTENUATION, param & 15, 0, 0 );
    break;
  case 1:
    aytrack_add_command( cell, CMD_ATTENUATION_ADDON, (signed char)param, 0,
                         0 );
    break;
  case 2:
    aytrack_add_command( cell, CMD_ATTENUATION, param, 1, 0 );
    break;
  case 3:
    aytrack_add_command( cell, CMD_ATTENUATION_ADDON, (signed char)param, 1,
                         0 );
    break;
  case 4:
    line->tempo = param & 31 ? param & 31 : 32;
    break;
  case 5:
    aytrack_add_command( cell, CMD_TEMPO_ADDON, param, 0, 0 );
    break;
  case 6:
    aytrack_add_command( cell, CMD_GLISS, -(int)param, 0, 0 );
    break;
  case 7:
    aytrack_add_command( cell, CMD_GLISS, param, 0, 0 );
    break;
  default:			/* as ZXTune, also for code 0 */
    aytrack_add_command( cell, CMD_ENVELOPE, ( code - 1 ) & 15, param, 0 );
    break;
  }
}

/* The sample, ornament and effect after a note; the offset after them */
static size_t
parse_note_parameters( parser *p, size_t cursor, raw_line *line )
{
  unsigned int cmd = byte_at( p, cursor++ );

  if( cmd & 128 ) {
    unsigned int sample = ( cmd >> 1 ) & 31;
    if( sample ) set_sample( p, &line->cell, sample );
    if( cmd & 64 ) {
      unsigned int param = byte_at( p, cursor++ );
      unsigned int ornament = ( param >> 4 ) | ( ( cmd & 1 ) << 4 );
      unsigned int effect = param & 15;
      if( ornament ) set_ornament( p, &line->cell, ornament );
      if( effect ) {
        unsigned int value = byte_at( p, cursor++ );
        parse_effect( line, effect, value );
      }
    }
  } else {
    unsigned int value = byte_at( p, cursor++ );
    parse_effect( line, cmd, value );
  }
  return cursor;
}

/* What the last note (or sample) set, again */
static void
parse_note( parser *p, size_t cursor, raw_line *line )
{
  unsigned int cmd = byte_at( p, cursor );

  if( cmd < 0x80 )
    parse_note_parameters( p, cursor + 1, line );
  else
    set_sample( p, &line->cell, cmd & 31 );
}

typedef struct pattern_state {
  unsigned int counter;
  size_t cursor;
  unsigned int last_note;
  size_t last_note_start;	/* 0: none (as ZXTune, read from there) */
  int repeat_last_note;
} pattern_state;

static void
parse_line( parser *p, pattern_state *s, raw_line *line )
{
  unsigned int cmd;

  s->repeat_last_note = 0;
  cmd = byte_at( p, s->cursor++ );
  if( cmd <= 0x5f ) {		/* a note and its parameters */
    s->last_note = cmd;
    s->last_note_start = s->cursor - 1;
    set_note( &line->cell, cmd );
    s->cursor = parse_note_parameters( p, s->cursor, line );
  } else if( cmd <= 0x6e ) {	/* an effect */
    unsigned int value = byte_at( p, s->cursor++ );
    parse_effect( line, cmd - 0x60, value );
  } else if( cmd == 0x6f ) {	/* rest */
    set_rest( &line->cell );
  } else if( cmd <= 0x7f ) {	/* rest and an effect */
    unsigned int value;
    set_rest( &line->cell );
    value = byte_at( p, s->cursor++ );
    parse_effect( line, cmd - 0x6f, value );
  } else if( cmd <= 0x9f ) {	/* the last note moved, its parameters */
    unsigned int addon = cmd & 15;
    if( !s->last_note_start ) {
      p->error = 1;
      return;
    }
    if( cmd & 16 )
      s->last_note -= addon;
    else
      s->last_note += addon;
    set_note( &line->cell, s->last_note );
    parse_note( p, s->last_note_start, line );
  } else if( cmd <= 0xbf ) {	/* lines to skip, maybe repeating */
    s->counter = cmd & 15;
    if( cmd & 16 ) {
      s->repeat_last_note = s->counter != 0;
      parse_note( p, s->last_note_start, line );
    }
  } else {			/* a sample */
    s->last_note_start = s->cursor - 1;
    set_sample( p, &line->cell, cmd & 31 );
  }
}

static void
parse_pattern( parser *p, int index )
{
  size_t start = table_entry( p, OFFSET_PATTERNS, index ), stop;
  pattern_state s;
  raw_pattern *pattern;
  int size, line;

  if( p->error ) return;
  size = byte_at( p, start );
  if( p->error || size < MIN_PATTERN_SIZE || size > MAX_PATTERN_SIZE ) {
    p->error = 1;
    return;
  }
  pattern = p->patterns[ index ] = calloc( 1, sizeof( *pattern ) );
  if( !pattern ) {
    p->error = 1;
    return;
  }
  pattern->size = size;

  memset( &s, 0, sizeof( s ) );
  s.cursor = start + 1;
  for( line = 0; line < size && !p->error; line++ ) {
    raw_line *l = &pattern->lines[ line ];
    if( s.counter ) {
      s.counter--;
      if( s.repeat_last_note ) {
        l->present = 1;
        parse_note( p, s.last_note_start, l );
      }
    } else {
      l->present = 1;
      parse_line( p, &s, l );
    }
  }
  if( p->error ) return;

  stop = s.cursor < p->length ? s.cursor : p->length;
  add_range( p, start, stop - start );
}

/* nnnnvvvv nTNDdddd dddddddd: noise high bits, level, noise low bit, tone
   and noise on, deviation sign and high bits, deviation low bits */
static void
parse_sample( sample *s, const unsigned char *src )
{
  int i;

  for( i = 0; i < SAMPLE_SIZE; i++ ) {
    const unsigned char *line = src + 2 + i * 3;
    sample_line *dst = &s->lines[i];
    int deviation = line[2] | ( line[1] & 15 ) << 8;
    dst->level = line[0] & 15;
    dst->noise = ( line[0] & 240 ) >> 3 | ( line[1] & 128 ) >> 7;
    dst->enable_noise = ( line[1] & 32 ) != 0;
    dst->enable_tone = ( line[1] & 64 ) != 0;
    dst->tone_deviation = line[1] & 16 ? deviation : -deviation;
  }
  s->loop = src[0] < SAMPLE_SIZE ? src[0] : SAMPLE_SIZE;
  s->loop_limit = s->loop + src[1] < SAMPLE_SIZE ? s->loop + src[1] :
                                                   SAMPLE_SIZE;
}

static void
parse_ornament( ornament *o, const unsigned char *src )
{
  int i;

  for( i = 0; i < ORNAMENT_SIZE; i++ ) o->lines[i] = (signed char)src[ 2 + i ];
  o->loop = src[0] < ORNAMENT_SIZE ? src[0] : ORNAMENT_SIZE;
  o->loop_limit = o->loop + src[1] < ORNAMENT_SIZE ? o->loop + src[1] :
                                                     ORNAMENT_SIZE;
}

/* The positions, up to the one with pattern 0 in a channel (or the end
   of the data, there with pattern 0 in channel C); the loop position is
   the one at the loop address (0 if none is) */
static void
parse_positions( parser *p, sqt_module *m )
{
  size_t offset = word( p->data + OFFSET_POSITIONS ) - p->delta;
  size_t loop = word( p->data + OFFSET_LOOP ) - p->delta;
  int count = 0, chan;

  m->loop_position = 0;
  for( ;; ) {
    const unsigned char *src;
    position *pos;

    if( offset == loop ) m->loop_position = count;
    if( offset + POSITION_SIZE > p->length ) {
      /* A partial position: it can only be the end */
      add_range( p, offset, POSITION_CHANNEL_SIZE );
      if( p->error || ( p->data[ offset ] & 127 ) ) {
        p->error = 1;
        return;
      }
      add_range( p, offset, p->length - offset );
      break;
    }
    add_range( p, offset, POSITION_SIZE );
    src = p->data + offset;
    if( !( src[0] & 127 ) || !( src[2] & 127 ) || !( src[4] & 127 ) ) break;
    /* More than the track can have: not played (ZXTune has no limit) */
    if( count >= AYTRACK_MAX_POSITIONS ) {
      p->error = 1;
      return;
    }
    pos = &m->positions[ count++ ];
    for( chan = 0; chan < 3; chan++ ) {
      const unsigned char *c = src + ( 2 - chan ) * 2;
      int transposition = c[1] >> 4;
      pos->channels[ chan ].pattern = c[0] & 127;
      pos->channels[ chan ].transposition =
        transposition < 9 ? transposition : -( transposition - 9 ) - 1;
      pos->channels[ chan ].attenuation = c[1] & 15;
      pos->channels[ chan ].effects = ( c[0] & 128 ) != 0;
      /* As ZXTune, only patterns 1-99 can be used */
      if( pos->channels[ chan ].pattern > MAX_PATTERNS ) {
        p->error = 1;
        return;
      }
      p->used_patterns[ pos->channels[ chan ].pattern ] = 1;
    }
    pos->tempo = src[6];
    offset += POSITION_SIZE;
  }
  if( !count ) p->error = 1;
  m->position_count = count;
}

static void
parse_samples( parser *p, sqt_module *m )
{
  int i, any = 0;

  for( i = 1; i <= MAX_SAMPLES && !p->error; i++ ) {
    size_t offset;
    if( !p->used_samples[i] ) continue;
    any = 1;
    offset = table_entry( p, OFFSET_SAMPLES, i );
    add_range( p, offset, RAW_SAMPLE_SIZE );
    if( !p->error ) parse_sample( &m->samples[i], p->data + offset );
  }
  if( !any ) p->error = 1;
}

static void
parse_ornaments( parser *p, sqt_module *m )
{
  int i;

  for( i = 1; i <= MAX_ORNAMENTS && !p->error; i++ ) {
    size_t offset;
    if( !p->used_ornaments[i] ) continue;
    offset = table_entry( p, OFFSET_ORNAMENTS, i );
    add_range( p, offset, RAW_ORNAMENT_SIZE );
    if( !p->error ) parse_ornament( &m->ornaments[i], p->data + offset );
  }
}

static int
same_position( const position *a, const position *b )
{
  int chan;

  if( a->tempo != b->tempo ) return 0;
  for( chan = 0; chan < 3; chan++ ) {
    const position_channel *x = &a->channels[ chan ], *y = &b->channels[ chan ];
    if( x->pattern != y->pattern || x->transposition != y->transposition ||
        x->attenuation != y->attenuation || x->effects != y->effects )
      return 0;
  }
  return 1;
}

static int
cell_has_data( const aytrack_cell *cell )
{
  return cell->has_enabled || cell->has_note || cell->has_sample ||
         cell->has_ornament || cell->has_volume || cell->command_count;
}

/* A channel's cell into the pattern of the position: the tempo addon (if
   the effects are enabled) is returned, not kept */
static unsigned int
convert_cell( const aytrack_cell *src, int effects, aytrack_cell *dst )
{
  unsigned int tempo_addon = 0;
  int i;

  if( src->has_enabled ) {
    dst->has_enabled = 1;
    dst->enabled = src->enabled;
  }
  if( src->has_note ) {
    dst->has_note = 1;
    dst->note = src->note;
  }
  if( src->has_sample ) {
    dst->has_sample = 1;
    dst->sample = src->sample;
  }
  if( src->has_ornament ) {
    dst->has_ornament = 1;
    dst->ornament = src->ornament;
  }
  for( i = 0; i < src->command_count; i++ ) {
    const aytrack_command *cmd = &src->commands[i];
    switch( cmd->type ) {
    case CMD_TEMPO_ADDON:
      if( effects ) tempo_addon += cmd->param1;
      break;
    case CMD_ATTENUATION:
    case CMD_ATTENUATION_ADDON:
      if( !effects ) break;
      /* fall through */
    default:
      aytrack_add_command( dst, cmd->type, cmd->param1, cmd->param2,
                           cmd->param3 );
      break;
    }
  }
  return tempo_addon;
}

/* The pattern of a position, as long as the pattern of channel C */
static void
convert_pattern( parser *p, aytrack *t, int index, const position *pos )
{
  const raw_pattern *raw[3];
  unsigned int tempo = pos->tempo, current_tempo = 0;
  int size, row, chan, i;

  for( chan = 0; chan < 3; chan++ )
    raw[ chan ] = p->patterns[ pos->channels[ chan ].pattern ];
  size = raw[2]->size;
  if( !aytrack_pattern_lines( t, index ) ) {
    p->error = 1;
    return;
  }

  for( row = 0; row < size; row++ ) {
    aytrack_line *out;
    unsigned int real_tempo;

    if( raw[0]->lines[ row ].present || raw[1]->lines[ row ].present ||
        raw[2]->lines[ row ].present ) {
      out = aytrack_start_line( t, index, row );
      /* Channels C, B, A */
      for( i = 0; i < 3; i++ ) {
        const raw_line *in;
        int effects;
        chan = 2 - i;
        in = &raw[ chan ]->lines[ row ];
        if( !in->present ) continue;
        effects = pos->channels[ chan ].effects;
        if( effects && in->tempo ) tempo = in->tempo;
        if( cell_has_data( &in->cell ) ) {
          unsigned int addon = convert_cell( &in->cell, effects,
                                             &out->cells[ chan ] );
          if( addon ) tempo = ( tempo + addon ) & 31;
        }
      }
    }
    real_tempo = tempo ? tempo : 32;
    if( real_tempo != current_tempo ) {
      out = aytrack_start_line( t, index, row );
      out->tempo = current_tempo = real_tempo;
    }
  }
  aytrack_pattern_size( t, index, size );
}

/* The track: a pattern for each different position */
static void
make_track( parser *p, sqt_module *m )
{
  int unique[ AYTRACK_MAX_POSITIONS ], patterns[ AYTRACK_MAX_POSITIONS ];
  int count = 0, i, j;

  for( i = 0; i < m->position_count; i++ ) {
    for( j = 0; j < count; j++ )
      if( same_position( &m->positions[ unique[j] ], &m->positions[i] ) )
        break;
    if( j == count ) unique[ count++ ] = i;
    patterns[i] = j;
  }

  if( !aytrack_init( &m->track, count ) ) {
    p->error = 1;
    return;
  }
  m->track.initial_tempo = 0;	/* the first line of a pattern sets it */
  for( i = 0; i < m->position_count; i++ ) {
    m->track.positions[i].pattern = patterns[i];
    m->track.positions[i].transposition = 0;
  }
  m->track.position_count = m->position_count;
  m->track.loop_position = m->loop_position;

  for( j = 0; j < count && !p->error; j++ )
    convert_pattern( p, &m->track, j, &m->positions[ unique[j] ] );
}

sqt_module *
sqt_load( const unsigned char *data, size_t length )
{
  parser p;
  sqt_module *m;
  int i;

  if( length > MAX_SIZE ) length = MAX_SIZE;
  if( !sqt_check( data, length ) ) return NULL;

  memset( &p, 0, sizeof( p ) );
  p.data = data;
  p.length = length;
  p.used = HEADER_SIZE;
  p.delta = word( data + OFFSET_SAMPLES ) - MIN_SAMPLES_ADDRESS;

  m = calloc( 1, sizeof( *m ) );
  if( !m ) return NULL;

  parse_positions( &p, m );
  for( i = 1; i <= MAX_PATTERNS && !p.error; i++ )
    if( p.used_patterns[i] ) parse_pattern( &p, i );
  if( !p.error ) parse_samples( &p, m );
  if( !p.error ) parse_ornaments( &p, m );
  if( !p.error && p.used < MIN_SIZE ) p.error = 1;

  if( !p.error ) make_track( &p, m );
  for( i = 0; i <= MAX_PATTERNS; i++ ) free( p.patterns[i] );

  if( p.error ) {
    sqt_free( m );
    return NULL;
  }

  sqt_reset( m );
  return m;
}

void
sqt_free( sqt_module *m )
{
  if( !m ) return;
  aytrack_free( &m->track );
  free( m );
}

const char *
sqt_title( const sqt_module *m )
{
  (void)m;
  return "";
}

long
sqt_length( sqt_module *m, long *loop )
{
  /* A loop position after the last one (at the end of the positions): the
     loop frame is the end, as aytrack_length gives it */
  long frames = aytrack_length( &m->track, loop );

  sqt_reset( m );
  return frames;
}

/* ------------------------------------------------------------------------
   The player
   ------------------------------------------------------------------------ */

void
sqt_reset( sqt_module *m )
{
  memset( m->channels, 0, sizeof( m->channels ) );
  aytrack_reset( &m->track );
}

/* A pattern starts: the attenuation and transposition of its position
   (after the last one: ZXTune's stub position, 0) */
static void
start_pattern( sqt_module *m, int index )
{
  static const position stub;
  const position *pos = index < m->position_count ?
                        &m->positions[ index ] : &stub;
  int chan;

  for( chan = 0; chan < 3; chan++ ) {
    m->channels[ chan ].attenuation = pos->channels[ chan ].attenuation;
    m->channels[ chan ].transposition = pos->channels[ chan ].transposition;
  }
}

static void
new_state( sqt_module *m, channel *c, const aytrack_cell *cell,
           aytrack_registers *regs )
{
  int i, chan;

  if( cell->has_enabled && !cell->enabled ) c->sample = c->ornament = 0;
  if( cell->has_note ) c->note = (unsigned int)cell->note;
  if( cell->has_sample ) {
    c->sample = cell->sample;
    c->sample_tick = 32;
    c->sample_pos = 0;
    c->ornament = 0;
    c->envelope = 0;
    c->glissade = 0;
  }
  if( cell->has_ornament ) {
    c->ornament = cell->ornament;
    c->ornament_tick = 32;
    c->ornament_pos = 0;
  }
  for( i = 0; i < cell->command_count; i++ ) {
    const aytrack_command *cmd = &cell->commands[i];
    switch( cmd->type ) {
    case CMD_ENVELOPE:
      aytrack_set_envelope_type( regs, cmd->param1 );
      aytrack_set_envelope_tone( regs, cmd->param2 );
      c->envelope = 1;
      break;
    case CMD_GLISS:
      c->glissade = cmd->param1;
      c->sliding = 0;
      break;
    case CMD_ATTENUATION:
      if( cmd->param2 ) {	/* all the channels */
        for( chan = 0; chan < 3; chan++ )
          m->channels[ chan ].attenuation = cmd->param1;
      } else {
        c->attenuation = cmd->param1;
      }
      break;
    case CMD_ATTENUATION_ADDON:
      if( cmd->param2 ) {
        for( chan = 0; chan < 3; chan++ )
          m->channels[ chan ].attenuation =
            ( m->channels[ chan ].attenuation + cmd->param1 ) & 15;
      } else {
        c->attenuation = ( c->attenuation + cmd->param1 ) & 15;
      }
      break;
    }
  }
}

static void
synthesize( const sqt_module *m, channel *c, int chan,
            aytrack_registers *regs )
{
  const sample *s;
  const sample_line *line;
  unsigned int note, offset;

  if( !c->sample ) {
    aytrack_set_level( regs, chan, 0 );
    aytrack_disable_noise( regs, chan );
    aytrack_disable_tone( regs, chan );
    return;
  }

  s = &m->samples[ c->sample ];
  line = c->sample_pos < SAMPLE_SIZE ? &s->lines[ c->sample_pos ] :
                                       &stub_line;
  c->sample_pos++;
  /* The level and the envelope */
  aytrack_set_level( regs, chan, line->level - (int)c->attenuation );
  if( !line->level && c->envelope ) aytrack_enable_envelope( regs, chan );
  /* The noise and the tone */
  if( line->enable_noise )
    aytrack_set_noise( regs, line->noise );
  else
    aytrack_disable_noise( regs, chan );
  if( !line->enable_tone ) aytrack_disable_tone( regs, chan );

  note = c->note;
  if( c->ornament ) {
    const ornament *o = &m->ornaments[ c->ornament ];
    note += c->ornament_pos < ORNAMENT_SIZE ?
            (unsigned int)o->lines[ c->ornament_pos ] : 0;
    c->ornament_pos++;
    if( !--c->ornament_tick ) {
      /* As ZXTune: an ornament without loop goes on with the sample's */
      if( o->loop != ORNAMENT_SIZE ) {
        c->ornament_pos = o->loop;
        c->ornament_tick = o->loop_limit - c->ornament_pos;
      } else {
        c->ornament_pos = s->loop;
        c->ornament_tick = s->loop_limit - c->ornament_pos;
      }
    }
  }
  note += (unsigned int)c->transposition;
  offset = (unsigned int)line->tone_deviation +
           ( c->glissade ? c->sliding : 0 );
  aytrack_set_tone_period( regs, chan,
                           ( aytrack_frequency( regs, (int)note ) + offset ) &
                           0xfff );

  if( !--c->sample_tick ) {
    c->sample_pos = s->loop;
    c->sample_tick = s->loop_limit - c->sample_pos;
    if( c->sample_pos == SAMPLE_SIZE ) c->sample = c->ornament = 0;
  }
  c->sliding += (unsigned int)c->glissade;
}

void
sqt_frame( sqt_module *m, unsigned char *registers, unsigned int *mask )
{
  aytrack_registers regs;
  aytrack *t = &m->track;
  int i;

  aytrack_registers_start( &regs, aytable_sqtracker );
  if( !t->quirk ) {
    const aytrack_line *line;
    if( !t->line ) start_pattern( m, t->position );
    line = aytrack_new_line( t );
    /* Channels C, B, A */
    if( line )
      for( i = 0; i < 3; i++ )
        new_state( m, &m->channels[ 2 - i ], &line->cells[ 2 - i ], &regs );
  }
  for( i = 0; i < 3; i++ )
    synthesize( m, &m->channels[ 2 - i ], 2 - i, &regs );
  aytrack_registers_end( &regs, registers, mask );

  aytrack_next_frame( t );
}

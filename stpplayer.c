/* stpplayer.c: Sound Tracker Pro (compiled, .stp) module player
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   Based on ZXTune (https://zxtune.bitbucket.io/), Copyright (c)
   Vitamin/CAIG <vitamin.caig@gmail.com>, GNU LGPL version 3: the module
   parser (src/formats/chiptune/aym/soundtrackerpro_compiled.cpp) and the
   player (src/module/players/aym/soundtrackerpro.cpp).

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

/* The module: a header (tempo, the offsets of the positions, patterns,
   ornaments and samples, the number of fixes), optionally "KSA SOFTWARE
   COMPILATION OF " and 25 characters of title, the patterns' data, the
   ornaments (loop, size, a halftone offset a line), the samples (loop,
   size, 4 bytes a line), the positions (length, loop, then the pattern
   (times 6) and transposition of each), the patterns (the offsets of the
   3 channels' data), the offsets of the 16 ornaments and those of the 15
   samples, last. The offsets in the module are those of where it was
   compiled to: the first pattern's data is right after the header, which
   tells how much they are off */

#include <stdlib.h>
#include <string.h>

#include "aytrack.h"
#include "stpplayer.h"

#define MIN_SIZE 200
#define MAX_SIZE 0x2800
#define HEADER_SIZE 10
#define ID_SIZE 53
#define ID_TEXT_SIZE 28
#define TITLE_SIZE 25
#define FORMAT_SIZE 9		/* the bytes ZXTune's format pattern checks */
#define MAX_SAMPLE_SIZE 32
#define MAX_ORNAMENT_SIZE 32
#define MIN_PATTERN_SIZE 5
#define MAX_PATTERN_SIZE 64
#define MAX_PATTERNS 32
#define MAX_SAMPLES 15
#define MAX_ORNAMENTS 16
#define MAX_OBJECT_LINES 127	/* sizes are signed bytes */
#define PATTERN_ENTRY 6
#define SAMPLE_LINE 4
#define SAMPLES_LIST ( MAX_SAMPLES * 2 )
#define ORNAMENTS_LIST ( MAX_ORNAMENTS * 2 )
#define DEFAULT_SAMPLE 0
#define DEFAULT_ORNAMENT 0

/* Offsets in the header */
#define OFFSET_TEMPO 0
#define OFFSET_POSITIONS 1
#define OFFSET_PATTERNS 3
#define OFFSET_ORNAMENTS 5
#define OFFSET_SAMPLES 7
#define OFFSET_FIXES 9

/* Commands of the cells. ZXTune keeps every command of a cell; here only
   the last one of each kind (which is all that counts) is kept, so that a
   cell fits in AYTRACK_MAX_COMMANDS */
enum {
  CMD_ENVELOPE = 1,		/* type (not 0), tone */
  CMD_ENVELOPE_SWITCH,		/* on */
  CMD_GLISS			/* glissade */
};

typedef struct sample_line {
  int level;			/* 0-15 */
  int noise;			/* 0-31 */
  int tone_mask, noise_mask, envelope_mask;
  int vibrato;
} sample_line;

/* Lines beyond those there are read as this (ZXTune's stub line) */
static const sample_line stub_line = { 0, 0, 1, 1, 1, 0 };

typedef struct sample {
  unsigned int size, loop;
  sample_line lines[ MAX_OBJECT_LINES ];
} sample;

typedef struct ornament {
  unsigned int size, loop;
  int lines[ MAX_OBJECT_LINES ];
} ornament;

typedef struct channel {
  int enabled, envelope;
  unsigned int volume, note;
  unsigned int sample, position_in_sample;
  unsigned int ornament, position_in_ornament;
  int tone_slide, glissade;
} channel;

struct stp_module {
  char title[ TITLE_SIZE + 1 ];
  aytrack track;
  sample samples[ MAX_SAMPLES ];
  ornament ornaments[ MAX_ORNAMENTS ];

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

static const unsigned char stp_id[] = "KSA SOFTWARE COMPILATION OF ";

static int
has_id( const unsigned char *data, size_t length )
{
  return HEADER_SIZE + ID_SIZE <= length &&
         !memcmp( data + HEADER_SIZE, stp_id, ID_TEXT_SIZE );
}

/* ZXTune's format pattern */
static int
format_matches( const unsigned char *data, size_t length )
{
  if( length < MIN_SIZE ) return 0;
  return data[0] >= 0x03 && data[0] <= 0x0f && data[2] <= 0x26 &&
         data[4] <= 0x27 && data[6] <= 0x27 && data[8] <= 0x27;
}

static int
header_valid( const unsigned char *data )
{
  unsigned int positions = word( data + OFFSET_POSITIONS );
  int i;

  if( data[ OFFSET_TEMPO ] < 3 || data[ OFFSET_TEMPO ] > 15 ) return 0;
  if( positions < HEADER_SIZE || positions > 0x2600 ) return 0;
  for( i = OFFSET_PATTERNS; i <= OFFSET_SAMPLES; i += 2 )
    if( word( data + i ) < HEADER_SIZE || word( data + i ) > 0x2700 )
      return 0;
  return 1;
}

/* The size of the module: it ends with the offsets of the samples */
static size_t
max_size( const unsigned char *data )
{
  return word( data + OFFSET_SAMPLES ) + SAMPLES_LIST;
}

/* The size of an area: up to the nearest area after it (ZXTune's
   AreaController); 0 for an area not there, UNDEFINED if none after it */
enum { AREA_HEADER, AREA_IDENTIFIER, AREA_POSITIONS, AREA_PATTERNS,
       AREA_ORNAMENTS, AREA_SAMPLES, AREA_END, AREAS };
#define UNDEFINED ( (size_t)-1 )

static size_t
area_size( const size_t *areas, int area )
{
  size_t begin = areas[ area ], result = UNDEFINED;
  int i;

  if( begin == UNDEFINED ) return 0;
  for( i = 0; i < AREAS; i++ ) {
    if( i == area || areas[i] == UNDEFINED || areas[i] < begin ) continue;
    if( result == UNDEFINED || areas[i] - begin < result )
      result = areas[i] - begin;
  }
  return result;
}

/* ZXTune's Check */
static int
structure_valid( const unsigned char *data, size_t length )
{
  size_t areas[ AREAS ], size, required, limit;
  unsigned int positions;

  if( length < HEADER_SIZE || !header_valid( data ) ) return 0;
  limit = max_size( data );
  if( limit > length ) limit = length;

  areas[ AREA_HEADER ] = 0;
  areas[ AREA_IDENTIFIER ] = has_id( data, limit ) ? HEADER_SIZE : UNDEFINED;
  areas[ AREA_POSITIONS ] = word( data + OFFSET_POSITIONS );
  areas[ AREA_PATTERNS ] = word( data + OFFSET_PATTERNS );
  areas[ AREA_ORNAMENTS ] = word( data + OFFSET_ORNAMENTS );
  areas[ AREA_SAMPLES ] = word( data + OFFSET_SAMPLES );
  areas[ AREA_END ] = limit;

  /* The header */
  if( area_size( areas, AREA_HEADER ) < HEADER_SIZE ||
      area_size( areas, AREA_END ) != UNDEFINED )
    return 0;
  /* The offsets of the samples: the last area */
  size = area_size( areas, AREA_SAMPLES );
  if( size == UNDEFINED || areas[ AREA_SAMPLES ] + size != limit ) return 0;
  /* The offsets of the ornaments */
  if( area_size( areas, AREA_ORNAMENTS ) != ORNAMENTS_LIST ) return 0;
  /* The positions: a whole number of them up to the next area */
  if( areas[ AREA_POSITIONS ] + 4 > length ) return 0;
  positions = data[ areas[ AREA_POSITIONS ] ];
  if( !positions ) return 0;
  size = area_size( areas, AREA_POSITIONS );
  if( size == UNDEFINED ) return 0;
  required = 2 + positions * 2;
  return required <= size && ( size - required ) % 2 == 0;
}

int
stp_check( const unsigned char *data, size_t length )
{
  return format_matches( data, length ) &&
         structure_valid( data, length > MAX_SIZE ? MAX_SIZE : length );
}

/* ------------------------------------------------------------------------
   Reading the module
   ------------------------------------------------------------------------ */

/* The areas of the header, the identifier, the positions, the pattern
   entries and the entries of the lists of the samples and ornaments: they
   may not overlap, but the same one may be there more than once (ZXTune's
   shared range checker) */
#define MAX_RANGES ( 3 + MAX_PATTERNS + MAX_SAMPLES + MAX_ORNAMENTS )

typedef struct ranges {
  int count;
  size_t offset[ MAX_RANGES ], size[ MAX_RANGES ];
} ranges;

static int
ranges_add( ranges *r, size_t offset, size_t size )
{
  size_t end = offset + size;
  int bound, prev;

  for( bound = 0; bound < r->count && r->offset[ bound ] <= offset; bound++ )
    ;
  if( bound < r->count && end > r->offset[ bound ] ) return 0;
  if( bound > 0 ) {
    prev = bound - 1;
    if( r->offset[ prev ] < offset ) {
      if( r->offset[ prev ] + r->size[ prev ] > offset ) return 0;
    } else {
      /* The same start: the same range, or one of them empty */
      if( r->size[ prev ] ) return !size || r->size[ prev ] == size;
      r->size[ prev ] = size;
      return 1;
    }
  } else if( !size ) {
    return 1;
  }
  if( r->count >= MAX_RANGES ) return 0;
  memmove( &r->offset[ bound + 1 ], &r->offset[ bound ],
           ( r->count - bound ) * sizeof( r->offset[0] ) );
  memmove( &r->size[ bound + 1 ], &r->size[ bound ],
           ( r->count - bound ) * sizeof( r->size[0] ) );
  r->offset[ bound ] = offset;
  r->size[ bound ] = size;
  r->count++;
  return 1;
}

typedef struct parser {
  const unsigned char *data;
  size_t length;
  size_t used;			/* the end of the data used */
  int error;
  ranges service;
  unsigned int unfix_delta;
  int used_samples[ MAX_SAMPLES ];
  int used_ornaments[ MAX_ORNAMENTS ];
} parser;

/* Data that the module is made of: it must be there */
static void
add_range( parser *p, size_t offset, size_t size )
{
  if( offset + size > p->length ) {
    p->error = 1;
    return;
  }
  if( offset + size > p->used ) p->used = offset + size;
}

static const unsigned char *
service_object( parser *p, size_t offset, size_t size )
{
  if( offset + size > p->length ||
      !ranges_add( &p->service, offset, size ) ) {
    p->error = 1;
    return NULL;
  }
  add_range( p, offset, size );
  return p->data + offset;
}

static int
peek( parser *p, size_t offset )
{
  if( offset >= p->length ) {
    p->error = 1;
    return 0;
  }
  return p->data[ offset ];
}

/* ------------------------------------------------------------------------
   The cells, as ZXTune's builder makes them
   ------------------------------------------------------------------------ */

/* A command of which only the last one counts */
static void
set_command( aytrack_cell *cell, int type, int p1, int p2 )
{
  aytrack_command *cmd = aytrack_find_command( cell, type );

  if( cmd ) {
    cmd->param1 = p1;
    cmd->param2 = p2;
  } else {
    aytrack_add_command( cell, type, p1, p2, 0 );
  }
}

/* An envelope: as ZXTune's player, a type of 0 sets no registers */
static void
set_envelope( aytrack_cell *cell, int type, int tone )
{
  if( type ) set_command( cell, CMD_ENVELOPE, type, tone );
  set_command( cell, CMD_ENVELOPE_SWITCH, 1, 0 );
}

static void
set_ornament( parser *p, aytrack_cell *cell, int ornament )
{
  cell->has_ornament = 1;
  cell->ornament = ornament;
  p->used_ornaments[ ornament ] = 1;
}

/* ------------------------------------------------------------------------
   The patterns
   ------------------------------------------------------------------------ */

typedef struct channel_cursor {
  size_t offset;
  unsigned int period, counter;
} channel_cursor;

static void
parse_channel( parser *p, channel_cursor *c, aytrack_cell *cell )
{
  while( c->offset < p->length && !p->error ) {
    int cmd = peek( p, c->offset++ );

    if( cmd == 0 ) {
      continue;
    } else if( cmd <= 0x60 ) {		/* note */
      cell->has_enabled = 1;
      cell->enabled = 1;
      cell->has_note = 1;
      cell->note = cmd - 1;
      break;
    } else if( cmd <= 0x6f ) {		/* sample */
      cell->has_sample = 1;
      cell->sample = cmd - 0x61;
      p->used_samples[ cmd - 0x61 ] = 1;
    } else if( cmd <= 0x7f ) {		/* ornament, no envelope */
      set_ornament( p, cell, cmd - 0x70 );
      set_command( cell, CMD_ENVELOPE_SWITCH, 0, 0 );
      set_command( cell, CMD_GLISS, 0, 0 );
    } else if( cmd <= 0xbf ) {		/* lines to skip */
      c->period = cmd - 0x80;
    } else if( cmd <= 0xcf ) {		/* envelope, no ornament */
      if( cmd != 0xc0 )
        set_envelope( cell, cmd - 0xc0, peek( p, c->offset++ ) );
      else
        set_envelope( cell, 0, 0 );
      set_ornament( p, cell, 0 );
      set_command( cell, CMD_GLISS, 0, 0 );
    } else if( cmd <= 0xdf ) {		/* rest */
      cell->has_enabled = 1;
      cell->enabled = 0;
      break;
    } else if( cmd <= 0xef ) {		/* empty */
      break;
    } else if( cmd == 0xf0 ) {		/* glissando */
      set_command( cell, CMD_GLISS, (signed char)peek( p, c->offset++ ), 0 );
    } else {				/* volume */
      cell->has_volume = 1;
      cell->volume = cmd - 0xf1;
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
parse_pattern( parser *p, aytrack *t, int index, size_t min_offset )
{
  const unsigned char *entry;
  size_t starts[3];
  channel_cursor c[3];
  unsigned int line;
  int chan;

  entry = service_object( p, word( p->data + OFFSET_PATTERNS ) +
                             index * PATTERN_ENTRY, PATTERN_ENTRY );
  if( !entry ) return 0;
  memset( c, 0, sizeof( c ) );
  for( chan = 0; chan < 3; chan++ ) {
    size_t offset = word( entry + chan * 2 );
    if( offset < min_offset + p->unfix_delta ) {
      p->error = 1;
      return 0;
    }
    c[ chan ].offset = starts[ chan ] = offset - p->unfix_delta;
  }
  if( !aytrack_pattern_lines( t, index ) ) {
    p->error = 1;
    return 0;
  }

  for( line = 0; line < MAX_PATTERN_SIZE && !p->error; line++ ) {
    unsigned int skip = c[0].counter;
    aytrack_line *l;

    for( chan = 1; chan < 3; chan++ )
      if( c[ chan ].counter < skip ) skip = c[ chan ].counter;
    if( skip ) {
      for( chan = 0; chan < 3; chan++ ) c[ chan ].counter -= skip;
      line += skip;
    }
    if( !has_line( p, c ) ) {
      aytrack_pattern_size( t, index, line > MIN_PATTERN_SIZE ?
                                      (int)line : MIN_PATTERN_SIZE );
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
      parse_channel( p, &c[ chan ], &l->cells[ chan ] );
      c[ chan ].counter = c[ chan ].period;
    }
  }
  if( p->error ) return 0;

  /* The data of the channels, up to the byte after the last one read */
  for( chan = 0; chan < 3; chan++ ) {
    size_t stop;
    if( starts[ chan ] >= p->length ) continue;
    stop = c[ chan ].offset + 1 < p->length ? c[ chan ].offset + 1 :
                                              p->length;
    add_range( p, starts[ chan ], stop - starts[ chan ] );
  }
  return line >= MIN_PATTERN_SIZE;
}

/* ------------------------------------------------------------------------
   The samples and ornaments
   ------------------------------------------------------------------------ */

/* Where an object is: its offset in the list, less how much the offsets
   are off (in 16 bits); its loop and size there */
static const unsigned char *
object_of( parser *p, size_t list, int index, unsigned int *size,
           unsigned int *loop, size_t line_size )
{
  const unsigned char *entry = service_object( p, list + index * 2, 2 );
  size_t offset, used;
  int raw_loop, raw_size;

  if( !entry ) return NULL;
  offset = ( word( entry ) - p->unfix_delta ) & 0xffff;
  if( offset + 2 > p->length ) {
    p->error = 1;
    return NULL;
  }
  raw_loop = (signed char)p->data[ offset ];
  raw_size = (signed char)p->data[ offset + 1 ];
  *size = raw_size < 0 ? 0 : raw_size;
  *loop = raw_loop < 0 ? *size : (unsigned int)raw_loop;
  if( *loop > *size ) *loop = *size;
  used = *size * line_size;
  add_range( p, offset, 2 + ( used < 256 ? used : 256 ) );
  return p->error ? NULL : p->data + offset + 2;
}

static void
parse_samples( parser *p, stp_module *m )
{
  size_t list = word( p->data + OFFSET_SAMPLES ), rest;
  int i;

  for( i = 0; i < MAX_SAMPLES && !p->error; i++ ) {
    sample *s = &m->samples[i];
    const unsigned char *src;
    unsigned int j;

    if( !p->used_samples[i] ) continue;
    src = object_of( p, list, i, &s->size, &s->loop, SAMPLE_LINE );
    if( !src ) return;
    /* NxxTaaaa xxnnnnnE vvvvvvvv vvvvvvvv: noise mask, tone mask, level;
       noise, envelope mask; vibrato (signed). As ZXTune, the lines are
       found in 256 bytes */
    for( j = 0; j < s->size; j++ ) {
      const unsigned char *line = src + ( j % ( 256 / SAMPLE_LINE ) ) *
                                        SAMPLE_LINE;
      sample_line *l = &s->lines[j];
      l->level = line[0] & 15;
      l->tone_mask = ( line[0] & 16 ) != 0;
      l->noise_mask = ( line[0] & 128 ) != 0;
      l->envelope_mask = ( line[1] & 1 ) != 0;
      l->noise = ( line[1] & 62 ) >> 1;
      l->vibrato = (short)word( line + 2 );
    }
  }
  if( p->error ) return;
  /* The list of the samples, as far as it is there */
  rest = p->length - list;
  add_range( p, list, rest < SAMPLES_LIST ? rest : SAMPLES_LIST );
}

static void
parse_ornaments( parser *p, stp_module *m )
{
  size_t list = word( p->data + OFFSET_ORNAMENTS );
  int i;

  for( i = 0; i < MAX_ORNAMENTS && !p->error; i++ ) {
    ornament *o = &m->ornaments[i];
    const unsigned char *src;
    unsigned int j;

    if( !p->used_ornaments[i] ) continue;
    src = object_of( p, list, i, &o->size, &o->loop, 1 );
    if( !src ) return;
    for( j = 0; j < o->size; j++ ) o->lines[j] = (signed char)src[j];
  }
}

/* ------------------------------------------------------------------------
   The module
   ------------------------------------------------------------------------ */

/* As ZXTune's Strings::OptimizeAscii: no spaces or control characters at
   the ends, those inside replaced by '?' */
static void
optimize_text( char *dest, const unsigned char *src, int size )
{
  int start = 0, end = size, i;

  while( start < end && ( src[ start ] <= ' ' || src[ start ] > 0x7e ) )
    start++;
  while( end > start && ( src[ end - 1 ] <= ' ' || src[ end - 1 ] > 0x7e ) )
    end--;
  for( i = start; i < end; i++ )
    *dest++ = src[i] < ' ' || src[i] > 0x7e ? '?' : src[i];
  *dest = '\0';
}

stp_module *
stp_load( const unsigned char *data, size_t length )
{
  parser p;
  stp_module *m;
  const unsigned char *entry;
  size_t header_size, first_data, positions;
  int i, count, any_pattern = 0, any_used = 0;
  int used_patterns[ MAX_PATTERNS ];

  if( !stp_check( data, length ) ) return NULL;
  if( length > MAX_SIZE ) length = MAX_SIZE;
  if( length > max_size( data ) ) length = max_size( data );

  memset( &p, 0, sizeof( p ) );
  p.data = data;
  p.length = length;

  m = calloc( 1, sizeof( *m ) );
  if( !m ) return NULL;
  if( !aytrack_init( &m->track, MAX_PATTERNS ) ) {
    free( m );
    return NULL;
  }
  m->track.initial_tempo = data[ OFFSET_TEMPO ];

  /* The header, the place of the identifier and the first pattern, whose
     data is right after them: how much the offsets are off */
  service_object( &p, 0, HEADER_SIZE );
  add_range( &p, HEADER_SIZE, ID_SIZE );
  header_size = HEADER_SIZE + ( has_id( data, length ) ? ID_SIZE : 0 );
  entry = p.error ? NULL :
          service_object( &p, word( data + OFFSET_PATTERNS ), PATTERN_ENTRY );
  if( entry ) {
    first_data = word( entry );
    if( data[ OFFSET_FIXES ] ? first_data != header_size :
                               first_data < header_size )
      p.error = 1;
    else
      p.unfix_delta = first_data - header_size;
  }
  if( !p.error && has_id( data, length ) ) {
    service_object( &p, HEADER_SIZE, ID_SIZE );
    optimize_text( m->title, data + HEADER_SIZE + ID_TEXT_SIZE, TITLE_SIZE );
  }

  /* Positions: the patterns there can be; the loop as it is */
  positions = word( data + OFFSET_POSITIONS );
  memset( used_patterns, 0, sizeof( used_patterns ) );
  count = p.error ? 0 : peek( &p, positions );
  if( !p.error && !count ) p.error = 1;
  if( !p.error ) service_object( &p, positions, 2 + count * 2 );
  for( i = 0; i < count && !p.error; i++ ) {
    int offset = data[ positions + 2 + i * 2 ];
    if( offset % PATTERN_ENTRY || offset / PATTERN_ENTRY >= MAX_PATTERNS ) {
      p.error = 1;
      break;
    }
    m->track.positions[i].pattern = offset / PATTERN_ENTRY;
    m->track.positions[i].transposition =
      (signed char)data[ positions + 3 + i * 2 ];
    used_patterns[ offset / PATTERN_ENTRY ] = 1;
    any_used = 1;
  }
  m->track.position_count = count;
  if( !p.error ) m->track.loop_position = peek( &p, positions + 1 );
  if( !any_used ) p.error = 1;

  /* Patterns: the used ones, with enough lines one at least */
  for( i = 0; i < MAX_PATTERNS && !p.error; i++ )
    if( used_patterns[i] &&
        parse_pattern( &p, &m->track, i, header_size ) )
      any_pattern = 1;
  if( !any_pattern ) p.error = 1;

  /* Samples and ornaments: the used ones, 0 always */
  p.used_samples[ DEFAULT_SAMPLE ] = 1;
  p.used_ornaments[ DEFAULT_ORNAMENT ] = 1;
  if( !p.error ) parse_samples( &p, m );
  if( !p.error ) parse_ornaments( &p, m );

  if( p.error || p.used + p.unfix_delta > 0x10000 || p.used < MIN_SIZE ||
      !aytrack_positions_valid( &m->track ) ) {
    stp_free( m );
    return NULL;
  }

  stp_reset( m );
  return m;
}

void
stp_free( stp_module *m )
{
  if( !m ) return;
  aytrack_free( &m->track );
  free( m );
}

const char *
stp_title( const stp_module *m )
{
  return m->title;
}

long
stp_length( stp_module *m, long *loop )
{
  /* A loop position after the last one (the header's loop is not
     checked): the loop frame is the end, as aytrack_length gives it */
  long frames = aytrack_length( &m->track, loop );

  stp_reset( m );
  return frames;
}

/* ------------------------------------------------------------------------
   The player
   ------------------------------------------------------------------------ */

void
stp_reset( stp_module *m )
{
  int i;

  for( i = 0; i < 3; i++ ) {
    channel *c = &m->channels[i];
    memset( c, 0, sizeof( *c ) );
    c->sample = DEFAULT_SAMPLE;
    c->ornament = DEFAULT_ORNAMENT;
  }
  aytrack_reset( &m->track );
}

static void
new_state( channel *c, const aytrack_cell *cell, aytrack_registers *regs )
{
  int i;

  if( cell->has_enabled ) {
    c->enabled = cell->enabled;
    c->position_in_sample = c->position_in_ornament = 0;
  }
  if( cell->has_note ) {
    c->note = cell->note;
    c->position_in_sample = c->position_in_ornament = 0;
    c->tone_slide = 0;
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
      break;
    case CMD_ENVELOPE_SWITCH:
      c->envelope = cmd->param1;
      break;
    case CMD_GLISS:
      c->glissade = cmd->param1;
      break;
    }
  }
}

static void
synthesize( const stp_module *m, channel *c, int chan, int transposition,
            aytrack_registers *regs )
{
  const sample *s;
  const sample_line *line;
  const ornament *o;
  int halftones;

  if( !c->enabled ) {
    aytrack_set_level( regs, chan, 0 );
    aytrack_disable_tone( regs, chan );
    aytrack_disable_noise( regs, chan );
    return;
  }

  s = &m->samples[ c->sample ];
  line = c->position_in_sample < s->size ?
         &s->lines[ c->position_in_sample ] : &stub_line;
  o = &m->ornaments[ c->ornament ];

  c->tone_slide += c->glissade;

  /* The level and the envelope */
  aytrack_set_level( regs, chan, line->level - (int)c->volume );
  if( line->envelope_mask && c->envelope )
    aytrack_enable_envelope( regs, chan );
  /* The tone: no ornament with the envelope */
  halftones = (int)c->note + transposition;
  if( !c->envelope && c->position_in_ornament < o->size )
    halftones += o->lines[ c->position_in_ornament ];
  aytrack_set_tone( regs, chan, halftones, c->tone_slide + line->vibrato );
  if( line->tone_mask ) aytrack_disable_tone( regs, chan );
  /* The noise */
  if( !line->noise_mask ) aytrack_set_noise( regs, line->noise );
  else aytrack_disable_noise( regs, chan );

  if( ++c->position_in_ornament >= o->size )
    c->position_in_ornament = o->loop;
  if( ++c->position_in_sample >= s->size ) {
    if( s->loop < c->position_in_sample )
      c->position_in_sample = s->loop;
    else
      c->enabled = 0;
  }
}

void
stp_frame( stp_module *m, unsigned char *registers, unsigned int *mask )
{
  aytrack_registers regs;
  const aytrack_line *line = aytrack_new_line( &m->track );
  int chan, transposition = aytrack_transposition( &m->track );

  aytrack_registers_start( &regs, aytable_soundtracker_pro );
  if( line )
    for( chan = 0; chan < 3; chan++ )
      new_state( &m->channels[ chan ], &line->cells[ chan ], &regs );
  for( chan = 0; chan < 3; chan++ )
    synthesize( m, &m->channels[ chan ], chan, transposition, &regs );
  aytrack_registers_end( &regs, registers, mask );

  aytrack_next_frame( &m->track );
}

/* gtrplayer.c: Global Tracker (.gtr) module player
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   Based on ZXTune (https://zxtune.bitbucket.io/), Copyright (c)
   Vitamin/CAIG <vitamin.caig@gmail.com>, GNU LGPL version 3: the module
   parser (src/formats/chiptune/aym/globaltracker.cpp) and the player
   (src/module/players/aym/globaltracker.cpp).

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

/* The module: a header (the tempo, an identifier, the version, the
   address the module was at in the Spectrum's memory, the title, the
   addresses of the 15 samples and 16 ornaments and of the data of the 3
   channels of the 32 patterns, the number of positions and the loop
   position), the positions (6 times the pattern each), then the data of
   the patterns, the samples (4 bytes a line) and the ornaments (a byte
   a line) */

#include <stdlib.h>
#include <string.h>

#include "aytrack.h"
#include "gtrplayer.h"

#define MIN_SIZE 1500
#define MAX_SIZE 0x2800
#define HEADER_SIZE 296		/* with the first position */
#define MAX_SAMPLES 15
#define MAX_ORNAMENTS 16
#define MAX_PATTERNS 32
#define MIN_PATTERN_SIZE 4
#define MAX_PATTERN_SIZE 64
#define PATTERN_ENTRY 6
#define OBJECT_HEADER 2
#define SAMPLE_LINE 4
#define MAX_SAMPLE_SIZE ( 256 / SAMPLE_LINE )
#define MAX_ORNAMENT_SIZE 255
#define UNDEFINED_OFFSET 0x10000

/* Offsets in the header */
#define OFFSET_TEMPO 0
#define OFFSET_VERSION 4
#define OFFSET_ADDRESS 5
#define OFFSET_TITLE 7
#define OFFSET_SAMPLES 39
#define OFFSET_ORNAMENTS 69
#define OFFSET_PATTERNS 101
#define OFFSET_LENGTH 293
#define OFFSET_LOOP 294
#define OFFSET_POSITIONS 295

/* Commands of the cells. ZXTune keeps every command of a cell in its
   order; here only the last envelope and switching it off after it are
   kept, which is all that counts */
enum {
  CMD_ENVELOPE = 1,		/* type, tone */
  CMD_NO_ENVELOPE
};

typedef struct sample_line {
  unsigned int level;		/* 0-255 */
  unsigned int noise;
  int tone_mask, noise_mask, envelope_mask;
  unsigned int vibrato;
} sample_line;

/* No lines: one not in the module (ZXTune's stub objects) */
typedef struct sample {
  unsigned int size, loop;
  sample_line lines[ MAX_SAMPLE_SIZE ];
} sample;

typedef struct ornament {
  unsigned int size, loop;
  int lines[ MAX_ORNAMENT_SIZE ];
} ornament;

typedef struct channel {
  int enabled, envelope;
  int note;
  unsigned int sample, position_in_sample;
  unsigned int ornament, position_in_ornament;
  unsigned int volume;
} channel;

struct gtr_module {
  char title[ 33 ];
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

/* Whether `size' bytes are there at `offset' */
static int
fits( size_t length, size_t offset, size_t size )
{
  return offset <= length && length - offset >= size;
}

/* A position: 6 times a pattern */
static int
position_valid( int entry )
{
  return entry % PATTERN_ENTRY == 0 && entry / PATTERN_ENTRY < MAX_PATTERNS;
}

/* ZXTune's format pattern */
static int
format_matches( const unsigned char *data, size_t length )
{
  if( length < MIN_SIZE ) return 0;
  return data[ OFFSET_TEMPO ] >= 0x03 && data[ OFFSET_TEMPO ] <= 0x0f &&
         data[ OFFSET_VERSION ] >= 0x10 && data[ OFFSET_VERSION ] <= 0x12 &&
         data[ OFFSET_LENGTH ] >= 0x01 && data[ OFFSET_LOOP ] <= 0xfe &&
         position_valid( data[ OFFSET_POSITIONS ] );
}

/* The size of the header with the positions, 0 if not right */
static size_t
header_size( const unsigned char *data, size_t length )
{
  size_t size, i;

  if( length < HEADER_SIZE ) return 0;
  size = HEADER_SIZE - 1 + data[ OFFSET_LENGTH ];
  if( size >= length ) return 0;
  for( i = OFFSET_POSITIONS; i < size; i++ )
    if( !position_valid( data[i] ) ) return 0;
  return size;
}

/* ZXTune's GetStart: the lowest offset of a part, a 0 (none) left out
   once; 0 if it is below where the module was */
static size_t
part_start( const unsigned char *data, int offset, int count, size_t start )
{
  size_t lowest = UNDEFINED_OFFSET, next = UNDEFINED_OFFSET;
  int i;

  for( i = 0; i < count; i++ ) {
    size_t value = word( data + offset + i * 2 );
    if( value < lowest ) {
      next = lowest;
      lowest = value;
    } else if( value < next ) {
      next = value;
    }
  }
  if( lowest == 0 ) lowest = next;
  return lowest >= start ? lowest - start : 0;
}

/* The size of an area: up to the nearest area after it (ZXTune's
   AreaController); UNDEFINED if none after it */
enum { AREA_HEADER, AREA_PATTERNS, AREA_SAMPLES, AREA_ORNAMENTS, AREA_END,
       AREAS };
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

/* ZXTune's FastCheck */
static int
fast_check( const unsigned char *data, size_t length )
{
  size_t header = header_size( data, length ), areas[ AREAS ];
  size_t address = word( data + OFFSET_ADDRESS );

  if( header < HEADER_SIZE ) return 0;
  areas[ AREA_HEADER ] = 0;
  areas[ AREA_PATTERNS ] = part_start( data, OFFSET_PATTERNS,
                                       MAX_PATTERNS * 3, address );
  areas[ AREA_SAMPLES ] = part_start( data, OFFSET_SAMPLES, MAX_SAMPLES,
                                      address );
  areas[ AREA_ORNAMENTS ] = part_start( data, OFFSET_ORNAMENTS,
                                        MAX_ORNAMENTS, address );
  areas[ AREA_END ] = length;
  if( header + address >= 0x10000 ||
      area_size( areas, AREA_HEADER ) < header ||
      area_size( areas, AREA_END ) != UNDEFINED )
    return 0;
  return area_size( areas, AREA_PATTERNS ) != 0 &&
         area_size( areas, AREA_SAMPLES ) != 0 &&
         area_size( areas, AREA_ORNAMENTS ) != 0;
}

int
gtr_check( const unsigned char *data, size_t length )
{
  if( length > MAX_SIZE ) length = MAX_SIZE;
  return format_matches( data, length ) && fast_check( data, length );
}

/* ------------------------------------------------------------------------
   Reading the module
   ------------------------------------------------------------------------ */

/* ZXTune checks only that the parts are in the data (the header and the
   positions cannot overlap): the size of the module is the end of the
   last one */
typedef struct parser {
  const unsigned char *data;
  size_t length;
  size_t header;		/* the size of the header and positions */
  size_t address;		/* where the module was */
  size_t used;			/* the end of the data used */
  int error;
  int notes;			/* whether the patterns have a note */
  int used_patterns[ MAX_PATTERNS ];
  int used_samples[ MAX_SAMPLES ];
  int used_ornaments[ MAX_ORNAMENTS ];
} parser;

static void
add_range( parser *p, size_t offset, size_t size )
{
  if( !fits( p->length, offset, size ) ) {
    p->error = 1;
    return;
  }
  if( offset + size > p->used ) p->used = offset + size;
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

static void
remove_commands( aytrack_cell *cell, int type )
{
  int i;

  for( i = cell->command_count - 1; i >= 0; i-- ) {
    if( cell->commands[i].type != type ) continue;
    memmove( &cell->commands[i], &cell->commands[ i + 1 ],
             ( cell->command_count - i - 1 ) * sizeof( cell->commands[0] ) );
    cell->command_count--;
  }
}

static void
add_envelope( aytrack_cell *cell, int type, int tone )
{
  remove_commands( cell, CMD_ENVELOPE );
  remove_commands( cell, CMD_NO_ENVELOPE );
  aytrack_add_command( cell, CMD_ENVELOPE, type, tone, 0 );
}

static void
add_no_envelope( aytrack_cell *cell )
{
  remove_commands( cell, CMD_NO_ENVELOPE );
  aytrack_add_command( cell, CMD_NO_ENVELOPE, 0, 0, 0 );
}

/* ------------------------------------------------------------------------
   The patterns
   ------------------------------------------------------------------------ */

typedef struct channel_cursor {
  size_t offset;
  unsigned int period, counter;
} channel_cursor;

/* From version 1.1 (0x11) an ornament switches the envelope off and a
   rest ends the cell */
static void
parse_channel( parser *p, channel_cursor *c, aytrack_cell *cell )
{
  int new_version = p->data[ OFFSET_VERSION ] != 0x10;

  c->period = 0;
  while( c->offset < p->length && !p->error ) {
    int cmd = peek( p, c->offset++ );

    if( cmd <= 0x5f ) {			/* note: the end */
      p->notes = 1;
      cell->has_enabled = 1;
      cell->enabled = 1;
      cell->has_note = 1;
      cell->note = cmd;
      break;
    } else if( cmd <= 0x6f ) {		/* sample */
      if( cmd - 0x60 >= MAX_SAMPLES ) {
        p->error = 1;
        return;
      }
      cell->has_sample = 1;
      cell->sample = cmd - 0x60;
      p->used_samples[ cmd - 0x60 ] = 1;
    } else if( cmd <= 0x7f ) {		/* ornament */
      cell->has_ornament = 1;
      cell->ornament = cmd - 0x70;
      p->used_ornaments[ cmd - 0x70 ] = 1;
      if( new_version ) add_no_envelope( cell );
    } else if( cmd <= 0xbf ) {		/* lines to skip */
      c->period = cmd - 0x80;
    } else if( cmd <= 0xcf ) {		/* envelope */
      int tone = peek( p, c->offset++ );
      add_envelope( cell, cmd - 0xc0, tone );
    } else if( cmd <= 0xdf ) {		/* the end */
      break;
    } else if( cmd == 0xe0 ) {		/* rest */
      cell->has_enabled = 1;
      cell->enabled = 0;
      if( new_version ) break;
    } else if( cmd <= 0xef ) {		/* volume */
      cell->has_volume = 1;
      cell->volume = 15 - ( cmd - 0xe0 );
    }
    /* else nothing */
  }
}

/* Whether the next line is there: the first channel ends with a 0xff, any
   channel with the end of the data */
static int
has_line( parser *p, const channel_cursor *c )
{
  int chan;

  for( chan = 0; chan < 3; chan++ ) {
    if( c[ chan ].counter ) continue;
    if( c[ chan ].offset >= p->length ||
        ( chan == 0 && peek( p, c[ chan ].offset ) == 0xff ) )
      return 0;
  }
  return 1;
}

/* As ZXTune: the pattern ends where a channel does, at 64 lines (or
   after the lines skipped then), its size being 4 at least; whether it
   has 4 lines at least */
static int
parse_pattern( parser *p, aytrack *t, int index )
{
  channel_cursor c[3];
  size_t starts[3];
  unsigned int line;
  int chan;

  if( !aytrack_pattern_lines( t, index ) ) {
    p->error = 1;
    return 0;
  }
  memset( c, 0, sizeof( c ) );
  for( chan = 0; chan < 3; chan++ ) {
    size_t address = word( p->data + OFFSET_PATTERNS +
                           index * PATTERN_ENTRY + chan * 2 );
    if( address < p->header + p->address ) {
      p->error = 1;
      return 0;
    }
    starts[ chan ] = c[ chan ].offset = address - p->address;
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

/* The offset of a sample or ornament, 0 for none */
static size_t
object_offset( parser *p, int entry )
{
  size_t address = word( p->data + entry );

  if( !address ) return 0;
  if( address < p->address ) {
    p->error = 1;
    return 0;
  }
  return address - p->address;
}

/* As ZXTune: those cut short by the end of the data are there as far as
   they are */
static void
parse_samples( parser *p, gtr_module *m )
{
  int i, sounding = 0;

  for( i = 0; i < MAX_SAMPLES && !p->error; i++ ) {
    sample *s = &m->samples[i];
    const unsigned char *src;
    size_t offset, avail, used;
    unsigned int size, j;

    if( !p->used_samples[i] ) continue;
    offset = object_offset( p, OFFSET_SAMPLES + i * 2 );
    if( p->error ) return;
    if( offset < p->header || offset > p->length ||
        !fits( p->length, offset, OBJECT_HEADER ) ) {
      p->error = 1;
      return;
    }
    avail = p->length - offset;
    src = p->data + offset;
    /* Loop and size in bytes, a size of 0 being 256 */
    if( src[1] % SAMPLE_LINE || src[0] % SAMPLE_LINE ) {
      p->error = 1;
      return;
    }
    size = ( src[1] ? src[1] : 256 ) / SAMPLE_LINE;
    used = OBJECT_HEADER + size * SAMPLE_LINE;
    if( used <= avail ) {
      add_range( p, offset, used );
    } else {
      add_range( p, offset, avail );
      size = ( avail - OBJECT_HEADER ) / SAMPLE_LINE;
    }
    s->size = size;
    s->loop = src[0] / SAMPLE_LINE < size ? src[0] / SAMPLE_LINE : size;
    for( j = 0; j < size; j++ ) {
      /* level; ENTnnnnn: envelope, noise and tone mask, noise; vibrato
         (16 bits) */
      const unsigned char *line = src + OBJECT_HEADER + j * SAMPLE_LINE;
      sample_line *l = &s->lines[j];
      l->level = line[0];
      l->tone_mask = ( line[1] & 32 ) != 0;
      l->noise_mask = ( line[1] & 64 ) != 0;
      l->envelope_mask = ( line[1] & 128 ) != 0;
      l->noise = line[1] & 31;
      l->vibrato = word( line + 2 );
      /* Tone with a level, or noise */
      if( ( !l->tone_mask && l->level ) || !l->noise_mask ) sounding = 1;
    }
  }
  if( !p->error && !sounding ) p->error = 1;
}

/* As ZXTune: one at 0 is a line of the first byte of the module; one cut
   short by the end of the data has the size it says, its lines after the
   end being 0, and one not there at all no lines */
static void
parse_ornaments( parser *p, gtr_module *m )
{
  int i;

  for( i = 0; i < MAX_ORNAMENTS && !p->error; i++ ) {
    ornament *o = &m->ornaments[i];
    size_t offset, avail;
    unsigned int lines, j;

    if( !p->used_ornaments[i] ) continue;
    offset = object_offset( p, OFFSET_ORNAMENTS + i * 2 );
    if( p->error ) return;
    if( !offset ) {
      o->size = 1;
      o->loop = 0;
      o->lines[0] = (signed char)p->data[0];
      continue;
    }
    if( offset < p->header ) {
      p->error = 1;
      return;
    }
    if( !fits( p->length, offset, OBJECT_HEADER ) ) continue;
    avail = p->length - offset;
    o->size = lines = p->data[ offset + 1 ];
    if( OBJECT_HEADER + lines <= avail ) {
      add_range( p, offset, OBJECT_HEADER + lines );
    } else {
      add_range( p, offset, avail );
      lines = avail - OBJECT_HEADER;
    }
    o->loop = p->data[ offset ] < lines ? p->data[ offset ] : lines;
    for( j = 0; j < lines; j++ )
      o->lines[j] = (signed char)p->data[ offset + OBJECT_HEADER + j ];
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

gtr_module *
gtr_load( const unsigned char *data, size_t length )
{
  parser p;
  gtr_module *m;
  int i, count, any_pattern = 0;

  if( length > MAX_SIZE ) length = MAX_SIZE;
  if( !gtr_check( data, length ) ) return NULL;

  memset( &p, 0, sizeof( p ) );
  p.data = data;
  p.length = length;
  p.header = header_size( data, length );
  p.address = word( data + OFFSET_ADDRESS );

  m = calloc( 1, sizeof( *m ) );
  if( !m ) return NULL;
  if( !aytrack_init( &m->track, MAX_PATTERNS ) ) {
    free( m );
    return NULL;
  }
  m->track.initial_tempo = data[ OFFSET_TEMPO ];
  optimize_text( m->title, data + OFFSET_TITLE, 32 );

  /* The header and the positions (checked already) */
  add_range( &p, 0, p.header );
  count = data[ OFFSET_LENGTH ];
  for( i = 0; i < count; i++ ) {
    int pattern = data[ OFFSET_POSITIONS + i ] / PATTERN_ENTRY;
    m->track.positions[i].pattern = pattern;
    m->track.positions[i].transposition = 0;
    p.used_patterns[ pattern ] = 1;
  }
  m->track.position_count = count;
  m->track.loop_position = data[ OFFSET_LOOP ] < count ? data[ OFFSET_LOOP ] :
                                                         count;

  for( i = 0; i < MAX_PATTERNS && !p.error; i++ )
    if( p.used_patterns[i] && parse_pattern( &p, &m->track, i ) )
      any_pattern = 1;
  if( !any_pattern || !p.notes ) p.error = 1;

  /* Samples and ornaments: the used ones, 0 always */
  p.used_samples[0] = p.used_ornaments[0] = 1;
  if( !p.error ) parse_samples( &p, m );
  if( !p.error ) parse_ornaments( &p, m );

  if( p.error || p.used < MIN_SIZE || p.used + p.address > 0x10000 ||
      !aytrack_positions_valid( &m->track ) ) {
    gtr_free( m );
    return NULL;
  }

  gtr_reset( m );
  return m;
}

void
gtr_free( gtr_module *m )
{
  if( !m ) return;
  aytrack_free( &m->track );
  free( m );
}

const char *
gtr_title( const gtr_module *m )
{
  return m->title;
}

long
gtr_length( gtr_module *m, long *loop )
{
  long frames = aytrack_length( &m->track, loop );
  gtr_reset( m );
  return frames;
}

/* ------------------------------------------------------------------------
   The player
   ------------------------------------------------------------------------ */

void
gtr_reset( gtr_module *m )
{
  memset( m->channels, 0, sizeof( m->channels ) );
  aytrack_reset( &m->track );
}

static void
new_state( channel *c, const aytrack_cell *cell, aytrack_registers *regs )
{
  int i;

  if( cell->has_enabled ) c->enabled = cell->enabled;
  if( cell->has_note ) {
    c->note = cell->note;
    c->position_in_sample = c->position_in_ornament = 0;
  }
  if( cell->has_sample ) c->sample = cell->sample;
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
    case CMD_NO_ENVELOPE:
      c->envelope = 0;
      break;
    }
  }
}

static void
synthesize( gtr_module *m, channel *c, int chan, aytrack_registers *regs,
            unsigned int *noise )
{
  /* As ZXTune: past the end of a sample a line with everything masked */
  static const sample_line no_line = { 0, 0, 1, 1, 1, 0 };
  const sample *s = &m->samples[ c->sample ];
  const ornament *o = &m->ornaments[ c->ornament ];
  const sample_line *sl;
  int halftones, level;

  if( !c->enabled ) {
    aytrack_set_level( regs, chan, 0 );
    aytrack_disable_tone( regs, chan );
    aytrack_disable_noise( regs, chan );
    return;
  }
  sl = c->position_in_sample < s->size ?
       &s->lines[ c->position_in_sample ] : &no_line;

  /* The tone */
  halftones = c->note + ( c->position_in_ornament < o->size ?
                          o->lines[ c->position_in_ornament ] : 0 );
  if( halftones < 0 ) halftones = 0;
  if( halftones > 95 ) halftones = 95;
  aytrack_set_tone_period( regs, chan,
                           ( aytrack_frequency( regs, halftones ) +
                             sl->vibrato ) & 0xfff );
  if( sl->tone_mask ) aytrack_disable_tone( regs, chan );

  /* The level */
  level = (int)sl->level - (int)c->volume;
  if( level < 0 ) level = 0;
  aytrack_set_level( regs, chan, level & 15 );

  /* The envelope and the noise */
  if( c->envelope && sl->envelope_mask ) aytrack_enable_envelope( regs, chan );
  *noise |= sl->noise;
  if( sl->noise_mask ) aytrack_disable_noise( regs, chan );

  if( ++c->position_in_sample >= s->size ) c->position_in_sample = s->loop;
  if( ++c->position_in_ornament >= o->size )
    c->position_in_ornament = o->loop;
}

void
gtr_frame( gtr_module *m, unsigned char *registers, unsigned int *mask )
{
  aytrack_registers regs;
  unsigned int noise = 0;
  int chan;

  aytrack_registers_start( &regs, aytable_protracker3_st );

  if( m->track.quirk == 0 ) {
    const aytrack_line *line = aytrack_new_line( &m->track );
    if( line )
      for( chan = 0; chan < 3; chan++ )
        new_state( &m->channels[ chan ], &line->cells[ chan ], &regs );
  }

  for( chan = 0; chan < 3; chan++ )
    synthesize( m, &m->channels[ chan ], chan, &regs, &noise );
  aytrack_set_noise( &regs, noise );
  aytrack_registers_end( &regs, registers, mask );

  aytrack_next_frame( &m->track );
}

/* ftcplayer.c: Fast Tracker (.ftc) module player
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   Based on ZXTune (https://zxtune.bitbucket.io/), Copyright (c)
   Vitamin/CAIG <vitamin.caig@gmail.com>, GNU LGPL version 3: the module
   parser (src/formats/chiptune/aym/fasttracker.cpp) and the player
   (src/module/players/aym/fasttracker.cpp).

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

/* The module: a header of 212 bytes ("Module: ", the title, the sign of
   the note table, the editor, the tempo, the loop position, the offset of
   the list of the patterns, and the addresses of the 32 samples and 33
   ornaments), the positions (pattern and transposition, up to a 0xff),
   the list of the patterns (the addresses of the data of their 3
   channels, up to a 0xffff), the data of the patterns, the samples and
   the ornaments. The addresses are those in the Spectrum's memory: where
   the module was is told by the address of the first pattern's data,
   which follows the end of the list of the patterns */

#include <stdlib.h>
#include <string.h>

#include "aytrack.h"
#include "ftcplayer.h"

#define MIN_SIZE 256
#define MAX_SIZE 0x3a00
#define HEADER_SIZE 212
#define MAX_SAMPLES 32
#define MAX_SAMPLE_SIZE 80
#define MAX_ORNAMENTS 33
#define MAX_ORNAMENT_SIZE 80
#define MIN_PATTERN_SIZE 1
#define MAX_PATTERN_SIZE 64
#define MAX_PATTERNS 32
#define POSITION_ENTRY 2
#define PATTERN_ENTRY 6
#define OBJECT_HEADER 3
#define SAMPLE_LINE 5
#define ORNAMENT_LINE 2
#define TEMPO_MIN 3

/* Offsets in the header */
#define OFFSET_TITLE 8
#define OFFSET_NOTE_TABLE 50
#define OFFSET_TEMPO 69
#define OFFSET_LOOP 70
#define OFFSET_PATTERNS 75
#define OFFSET_SAMPLES 82
#define OFFSET_ORNAMENTS 146

/* Commands of the cells. ZXTune keeps every command of a cell in its
   order; here only those that still count are kept (the last envelope
   and switching it off after it, the last noise, the last slide after the
   last note slide, and of the note slides the first one, which gets the
   note, and the last two, the one before the last telling the note the
   last one slides from), so that a cell fits in AYTRACK_MAX_COMMANDS */
enum {
  CMD_ENVELOPE = 1,		/* type, tone */
  CMD_ENVELOPE_OFF,
  CMD_NOISE,			/* value */
  CMD_SLIDE,			/* step */
  CMD_SLIDE_NOTE		/* step, note */
};

typedef struct sample_line {
  unsigned int level;		/* 0-15 */
  int volume_slide;		/* 0, +1, -1 */
  unsigned int noise;
  int accumulate_noise, noise_mask;
  unsigned int tone;
  int accumulate_tone, tone_mask;
  int envelope_addon;
  int accumulate_envelope, enable_envelope;
} sample_line;

typedef struct sample {
  unsigned int size, loop;
  sample_line lines[ MAX_SAMPLE_SIZE ];
} sample;

typedef struct ornament_line {
  int note_addon, keep_note_addon;
  int noise_addon, keep_noise_addon;
} ornament_line;

typedef struct ornament {
  unsigned int size, loop;
  ornament_line lines[ MAX_ORNAMENT_SIZE ];
} ornament;

typedef struct channel {
  int note;
  unsigned int envelope;
  int envelope_enabled;
  unsigned int volume;
  int volume_slide;
  int noise;
  /* ZXTune's ObjectLinesIterator: a new sample does not move it */
  const sample *sample;
  unsigned int sample_position;
  const ornament *ornament;
  unsigned int ornament_position;
  /* The accumulators (32 bits, wrapping) */
  unsigned int note_accumulator, tone_accumulator, noise_accumulator;
  unsigned int sample_noise_accumulator, envelope_accumulator;
  unsigned int tone_addon;
  /* The tone slide */
  int sliding, glissade, direction;
} channel;

struct ftc_module {
  char title[ 43 ];
  const unsigned short *table;
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

/* ZXTune's format pattern */
static int
format_matches( const unsigned char *data, size_t length )
{
  int i;

  if( length < MIN_SIZE ) return 0;
  if( data[ OFFSET_TEMPO ] < 0x03 || data[ OFFSET_LOOP ] > 0xfe ||
      data[ OFFSET_PATTERNS + 1 ] > 0x02 )
    return 0;
  for( i = 0; i < MAX_SAMPLES; i++ ) {
    int high = data[ OFFSET_SAMPLES + i * 2 + 1 ];
    if( !( ( high >= 0x03 && high <= 0x2c ) || high >= 0x64 ) ) return 0;
  }
  for( i = 0; i < MAX_ORNAMENTS; i++ ) {
    int high = data[ OFFSET_ORNAMENTS + i * 2 + 1 ];
    if( !( ( high >= 0x05 && high <= 0x2d ) || high >= 0x66 ) ) return 0;
  }
  /* A position at least, then another or the end */
  return data[ HEADER_SIZE ] <= 0x1f &&
         ( data[ HEADER_SIZE + 2 ] <= 0x1f ||
           data[ HEADER_SIZE + 2 ] == 0xff );
}

/* The size of an area: up to the nearest area after it (ZXTune's
   AreaController); 0 for an area not there, UNDEFINED if none after it */
enum { AREA_HEADER, AREA_POSITIONS, AREA_PATTERNS, AREA_PATTERNS_DATA,
       AREA_SAMPLES, AREA_ORNAMENTS, AREA_END, AREAS };
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

/* ZXTune's Areas: where the parts are, and where the module was (the
   address of the first pattern's data less its offset) */
static void
get_areas( const unsigned char *data, size_t length, size_t *areas,
           size_t *base )
{
  size_t patterns = word( data + OFFSET_PATTERNS );
  size_t first_ornament = word( data + OFFSET_ORNAMENTS );
  size_t first_sample = word( data + OFFSET_SAMPLES );
  size_t data_address = 0, pos;
  int i;

  for( i = 0; i < AREAS; i++ ) areas[i] = UNDEFINED;
  *base = 0;
  areas[ AREA_HEADER ] = 0;
  areas[ AREA_POSITIONS ] = HEADER_SIZE;
  areas[ AREA_PATTERNS ] = patterns;
  areas[ AREA_END ] = length;
  if( area_size( areas, AREA_PATTERNS ) < PATTERN_ENTRY ) return;
  if( first_sample >= first_ornament ) return;

  for( pos = patterns; ; pos += PATTERN_ENTRY ) {
    unsigned int offset;
    if( !fits( length, pos, PATTERN_ENTRY ) ) return;
    offset = word( data + pos );
    if( offset == 0xffff ) {
      size_t data_offset = pos + 2;
      if( data_address < data_offset ) return;
      *base = data_address - data_offset;
      areas[ AREA_PATTERNS_DATA ] = data_offset;
      break;
    } else if( data_address == 0 && offset != 0 ) {
      data_address = offset;
    }
  }
  /* (ZXTune checks for room for an ornament's header at both) */
  if( first_sample > *base &&
      fits( length, first_sample - *base, OBJECT_HEADER + ORNAMENT_LINE ) )
    areas[ AREA_SAMPLES ] = first_sample - *base;
  if( first_ornament > *base &&
      fits( length, first_ornament - *base, OBJECT_HEADER + ORNAMENT_LINE ) )
    areas[ AREA_ORNAMENTS ] = first_ornament - *base;
}

/* ZXTune's FastCheck */
static int
fast_check( const unsigned char *data, size_t length, size_t *base )
{
  size_t areas[ AREAS ], size;

  get_areas( data, length, areas, base );
  if( area_size( areas, AREA_HEADER ) < HEADER_SIZE ||
      area_size( areas, AREA_END ) != UNDEFINED )
    return 0;
  size = area_size( areas, AREA_POSITIONS );
  if( size == UNDEFINED || size < POSITION_ENTRY + 1 ) return 0;
  size = area_size( areas, AREA_SAMPLES );
  if( size == UNDEFINED || size < OBJECT_HEADER + SAMPLE_LINE ) return 0;
  size = area_size( areas, AREA_ORNAMENTS );
  return size != UNDEFINED && size >= OBJECT_HEADER + ORNAMENT_LINE;
}

int
ftc_check( const unsigned char *data, size_t length )
{
  size_t base;

  if( length > MAX_SIZE ) length = MAX_SIZE;
  return format_matches( data, length ) && fast_check( data, length, &base );
}

/* ------------------------------------------------------------------------
   Reading the module
   ------------------------------------------------------------------------ */

/* ZXTune checks only that the parts are in the data (simple range
   checkers): the size of the module is the end of the last one */
typedef struct parser {
  const unsigned char *data;
  size_t length;
  size_t base;			/* where the module was */
  size_t used;			/* the end of the data used */
  int error;
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

static unsigned int
peek_word( parser *p, size_t offset )
{
  if( !fits( p->length, offset, 2 ) ) {
    p->error = 1;
    return 0;
  }
  return word( p->data + offset );
}

/* ------------------------------------------------------------------------
   The cells, as ZXTune's builder makes them
   ------------------------------------------------------------------------ */

static void
remove_command_at( aytrack_cell *cell, int i )
{
  memmove( &cell->commands[i], &cell->commands[ i + 1 ],
           ( cell->command_count - i - 1 ) * sizeof( cell->commands[0] ) );
  cell->command_count--;
}

static void
remove_commands( aytrack_cell *cell, int type )
{
  int i;

  for( i = cell->command_count - 1; i >= 0; i-- )
    if( cell->commands[i].type == type ) remove_command_at( cell, i );
}

static int
count_commands( const aytrack_cell *cell, int type )
{
  int i, count = 0;

  for( i = 0; i < cell->command_count; i++ )
    if( cell->commands[i].type == type ) count++;
  return count;
}

/* The envelope: all that was before it goes */
static void
add_envelope( aytrack_cell *cell, int type, int tone )
{
  remove_commands( cell, CMD_ENVELOPE );
  remove_commands( cell, CMD_ENVELOPE_OFF );
  aytrack_add_command( cell, CMD_ENVELOPE, type, tone, 0 );
}

static void
add_envelope_off( aytrack_cell *cell )
{
  remove_commands( cell, CMD_ENVELOPE_OFF );
  aytrack_add_command( cell, CMD_ENVELOPE_OFF, 0, 0, 0 );
}

static void
add_noise( aytrack_cell *cell, int noise )
{
  remove_commands( cell, CMD_NOISE );
  aytrack_add_command( cell, CMD_NOISE, noise, 0, 0 );
}

static void
add_slide( aytrack_cell *cell, int step )
{
  remove_commands( cell, CMD_SLIDE );
  aytrack_add_command( cell, CMD_SLIDE, step, 0, 0 );
}

/* A note slide sets all of the tone slide and the note: the slides
   before it go, and of three note slides before it the second one, as
   all it leaves is the note, which the third one sets again */
static void
add_slide_note( aytrack_cell *cell, int step )
{
  remove_commands( cell, CMD_SLIDE );
  if( count_commands( cell, CMD_SLIDE_NOTE ) >= 3 ) {
    int i, seen = 0;
    for( i = 0; i < cell->command_count; i++ )
      if( cell->commands[i].type == CMD_SLIDE_NOTE && ++seen == 2 ) {
        remove_command_at( cell, i );
        break;
      }
  }
  aytrack_add_command( cell, CMD_SLIDE_NOTE, step, 0, 0 );
}

/* ------------------------------------------------------------------------
   The patterns
   ------------------------------------------------------------------------ */

typedef struct channel_cursor {
  size_t offset;
  unsigned int period, counter;
} channel_cursor;

static void
parse_channel( parser *p, channel_cursor *c, aytrack_line *line,
               aytrack_cell *cell )
{
  while( c->offset < p->length && !p->error ) {
    int cmd = peek( p, c->offset++ );

    if( cmd <= 0x1f ) {			/* sample */
      cell->has_sample = 1;
      cell->sample = cmd;
      p->used_samples[ cmd ] = 1;
    } else if( cmd <= 0x2f ) {		/* volume */
      cell->has_volume = 1;
      cell->volume = cmd - 0x20;
    } else if( cmd == 0x30 ) {		/* rest */
      cell->has_enabled = 1;
      cell->enabled = 0;
      c->period = 0;
      break;
    } else if( cmd <= 0x3e ) {		/* envelope */
      unsigned int tone = peek_word( p, c->offset );
      add_envelope( cell, cmd - 0x30, tone );
      c->offset += 2;
    } else if( cmd == 0x3f ) {		/* no envelope */
      add_envelope_off( cell );
    } else if( cmd <= 0x5f ) {		/* lines to skip: the end */
      c->period = cmd - 0x40;
      break;
    } else if( cmd <= 0xcb ) {		/* note: the end */
      aytrack_command *slide = aytrack_find_command( cell, CMD_SLIDE_NOTE );
      cell->has_enabled = 1;
      cell->enabled = 1;
      /* As ZXTune: after a note slide the note is where it slides to */
      if( slide ) {
        slide->param2 = cmd - 0x60;
      } else {
        cell->has_note = 1;
        cell->note = cmd - 0x60;
      }
      c->period = 0;
      break;
    } else if( cmd <= 0xec ) {		/* ornament */
      cell->has_ornament = 1;
      cell->ornament = cmd - 0xcc;
      p->used_ornaments[ cmd - 0xcc ] = 1;
    } else if( cmd == 0xed ) {		/* slide */
      add_slide( cell, peek_word( p, c->offset ) );
      c->offset += 2;
    } else if( cmd == 0xee ) {		/* note slide */
      add_slide_note( cell, peek( p, c->offset++ ) );
    } else if( cmd == 0xef ) {		/* noise */
      add_noise( cell, peek( p, c->offset++ ) );
    } else {				/* tempo */
      int tempo = peek( p, c->offset++ );
      if( tempo < TEMPO_MIN ) p->error = 1;
      line->tempo = tempo;
    }
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
   after the lines skipped then); whether it has a line at least */
static int
parse_pattern( parser *p, aytrack *t, int index, size_t list,
               size_t min_offset )
{
  channel_cursor c[3];
  size_t starts[3];
  unsigned int line;
  int chan;

  add_range( p, list + index * PATTERN_ENTRY, PATTERN_ENTRY );
  if( p->error ) return 0;
  if( !aytrack_pattern_lines( t, index ) ) {
    p->error = 1;
    return 0;
  }
  memset( c, 0, sizeof( c ) );
  for( chan = 0; chan < 3; chan++ ) {
    size_t address = word( p->data + list + index * PATTERN_ENTRY +
                           chan * 2 );
    if( address < p->base ) {
      p->error = 1;
      return 0;
    }
    starts[ chan ] = c[ chan ].offset = address - p->base;
  }
  for( chan = 0; chan < 3; chan++ )
    if( starts[ chan ] < min_offset || starts[ chan ] >= p->length ) {
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
      parse_channel( p, &c[ chan ], l, &l->cells[ chan ] );
      c[ chan ].counter = c[ chan ].period;
    }
  }
  if( p->error ) return 0;

  /* The data of the channels, up to the byte after the last one read */
  for( chan = 0; chan < 3; chan++ ) {
    size_t stop = c[ chan ].offset + 1 < p->length ? c[ chan ].offset + 1 :
                                                     p->length;
    add_range( p, starts[ chan ], stop - starts[ chan ] );
  }
  return line >= MIN_PATTERN_SIZE;
}

/* ------------------------------------------------------------------------
   The samples and ornaments
   ------------------------------------------------------------------------ */

/* Size (lines - 1), loop, loop limit (lines - 1): those up to the loop
   limit are the object's. NULL if it is not there or is not right */
static const unsigned char *
peek_object( parser *p, size_t address, size_t line_size,
             unsigned int max_lines, unsigned int *lines, unsigned int *loop )
{
  size_t offset = address - p->base, used;
  const unsigned char *src;

  if( address < p->base ||
      !fits( p->length, offset, OBJECT_HEADER + line_size ) )
    return NULL;
  src = p->data + offset;
  /* the loop limit within the size, the loop within the loop limit */
  if( src[2] > src[0] || src[1] > src[2] + 1 ) return NULL;
  used = OBJECT_HEADER + ( src[0] + 1 ) * line_size;
  if( !fits( p->length, offset, used ) ) return NULL;
  add_range( p, offset, used );
  *lines = src[2] + 1;
  if( *lines > max_lines ) return NULL;
  *loop = src[1] < *lines ? src[1] : *lines;
  return src + OBJECT_HEADER;
}

static void
parse_samples( parser *p, ftc_module *m )
{
  int i, sounding = 0;

  for( i = 0; i < MAX_SAMPLES; i++ ) {
    const unsigned char *src;
    sample *s = &m->samples[i];
    unsigned int j;

    if( !p->used_samples[i] ) continue;
    src = peek_object( p, word( p->data + OFFSET_SAMPLES + i * 2 ),
                       SAMPLE_LINE, MAX_SAMPLE_SIZE, &s->size, &s->loop );
    if( !src ) {
      p->error = 1;
      return;
    }
    for( j = 0; j < s->size; j++, src += SAMPLE_LINE ) {
      /* KMxnnnnn: keep noise offset, noise mask, noise; tone low byte;
         KMxxtttt: keep tone offset, tone mask, tone high bits; KMvvaaaa:
         keep envelope offset, envelope, volume slide (0x none, 10 up, 11
         down), level; envelope offset (signed) */
      sample_line *l = &s->lines[j];
      l->level = src[3] & 15;
      l->volume_slide = src[3] & 32 ? ( src[3] & 16 ? -1 : 1 ) : 0;
      l->noise = src[0] & 31;
      l->accumulate_noise = ( src[0] & 128 ) != 0;
      l->noise_mask = ( src[0] & 64 ) != 0;
      l->tone = ( src[2] & 15 ) * 256 + src[1];
      l->accumulate_tone = ( src[2] & 128 ) != 0;
      l->tone_mask = ( src[2] & 64 ) != 0;
      l->envelope_addon = (signed char)src[4];
      l->accumulate_envelope = ( src[3] & 128 ) != 0;
      l->enable_envelope = ( src[3] & 64 ) != 0;
    }
    src -= s->size * SAMPLE_LINE;
    if( s->size > 1 || src[0] || src[1] || src[2] || src[3] || src[4] )
      sounding = 1;
  }
  /* A sample that is not empty, at least */
  if( !sounding ) p->error = 1;
}

static void
parse_ornaments( parser *p, ftc_module *m )
{
  int i, last = 0;

  for( i = 0; i < MAX_ORNAMENTS; i++ ) {
    const unsigned char *src;
    ornament *o = &m->ornaments[i];
    unsigned int j;

    if( !p->used_ornaments[i] ) continue;
    last = i;
    src = peek_object( p, word( p->data + OFFSET_ORNAMENTS + i * 2 ),
                       ORNAMENT_LINE, MAX_ORNAMENT_SIZE, &o->size, &o->loop );
    if( !src ) {
      p->error = 1;
      return;
    }
    for( j = 0; j < o->size; j++, src += ORNAMENT_LINE ) {
      /* nNxooooo OOOOOOOO: keep noise offset, keep note offset, noise
         offset; note offset (signed) */
      ornament_line *l = &o->lines[j];
      l->noise_addon = src[0] & 31;
      l->keep_noise_addon = ( src[0] & 128 ) != 0;
      l->note_addon = (signed char)src[1];
      l->keep_note_addon = ( src[0] & 64 ) != 0;
    }
  }

  /* As ZXTune: those after the last one used count in the size of the
     module, if they are there */
  for( i = last + 1; i < MAX_ORNAMENTS; i++ ) {
    size_t address = word( p->data + OFFSET_ORNAMENTS + i * 2 ), offset;
    if( address < p->base ) continue;
    offset = address - p->base;
    if( fits( p->length, offset, OBJECT_HEADER + ORNAMENT_LINE ) &&
        fits( p->length, offset,
              OBJECT_HEADER + ( p->data[ offset ] + 1 ) * ORNAMENT_LINE ) )
      add_range( p, offset,
                 OBJECT_HEADER + ( p->data[ offset ] + 1 ) * ORNAMENT_LINE );
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

/* The positions, up to a 0xff: 32 patterns at most. More positions than
   the track model holds are not played (ZXTune does not limit them, but
   there is room for 255 in a module) */
static void
parse_positions( parser *p, ftc_module *m )
{
  int count = 0;

  for( ;; count++ ) {
    size_t offset = HEADER_SIZE + count * POSITION_ENTRY;
    int pattern;

    add_range( p, offset, POSITION_ENTRY );
    if( p->error ) return;
    pattern = p->data[ offset ];
    if( pattern == 0xff ) break;
    if( pattern >= MAX_PATTERNS || count >= AYTRACK_MAX_POSITIONS ) {
      p->error = 1;
      return;
    }
    p->used_patterns[ pattern ] = 1;
    m->track.positions[ count ].pattern = pattern;
    m->track.positions[ count ].transposition =
      (signed char)p->data[ offset + 1 ];
  }
  if( !count ) p->error = 1;
  m->track.position_count = count;
  m->track.loop_position = p->data[ OFFSET_LOOP ];
}

ftc_module *
ftc_load( const unsigned char *data, size_t length )
{
  parser p;
  ftc_module *m;
  size_t list, min_offset;
  int i, last = 0, any_pattern = 0;

  if( length > MAX_SIZE ) length = MAX_SIZE;
  memset( &p, 0, sizeof( p ) );
  if( !format_matches( data, length ) ||
      !fast_check( data, length, &p.base ) )
    return NULL;
  p.data = data;
  p.length = length;

  m = calloc( 1, sizeof( *m ) );
  if( !m ) return NULL;
  if( !aytrack_init( &m->track, MAX_PATTERNS ) ) {
    free( m );
    return NULL;
  }
  m->track.initial_tempo = data[ OFFSET_TEMPO ];
  switch( data[ OFFSET_NOTE_TABLE ] ) {
  case 0x01: m->table = aytable_soundtracker; break;
  case 0x02: m->table = aytable_fasttracker; break;
  default: m->table = aytable_protracker2; break;
  }
  if( !memcmp( data, "Module: ", 8 ) )
    optimize_text( m->title, data + OFFSET_TITLE, 42 );

  add_range( &p, 0, HEADER_SIZE );
  parse_positions( &p, m );

  /* The patterns used, the list of them up to the last one being before
     their data */
  list = word( data + OFFSET_PATTERNS );
  for( i = 0; i < MAX_PATTERNS; i++ ) if( p.used_patterns[i] ) last = i;
  min_offset = list + last * PATTERN_ENTRY;
  for( i = 0; i < MAX_PATTERNS && !p.error; i++ )
    if( p.used_patterns[i] &&
        parse_pattern( &p, &m->track, i, list, min_offset ) )
      any_pattern = 1;
  if( !any_pattern ) p.error = 1;

  /* Samples and ornaments: the used ones, 0 always */
  p.used_samples[0] = p.used_ornaments[0] = 1;
  if( !p.error ) parse_samples( &p, m );
  if( !p.error ) parse_ornaments( &p, m );

  if( p.error || p.used < MIN_SIZE ||
      !aytrack_positions_valid( &m->track ) ) {
    ftc_free( m );
    return NULL;
  }

  ftc_reset( m );
  return m;
}

void
ftc_free( ftc_module *m )
{
  if( !m ) return;
  aytrack_free( &m->track );
  free( m );
}

const char *
ftc_title( const ftc_module *m )
{
  return m->title;
}

long
ftc_length( ftc_module *m, long *loop )
{
  long frames = aytrack_length( &m->track, loop );
  ftc_reset( m );
  return frames;
}

/* ------------------------------------------------------------------------
   The player
   ------------------------------------------------------------------------ */

/* As ZXTune: all the channels on sample 0, at its end, and ornament 0 */
void
ftc_reset( ftc_module *m )
{
  int i;

  for( i = 0; i < 3; i++ ) {
    channel *c = &m->channels[i];
    memset( c, 0, sizeof( *c ) );
    c->volume = 15;
    c->sample = &m->samples[0];
    c->sample_position = c->sample->size;
    c->ornament = &m->ornaments[0];
    c->ornament_position = 0;
  }
  aytrack_reset( &m->track );
}

/* ZXTune's Accumulator */
static unsigned int
accumulate( unsigned int *value, int delta, int keep )
{
  unsigned int result = *value + (unsigned int)delta;
  if( keep ) *value = result;
  return result;
}

/* On to the next line of a sample or ornament */
static void
next_line( unsigned int *position, unsigned int size, unsigned int loop )
{
  if( ++*position == size ) *position = loop;
}

static int
tone_slide_update( channel *c )
{
  c->sliding += c->glissade;
  if( ( c->direction > 0 && c->sliding >= 0 ) ||
      ( c->direction < 0 && c->sliding < 0 ) ) {
    c->sliding = 0;
    c->glissade = 0;
  }
  return c->sliding;
}

static void
new_state( ftc_module *m, channel *c, const aytrack_cell *cell,
           aytrack_registers *regs )
{
  int transposition = aytrack_transposition( &m->track );
  int i;

  if( cell->has_enabled ) {
    c->sample_position = cell->enabled ? 0 : c->sample->size;
    c->sample_noise_accumulator = 0;
    c->volume_slide = 0;
    c->noise_accumulator = 0;
    c->note_accumulator = 0;
    c->ornament_position = 0;
    c->tone_accumulator = 0;
    c->envelope_accumulator = 0;
    c->sliding = c->glissade = c->direction = 0;
  }
  if( cell->has_note ) c->note = cell->note + transposition;
  if( cell->has_sample ) c->sample = &m->samples[ cell->sample ];
  if( cell->has_ornament ) {
    c->ornament = &m->ornaments[ cell->ornament ];
    c->ornament_position = 0;
    c->noise_accumulator = 0;
    c->note_accumulator = 0;
  }
  if( cell->has_volume ) c->volume = cell->volume;
  for( i = 0; i < cell->command_count; i++ ) {
    const aytrack_command *cmd = &cell->commands[i];
    switch( cmd->type ) {
    case CMD_ENVELOPE:
      aytrack_set_envelope_type( regs, cmd->param1 );
      c->envelope = cmd->param2;
      c->envelope_enabled = 1;
      break;
    case CMD_ENVELOPE_OFF:
      c->envelope_enabled = 0;
      break;
    case CMD_NOISE:
      c->noise = cmd->param1;
      break;
    case CMD_SLIDE:
      c->glissade = cmd->param1;
      break;
    case CMD_SLIDE_NOTE: {
      int slide = aytrack_sliding_difference( regs, cmd->param2, c->note );
      c->sliding = slide;
      c->glissade = slide >= 0 ? -cmd->param1 : cmd->param1;
      c->direction = slide >= 0 ? -1 : 1;
      c->note = cmd->param2 + transposition;
      break;
    }
    }
  }
}

static void
synthesize( channel *c, int chan, aytrack_registers *regs )
{
  /* As ZXTune: past the end of an ornament (looping to its end) all is 0 */
  static const ornament_line no_line;
  const ornament_line *ol = &no_line;
  unsigned int note_addon, noise_addon;

  if( c->ornament_position < c->ornament->size )
    ol = &c->ornament->lines[ c->ornament_position ];
  note_addon = accumulate( &c->note_accumulator, ol->note_addon,
                           ol->keep_note_addon );
  noise_addon = accumulate( &c->noise_accumulator, ol->noise_addon,
                            ol->keep_noise_addon );
  next_line( &c->ornament_position, c->ornament->size, c->ornament->loop );

  if( c->sample_position < c->sample->size ) {
    const sample_line *sl = &c->sample->lines[ c->sample_position ];
    unsigned int sample_noise, envelope;
    int volume;

    /* The noise */
    sample_noise = accumulate( &c->sample_noise_accumulator, sl->noise,
                               sl->accumulate_noise );
    if( sl->noise_mask )
      aytrack_disable_noise( regs, chan );
    else
      aytrack_set_noise( regs, (unsigned int)c->noise + noise_addon +
                               sample_noise );

    /* The tone */
    c->tone_addon = accumulate( &c->tone_accumulator, sl->tone,
                                sl->accumulate_tone );
    if( sl->tone_mask ) aytrack_disable_tone( regs, chan );

    /* The level */
    c->volume_slide += sl->volume_slide;
    volume = (int)sl->level + c->volume_slide;
    if( volume < 0 ) volume = 0;
    if( volume > 15 ) volume = 15;
    aytrack_set_level( regs, chan,
                       ( ( c->volume * 17 + ( c->volume > 7 ) ) * volume +
                         128 ) / 256 );

    /* The envelope */
    envelope = accumulate( &c->envelope_accumulator, sl->envelope_addon,
                           sl->accumulate_envelope );
    if( sl->enable_envelope && c->envelope_enabled ) {
      aytrack_enable_envelope( regs, chan );
      aytrack_set_envelope_tone( regs, c->envelope - envelope );
    }

    next_line( &c->sample_position, c->sample->size, c->sample->loop );
  } else {
    aytrack_set_level( regs, chan, 0 );
    aytrack_disable_tone( regs, chan );
    aytrack_disable_noise( regs, chan );
  }
  aytrack_set_tone( regs, chan, c->note + (int)note_addon,
                    (int)( c->tone_addon +
                           (unsigned int)tone_slide_update( c ) ) );
}

void
ftc_frame( ftc_module *m, unsigned char *registers, unsigned int *mask )
{
  aytrack_registers regs;
  int chan;

  aytrack_registers_start( &regs, m->table );

  if( m->track.quirk == 0 ) {
    const aytrack_line *line = aytrack_new_line( &m->track );
    if( line )
      for( chan = 0; chan < 3; chan++ )
        new_state( m, &m->channels[ chan ], &line->cells[ chan ], &regs );
  }

  for( chan = 0; chan < 3; chan++ )
    synthesize( &m->channels[ chan ], chan, &regs );
  aytrack_registers_end( &regs, registers, mask );

  aytrack_next_frame( &m->track );
}

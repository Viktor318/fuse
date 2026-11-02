/* psmplayer.c: Pro Sound Maker (compiled, .psm) module player
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   Based on ZXTune (https://zxtune.bitbucket.io/), Copyright (c)
   Vitamin/CAIG <vitamin.caig@gmail.com>, GNU LGPL version 3: the module
   parser (src/formats/chiptune/aym/prosoundmaker_compiled.cpp) and the
   player (src/module/players/aym/prosoundmaker.cpp).

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

/* The module: a header of the offsets of the positions, the list of the
   samples, the list of the ornaments and the patterns (4 words), then
   optionally "psm1" and a 0 and the title up to the positions. The
   positions: the pattern and the transposition (signed) of each, up to a
   0xff pattern, whose second byte is the loop position (0xff: none). The
   lists of the offsets of the samples (3 bytes a line) and ornaments (a
   halftone offset a line), these, and the patterns: the tempo and the
   offsets of the 3 channels' data, 7 bytes each. No ornaments: their list
   is where the patterns are */

#include <stdlib.h>
#include <string.h>

#include "aytrack.h"
#include "psmplayer.h"

#define MIN_SIZE 0x80
#define MAX_SIZE 0x4000
#define HEADER_SIZE 8
#define MAX_POSITIONS 100
#define MIN_PATTERN_SIZE 1
#define MAX_PATTERN_SIZE 64
#define MAX_PATTERNS 32
#define MAX_SAMPLES 15
#define MAX_SAMPLE_SIZE 32
#define MAX_ORNAMENTS 32
#define MAX_ORNAMENT_SIZE 256
#define POSITION_ENTRY 2
#define PATTERN_ENTRY 7
#define SAMPLE_LINE 3
#define OBJECT_HEADER 2
#define POS_END_MARKER 0xff

/* Offsets in the header */
#define OFFSET_POSITIONS 0
#define OFFSET_SAMPLES 2
#define OFFSET_ORNAMENTS 4
#define OFFSET_PATTERNS 6

/* Commands of the cells. ZXTune keeps every command of a cell in its
   order; there may be one envelope only, and before and after it the
   others do not depend on each other's order, so that only one (the last)
   of each kind is kept on either side of it and a cell fits in
   AYTRACK_MAX_COMMANDS */
enum {
  CMD_ENVELOPE = 1,		/* type, tone or -1, note or -1 */
  CMD_NO_ORNAMENT,
  CMD_ENVELOPE_REINIT		/* on */
};

typedef struct sample_line {
  int level;			/* 0-15 */
  unsigned int noise;		/* 0-31 */
  int tone_mask, noise_mask;
  int gliss;
} sample_line;

/* No lines: one not in the module (ZXTune's stub objects) */
typedef struct sample {
  unsigned int size, loop;
  unsigned int volume_delta_period;
  int volume_delta_value;
  sample_line lines[ MAX_SAMPLE_SIZE ];
} sample;

typedef struct ornament {
  unsigned int size, loop;
  int lines[ MAX_ORNAMENT_SIZE ];
} ornament;

typedef struct envelope_state {
  int reinit;
  unsigned int type;
  int has_note, has_tone;
  unsigned int note, tone;
} envelope_state;

typedef struct channel {
  int enabled;
  envelope_state envelope;
  unsigned int note;		/* unsigned, as ZXTune's */
  const sample *sample;
  unsigned int sample_position, loops_count;
  int sample_finished;
  const ornament *ornament;
  unsigned int ornament_position;
  int ornament_finished, keep_finished;
  unsigned int volume_delta, base_volume_delta;
  int slide;
} channel;

struct psm_module {
  char title[ 129 ];
  aytrack track;
  sample samples[ MAX_SAMPLES ];
  ornament ornaments[ MAX_ORNAMENTS ];
  ornament stub;		/* what an envelope reinit switches to */

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

/* ZXTune's format pattern: the high bytes of the offsets in the header */
static int
format_matches( const unsigned char *data, size_t length )
{
  if( length < MIN_SIZE ) return 0;
  return data[0] >= 0x08 && data[0] <= 0x88 && !( data[0] & 0x40 ) &&
         data[1] == 0x00 && data[3] <= 0x01 && data[5] <= 0x04 &&
         data[7] <= 0x05;
}

/* The size of an area: up to the nearest area after it (ZXTune's
   AreaController); 0 for an area not there, UNDEFINED if none after it */
enum { AREA_HEADER, AREA_POSITIONS, AREA_SAMPLES, AREA_ORNAMENTS,
       AREA_PATTERNS, AREA_END, AREAS };
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

/* ZXTune's FastCheck */
static int
fast_check( const unsigned char *data, size_t length )
{
  size_t areas[ AREAS ], size, count;
  const unsigned char *last;

  areas[ AREA_HEADER ] = 0;
  areas[ AREA_POSITIONS ] = word( data + OFFSET_POSITIONS );
  areas[ AREA_SAMPLES ] = word( data + OFFSET_SAMPLES );
  areas[ AREA_ORNAMENTS ] = word( data + OFFSET_ORNAMENTS );
  areas[ AREA_PATTERNS ] = word( data + OFFSET_PATTERNS );
  areas[ AREA_END ] = length;

  if( area_size( areas, AREA_HEADER ) < HEADER_SIZE ||
      area_size( areas, AREA_END ) != UNDEFINED )
    return 0;

  /* The positions: their end marker right before the next area */
  size = area_size( areas, AREA_POSITIONS );
  if( size == UNDEFINED || size == 0 || size % POSITION_ENTRY ) return 0;
  count = size / POSITION_ENTRY - 1;
  if( !count ) return 0;
  last = data + areas[ AREA_POSITIONS ] + count * POSITION_ENTRY;
  if( last[0] != POS_END_MARKER ) return 0;
  if( last[1] != POS_END_MARKER && last[1] >= count ) return 0;

  size = area_size( areas, AREA_SAMPLES );
  return size != UNDEFINED && size != 0;
}

int
psm_check( const unsigned char *data, size_t length )
{
  if( length > MAX_SIZE ) length = MAX_SIZE;
  return format_matches( data, length ) && fast_check( data, length );
}

/* ------------------------------------------------------------------------
   Reading the module
   ------------------------------------------------------------------------ */

/* The areas of the header, the title, the positions and the entries of
   the lists of the patterns, samples and ornaments: they may not overlap,
   but the same one may be there more than once (ZXTune's shared range
   checker) */
#define MAX_RANGES ( 2 + MAX_POSITIONS + 1 + MAX_PATTERNS + MAX_SAMPLES + \
                     MAX_ORNAMENTS )

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
  int used_patterns[ MAX_PATTERNS ];
  int used_samples[ MAX_SAMPLES ];
  int used_ornaments[ MAX_ORNAMENTS ];
  int any_note;			/* a pattern has a note */
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

static void
add_service( parser *p, size_t offset, size_t size )
{
  if( offset + size > p->length ||
      !ranges_add( &p->service, offset, size ) ) {
    p->error = 1;
    return;
  }
  add_range( p, offset, size );
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

/* Where the commands after the envelope (or all, if none) start */
static int
segment_start( const aytrack_cell *cell )
{
  int i;

  for( i = 0; i < cell->command_count; i++ )
    if( cell->commands[i].type == CMD_ENVELOPE ) return i + 1;
  return 0;
}

/* A command of which only the last one on its side of the envelope
   counts */
static void
set_command( aytrack_cell *cell, int type, int p1 )
{
  int i;

  for( i = segment_start( cell ); i < cell->command_count; i++ )
    if( cell->commands[i].type == type ) {
      cell->commands[i].param1 = p1;
      return;
    }
  aytrack_add_command( cell, type, p1, 0, 0 );
}

/* As ZXTune: a second envelope in a cell is an error */
static void
set_envelope( parser *p, aytrack_cell *cell, int type, int tone, int note )
{
  if( aytrack_find_command( cell, CMD_ENVELOPE ) ) {
    p->error = 1;
    return;
  }
  aytrack_add_command( cell, CMD_ENVELOPE, type, tone, note );
}

static int
cell_has_data( const aytrack_cell *cell )
{
  return cell->has_enabled || cell->has_note || cell->has_sample ||
         cell->has_ornament || cell->has_volume || cell->command_count;
}

/* ------------------------------------------------------------------------
   The patterns
   ------------------------------------------------------------------------ */

typedef struct channel_cursor {
  size_t offset;
  unsigned int period, counter;
  unsigned int note;
} channel_cursor;

/* The notes are told by how many halftones down from the last one, round
   in 96 */
static void
update_note( channel_cursor *c, unsigned int delta )
{
  if( c->note >= delta ) c->note -= delta;
  else c->note = ( c->note + 0x60 - delta ) & 0xff;
}

static void
parse_channel( parser *p, channel_cursor *c, aytrack_cell *cell )
{
  while( c->offset < p->length && !p->error ) {
    int cmd = p->data[ c->offset++ ];

    if( cmd <= 0x5f ) {			/* note */
      update_note( c, cmd );
      cell->has_enabled = 1;
      cell->enabled = 1;
      cell->has_note = 1;
      cell->note = c->note & 0x7f;
      p->any_note = 1;
      if( c->note & 0x80 ) cell->enabled = 0;
      break;
    } else if( cmd == 0x60 ) {		/* rest */
      cell->has_enabled = 1;
      cell->enabled = 0;
      break;
    } else if( cmd <= 0x6f ) {		/* sample */
      cell->has_sample = 1;
      cell->sample = cmd - 0x61;
      p->used_samples[ cmd - 0x61 ] = 1;
    } else if( cmd <= 0x8f ) {		/* ornament */
      cell->has_ornament = 1;
      cell->ornament = cmd - 0x70;
      p->used_ornaments[ cmd - 0x70 ] = 1;
    } else if( cmd == 0x90 ) {		/* nothing more */
      break;
    } else if( cmd <= 0x9f ) {		/* volume */
      cell->has_volume = 1;
      cell->volume = cmd - 0x90;
    } else if( cmd == 0xa0 ) {		/* no ornament */
      set_command( cell, CMD_NO_ORNAMENT, 0 );
    } else if( cmd <= 0xb0 ) {		/* envelope by the note */
      int packed = cmd - 0xa1;
      set_envelope( p, cell, 8 + ( ( packed & 3 ) << 1 ), -1,
                    3 * ( packed & 12 ) );
    } else if( cmd <= 0xb7 ) {		/* envelope by its tone */
      int packed = peek( p, c->offset++ );
      if( p->error ) break;
      set_envelope( p, cell, cmd - 0xa9,
                    packed >= 0xf1 ? 256 * ( packed & 15 ) : packed, -1 );
    } else if( cmd <= 0xf8 ) {		/* lines to skip */
      c->period = cmd - 0xb8;
    } else if( cmd == 0xf9 ) {		/* nothing */
      continue;
    } else if( cmd <= 0xfb ) {		/* envelope reinit */
      set_command( cell, CMD_ENVELOPE_REINIT, cmd == 0xfb );
    } else {				/* the end of the pattern */
      break;
    }
  }
}

/* Whether all the channels to be read on this line have data */
static int
has_line( parser *p, const channel_cursor *c )
{
  int chan;

  for( chan = 0; chan < 3; chan++ ) {
    if( c[ chan ].counter ) continue;
    if( c[ chan ].offset >= p->length ||
        p->data[ c[ chan ].offset ] >= 0xfc )
      return 0;
  }
  return 1;
}

/* As ZXTune: up to the end of the data of a channel to be read, 64 lines
   at most (plus the lines skipped); whether it has a line at least */
static int
parse_pattern( parser *p, aytrack *t, int index, size_t min_offset )
{
  size_t entry = word( p->data + OFFSET_PATTERNS ) + index * PATTERN_ENTRY;
  channel_cursor c[3];
  unsigned int line;
  int chan, tempo;
  aytrack_line *l;

  add_service( p, entry, PATTERN_ENTRY );
  if( p->error ) return 0;
  tempo = p->data[ entry ];
  if( tempo < 2 || tempo > 50 ) {
    p->error = 1;
    return 0;
  }
  memset( c, 0, sizeof( c ) );
  for( chan = 0; chan < 3; chan++ ) {
    c[ chan ].offset = word( p->data + entry + 1 + chan * 2 );
    c[ chan ].note = 48;
    /* Only the lower bound is checked */
    if( c[ chan ].offset < min_offset ) {
      p->error = 1;
      return 0;
    }
  }

  if( !aytrack_pattern_lines( t, index ) ||
      !( l = aytrack_start_line( t, index, 0 ) ) ) {
    p->error = 1;
    return 0;
  }
  l->tempo = tempo;

  for( line = 0; line < MAX_PATTERN_SIZE && !p->error; line++ ) {
    unsigned int skip = c[0].counter;

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
      parse_channel( p, &c[ chan ], &l->cells[ chan ] );
      c[ chan ].counter = c[ chan ].period;
    }
  }
  if( p->error ) return 0;

  /* The data of the channels, up to the byte after the last one read */
  for( chan = 0; chan < 3; chan++ ) {
    size_t start = word( p->data + entry + 1 + chan * 2 ), stop;
    if( start >= p->length ) continue;
    stop = c[ chan ].offset + 1 < p->length ? c[ chan ].offset + 1 :
                                              p->length;
    add_range( p, start, stop - start );
  }
  return line >= MIN_PATTERN_SIZE;
}

static void
parse_patterns( parser *p, aytrack *t )
{
  int i, max = 0, any = 0;
  size_t min_offset;

  for( i = 0; i < MAX_PATTERNS; i++ )
    if( p->used_patterns[i] ) max = i;
  min_offset = word( p->data + OFFSET_PATTERNS ) + max * PATTERN_ENTRY;
  for( i = 0; i < MAX_PATTERNS && !p->error; i++ )
    if( p->used_patterns[i] && parse_pattern( p, t, i, min_offset ) )
      any = 1;
  if( !any ) p->error = 1;
}

/* ------------------------------------------------------------------------
   The samples and ornaments
   ------------------------------------------------------------------------ */

/* The offset of an object in a list (the first one after the list at
   least) and how many bytes of it there are */
static size_t
object_offset( parser *p, size_t list, int index, size_t min_offset,
               size_t *avail )
{
  size_t offset;

  add_service( p, list + index * 2, 2 );
  if( p->error ) return 0;
  offset = word( p->data + list + index * 2 );
  if( offset < min_offset || offset > p->length ||
      p->length - offset < OBJECT_HEADER ) {
    p->error = 1;
    return 0;
  }
  *avail = p->length - offset;
  return offset;
}

/* Whether a sample makes a sound: noise, or a tone with a level */
static int
sample_sounds( const sample *s )
{
  unsigned int i;

  for( i = 0; i < s->size; i++ )
    if( !s->lines[i].noise_mask ||
        ( !s->lines[i].tone_mask && s->lines[i].level ) )
      return 1;
  return 0;
}

static void
parse_sample( parser *p, sample *s, size_t offset, size_t avail )
{
  /* DDSlllll: volume delta (DD, negative with S: -DD-1), the last line;
     cccLLLLL: loop counter (0: no loop), loop line */
  const unsigned char *src = p->data + offset;
  unsigned int size = 1 + ( src[0] & 0x1f ), counter = src[1] >> 5, i;
  size_t used = OBJECT_HEADER + size * SAMPLE_LINE;

  if( used <= avail ) {
    add_range( p, offset, used );
  } else {
    add_range( p, offset, avail );
    size = ( avail - OBJECT_HEADER ) / SAMPLE_LINE;
  }
  s->size = size;
  for( i = 0; i < size; i++ ) {
    /* NxxTaaaa nnnnnSHH LLLLLLLL: noise off, tone off, level; noise,
       glissando (11 bits, signed) */
    const unsigned char *line = src + OBJECT_HEADER + i * SAMPLE_LINE;
    sample_line *l = &s->lines[i];
    int shift = ( line[1] & 7 ) << 8 | line[2];

    l->level = line[0] & 15;
    l->tone_mask = ( line[0] & 16 ) != 0;
    l->noise_mask = ( line[0] & 128 ) != 0;
    l->noise = line[1] >> 3;
    l->gliss = line[1] & 4 ? shift - 0x800 : shift;
  }
  if( counter ) {
    int delta = src[0] >> 6;
    s->volume_delta_period = counter;
    s->volume_delta_value = src[0] & 0x20 ? -delta - 1 : delta;
    s->loop = src[1] & 0x1f;
    if( s->loop > size ) s->loop = size;
  } else {
    s->loop = size;
  }
}

/* The lines missing at the end of the data are 0 */
static void
parse_ornament( parser *p, ornament *o, size_t offset, size_t avail )
{
  const unsigned char *src = p->data + offset;
  unsigned int size = src[0] + 1, lines = size, i;
  size_t used = OBJECT_HEADER + size;

  if( used <= avail ) {
    add_range( p, offset, used );
  } else {
    add_range( p, offset, avail );
    lines = avail - OBJECT_HEADER;
  }
  o->size = size;
  for( i = 0; i < size; i++ )
    o->lines[i] = i < lines ? (signed char)src[ OBJECT_HEADER + i ] : 0;
  if( src[1] & 128 ) {
    o->loop = src[1] & 31;
    if( o->loop > size ) o->loop = size;
  } else {
    o->loop = size;
  }
}

static void
parse_samples( parser *p, psm_module *m )
{
  size_t list = word( p->data + OFFSET_SAMPLES ), min_offset, offset, avail;
  int i, max = 0, sounds = 0;

  for( i = 0; i < MAX_SAMPLES; i++ )
    if( p->used_samples[i] ) max = i;
  min_offset = list + ( max + 1 ) * 2;
  for( i = 0; i < MAX_SAMPLES && !p->error; i++ ) {
    if( !p->used_samples[i] ) continue;
    offset = object_offset( p, list, i, min_offset, &avail );
    if( p->error ) return;
    parse_sample( p, &m->samples[i], offset, avail );
    if( sample_sounds( &m->samples[i] ) ) sounds = 1;
  }
  if( !sounds ) p->error = 1;
}

static void
parse_ornaments( parser *p, psm_module *m )
{
  size_t list = word( p->data + OFFSET_ORNAMENTS ), min_offset, offset, avail;
  int i, max = 0;

  for( i = 0; i < MAX_ORNAMENTS; i++ )
    if( p->used_ornaments[i] ) max = i;

  /* None: only the (empty) ornament 0 may be used */
  if( list == word( p->data + OFFSET_PATTERNS ) ) {
    if( max ) p->error = 1;
    return;
  }
  min_offset = list + ( max + 1 ) * 2;
  for( i = 0; i < MAX_ORNAMENTS && !p->error; i++ ) {
    if( !p->used_ornaments[i] ) continue;
    offset = object_offset( p, list, i, min_offset, &avail );
    if( p->error ) return;
    parse_ornament( p, &m->ornaments[i], offset, avail );
  }
}

/* ------------------------------------------------------------------------
   The module
   ------------------------------------------------------------------------ */

/* As ZXTune's Strings::OptimizeAscii: no spaces or control characters at
   the ends, those inside replaced by '?' */
static void
optimize_text( char *dest, const unsigned char *src, size_t size )
{
  size_t start = 0, end = size, i;

  while( start < end && ( src[ start ] <= ' ' || src[ start ] > 0x7e ) )
    start++;
  while( end > start && ( src[ end - 1 ] <= ' ' || src[ end - 1 ] > 0x7e ) )
    end--;
  for( i = start; i < end; i++ )
    *dest++ = src[i] < ' ' || src[i] > 0x7e ? '?' : src[i];
  *dest = '\0';
}

static const unsigned char psm_id[] = { 'p', 's', 'm', '1', 0 };

/* Between the header and the positions: "psm1" and a 0 (or not), then the
   title */
static void
parse_title( parser *p, psm_module *m )
{
  size_t end = word( p->data + OFFSET_POSITIONS ), start = HEADER_SIZE;
  size_t gap = end - HEADER_SIZE;

  if( !gap ) return;
  if( gap >= sizeof( psm_id ) &&
      !memcmp( p->data + start, psm_id, sizeof( psm_id ) ) )
    start += sizeof( psm_id );
  if( start < end ) optimize_text( m->title, p->data + start, end - start );
  add_service( p, HEADER_SIZE, gap );
}

static void
parse_positions( parser *p, psm_module *m )
{
  size_t list = word( p->data + OFFSET_POSITIONS );
  int count = 0;

  for( ;; ) {
    size_t entry = list + count * POSITION_ENTRY;
    int pattern, transposition;

    add_service( p, entry, POSITION_ENTRY );
    if( p->error ) return;
    pattern = p->data[ entry ];
    if( pattern == POS_END_MARKER ) {
      int loop = p->data[ entry + 1 ];
      if( loop != POS_END_MARKER ) {
        if( loop >= count ) {
          p->error = 1;
          return;
        }
        m->track.loop_position = loop;
      }
      break;
    }
    transposition = (signed char)p->data[ entry + 1 ];
    if( transposition < -36 || transposition > 36 || count >= MAX_POSITIONS ) {
      p->error = 1;
      return;
    }
    m->track.positions[ count ].pattern = pattern;
    m->track.positions[ count ].transposition = transposition;
    count++;
  }
  m->track.position_count = count;

  /* As ZXTune: 32 patterns at most, used ones */
  if( !count ) p->error = 1;
  for( count = 0; count < m->track.position_count; count++ ) {
    int pattern = m->track.positions[ count ].pattern;
    if( pattern >= MAX_PATTERNS ) {
      p->error = 1;
      return;
    }
    p->used_patterns[ pattern ] = 1;
  }
}

psm_module *
psm_load( const unsigned char *data, size_t length )
{
  parser p;
  psm_module *m;

  if( length > MAX_SIZE ) length = MAX_SIZE;
  if( !psm_check( data, length ) ) return NULL;

  memset( &p, 0, sizeof( p ) );
  p.data = data;
  p.length = length;

  m = calloc( 1, sizeof( *m ) );
  if( !m ) return NULL;
  if( !aytrack_init( &m->track, MAX_PATTERNS ) ) {
    free( m );
    return NULL;
  }

  add_service( &p, 0, HEADER_SIZE );
  parse_title( &p, m );
  if( !p.error ) parse_positions( &p, m );
  if( !p.error ) parse_patterns( &p, &m->track );
  if( !p.any_note ) p.error = 1;

  /* Samples and ornaments: the used ones, 0 always */
  p.used_samples[0] = p.used_ornaments[0] = 1;
  if( !p.error ) parse_samples( &p, m );
  if( !p.error ) parse_ornaments( &p, m );

  if( p.error || p.used < MIN_SIZE ||
      !aytrack_positions_valid( &m->track ) ) {
    psm_free( m );
    return NULL;
  }

  psm_reset( m );
  return m;
}

void
psm_free( psm_module *m )
{
  if( !m ) return;
  aytrack_free( &m->track );
  free( m );
}

const char *
psm_title( const psm_module *m )
{
  return m->title;
}

long
psm_length( psm_module *m, long *loop )
{
  long frames = aytrack_length( &m->track, loop );
  psm_reset( m );
  return frames;
}

/* ------------------------------------------------------------------------
   The player
   ------------------------------------------------------------------------ */

/* As ZXTune: all the channels off, on sample and ornament 0 */
void
psm_reset( psm_module *m )
{
  int i;

  for( i = 0; i < 3; i++ ) {
    channel *c = &m->channels[i];
    memset( c, 0, sizeof( *c ) );
    c->sample = &m->samples[0];
    c->ornament = &m->ornaments[0];
  }
  aytrack_reset( &m->track );
}

static int
envelope_enabled( const envelope_state *e )
{
  return e->has_note || e->has_tone;
}

static void
envelope_disable( envelope_state *e )
{
  e->has_note = e->has_tone = 0;
}

/* The period of the note of the envelope */
static unsigned int
envelope_frequency( const aytrack_registers *regs, unsigned int halftone )
{
  return halftone < AYTABLE_NOTES ? regs->table[ halftone ] : 0;
}

static void
new_state( psm_module *m, channel *c, const aytrack_cell *cell,
           int transposition, aytrack_registers *regs )
{
  int i;

  if( cell->has_enabled ) c->enabled = cell->enabled;
  if( cell->has_sample ) c->sample = &m->samples[ cell->sample ];
  if( cell->has_ornament ) {
    c->ornament = &m->ornaments[ cell->ornament ];
    c->ornament_position = 0;
    c->ornament_finished = c->keep_finished = 0;
    envelope_disable( &c->envelope );
    c->envelope.reinit = 0;
  }
  if( cell->has_volume ) c->base_volume_delta = cell->volume;
  for( i = 0; i < cell->command_count; i++ ) {
    const aytrack_command *cmd = &cell->commands[i];
    switch( cmd->type ) {
    case CMD_ENVELOPE:
      c->envelope.type = cmd->param1;
      if( cmd->param2 != -1 ) {
        aytrack_set_envelope_type( regs, cmd->param1 );
        aytrack_set_envelope_tone( regs, cmd->param2 );
        c->envelope.has_note = 0;
        c->envelope.has_tone = 1;
        c->envelope.tone = cmd->param2;
      } else if( cmd->param3 != -1 ) {
        c->envelope.has_note = 1;
        c->envelope.note = cmd->param3;
        c->envelope.has_tone = 0;
        c->envelope.reinit = 1;
      }
      break;
    case CMD_NO_ORNAMENT:
      c->ornament_finished = c->keep_finished = 1;
      c->ornament_position = 0;
      envelope_disable( &c->envelope );
      break;
    case CMD_ENVELOPE_REINIT:
      c->envelope.reinit = cmd->param1 != 0;
      c->ornament = &m->stub;
      break;
    }
  }
  if( cell->has_note ) {
    c->note = cell->note + transposition;
    c->volume_delta = c->base_volume_delta;
    c->sample_position = 0;
    c->sample_finished = 0;
    c->slide = 0;
    c->loops_count = 1;
    c->ornament_position = 0;
    c->ornament_finished = c->keep_finished;
    if( c->envelope.reinit && envelope_enabled( &c->envelope ) ) {
      aytrack_set_envelope_type( regs, c->envelope.type );
      if( c->envelope.has_note ) {
        unsigned int note = c->note + c->envelope.note;
        if( note >= 0x60 ) note -= 0x60;
        else if( note >= 0x30 ) note -= 0x30;
        aytrack_set_envelope_tone( regs, envelope_frequency(
                                     regs, ( note & 0x7f ) + 0x30 ) & 0xff );
      } else {
        aytrack_set_envelope_tone( regs, c->envelope.tone );
      }
    }
  }
}

static int
clamp( int value, int low, int high )
{
  return value < low ? low : value > high ? high : value;
}

static void
synthesize( channel *c, int chan, aytrack_registers *regs )
{
  static const sample_line stub_line = { 0, 0, 1, 1, 0 };
  int envelope = envelope_enabled( &c->envelope );
  const ornament *o = c->ornament;
  const sample *s = c->sample;
  const sample_line *sl = c->sample_position < s->size ?
                          &s->lines[ c->sample_position ] : &stub_line;
  int ornament_line = 0, halftones, tone, level;

  if( !envelope && !c->ornament_finished &&
      c->ornament_position < o->size )
    ornament_line = o->lines[ c->ornament_position ];

  c->slide += sl->gliss;
  halftones = clamp( (int)c->note + ornament_line, 0, AYTABLE_NOTES - 1 );
  tone = clamp( (int)regs->table[ halftones ] + c->slide, 0, 4095 );
  aytrack_set_tone_period( regs, chan, tone );

  /* The level: the envelope bit may be lost on the way, as ZXTune's */
  level = ( sl->level | ( envelope ? 16 : 0 ) ) + (int)c->volume_delta - 15;
  if( !c->enabled || level < 0 ) level = 0;
  aytrack_set_level( regs, chan, level & 15 );
  if( level & 16 ) aytrack_enable_envelope( regs, chan );
  if( sl->tone_mask ) aytrack_disable_tone( regs, chan );
  if( !sl->noise_mask && c->enabled ) {
    if( level ) aytrack_set_noise( regs, sl->noise );
  } else {
    aytrack_disable_noise( regs, chan );
  }

  if( !c->sample_finished && ++c->sample_position >= s->size ) {
    if( s->loop < s->size ) {
      c->sample_position = s->loop;
      if( !--c->loops_count ) {
        c->loops_count = s->volume_delta_period;
        c->volume_delta = clamp( (int)c->volume_delta + s->volume_delta_value,
                                 0, 15 );
      }
    } else {
      c->enabled = 0;
      c->sample_finished = 1;
    }
  }

  if( !c->ornament_finished && ++c->ornament_position >= o->size ) {
    if( o->loop < o->size ) c->ornament_position = o->loop;
    else c->ornament_finished = 1;
  }
}

void
psm_frame( psm_module *m, unsigned char *registers, unsigned int *mask )
{
  aytrack_registers regs;
  int chan;

  aytrack_registers_start( &regs, aytable_prosoundmaker );

  if( m->track.quirk == 0 ) {
    const aytrack_line *line = aytrack_new_line( &m->track );
    if( line ) {
      int transposition = aytrack_transposition( &m->track );
      int new_pattern = m->track.line == 0;
      for( chan = 0; chan < 3; chan++ ) {
        const aytrack_cell *cell = &line->cells[ chan ];
        /* As ZXTune: only the channels with data */
        if( !cell_has_data( cell ) ) continue;
        if( new_pattern ) m->channels[ chan ].envelope.reinit = 0;
        new_state( m, &m->channels[ chan ], cell, transposition, &regs );
      }
    }
  }

  for( chan = 0; chan < 3; chan++ )
    synthesize( &m->channels[ chan ], chan, &regs );
  aytrack_registers_end( &regs, registers, mask );

  aytrack_next_frame( &m->track );
}

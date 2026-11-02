/* pscplayer.c: Pro Sound Creator (.psc) module player
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   Based on ZXTune (https://zxtune.bitbucket.io/), Copyright (c)
   Vitamin/CAIG <vitamin.caig@gmail.com>, GNU LGPL version 3: the module
   parser (src/formats/chiptune/aym/prosoundcreator.cpp) and the player
   (src/module/players/aym/prosoundcreator.cpp).

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

/* The module: a header ("PSC Vx.xx COMPILATION OF " with the title and
   author, 69 bytes, then the offsets of the start of the samples and of
   the positions, the tempo and the offset of the list of the ornaments),
   the list of the offsets of the samples right after it, the list of the
   ornaments, the samples (6 bytes a line), the ornaments (2 bytes a line),
   the patterns' data, and the positions: 8 bytes each (the size of the
   pattern and the offsets of its 3 channels' data), up to a 0xff in the
   second byte of one, whose first byte is the loop position. From version
   1.03 the offsets of the samples are from the end of the header and those
   of the ornaments from the start of their list */

#include <stdlib.h>
#include <string.h>

#include "aytrack.h"
#include "pscplayer.h"

#define MIN_SIZE 256
#define MAX_SIZE 0x4200
#define HEADER_SIZE 76
#define ID_SIZE 69
#define MAX_SAMPLES 32
#define MAX_SAMPLE_SIZE 32
#define MAX_ORNAMENTS 32
#define MAX_ORNAMENT_SIZE 32
#define MIN_PATTERN_SIZE 1
#define MAX_PATTERNS 32
#define POSITION_ENTRY 8
#define LAST_POSITION_ENTRY 4
#define SAMPLE_LINE 6
#define ORNAMENT_LINE 2
#define END_POSITION_MARKER 0xff

/* Offsets in the header */
#define OFFSET_VERSION 5
#define OFFSET_TITLE 25
#define OFFSET_BY 45
#define OFFSET_AUTHOR 49
#define OFFSET_SAMPLES_START 69
#define OFFSET_POSITIONS 71
#define OFFSET_TEMPO 73
#define OFFSET_ORNAMENTS 74

/* Commands of the cells. ZXTune keeps every command of a cell in its
   order; here those of which only the last one counts are kept once, the
   flags are put together and a glissando followed by another glissando or
   slide is dropped as nothing of it is left, so that a cell fits in
   AYTRACK_MAX_COMMANDS */
enum {
  CMD_ENVELOPE = 1,		/* type, tone */
  CMD_ENVELOPE_SWITCH,		/* on */
  CMD_NOISE_BASE,		/* value */
  CMD_FLAGS,			/* FLAG_* */
  CMD_GLISS,			/* step */
  CMD_SLIDE,			/* glissade */
  CMD_VOLUME_SLIDE		/* period, delta */
};

#define FLAG_BREAK_SAMPLE 1
#define FLAG_BREAK_ORNAMENT 2
#define FLAG_NO_ORNAMENT 4

typedef struct sample_line {
  int level;			/* 0-15 */
  unsigned int tone;
  int tone_mask, noise_mask;
  int adding;
  int envelope;
  int volume_delta;		/* -1, 0, +1 */
  int loop_begin, loop_end;
} sample_line;

/* No lines: one not in the module (ZXTune's stub objects) */
typedef struct sample {
  unsigned int size;
  sample_line lines[ MAX_SAMPLE_SIZE ];
} sample;

typedef struct ornament_line {
  int note_addon;
  unsigned int noise_addon;
  int loop_begin, loop_end;
} ornament_line;

typedef struct ornament {
  unsigned int size;
  ornament_line lines[ MAX_ORNAMENT_SIZE ];
} ornament;

/* ZXTune's ObjectLinesIterator: the object may change under it while its
   position stays */
typedef struct lines_iterator {
  unsigned int position, loop_position;
  int break_loop;
} lines_iterator;

typedef struct channel {
  const sample *sample;
  lines_iterator sample_iterator;
  const ornament *ornament;
  lines_iterator ornament_iterator;
  /* The tone slide */
  int sliding, glissade;
  unsigned int sliding_steps;
  /* The volume slide */
  unsigned int volume_counter, volume_period;
  int volume_delta;
  int note;
  short tone_accumulator;	/* 16 bits, as ZXTune's */
  unsigned int volume;
  int attenuation;
  unsigned int noise_accumulator;
  int envelope;
} channel;

struct psc_module {
  char title[ 45 ], author[ 21 ];
  aytrack track;
  sample samples[ MAX_SAMPLES ];
  ornament ornaments[ MAX_ORNAMENTS ];

  channel channels[3];
  unsigned int envelope_tone;
  unsigned int noise_base;
};

/* ------------------------------------------------------------------------
   Checking the module
   ------------------------------------------------------------------------ */

static unsigned int
word( const unsigned char *p )
{
  return p[0] | p[1] << 8;
}

static const unsigned char psc_id0[] = "PSC V";
static const unsigned char psc_id1[] = " COMPILATION OF ";

static int
has_id( const unsigned char *data )
{
  return !memcmp( data, psc_id0, sizeof( psc_id0 ) - 1 ) &&
         !memcmp( data + 9, psc_id1, sizeof( psc_id1 ) - 1 );
}

/* The version from the identifier (x.xx), 0 if it is not there */
static int
version( const unsigned char *data )
{
  const unsigned char *v = data + OFFSET_VERSION;

  if( v[0] >= '0' && v[0] <= '9' && v[1] == '.' &&
      v[2] >= '0' && v[2] <= '9' && v[3] >= '0' && v[3] <= '9' )
    return 100 * ( v[0] - '0' ) + 10 * ( v[2] - '0' ) + ( v[3] - '0' );
  return 0;
}

/* Where the offsets of the samples and the ornaments count from */
typedef struct traits {
  size_t samples_base, ornaments_base;
} traits;

static void
get_traits( const unsigned char *data, traits *t )
{
  int v = version( data );

  if( v == 0 || v >= 103 ) {
    t->samples_base = HEADER_SIZE;
    t->ornaments_base = word( data + OFFSET_ORNAMENTS );
  } else {
    t->samples_base = t->ornaments_base = 0;
  }
}

/* ZXTune's format pattern: the bytes after the identifier */
static int
format_matches( const unsigned char *data, size_t length )
{
  if( length < MIN_SIZE ) return 0;
  return data[70] == 0x00 &&
         data[72] >= 0x03 && data[72] <= 0x3f &&
         data[73] >= 0x03 && data[73] <= 0x1f &&
         data[74] >= 0x50 && data[74] <= 0x90 && data[75] == 0x00 &&
         data[76] >= 0x08 && data[76] <= 0xcf && data[77] == 0x00;
}

/* The size of an area: up to the nearest area after it (ZXTune's
   AreaController); 0 for an area not there, UNDEFINED if none after it */
enum { AREA_HEADER, AREA_SAMPLES_OFFSETS, AREA_ORNAMENTS_OFFSETS,
       AREA_SAMPLES, AREA_ORNAMENTS, AREA_PATTERNS, AREA_POSITIONS,
       AREA_END, AREAS };
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
  size_t areas[ AREAS ], size;
  size_t ornaments = word( data + OFFSET_ORNAMENTS );
  size_t positions = word( data + OFFSET_POSITIONS );
  traits t;
  int i;

  get_traits( data, &t );
  for( i = 0; i < AREAS; i++ ) areas[i] = UNDEFINED;
  areas[ AREA_HEADER ] = 0;
  areas[ AREA_SAMPLES_OFFSETS ] = HEADER_SIZE;
  areas[ AREA_ORNAMENTS_OFFSETS ] = ornaments;
  areas[ AREA_POSITIONS ] = positions;
  areas[ AREA_END ] = length;
  /* The first sample: one byte after the start of the samples */
  if( word( data + HEADER_SIZE ) + t.samples_base ==
      (size_t)word( data + OFFSET_SAMPLES_START ) + 1 )
    areas[ AREA_SAMPLES ] = word( data + HEADER_SIZE ) + t.samples_base;
  if( ornaments + 2 <= length )
    areas[ AREA_ORNAMENTS ] = word( data + ornaments ) + t.ornaments_base;
  if( positions + POSITION_ENTRY <= length ) {
    size_t first = word( data + positions + 2 ), offset;
    offset = word( data + positions + 4 );
    if( offset < first ) first = offset;
    offset = word( data + positions + 6 );
    if( offset < first ) first = offset;
    areas[ AREA_PATTERNS ] = first;
  }

  /* The header */
  if( area_size( areas, AREA_HEADER ) != HEADER_SIZE ||
      area_size( areas, AREA_END ) != UNDEFINED )
    return 0;
  if( !( areas[ AREA_SAMPLES_OFFSETS ] < areas[ AREA_ORNAMENTS_OFFSETS ] &&
         areas[ AREA_SAMPLES ] < areas[ AREA_ORNAMENTS ] ) )
    return 0;

  /* The samples */
  size = area_size( areas, AREA_SAMPLES_OFFSETS );
  if( size == UNDEFINED || size < 2 || size % 2 ) return 0;
  size = area_size( areas, AREA_SAMPLES );
  if( size == UNDEFINED || size < SAMPLE_LINE ) return 0;

  /* The ornaments */
  size = area_size( areas, AREA_ORNAMENTS_OFFSETS );
  if( size == UNDEFINED || size < 2 ) return 0;
  size = area_size( areas, AREA_ORNAMENTS );
  if( size == UNDEFINED || size < ORNAMENT_LINE ) return 0;

  /* The positions and the patterns */
  size = area_size( areas, AREA_POSITIONS );
  if( size == UNDEFINED || size < POSITION_ENTRY + LAST_POSITION_ENTRY )
    return 0;
  return area_size( areas, AREA_PATTERNS ) != UNDEFINED;
}

int
psc_check( const unsigned char *data, size_t length )
{
  if( length > MAX_SIZE ) length = MAX_SIZE;
  return format_matches( data, length ) && fast_check( data, length );
}

/* ------------------------------------------------------------------------
   Reading the module
   ------------------------------------------------------------------------ */

/* The areas of the header, the positions and the entries of the lists of
   the samples and ornaments: they may not overlap, but the same one may be
   there more than once (ZXTune's shared range checker) */
#define MAX_RANGES ( 1 + AYTRACK_MAX_POSITIONS + 1 + MAX_SAMPLES + \
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

static void
remove_command( aytrack_cell *cell, int type )
{
  int i;

  for( i = 0; i < cell->command_count; i++ ) {
    if( cell->commands[i].type != type ) continue;
    memmove( &cell->commands[i], &cell->commands[ i + 1 ],
             ( cell->command_count - i - 1 ) * sizeof( cell->commands[0] ) );
    cell->command_count--;
    return;
  }
}

static void
set_flag( aytrack_cell *cell, int flag )
{
  aytrack_command *cmd = aytrack_find_command( cell, CMD_FLAGS );

  if( cmd ) cmd->param1 |= flag;
  else aytrack_add_command( cell, CMD_FLAGS, flag, 0, 0 );
}

/* A glissando sets all the tone slide: what was before it goes. A slide
   sets all but the sliding: a glissando before it stays (before it) */
static void
add_gliss( aytrack_cell *cell, int step )
{
  remove_command( cell, CMD_GLISS );
  remove_command( cell, CMD_SLIDE );
  aytrack_add_command( cell, CMD_GLISS, step, 0, 0 );
}

static void
add_slide( aytrack_cell *cell, int glissade )
{
  remove_command( cell, CMD_SLIDE );
  aytrack_add_command( cell, CMD_SLIDE, glissade, 0, 0 );
}

/* ------------------------------------------------------------------------
   The patterns
   ------------------------------------------------------------------------ */

typedef struct pattern_entry {
  int size;
  size_t offsets[3];
} pattern_entry;

typedef struct channel_cursor {
  size_t offset;
  unsigned int period, counter;
} channel_cursor;

static void
parse_channel( parser *p, int chan, channel_cursor *c, aytrack_line *line,
               aytrack_cell *cell )
{
  while( c->offset < p->length && !p->error ) {
    int cmd = peek( p, c->offset++ );

    if( cmd >= 0xc0 ) {			/* lines to skip: the end */
      c->period = cmd - 0xc0;
      break;
    } else if( cmd >= 0xa0 ) {		/* ornament */
      cell->has_ornament = 1;
      cell->ornament = cmd - 0xa0;
      p->used_ornaments[ cmd - 0xa0 ] = 1;
    } else if( cmd >= 0x80 ) {		/* sample */
      cell->has_sample = 1;
      cell->sample = cmd - 0x80;
      p->used_samples[ cmd - 0x80 ] = 1;
    } else if( cmd == 0x7d ) {		/* break the sample's loop */
      set_flag( cell, FLAG_BREAK_SAMPLE );
    } else if( cmd == 0x7c ) {		/* rest */
      cell->has_enabled = 1;
      cell->enabled = 0;
    } else if( cmd == 0x7b ) {		/* noise base, channel B only */
      if( chan == 1 )
        set_command( cell, CMD_NOISE_BASE, peek( p, c->offset++ ), 0 );
    } else if( cmd == 0x7a ) {		/* envelope, channel B only */
      if( chan == 1 ) {
        int type = peek( p, c->offset++ ) & 15;
        int tone = peek( p, c->offset + 1 ) << 8 | peek( p, c->offset );
        c->offset += 2;
        /* As ZXTune's player: no type and tone is the envelope on */
        if( type || tone )
          set_command( cell, CMD_ENVELOPE, type, tone );
        else
          set_command( cell, CMD_ENVELOPE_SWITCH, 1, 0 );
      }
    } else if( cmd == 0x71 ) {		/* break the ornament's loop */
      set_flag( cell, FLAG_BREAK_ORNAMENT );
      c->offset++;
    } else if( cmd == 0x70 ) {		/* volume slide */
      int value = peek( p, c->offset++ );
      if( value & 64 )
        set_command( cell, CMD_VOLUME_SLIDE,
                     -(int)(signed char)( value | 128 ), -1 );
      else
        set_command( cell, CMD_VOLUME_SLIDE, value, 1 );
    } else if( cmd == 0x6f ) {		/* no ornament */
      set_flag( cell, FLAG_NO_ORNAMENT );
      c->offset++;
    } else if( cmd == 0x6e ) {		/* tempo, not checked */
      line->tempo = peek( p, c->offset++ );
    } else if( cmd == 0x6d ) {		/* glissando */
      add_gliss( cell, peek( p, c->offset++ ) );
    } else if( cmd == 0x6c ) {		/* slide down */
      add_slide( cell, -(int)(signed char)peek( p, c->offset++ ) );
    } else if( cmd == 0x6b ) {		/* slide up */
      add_slide( cell, peek( p, c->offset++ ) );
    } else if( cmd >= 0x58 && cmd <= 0x66 ) {	/* volume, no envelope */
      cell->has_volume = 1;
      cell->volume = cmd - 0x57;
      set_command( cell, CMD_ENVELOPE_SWITCH, 0, 0 );
    } else if( cmd == 0x57 ) {		/* full volume, envelope */
      cell->has_volume = 1;
      cell->volume = 15;
      set_command( cell, CMD_ENVELOPE_SWITCH, 1, 0 );
    } else if( cmd <= 0x56 ) {		/* note */
      cell->has_enabled = 1;
      cell->enabled = 1;
      cell->has_note = 1;
      cell->note = cmd;
    }
    /* else nothing */
  }
}

/* As ZXTune: the pattern is as long as its entry says, whatever the data
   of its channels; whether it has a line at least */
static int
parse_pattern( parser *p, aytrack *t, int index, const pattern_entry *e )
{
  channel_cursor c[3];
  unsigned int line;
  int chan;

  if( !aytrack_pattern_lines( t, index ) ) {
    p->error = 1;
    return 0;
  }
  memset( c, 0, sizeof( c ) );
  for( chan = 0; chan < 3; chan++ ) c[ chan ].offset = e->offsets[ chan ];

  for( line = 0; line < (unsigned int)e->size && !p->error; line++ ) {
    unsigned int skip = c[0].counter;
    aytrack_line *l;

    for( chan = 1; chan < 3; chan++ )
      if( c[ chan ].counter < skip ) skip = c[ chan ].counter;
    if( skip ) {
      for( chan = 0; chan < 3; chan++ ) c[ chan ].counter -= skip;
      line += skip - 1;
      continue;
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
      parse_channel( p, chan, &c[ chan ], l, &l->cells[ chan ] );
      c[ chan ].counter = c[ chan ].period;
    }
  }
  if( p->error ) return 0;
  aytrack_pattern_size( t, index, e->size );

  /* The data of the channels, up to the byte after the last one read */
  for( chan = 0; chan < 3; chan++ ) {
    size_t stop;
    if( e->offsets[ chan ] >= p->length ) continue;
    stop = c[ chan ].offset + 1 < p->length ? c[ chan ].offset + 1 :
                                              p->length;
    add_range( p, e->offsets[ chan ], stop - e->offsets[ chan ] );
  }
  return line >= MIN_PATTERN_SIZE;
}

/* ------------------------------------------------------------------------
   The samples and ornaments
   ------------------------------------------------------------------------ */

static void
parse_sample( parser *p, sample *s, size_t offset )
{
  size_t avail, i;

  if( offset + SAMPLE_LINE > p->length ) {
    p->error = 1;
    return;
  }
  add_range( p, offset, SAMPLE_LINE );
  avail = ( p->length - offset ) / SAMPLE_LINE;
  if( avail > MAX_SAMPLE_SIZE ) avail = MAX_SAMPLE_SIZE;
  s->size = 0;
  for( i = 0; i < avail; i++ ) {
    /* Tone (16 bits), adding (signed), xxxxLLLL level, BEFeNDUT flags:
       loop begin, loop end, finished (all 0 for yes), envelope (0 for
       on), noise mask, volume down, volume up, tone mask */
    const unsigned char *src = p->data + offset + i * SAMPLE_LINE;
    sample_line *l = &s->lines[i];
    int flags = src[4];

    l->tone = word( src );
    l->adding = (signed char)src[2];
    l->level = src[3] & 15;
    l->tone_mask = ( flags & 1 ) != 0;
    l->noise_mask = ( flags & 8 ) != 0;
    l->envelope = ( flags & 16 ) == 0;
    l->volume_delta = ( ( flags & 2 ) != 0 ) - ( ( flags & 4 ) != 0 );
    l->loop_begin = ( flags & 128 ) == 0;
    l->loop_end = ( flags & 64 ) == 0;
    s->size = i + 1;
    if( !( flags & 32 ) ) break;
  }
  add_range( p, offset, s->size * SAMPLE_LINE );
}

static void
parse_ornament( parser *p, ornament *o, size_t offset )
{
  size_t avail, i;

  if( offset + ORNAMENT_LINE > p->length ) {
    p->error = 1;
    return;
  }
  add_range( p, offset, ORNAMENT_LINE );
  avail = ( p->length - offset ) / ORNAMENT_LINE;
  if( avail > MAX_ORNAMENT_SIZE ) avail = MAX_ORNAMENT_SIZE;
  o->size = 0;
  for( i = 0; i < avail; i++ ) {
    /* BEFooooo OOOOOOOO: loop begin, loop end, finished (all 0 for yes),
       noise offset; note offset (signed) */
    const unsigned char *src = p->data + offset + i * ORNAMENT_LINE;
    ornament_line *l = &o->lines[i];

    l->noise_addon = src[0] & 31;
    l->note_addon = (signed char)src[1];
    l->loop_begin = ( src[0] & 128 ) == 0;
    l->loop_end = ( src[0] & 64 ) == 0;
    o->size = i + 1;
    if( !( src[0] & 32 ) ) break;
  }
  add_range( p, offset, o->size * ORNAMENT_LINE );
}

static void
parse_samples( parser *p, psc_module *m, const traits *t )
{
  int i;

  for( i = 0; i < MAX_SAMPLES && !p->error; i++ ) {
    size_t entry = HEADER_SIZE + i * 2;
    if( !p->used_samples[i] ) continue;
    add_service( p, entry, 2 );
    if( p->error ) return;
    parse_sample( p, &m->samples[i], t->samples_base +
                                     word( p->data + entry ) );
  }
}

/* As ZXTune: those beyond the list (whose end is the start of the
   samples) are stubs */
static void
parse_ornaments( parser *p, psc_module *m, const traits *t )
{
  size_t start = word( p->data + OFFSET_ORNAMENTS );
  size_t end = word( p->data + OFFSET_SAMPLES_START );
  size_t count = ( end - start ) / 2;	/* huge if the end is before */
  int i;

  for( i = 0; i < MAX_ORNAMENTS && !p->error; i++ ) {
    size_t entry = start + i * 2;
    if( !p->used_ornaments[i] || (size_t)i >= count ) continue;
    add_service( p, entry, 2 );
    if( p->error ) return;
    parse_ornament( p, &m->ornaments[i], t->ornaments_base +
                                         word( p->data + entry ) );
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

/* 20 characters of title, " BY " (or something else: then all the rest
   is the title) and 20 of author */
static void
read_texts( psc_module *m, const unsigned char *data )
{
  const unsigned char *by = data + OFFSET_BY;
  int start = 0, end = 4;

  if( !has_id( data ) ) return;
  while( start < end && ( by[ start ] <= ' ' || by[ start ] > 0x7f ) )
    start++;
  while( end > start && ( by[ end - 1 ] <= ' ' || by[ end - 1 ] > 0x7f ) )
    end--;
  if( end - start == 2 && ( by[ start ] == 'B' || by[ start ] == 'b' ) &&
      ( by[ start + 1 ] == 'Y' || by[ start + 1 ] == 'y' ) ) {
    optimize_text( m->title, data + OFFSET_TITLE, 20 );
    optimize_text( m->author, data + OFFSET_AUTHOR, 20 );
  } else {
    optimize_text( m->title, data + OFFSET_TITLE, 44 );
  }
}

/* The positions: the patterns are told by their entries, the same entry
   is the same pattern. As ZXTune, there may be 32 patterns at most; more
   positions than the track model holds are not played (ZXTune does not
   limit them, but such a module can hardly be) */
static int
parse_positions( parser *p, psc_module *m, pattern_entry *patterns )
{
  size_t offset = word( p->data + OFFSET_POSITIONS );
  int count = 0, pattern_count = 0;

  for( ;; offset += POSITION_ENTRY ) {
    pattern_entry e;
    int i, chan;

    if( offset + LAST_POSITION_ENTRY > p->length ) {
      p->error = 1;
      return 0;
    }
    if( p->data[ offset + 1 ] == END_POSITION_MARKER ) {
      size_t tail = p->length - offset;
      add_service( p, offset, tail < POSITION_ENTRY ? tail : POSITION_ENTRY );
      if( count == 0 ) p->error = 1;
      if( p->error ) return 0;
      m->track.position_count = count;
      m->track.loop_position = p->data[ offset ] < count ? p->data[ offset ] :
                                                          count - 1;
      return pattern_count;
    }
    add_service( p, offset, POSITION_ENTRY );
    if( p->error ) return 0;
    e.size = p->data[ offset + 1 ];
    for( chan = 0; chan < 3; chan++ )
      e.offsets[ chan ] = word( p->data + offset + 2 + chan * 2 );
    for( i = 0; i < pattern_count; i++ )
      if( patterns[i].size == e.size &&
          !memcmp( patterns[i].offsets, e.offsets, sizeof( e.offsets ) ) )
        break;
    if( i == pattern_count ) {
      if( pattern_count >= MAX_PATTERNS ) {
        p->error = 1;
        return 0;
      }
      patterns[ pattern_count++ ] = e;
    }
    if( count >= AYTRACK_MAX_POSITIONS ) {
      p->error = 1;
      return 0;
    }
    m->track.positions[ count ].pattern = i;
    m->track.positions[ count ].transposition = 0;
    count++;
  }
}

psc_module *
psc_load( const unsigned char *data, size_t length )
{
  parser p;
  psc_module *m;
  traits t;
  pattern_entry patterns[ MAX_PATTERNS ];
  int i, pattern_count, any_pattern = 0;

  if( length > MAX_SIZE ) length = MAX_SIZE;
  if( !psc_check( data, length ) ) return NULL;
  get_traits( data, &t );

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

  add_service( &p, 0, HEADER_SIZE );
  read_texts( m, data );

  pattern_count = parse_positions( &p, m, patterns );
  for( i = 0; i < pattern_count && !p.error; i++ )
    if( parse_pattern( &p, &m->track, i, &patterns[i] ) ) any_pattern = 1;
  if( !any_pattern ) p.error = 1;

  /* Samples and ornaments: the used ones, 0 always */
  p.used_samples[0] = p.used_ornaments[0] = 1;
  if( !p.error ) parse_samples( &p, m, &t );
  if( !p.error ) parse_ornaments( &p, m, &t );

  if( p.error || p.used < MIN_SIZE ||
      !aytrack_positions_valid( &m->track ) ) {
    psc_free( m );
    return NULL;
  }

  psc_reset( m );
  return m;
}

void
psc_free( psc_module *m )
{
  if( !m ) return;
  aytrack_free( &m->track );
  free( m );
}

const char *
psc_title( const psc_module *m )
{
  return m->title;
}

const char *
psc_author( const psc_module *m )
{
  return m->author;
}

long
psc_length( psc_module *m, long *loop )
{
  long frames = aytrack_length( &m->track, loop );
  psc_reset( m );
  return frames;
}

/* ------------------------------------------------------------------------
   The player
   ------------------------------------------------------------------------ */

/* As ZXTune: all the channels on the (stub) sample and ornament 0, at
   their end */
void
psc_reset( psc_module *m )
{
  int i;

  for( i = 0; i < 3; i++ ) {
    channel *c = &m->channels[i];
    memset( c, 0, sizeof( *c ) );
    c->sample = &m->samples[0];
    c->sample_iterator.position = c->sample->size;
    c->ornament = &m->ornaments[0];
    c->ornament_iterator.position = c->ornament->size;
  }
  m->envelope_tone = 0;
  m->noise_base = 0;
  aytrack_reset( &m->track );
}

/* On to the next line of a sample or ornament, after the one played */
static void
iterator_next( lines_iterator *it, int loop_begin, int loop_end )
{
  if( loop_begin ) it->loop_position = it->position;
  if( loop_end ) {
    if( !it->break_loop ) {
      it->position = it->loop_position;
    } else {
      it->break_loop = 0;
      it->position++;
    }
  } else {
    it->position++;
  }
}

static int
tone_slide_update( channel *c )
{
  if( !c->glissade ) return 0;
  c->sliding += c->glissade;
  if( c->sliding_steps && !--c->sliding_steps ) c->glissade = 0;
  return c->sliding;
}

static int
volume_slide_update( channel *c )
{
  if( c->volume_counter && !--c->volume_counter ) {
    c->volume_counter = c->volume_period;
    return c->volume_delta;
  }
  return 0;
}

static void
new_state( psc_module *m, channel *c, const aytrack_cell *cell,
           aytrack_registers *regs )
{
  /* The tone before the line, for a glissando (16 bits, as ZXTune's) */
  unsigned short old_tone = aytrack_frequency( regs, c->note ) +
                            c->tone_accumulator + c->sliding;
  int i;

  if( cell->has_enabled ) {
    if( cell->enabled ) {
      c->sample_iterator.position = 0;
      c->sample_iterator.break_loop = 0;
      c->ornament_iterator.position = 0;
      c->ornament_iterator.break_loop = 0;
      c->volume_counter = 0;
      c->sliding = 0;
      c->tone_accumulator = 0;
      c->noise_accumulator = 0;
      c->attenuation = 0;
    } else {
      c->sample_iterator.break_loop = 0;
      c->sample_iterator.position = c->sample->size;
      c->ornament_iterator.break_loop = 0;
      c->ornament_iterator.position = c->ornament->size;
    }
    c->glissade = 0;
  }
  if( cell->has_note ) c->note = cell->note;
  if( cell->has_sample ) c->sample = &m->samples[ cell->sample ];
  if( cell->has_ornament ) c->ornament = &m->ornaments[ cell->ornament ];
  if( cell->has_volume ) {
    c->volume = cell->volume;
    c->attenuation = 0;
  }
  for( i = 0; i < cell->command_count; i++ ) {
    const aytrack_command *cmd = &cell->commands[i];
    switch( cmd->type ) {
    case CMD_FLAGS:
      if( cmd->param1 & FLAG_BREAK_SAMPLE ) c->sample_iterator.break_loop = 1;
      if( cmd->param1 & FLAG_BREAK_ORNAMENT )
        c->ornament_iterator.break_loop = 1;
      if( cmd->param1 & FLAG_NO_ORNAMENT )
        c->ornament_iterator.position = c->ornament->size;
      break;
    case CMD_ENVELOPE:
      aytrack_set_envelope_type( regs, cmd->param1 );
      m->envelope_tone = cmd->param2;
      aytrack_set_envelope_tone( regs, m->envelope_tone );
      break;
    case CMD_ENVELOPE_SWITCH:
      c->envelope = cmd->param1;
      break;
    case CMD_NOISE_BASE:
      m->noise_base = cmd->param1;
      break;
    case CMD_GLISS: {
      int sliding = (int)old_tone -
                    (int)aytrack_frequency( regs, c->note );
      int step = cmd->param1;
      c->sliding = sliding;
      c->glissade = sliding >= 0 ? -step : step;
      c->sliding_steps = step ?
        1 + ( sliding < 0 ? -sliding : sliding ) / step : 0;
      break;
    }
    case CMD_SLIDE:
      c->glissade = cmd->param1;
      c->sliding_steps = 0;
      break;
    case CMD_VOLUME_SLIDE:
      c->volume_period = c->volume_counter = cmd->param1;
      c->volume_delta = cmd->param2;
      break;
    }
  }
}

static void
synthesize( psc_module *m, channel *c, int chan, aytrack_registers *regs )
{
  const sample_line *sl;
  int level, envelope;

  if( c->sample_iterator.position >= c->sample->size ) {
    aytrack_set_level( regs, chan, 0 );
    return;
  }
  sl = &c->sample->lines[ c->sample_iterator.position ];
  iterator_next( &c->sample_iterator, sl->loop_begin, sl->loop_end );

  /* The tone: the ornament moves the note for good */
  if( c->ornament_iterator.position < c->ornament->size ) {
    const ornament_line *ol =
      &c->ornament->lines[ c->ornament_iterator.position ];
    c->noise_accumulator += ol->noise_addon;
    c->note += ol->note_addon;
    if( c->note < 0 ) c->note += 0x56;
    else if( c->note > 0x55 ) c->note -= 0x56;
    if( c->note > 0x55 ) c->note = 0x55;
    iterator_next( &c->ornament_iterator, ol->loop_begin, ol->loop_end );
  }
  c->tone_accumulator = (short)( c->tone_accumulator + sl->tone );
  aytrack_set_tone( regs, chan, c->note,
                    c->tone_accumulator + tone_slide_update( c ) );
  if( sl->tone_mask ) aytrack_disable_tone( regs, chan );

  /* The level */
  c->attenuation += sl->volume_delta + volume_slide_update( c );
  level = c->attenuation + (int)c->volume;
  if( level < 0 ) level = 0;
  if( level > 15 ) level = 15;
  c->attenuation = level - (int)c->volume;
  aytrack_set_level( regs, chan, ( 1 + level ) * sl->level >> 4 );

  /* The noise and the envelope */
  envelope = c->envelope && sl->envelope;
  if( envelope ) aytrack_enable_envelope( regs, chan );
  if( envelope && sl->noise_mask ) {
    m->envelope_tone += sl->adding;
    aytrack_set_envelope_tone( regs, m->envelope_tone );
  } else {
    c->noise_accumulator += sl->adding;
    if( !sl->noise_mask ) aytrack_set_noise( regs, c->noise_accumulator );
  }
  if( sl->noise_mask ) aytrack_disable_noise( regs, chan );
}

void
psc_frame( psc_module *m, unsigned char *registers, unsigned int *mask )
{
  aytrack_registers regs;
  int chan;

  aytrack_registers_start( &regs, aytable_asm );

  /* A new line: and the noise base goes on, with data there or not */
  if( m->track.quirk == 0 ) {
    const aytrack_line *line = aytrack_new_line( &m->track );
    if( line )
      for( chan = 0; chan < 3; chan++ )
        new_state( m, &m->channels[ chan ], &line->cells[ chan ], &regs );
    for( chan = 0; chan < 3; chan++ )
      m->channels[ chan ].noise_accumulator += m->noise_base;
  }

  for( chan = 0; chan < 3; chan++ )
    synthesize( m, &m->channels[ chan ], chan, &regs );
  aytrack_registers_end( &regs, registers, mask );

  aytrack_next_frame( &m->track );
}

/* ascplayer.c: ASC Sound Master (.asc, .as0) module player
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   Based on ZXTune (https://zxtune.bitbucket.io/), Copyright (c)
   Vitamin/CAIG <vitamin.caig@gmail.com>, GNU LGPL version 3: the module
   parser (src/formats/chiptune/aym/ascsoundmaster.cpp) and the player
   (src/module/players/aym/ascsoundmaster.cpp).

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

/* The module: a header (tempo, in v1.x the loop position, the offsets of
   the patterns, samples and ornaments, the number of positions, then the
   positions), optionally "ASM COMPILATION OF " with the title and author
   (63 bytes), the patterns (3 offsets each, from the start of the
   patterns, of the channels' data), the samples (a list of 32 offsets from
   the start of the samples, then their lines of 3 bytes) and the ornaments
   (a list of 32 offsets, then their lines of 2 bytes) */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "aytrack.h"
#include "ascplayer.h"

#define MIN_SIZE_V0 255
#define MAX_SIZE_V0 0x2400
#define MIN_SIZE_V1 256
#define MAX_SIZE_V1 0x3a00
#define ID_SIZE 63
#define LIST_SIZE 64		/* the offsets of the samples or ornaments */
#define PATTERN_ENTRY 6
#define SAMPLE_LINE 3
#define ORNAMENT_LINE 2
#define MAX_SAMPLES 32
#define MAX_SAMPLE_SIZE 150
#define MAX_ORNAMENTS 32
#define MAX_ORNAMENT_SIZE 30
#define MIN_PATTERN_SIZE 1
#define MAX_PATTERN_SIZE 64
#define MAX_PATTERNS 32

/* The sliding target when there is none */
#define NO_TARGET UINT_MAX

/* Commands of the cells. ZXTune keeps ENVELOPE_ON and ENVELOPE_OFF apart
   and every repeated command; here those whose effect is just that the
   last one wins are kept once (the order of the others does not matter
   to them), so that a cell fits in AYTRACK_MAX_COMMANDS */
enum {
  CMD_ENVELOPE = 1,		/* type or -1, tone or -1 */
  CMD_ENVELOPE_SWITCH,		/* on */
  CMD_NOISE,			/* value */
  CMD_CONT_SAMPLE,
  CMD_CONT_ORNAMENT,
  CMD_GLISS,			/* glissade */
  CMD_SLIDE,			/* steps, use the tone sliding */
  CMD_SLIDE_NOTE,		/* steps, note, use the tone sliding */
  CMD_AMPLITUDE_SLIDE,		/* period, delta */
  CMD_BREAK_SAMPLE
};

typedef struct sample_line {
  int level, tone_deviation, tone_mask, noise_mask, adding, envelope;
  int vol_slide;
} sample_line;

typedef struct sample {
  unsigned int size, loop, loop_limit;
  sample_line lines[ MAX_SAMPLE_SIZE ];
} sample;

typedef struct ornament_line {
  int note_addon, noise_addon;
} ornament_line;

typedef struct ornament {
  unsigned int size, loop, loop_limit;
  ornament_line lines[ MAX_ORNAMENT_SIZE ];
} ornament;

typedef struct channel {
  int enabled, envelope, break_sample;
  unsigned int volume;
  int volume_addon;
  unsigned int vol_slide_delay;
  int vol_slide_addon;
  unsigned int vol_slide_counter;
  int base_noise, current_noise;
  unsigned int note;
  signed char note_addon;
  unsigned int sample_num, current_sample_num, pos_in_sample;
  unsigned int ornament_num, current_ornament_num, pos_in_ornament;
  int tone_deviation;
  int sliding_steps;		/* negative: endless */
  int sliding;
  unsigned int sliding_target_note;
  int glissade;
} channel;

struct asc_module {
  char title[ 45 ], author[ 21 ];
  aytrack track;
  sample samples[ MAX_SAMPLES ];	/* those not in the module: no lines */
  ornament ornaments[ MAX_ORNAMENTS ];

  channel channels[3];
  unsigned int envelope_tone;
};

/* ------------------------------------------------------------------------
   Reading the module
   ------------------------------------------------------------------------ */

static unsigned int
word( const unsigned char *p )
{
  return p[0] | p[1] << 8;
}

static const unsigned char asc_id[] = "ASM COMPILATION OF ";

typedef struct header {
  int version;
  size_t min_size, max_size;
  size_t size;			/* with the positions */
  size_t patterns, samples, ornaments;
  int tempo, loop, length;
  const unsigned char *positions;
} header;

static void
read_header( const unsigned char *data, int version, header *h )
{
  int v1 = version == 1;

  h->version = version;
  h->min_size = v1 ? MIN_SIZE_V1 : MIN_SIZE_V0;
  h->max_size = v1 ? MAX_SIZE_V1 : MAX_SIZE_V0;
  h->tempo = data[0];
  h->loop = v1 ? data[1] : 0;
  h->patterns = word( data + 1 + v1 );
  h->samples = word( data + 3 + v1 );
  h->ornaments = word( data + 5 + v1 );
  h->length = data[ 7 + v1 ];
  h->positions = data + 8 + v1;
  h->size = 8 + v1 + h->length;
}

/* The bytes of typical modules (ZXTune's format patterns) */
static int
typical( const unsigned char *data, int version )
{
  if( version == 1 )
    return data[0] >= 0x03 && data[0] <= 0x32 && data[1] <= 0x63 &&
           data[2] >= 0x0a && data[2] <= 0xac && data[3] == 0 &&
           data[5] <= 0x35 && data[7] <= 0x37 &&
           data[8] >= 0x01 && data[8] <= 0x64 && data[9] <= 0x1f;
  return data[0] >= 0x03 && data[0] <= 0x32 &&
         data[1] >= 0x09 && data[1] <= 0xab && data[2] == 0 &&
         data[4] <= 0x21 && data[6] <= 0x22 &&
         data[7] >= 0x01 && data[7] <= 0x64 && data[8] <= 0x1f;
}

static int
header_valid( const header *h )
{
  int i;

  if( h->tempo < 0x03 || h->tempo > 0x32 || h->loop > 0x63 ||
      h->length < 0x01 || h->length > 0x64 )
    return 0;
  for( i = 0; i < h->length; i++ )
    if( h->positions[i] >= MAX_PATTERNS ) return 0;
  return 1;
}

static int
has_id( const unsigned char *data, size_t length, const header *h )
{
  return h->size + ID_SIZE <= length &&
         !memcmp( data + h->size, asc_id, sizeof( asc_id ) - 1 );
}

/* The size of an area: up to the nearest area after it (ZXTune's
   AreaController); 0 for an area not there, -1 if none after it */
enum { AREA_HEADER, AREA_ID, AREA_PATTERNS, AREA_SAMPLES, AREA_ORNAMENTS,
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

static int
check_version( const unsigned char *data, size_t length, int version )
{
  header h;
  size_t areas[ AREAS ], size;

  if( length < ( version == 1 ? MIN_SIZE_V1 : MIN_SIZE_V0 ) ) return 0;
  if( !typical( data, version ) ) return 0;
  read_header( data, version, &h );
  if( length > h.max_size ) length = h.max_size;

  areas[ AREA_HEADER ] = 0;
  areas[ AREA_ID ] = has_id( data, length, &h ) ? h.size : UNDEFINED;
  areas[ AREA_PATTERNS ] = h.patterns;
  areas[ AREA_SAMPLES ] = h.samples;
  areas[ AREA_ORNAMENTS ] = h.ornaments;
  areas[ AREA_END ] = length;

  /* The header */
  if( !header_valid( &h ) ) return 0;
  if( h.size > area_size( areas, AREA_HEADER ) ||
      area_size( areas, AREA_END ) != UNDEFINED )
    return 0;
  size = area_size( areas, AREA_ID );
  if( size != 0 && size < ID_SIZE ) return 0;

  /* The list of the samples: the first one just after it */
  if( h.samples < h.patterns ) return 0;
  size = area_size( areas, AREA_SAMPLES );
  if( size == UNDEFINED || size < LIST_SIZE ) return 0;
  if( h.samples + LIST_SIZE > length ||
      word( data + h.samples ) != LIST_SIZE )
    return 0;

  /* The list of the ornaments */
  if( h.ornaments < h.samples ) return 0;
  size = area_size( areas, AREA_ORNAMENTS );
  if( size == UNDEFINED || size < LIST_SIZE ) return 0;
  if( h.ornaments + LIST_SIZE > length ||
      word( data + h.ornaments ) < LIST_SIZE )
    return 0;

  return 1;
}

int
asc_check( const unsigned char *data, size_t length )
{
  return check_version( data, length, 1 );
}

int
asc0_check( const unsigned char *data, size_t length )
{
  return check_version( data, length, 0 );
}

/* The areas of the header, the identifier, the pattern entries and the two
   lists: they may not overlap, but the same one may be there more than once
   (ZXTune's shared range checker) */
#define MAX_RANGES ( 4 + MAX_PATTERNS )

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

static const unsigned char *
service_object( parser *p, size_t offset, size_t size )
{
  if( offset + size > p->length || !ranges_add( &p->service, offset, size ) ) {
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

static void
add_command( aytrack_cell *cell, int type, int p1, int p2, int p3 )
{
  aytrack_add_command( cell, type, p1, p2, p3 );
}

/* A command of which only the last one counts */
static void
set_command( aytrack_cell *cell, int type, int p1, int p2 )
{
  aytrack_command *cmd = aytrack_find_command( cell, type );

  if( cmd ) {
    cmd->param1 = p1;
    cmd->param2 = p2;
  } else {
    add_command( cell, type, p1, p2, 0 );
  }
}

/* A glissade or slide: a glissade followed by either of them does nothing
   (they set all it set), so it goes */
static void
add_slide( aytrack_cell *cell, int type, int p1, int p2 )
{
  int i, last = -1;

  for( i = 0; i < cell->command_count; i++ )
    if( cell->commands[i].type == CMD_GLISS ||
        cell->commands[i].type == CMD_SLIDE )
      last = i;
  if( last >= 0 && cell->commands[ last ].type == CMD_GLISS ) {
    memmove( &cell->commands[ last ], &cell->commands[ last + 1 ],
             ( cell->command_count - last - 1 ) *
             sizeof( cell->commands[0] ) );
    cell->command_count--;
  }
  add_command( cell, type, p1, p2, 0 );
}

static void
set_note( aytrack_cell *cell, int note )
{
  aytrack_command *slide;

  if( !aytrack_find_command( cell, CMD_BREAK_SAMPLE ) ) {
    cell->has_enabled = 1;
    cell->enabled = 1;
  }
  slide = aytrack_find_command( cell, CMD_SLIDE );
  if( slide ) {
    /* A slide to the note */
    slide->type = CMD_SLIDE_NOTE;
    slide->param3 = slide->param2;
    slide->param2 = note;
  } else {
    cell->has_note = 1;
    cell->note = note;
  }
}

static void
set_envelope_type( aytrack_cell *cell, int type )
{
  aytrack_command *cmd = aytrack_find_command( cell, CMD_ENVELOPE );

  if( cmd ) cmd->param1 = type;
  else add_command( cell, CMD_ENVELOPE, type, -1, 0 );
}

static void
set_envelope_tone( aytrack_cell *cell, int tone )
{
  aytrack_command *cmd = aytrack_find_command( cell, CMD_ENVELOPE );

  if( cmd ) cmd->param2 = tone;
  else add_command( cell, CMD_ENVELOPE, -1, tone, 0 );
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
  int envelope;
} channel_cursor;

static void
parse_channel( parser *p, channel_cursor *c, aytrack_line *line,
               aytrack_cell *cell )
{
  while( c->offset < p->length && !p->error ) {
    int cmd = peek( p, c->offset++ );

    if( cmd <= 0x55 ) {			/* note */
      set_note( cell, cmd );
      if( c->envelope ) set_envelope_tone( cell, peek( p, c->offset++ ) );
      break;
    } else if( cmd <= 0x5d ) {		/* stop */
      break;
    } else if( cmd == 0x5e ) {		/* break the sample */
      set_command( cell, CMD_BREAK_SAMPLE, 0, 0 );
      break;
    } else if( cmd == 0x5f ) {		/* rest */
      cell->has_enabled = 1;
      cell->enabled = 0;
      break;
    } else if( cmd <= 0x9f ) {		/* lines to skip */
      c->period = cmd - 0x60;
    } else if( cmd <= 0xbf ) {		/* sample */
      cell->has_sample = 1;
      cell->sample = cmd - 0xa0;
      p->used_samples[ cmd - 0xa0 ] = 1;
    } else if( cmd <= 0xdf ) {		/* ornament */
      cell->has_ornament = 1;
      cell->ornament = cmd - 0xc0;
      p->used_ornaments[ cmd - 0xc0 ] = 1;
    } else if( cmd == 0xe0 ) {		/* envelope, full volume */
      cell->has_volume = 1;
      cell->volume = 15;
      set_command( cell, CMD_ENVELOPE_SWITCH, 1, 0 );
      c->envelope = 1;
    } else if( cmd <= 0xef ) {		/* volume, no envelope */
      cell->has_volume = 1;
      cell->volume = cmd - 0xe0;
      set_command( cell, CMD_ENVELOPE_SWITCH, 0, 0 );
      c->envelope = 0;
    } else if( cmd == 0xf0 ) {		/* noise */
      set_command( cell, CMD_NOISE, peek( p, c->offset++ ), 0 );
    } else if( ( cmd & 0xfc ) == 0xf0 ) {	/* continue sample/ornament */
      if( cmd & 1 ) set_command( cell, CMD_CONT_SAMPLE, 0, 0 );
      if( cmd & 2 ) set_command( cell, CMD_CONT_ORNAMENT, 0, 0 );
    } else if( cmd == 0xf4 ) {		/* tempo (not checked) */
      line->tempo = peek( p, c->offset++ );
    } else if( cmd <= 0xf6 ) {		/* glissade */
      int slide = ( cmd == 0xf5 ? -16 : 16 ) * peek( p, c->offset++ );
      add_slide( cell, CMD_GLISS, slide, 0 );
    } else if( cmd == 0xf7 || cmd == 0xf9 ) {	/* stepped slide */
      if( cmd == 0xf7 ) set_command( cell, CMD_CONT_SAMPLE, 0, 0 );
      add_slide( cell, CMD_SLIDE, (signed char)peek( p, c->offset++ ),
                 cmd == 0xf7 );
    } else if( ( cmd & 0xf9 ) == 0xf8 ) {	/* envelope type */
      set_envelope_type( cell, cmd & 0xf );
    } else if( cmd == 0xfb ) {		/* volume slide */
      int step = peek( p, c->offset++ );
      set_command( cell, CMD_AMPLITUDE_SLIDE, step & 31, step & 32 ? -1 : 1 );
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

/* Whether the pattern has a line at least */
static int
parse_pattern( parser *p, aytrack *t, size_t base, int index )
{
  const unsigned char *entry;
  channel_cursor c[3];
  size_t start[3];
  unsigned int line;
  int chan;

  entry = service_object( p, base + index * PATTERN_ENTRY, PATTERN_ENTRY );
  if( !entry ) return 0;
  if( !aytrack_pattern_lines( t, index ) ) {
    p->error = 1;
    return 0;
  }
  memset( c, 0, sizeof( c ) );
  for( chan = 0; chan < 3; chan++ )
    c[ chan ].offset = start[ chan ] = base + word( entry + chan * 2 );

  for( line = 0; line < MAX_PATTERN_SIZE; line++ ) {
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
    if( p->error ) return 0;
  }

  /* The data of the channels, up to the byte after the last one read */
  for( chan = 0; chan < 3; chan++ ) {
    size_t stop;
    if( start[ chan ] >= p->length ) continue;
    stop = c[ chan ].offset + 1 < p->length ? c[ chan ].offset + 1 :
                                              p->length;
    add_range( p, start[ chan ], stop - start[ chan ] );
  }
  return line >= MIN_PATTERN_SIZE;
}

/* ------------------------------------------------------------------------
   The samples and ornaments
   ------------------------------------------------------------------------ */

/* The low 5 bits, signed */
static int
low5( int value )
{
  return (signed char)( ( value * 8 ) & 0xff ) / 8;
}

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
  for( i = 0; i < avail; i++ ) {
    /* BEFaaaaa TTTTTTTT LLLLnCCt: loop begin, loop end, finished, adding;
       tone deviation; level, noise mask, command, tone mask */
    const unsigned char *src = p->data + offset + i * SAMPLE_LINE;
    sample_line *l = &s->lines[i];
    int command = ( src[2] & 6 ) >> 1;

    l->level = src[2] >> 4;
    l->tone_deviation = (signed char)src[1];
    l->tone_mask = ( src[2] & 1 ) != 0;
    l->noise_mask = ( src[2] & 8 ) != 0;
    l->adding = low5( src[0] );
    l->envelope = command == 1;
    l->vol_slide = command == 2 ? -1 : command == 3 ? 1 : 0;
    s->size = i + 1;
    if( src[0] & 128 ) s->loop = i;
    if( src[0] & 64 ) s->loop_limit = i;
    if( src[0] & 32 ) break;
  }
  add_range( p, offset, s->size * SAMPLE_LINE );
}

/* One out of the module is empty */
static void
parse_ornament( parser *p, ornament *o, size_t offset )
{
  size_t avail, i;

  if( offset + ORNAMENT_LINE > p->length ) return;
  avail = ( p->length - offset ) / ORNAMENT_LINE;
  if( avail > MAX_ORNAMENT_SIZE ) avail = MAX_ORNAMENT_SIZE;
  for( i = 0; i < avail; i++ ) {
    /* BEFooooo OOOOOOOO: loop begin, loop end, finished, noise offset;
       note offset */
    const unsigned char *src = p->data + offset + i * ORNAMENT_LINE;

    o->lines[i].note_addon = (signed char)src[1];
    o->lines[i].noise_addon = low5( src[0] );
    o->size = i + 1;
    if( src[0] & 128 ) o->loop = i;
    if( src[0] & 64 ) o->loop_limit = i;
    if( src[0] & 32 ) break;
  }
  add_range( p, offset, o->size * ORNAMENT_LINE );
}

/* The used ones, in their order: their offsets must grow by whole lines */
static void
parse_samples( parser *p, asc_module *m, size_t base )
{
  const unsigned char *list = service_object( p, base, LIST_SIZE );
  size_t previous;
  int i;

  if( !list ) return;
  previous = word( list ) - SAMPLE_LINE;
  for( i = 0; i < MAX_SAMPLES && !p->error; i++ ) {
    size_t current;
    if( !p->used_samples[i] ) continue;
    current = word( list + i * 2 );
    if( current <= previous || ( current - previous ) % SAMPLE_LINE ) {
      p->error = 1;
      return;
    }
    parse_sample( p, &m->samples[i], base + current );
    previous = current;
  }
}

static void
parse_ornaments( parser *p, asc_module *m, size_t base )
{
  const unsigned char *list = service_object( p, base, LIST_SIZE );
  size_t previous;
  int i;

  if( !list ) return;
  previous = word( list ) - ORNAMENT_LINE;
  for( i = 0; i < MAX_ORNAMENTS && !p->error; i++ ) {
    size_t current;
    if( !p->used_ornaments[i] ) continue;
    current = word( list + i * 2 );
    if( current <= previous || ( current - previous ) % ORNAMENT_LINE ) {
      p->error = 1;
      return;
    }
    parse_ornament( p, &m->ornaments[i], base + current );
    previous = current;
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

/* The texts after "ASM COMPILATION OF ": 20 characters of title, " BY "
   (or something else: then all the rest is the title) and 20 of author */
static void
read_texts( asc_module *m, const unsigned char *id )
{
  const unsigned char *by = id + 39;
  int start = 0, end = 4;

  while( start < end && ( by[ start ] <= ' ' || by[ start ] > 0x7f ) )
    start++;
  while( end > start && ( by[ end - 1 ] <= ' ' || by[ end - 1 ] > 0x7f ) )
    end--;
  if( end - start == 2 && ( by[ start ] == 'B' || by[ start ] == 'b' ) &&
      ( by[ start + 1 ] == 'Y' || by[ start + 1 ] == 'y' ) ) {
    optimize_text( m->title, id + 19, 20 );
    optimize_text( m->author, id + 43, 20 );
  } else {
    optimize_text( m->title, id + 19, 44 );
  }
}

static asc_module *
load_version( const unsigned char *data, size_t length, int version )
{
  parser p;
  header h;
  asc_module *m;
  int i, any_pattern = 0, used_patterns[ MAX_PATTERNS ];

  if( !check_version( data, length, version ) ) return NULL;
  read_header( data, version, &h );
  if( length > h.max_size ) length = h.max_size;

  memset( &p, 0, sizeof( p ) );
  p.data = data;
  p.length = length;

  m = calloc( 1, sizeof( *m ) );
  if( !m ) return NULL;
  if( !aytrack_init( &m->track, MAX_PATTERNS ) ) {
    free( m );
    return NULL;
  }
  m->track.initial_tempo = h.tempo;

  /* The header and the identifier (whose place must be there) */
  if( !ranges_add( &p.service, 0, h.size ) ) p.error = 1;
  add_range( &p, 0, h.size );
  add_range( &p, h.size, ID_SIZE );
  if( !p.error && has_id( data, length, &h ) ) {
    if( !ranges_add( &p.service, h.size, ID_SIZE ) ) p.error = 1;
    read_texts( m, data + h.size );
  }

  /* Positions */
  memset( used_patterns, 0, sizeof( used_patterns ) );
  for( i = 0; i < h.length; i++ ) {
    m->track.positions[i].pattern = h.positions[i];
    m->track.positions[i].transposition = 0;
    used_patterns[ h.positions[i] ] = 1;
  }
  m->track.position_count = h.length;
  m->track.loop_position = h.loop;

  /* Patterns: the used ones */
  for( i = 0; i < MAX_PATTERNS && !p.error; i++ )
    if( used_patterns[i] &&
        parse_pattern( &p, &m->track, h.patterns, i ) )
      any_pattern = 1;

  /* Samples and ornaments: the used ones, 0 always */
  p.used_samples[0] = p.used_ornaments[0] = 1;
  if( !p.error ) parse_samples( &p, m, h.samples );
  if( !p.error ) parse_ornaments( &p, m, h.ornaments );

  if( p.error || !any_pattern || p.used < h.min_size ||
      !aytrack_positions_valid( &m->track ) ) {
    asc_free( m );
    return NULL;
  }

  asc_reset( m );
  return m;
}

asc_module *
asc_load( const unsigned char *data, size_t length )
{
  return load_version( data, length, 1 );
}

asc_module *
asc0_load( const unsigned char *data, size_t length )
{
  return load_version( data, length, 0 );
}

void
asc_free( asc_module *m )
{
  if( !m ) return;
  aytrack_free( &m->track );
  free( m );
}

const char *
asc_title( const asc_module *m )
{
  return m->title;
}

const char *
asc_author( const asc_module *m )
{
  return m->author;
}

long
asc_length( asc_module *m, long *loop )
{
  long frames = aytrack_length( &m->track, loop );
  asc_reset( m );
  return frames;
}

/* ------------------------------------------------------------------------
   The player
   ------------------------------------------------------------------------ */

/* As ZXTune's player made anew (its own reset keeps the envelope tone) */
void
asc_reset( asc_module *m )
{
  int i;

  for( i = 0; i < 3; i++ ) {
    memset( &m->channels[i], 0, sizeof( m->channels[i] ) );
    m->channels[i].volume = 15;
    m->channels[i].sliding_target_note = NO_TARGET;
  }
  m->envelope_tone = 0;
  aytrack_reset( &m->track );
}

static const sample_line *
sample_line_of( const sample *s, unsigned int position )
{
  static const sample_line stub = { 0, 0, 1, 1, 0, 0, 0 };

  return position < s->size ? &s->lines[ position ] : &stub;
}

static const ornament_line *
ornament_line_of( const ornament *o, unsigned int position )
{
  static const ornament_line stub = { 0, 0 };

  return position < o->size ? &o->lines[ position ] : &stub;
}

static int
clamp( int value, int low, int high )
{
  return value < low ? low : value > high ? high : value;
}

static void
new_state( asc_module *m, channel *c, const aytrack_cell *cell,
           aytrack_registers *regs )
{
  int i, cont_sample = 0, cont_ornament = 0, reload_note = 0;

  if( cell->has_enabled ) c->enabled = cell->enabled;
  c->vol_slide_counter = 0;
  c->sliding_steps = 0;
  for( i = 0; i < cell->command_count; i++ ) {
    const aytrack_command *cmd = &cell->commands[i];
    switch( cmd->type ) {
    case CMD_ENVELOPE:
      if( cmd->param1 != -1 ) aytrack_set_envelope_type( regs, cmd->param1 );
      if( cmd->param2 != -1 ) {
        m->envelope_tone = cmd->param2;
        aytrack_set_envelope_tone( regs, m->envelope_tone );
      }
      break;
    case CMD_ENVELOPE_SWITCH:
      c->envelope = cmd->param1;
      break;
    case CMD_NOISE:
      c->base_noise = cmd->param1;
      break;
    case CMD_CONT_SAMPLE:
      cont_sample = 1;
      break;
    case CMD_CONT_ORNAMENT:
      cont_ornament = 1;
      break;
    case CMD_GLISS:
      c->glissade = cmd->param1;
      c->sliding_steps = -1;
      break;
    case CMD_SLIDE: {
      int sliding = ( c->sliding | 0xf ) ^ 0xf;
      c->sliding_steps = cmd->param1;
      c->glissade = -sliding / ( c->sliding_steps ? c->sliding_steps : 1 );
      c->sliding = c->glissade * c->sliding_steps;
      break;
    }
    case CMD_SLIDE_NOTE: {
      int absolute, sliding;
      c->sliding_steps = cmd->param1;
      c->sliding_target_note = cmd->param2;
      absolute = aytrack_sliding_difference( regs, c->note,
                                             c->sliding_target_note );
      sliding = absolute - ( cmd->param3 ? c->sliding / 16 : 0 );
      c->glissade = 16 * sliding /
                    ( c->sliding_steps ? c->sliding_steps : 1 );
      reload_note = 1;
      break;
    }
    case CMD_AMPLITUDE_SLIDE:
      c->vol_slide_counter = c->vol_slide_delay = cmd->param1;
      c->vol_slide_addon = cmd->param2;
      break;
    case CMD_BREAK_SAMPLE:
      c->break_sample = 1;
      break;
    }
  }
  if( cell->has_ornament ) c->ornament_num = cell->ornament;
  if( cell->has_sample ) c->sample_num = cell->sample;
  if( cell->has_note ) {
    c->note = cell->note;
    reload_note = 1;
  }
  if( reload_note ) {
    c->current_noise = c->base_noise;
    if( c->sliding_steps <= 0 ) c->sliding = 0;
    if( !cont_sample ) {
      c->current_sample_num = c->sample_num;
      c->pos_in_sample = 0;
      c->volume_addon = 0;
      c->tone_deviation = 0;
      c->break_sample = 0;
    }
    if( !cont_ornament ) {
      c->current_ornament_num = c->ornament_num;
      c->pos_in_ornament = 0;
      c->note_addon = 0;
    }
  }
  if( cell->has_volume ) c->volume = cell->volume;
}

static void
synthesize( asc_module *m, channel *c, int chan, aytrack_registers *regs )
{
  const sample *s;
  const sample_line *sl;
  const ornament *o;
  const ornament_line *ol;
  int halftone;

  if( !c->enabled ) {
    aytrack_set_level( regs, chan, 0 );
    return;
  }

  s = &m->samples[ c->current_sample_num ];
  sl = sample_line_of( s, c->pos_in_sample );
  o = &m->ornaments[ c->current_ornament_num ];
  ol = ornament_line_of( o, c->pos_in_ornament );

  /* The volume */
  if( c->vol_slide_counter >= 2 ) {
    c->vol_slide_counter--;
  } else if( c->vol_slide_counter ) {
    c->volume_addon += c->vol_slide_addon;
    c->vol_slide_counter = c->vol_slide_delay;
  }
  c->volume_addon = clamp( c->volume_addon + sl->vol_slide, -15, 15 );

  /* The tone; the halftone wraps as a signed byte before it is clamped */
  c->tone_deviation += sl->tone_deviation;
  c->note_addon = (signed char)( c->note_addon + ol->note_addon );
  halftone = clamp( (signed char)( (signed char)c->note + c->note_addon ),
                    0, 0x55 );
  aytrack_set_tone( regs, chan, halftone,
                    c->tone_deviation + c->sliding / 16 );

  aytrack_set_level( regs, chan, ( c->volume + 1 ) *
                     clamp( c->volume_addon + sl->level, 0, 15 ) / 16 );
  if( c->envelope && sl->envelope ) aytrack_enable_envelope( regs, chan );

  /* The noise and the mixer */
  c->current_noise += ol->noise_addon;
  if( sl->tone_mask ) aytrack_disable_tone( regs, chan );
  if( sl->noise_mask && sl->envelope ) {
    m->envelope_tone += sl->adding;
    aytrack_set_envelope_tone( regs, m->envelope_tone );
  } else {
    c->current_noise += sl->adding;
  }
  if( !sl->noise_mask )
    aytrack_set_noise( regs, ( c->current_noise + c->sliding / 256 ) & 0x1f );
  else
    aytrack_disable_noise( regs, chan );

  /* On: the slide */
  if( c->sliding_steps != 0 ) {
    if( c->sliding_steps > 0 ) {
      if( !--c->sliding_steps && c->sliding_target_note != NO_TARGET ) {
        /* The slide to a note is over */
        c->note = c->sliding_target_note;
        c->sliding_target_note = NO_TARGET;
        c->sliding = c->glissade = 0;
      }
    }
    c->sliding += c->glissade;
  }

  /* The sample and the ornament */
  if( c->pos_in_sample++ >= s->loop_limit ) {
    if( !c->break_sample )
      c->pos_in_sample = s->loop;
    else if( c->pos_in_sample >= s->size )
      c->enabled = 0;
  }
  if( c->pos_in_ornament++ >= o->loop_limit )
    c->pos_in_ornament = o->loop;
}

void
asc_frame( asc_module *m, unsigned char *registers, unsigned int *mask )
{
  aytrack_registers regs;
  int chan;

  aytrack_registers_start( &regs, aytable_asm );

  /* A new line: at the start of a pattern the noise goes back to 0 */
  if( m->track.quirk == 0 ) {
    const aytrack_line *line;
    if( m->track.line == 0 )
      for( chan = 0; chan < 3; chan++ ) m->channels[ chan ].base_noise = 0;
    line = aytrack_new_line( &m->track );
    if( line )
      for( chan = 0; chan < 3; chan++ )
        if( cell_has_data( &line->cells[ chan ] ) )
          new_state( m, &m->channels[ chan ], &line->cells[ chan ], &regs );
  }

  for( chan = 0; chan < 3; chan++ )
    synthesize( m, &m->channels[ chan ], chan, &regs );
  aytrack_registers_end( &regs, registers, mask );

  aytrack_next_frame( &m->track );
}

/* aymodule.c: The music formats played by register (trackers and register
   dumps), behind one interface
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ascplayer.h"
#include "aymodule.h"
#include "pscplayer.h"
#include "psmplayer.h"
#include "gtrplayer.h"
#include "ftcplayer.h"
#include "pt1player.h"
#include "pt2player.h"
#include "psgplayer.h"
#include "pt3model.h"
#include "pt3player.h"
#include "sqtplayer.h"
#include "stcplayer.h"
#include "stpplayer.h"
#include "ymplayer.h"

struct aymodule {
  const aymodule_format *format;
  void *data;
};

static aymodule *load_single( const unsigned char *data, size_t length );

/* ------------------------------------------------------------------------
   The formats
   ------------------------------------------------------------------------ */

/* PSG register dumps */

static void *psg_load( const unsigned char *d, size_t l )
{ return psgplayer_load( d, l ); }
static void psg_free( void *m ) { psgplayer_free( m ); }
static void psg_reset( void *m ) { psgplayer_reset( m ); }
static void psg_frame( void *m, unsigned char *r, unsigned int *mask )
{ psgplayer_frame( m, r, mask ); }
static long
psg_length( const void *m, long *loop )
{
  *loop = -1;
  return psgplayer_frames( m );
}

static const aymodule_format psg_format = {
  "PSG", psgplayer_check, psg_load, psg_free, psg_reset, psg_frame,
  NULL, NULL, NULL, psg_length, NULL
};

/* YM and VTX register dumps */

static void *ym_load( const unsigned char *d, size_t l )
{ return ymplayer_load( d, l ); }
static void ym_free( void *m ) { ymplayer_free( m ); }
static void ym_reset( void *m ) { ymplayer_reset( m ); }
static void ym_frame( void *m, unsigned char *r, unsigned int *mask )
{ ymplayer_frame( m, r, mask ); }
static const char *ym_title( const void *m ) { return ymplayer_title( m ); }
static const char *ym_author( const void *m ) { return ymplayer_author( m ); }
static const char *ym_comment( const void *m )
{ return ymplayer_comment( m ); }
static const char *ym_stereo( const void *m ) { return ymplayer_stereo( m ); }
static long
ym_length( const void *m, long *loop )
{
  *loop = ymplayer_loop( m );
  return ymplayer_frames( m );
}

static const aymodule_format ym_format = {
  "YM/VTX", ymplayer_check, ym_load, ym_free, ym_reset, ym_frame,
  ym_title, ym_author, ym_comment, ym_length, ym_stereo
};

/* Pro Tracker 3 (also Vortex Tracker II, and its text modules) */

static void *pt3_load_module( const unsigned char *d, size_t l )
{ return pt3_load( d, l ); }
static void pt3_free_module( void *m ) { pt3_free( m ); }
static void pt3_reset_module( void *m ) { pt3_reset( m ); }
static void pt3_frame_module( void *m, unsigned char *r, unsigned int *mask )
{ pt3_frame( m, r, mask ); }
static const char *pt3_title_module( const void *m )
{ return pt3_title( m ); }
static const char *pt3_author_module( const void *m )
{ return pt3_author( m ); }
/* The module is played from the start after it */
static long pt3_length_module( const void *m, long *loop )
{ return pt3_length( (pt3_module *)m, loop ); }

static int pt3_chips_module( const void *m ) { return pt3_chips( m ); }
static void pt3_frame_chips_module( void *m, unsigned char r[][ 14 ],
                                    unsigned int mask[] )
{ pt3_frame_ts( m, r, mask ); }

static const aymodule_format pt3_format = {
  "PT3", pt3_check, pt3_load_module, pt3_free_module, pt3_reset_module,
  pt3_frame_module, pt3_title_module, pt3_author_module, NULL,
  pt3_length_module, NULL, pt3_chips_module, pt3_frame_chips_module
};

/* Pro Tracker 2 */

static void *pt2_load_module( const unsigned char *d, size_t l )
{ return pt2_load( d, l ); }
static void pt2_free_module( void *m ) { pt2_free( m ); }
static void pt2_reset_module( void *m ) { pt2_reset( m ); }
static void pt2_frame_module( void *m, unsigned char *r, unsigned int *mask )
{ pt2_frame( m, r, mask ); }
static const char *pt2_title_module( const void *m )
{ return pt2_title( m ); }
static long pt2_length_module( const void *m, long *loop )
{ return pt2_length( (pt2_module *)m, loop ); }

static const aymodule_format pt2_format = {
  "PT2", pt2_check, pt2_load_module, pt2_free_module, pt2_reset_module,
  pt2_frame_module, pt2_title_module, NULL, NULL, pt2_length_module, NULL
};

/* Sound Tracker */

static void *stc_load_module( const unsigned char *d, size_t l )
{ return stc_load( d, l ); }
static void stc_free_module( void *m ) { stc_free( m ); }
static void stc_reset_module( void *m ) { stc_reset( m ); }
static void stc_frame_module( void *m, unsigned char *r, unsigned int *mask )
{ stc_frame( m, r, mask ); }
static const char *stc_title_module( const void *m )
{ return stc_title( m ); }
static long stc_length_module( const void *m, long *loop )
{ return stc_length( (stc_module *)m, loop ); }

static const aymodule_format stc_format = {
  "STC", stc_check, stc_load_module, stc_free_module, stc_reset_module,
  stc_frame_module, stc_title_module, NULL, NULL, stc_length_module, NULL
};

/* ASC Sound Master (version 0: .as0, version 1: .asc) */

static void *asc_load_module( const unsigned char *d, size_t l )
{ return asc_load( d, l ); }
static void *asc0_load_module( const unsigned char *d, size_t l )
{ return asc0_load( d, l ); }
static void asc_free_module( void *m ) { asc_free( m ); }
static void asc_reset_module( void *m ) { asc_reset( m ); }
static void asc_frame_module( void *m, unsigned char *r, unsigned int *mask )
{ asc_frame( m, r, mask ); }
static const char *asc_title_module( const void *m )
{ return asc_title( m ); }
static const char *asc_author_module( const void *m )
{ return asc_author( m ); }
static long asc_length_module( const void *m, long *loop )
{ return asc_length( (asc_module *)m, loop ); }

static const aymodule_format as0_format = {
  "AS0", asc0_check, asc0_load_module, asc_free_module, asc_reset_module,
  asc_frame_module, asc_title_module, asc_author_module, NULL,
  asc_length_module, NULL
};

static const aymodule_format asc_format = {
  "ASC", asc_check, asc_load_module, asc_free_module, asc_reset_module,
  asc_frame_module, asc_title_module, asc_author_module, NULL,
  asc_length_module, NULL
};

/* Sound Tracker Pro (compiled) */

static void *stp_load_module( const unsigned char *d, size_t l )
{ return stp_load( d, l ); }
static void stp_free_module( void *m ) { stp_free( m ); }
static void stp_reset_module( void *m ) { stp_reset( m ); }
static void stp_frame_module( void *m, unsigned char *r, unsigned int *mask )
{ stp_frame( m, r, mask ); }
static const char *stp_title_module( const void *m )
{ return stp_title( m ); }
static long stp_length_module( const void *m, long *loop )
{ return stp_length( (stp_module *)m, loop ); }

static const aymodule_format stp_format = {
  "STP", stp_check, stp_load_module, stp_free_module, stp_reset_module,
  stp_frame_module, stp_title_module, NULL, NULL, stp_length_module, NULL
};

/* Sound Tracker 1 (uncompiled) and Sound Tracker 3: the same player */

static void *st1_load_module( const unsigned char *d, size_t l )
{ return st1_load( d, l ); }

static const aymodule_format st1_format = {
  "ST1", st1_check, st1_load_module, stc_free_module, stc_reset_module,
  stc_frame_module, stc_title_module, NULL, NULL, stc_length_module, NULL
};

static void *st3_load_module( const unsigned char *d, size_t l )
{ return st3_load( d, l ); }

static const aymodule_format st3_format = {
  "ST3", st3_check, st3_load_module, stc_free_module, stc_reset_module,
  stc_frame_module, stc_title_module, NULL, NULL, stc_length_module, NULL
};

/* Pro Sound Maker (compiled) */

static void *psm_load_module( const unsigned char *d, size_t l )
{ return psm_load( d, l ); }
static void psm_free_module( void *m ) { psm_free( m ); }
static void psm_reset_module( void *m ) { psm_reset( m ); }
static void psm_frame_module( void *m, unsigned char *r, unsigned int *mask )
{ psm_frame( m, r, mask ); }
static const char *psm_title_module( const void *m )
{ return psm_title( m ); }
static long psm_length_module( const void *m, long *loop )
{ return psm_length( (psm_module *)m, loop ); }

static const aymodule_format psm_format = {
  "PSM", psm_check, psm_load_module, psm_free_module, psm_reset_module,
  psm_frame_module, psm_title_module, NULL, NULL, psm_length_module, NULL
};

/* Global Tracker */

static void *gtr_load_module( const unsigned char *d, size_t l )
{ return gtr_load( d, l ); }
static void gtr_free_module( void *m ) { gtr_free( m ); }
static void gtr_reset_module( void *m ) { gtr_reset( m ); }
static void gtr_frame_module( void *m, unsigned char *r, unsigned int *mask )
{ gtr_frame( m, r, mask ); }
static const char *gtr_title_module( const void *m )
{ return gtr_title( m ); }
static long gtr_length_module( const void *m, long *loop )
{ return gtr_length( (gtr_module *)m, loop ); }

static const aymodule_format gtr_format = {
  "GTR", gtr_check, gtr_load_module, gtr_free_module, gtr_reset_module,
  gtr_frame_module, gtr_title_module, NULL, NULL, gtr_length_module, NULL
};

/* Fast Tracker */

static void *ftc_load_module( const unsigned char *d, size_t l )
{ return ftc_load( d, l ); }
static void ftc_free_module( void *m ) { ftc_free( m ); }
static void ftc_reset_module( void *m ) { ftc_reset( m ); }
static void ftc_frame_module( void *m, unsigned char *r, unsigned int *mask )
{ ftc_frame( m, r, mask ); }
static const char *ftc_title_module( const void *m )
{ return ftc_title( m ); }
static long ftc_length_module( const void *m, long *loop )
{ return ftc_length( (ftc_module *)m, loop ); }

static const aymodule_format ftc_format = {
  "FTC", ftc_check, ftc_load_module, ftc_free_module, ftc_reset_module,
  ftc_frame_module, ftc_title_module, NULL, NULL, ftc_length_module, NULL
};

/* Pro Tracker 1 */

static void *pt1_load_module( const unsigned char *d, size_t l )
{ return pt1_load( d, l ); }
static void pt1_free_module( void *m ) { pt1_free( m ); }
static void pt1_reset_module( void *m ) { pt1_reset( m ); }
static void pt1_frame_module( void *m, unsigned char *r, unsigned int *mask )
{ pt1_frame( m, r, mask ); }
static const char *pt1_title_module( const void *m )
{ return pt1_title( m ); }
static long pt1_length_module( const void *m, long *loop )
{ return pt1_length( (pt1_module *)m, loop ); }

static const aymodule_format pt1_format = {
  "PT1", pt1_check, pt1_load_module, pt1_free_module, pt1_reset_module,
  pt1_frame_module, pt1_title_module, NULL, NULL, pt1_length_module, NULL
};

/* SQ-Tracker (compiled) */

static void *sqt_load_module( const unsigned char *d, size_t l )
{ return sqt_load( d, l ); }
static void sqt_free_module( void *m ) { sqt_free( m ); }
static void sqt_reset_module( void *m ) { sqt_reset( m ); }
static void sqt_frame_module( void *m, unsigned char *r, unsigned int *mask )
{ sqt_frame( m, r, mask ); }
static const char *sqt_title_module( const void *m )
{ return sqt_title( m ); }
static long sqt_length_module( const void *m, long *loop )
{ return sqt_length( (sqt_module *)m, loop ); }

static const aymodule_format sqt_format = {
  "SQT", sqt_check, sqt_load_module, sqt_free_module, sqt_reset_module,
  sqt_frame_module, sqt_title_module, NULL, NULL, sqt_length_module, NULL
};

/* Pro Sound Creator */

static void *psc_load_module( const unsigned char *d, size_t l )
{ return psc_load( d, l ); }
static void psc_free_module( void *m ) { psc_free( m ); }
static void psc_reset_module( void *m ) { psc_reset( m ); }
static void psc_frame_module( void *m, unsigned char *r, unsigned int *mask )
{ psc_frame( m, r, mask ); }
static const char *psc_title_module( const void *m )
{ return psc_title( m ); }
static const char *psc_author_module( const void *m )
{ return psc_author( m ); }
static long psc_length_module( const void *m, long *loop )
{ return psc_length( (psc_module *)m, loop ); }

static const aymodule_format psc_format = {
  "PSC", psc_check, psc_load_module, psc_free_module, psc_reset_module,
  psc_frame_module, psc_title_module, psc_author_module, NULL,
  psc_length_module, NULL
};

/* The formats in the order they are tried: those with a signature first,
   then those told by their structure only, in ZXTune's order */
/* TurboSound: the modules of the chips, each loaded as a format of its own.
   As in ZXTune, the length and the loop are those of the first module, and
   a text is shown once if the modules agree, else joined with "/" */

typedef struct turbosound {
  int chips;
  aymodule *chip[ AYMODULE_CHIPS ];
  char title[ 256 ], author[ 256 ], comment[ 256 ];
} turbosound;

/* A footer: for each module an ID ("PT3!": three 7 bit characters and
   '!') and its size (16 bits, little endian), then "02TS" or "03TS". The
   modules are before it, one after the other, in the order of the chips.
   The offset of the footer and the number of chips; 0 if none */
static int
ts_footer( const unsigned char *data, size_t length, size_t *footer )
{
  size_t at;

  /* Usually at the end; ZXTune also finds it before other data */
  for( at = length >= 4 ? length - 4 : 0; at >= 12; at-- ) {
    int chips, i, ok = 1;
    size_t start, total = 0;

    if( data[ at + 1 ] != '2' && data[ at + 1 ] != '3' ) continue;
    if( data[ at ] != '0' || data[ at + 2 ] != 'T' || data[ at + 3 ] != 'S' )
      continue;
    chips = data[ at + 1 ] - '0';
    if( at < (size_t)chips * 6 ) continue;
    start = at - chips * 6;
    for( i = 0; i < chips && ok; i++ ) {
      const unsigned char *entry = data + start + i * 6;
      if( entry[3] != '!' || ( entry[0] & 0x80 ) || ( entry[1] & 0x80 ) ||
          ( entry[2] & 0x80 ) ) ok = 0;
      total += entry[4] | entry[5] << 8;
    }
    if( !ok || total != start || total < 256 ||
        total > (size_t)32767 * chips ) continue;
    *footer = start;
    return chips;
  }
  return 0;
}

/* A Vortex Tracker text file with a second module: where it starts */
static size_t
ts_text_second( const unsigned char *data, size_t length )
{
  size_t i;

  if( length < 8 ) return 0;
  /* After a line break, not at the start */
  for( i = 9; i + 8 <= length; i++ )
    if( ( data[ i - 1 ] == '\n' || data[ i - 1 ] == '\r' ) &&
        !memcmp( data + i, "[Module]", 8 ) )
      return i;
  return 0;
}

static int
ts_check( const unsigned char *data, size_t length )
{
  size_t footer;

  if( ts_footer( data, length, &footer ) ) return 1;
  return pt3text_check( data, length ) && ts_text_second( data, length );
}

static void ts_free( void *m );

static void
merge_text( char *dest, size_t size,
            const char *( *get )( const aymodule *module ),
            const turbosound *ts )
{
  int i, j, same;

  dest[0] = '\0';
  for( i = 0; i < ts->chips; i++ ) {
    const char *text = get( ts->chip[i] );
    if( !text[0] ) continue;
    /* Once only: a text already there (also as part of a longer one) is
       left out, a longer one starting with what is there replaces it
       ("Author" and "Author / date": "Author / date") */
    for( same = 0, j = 0; j < i && !same; j++ )
      same = !strcmp( get( ts->chip[j] ), text );
    if( same || strstr( dest, text ) ) continue;
    if( dest[0] && !strncmp( text, dest, strlen( dest ) ) ) {
      snprintf( dest, size, "%s", text );
      continue;
    }
    if( dest[0] ) strncat( dest, " / ", size - strlen( dest ) - 1 );
    strncat( dest, text, size - strlen( dest ) - 1 );
  }
}

static void *
ts_load( const unsigned char *data, size_t length )
{
  turbosound *ts = calloc( 1, sizeof( *ts ) );
  size_t footer, offset = 0, second;
  int chips, i;

  if( !ts ) return NULL;

  if( ( chips = ts_footer( data, length, &footer ) ) ) {
    for( i = 0; i < chips; i++ ) {
      const unsigned char *entry = data + footer + i * 6;
      size_t size = entry[4] | entry[5] << 8;
      ts->chip[i] = load_single( data + offset, size );
      if( !ts->chip[i] ) break;
      ts->chips++;
      offset += size;
    }
  } else if( ( second = ts_text_second( data, length ) ) ) {
    chips = 2;
    ts->chip[0] = load_single( data, second );
    if( ts->chip[0] ) {
      ts->chips++;
      ts->chip[1] = load_single( data + second, length - second );
      if( ts->chip[1] ) ts->chips++;
    }
  }

  if( !chips || ts->chips != chips ) {
    ts_free( ts );
    return NULL;
  }

  merge_text( ts->title, sizeof( ts->title ), aymodule_title, ts );
  merge_text( ts->author, sizeof( ts->author ), aymodule_author, ts );
  merge_text( ts->comment, sizeof( ts->comment ), aymodule_comment, ts );
  return ts;
}

static void
ts_free( void *m )
{
  turbosound *ts = m;
  int i;

  if( !ts ) return;
  for( i = 0; i < AYMODULE_CHIPS; i++ ) aymodule_free( ts->chip[i] );
  free( ts );
}

static void
ts_reset( void *m )
{
  turbosound *ts = m;
  int i;

  for( i = 0; i < ts->chips; i++ ) aymodule_reset( ts->chip[i] );
}

static const char *ts_title( const void *m )
{ return ( (const turbosound *)m )->title; }
static const char *ts_author( const void *m )
{ return ( (const turbosound *)m )->author; }
static const char *ts_comment( const void *m )
{ return ( (const turbosound *)m )->comment; }
static const char *ts_stereo( const void *m )
{ return aymodule_stereo( ( (const turbosound *)m )->chip[0] ); }

static long
ts_length( const void *m, long *loop )
{
  return aymodule_exact_length( ( (const turbosound *)m )->chip[0], loop );
}

static const aymodule_format ts_format = {
  "TurboSound", ts_check, ts_load, ts_free, ts_reset, NULL,
  ts_title, ts_author, ts_comment, ts_length, ts_stereo
};

static const aymodule_format * const formats[] = {
  &ts_format,
  &psg_format,
  &ym_format,
  &pt3_format,
  &pt2_format,
  &stc_format,
  &st1_format,
  &st3_format,
  &as0_format,
  &asc_format,
  &stp_format,
  &psm_format,
  &gtr_format,
  &pt1_format,
  &sqt_format,
  &psc_format,
  &ftc_format,
  NULL
};

/* ------------------------------------------------------------------------
   The interface
   ------------------------------------------------------------------------ */

const aymodule_format *
aymodule_identify( const unsigned char *data, size_t length )
{
  int i;

  for( i = 0; formats[i]; i++ )
    if( formats[i]->check( data, length ) ) return formats[i];
  return NULL;
}

/* A module of one chip: the first format that loads it (a weak check may
   match more than one) */
static aymodule *
load_single( const unsigned char *data, size_t length )
{
  aymodule *m;
  int i;

  for( i = 1; formats[i]; i++ ) {
    void *loaded;

    if( !formats[i]->check( data, length ) ) continue;
    loaded = formats[i]->load( data, length );
    if( !loaded ) continue;
    m = malloc( sizeof( *m ) );
    if( !m ) {
      formats[i]->free( loaded );
      return NULL;
    }
    m->format = formats[i];
    m->data = loaded;
    return m;
  }
  return NULL;
}

aymodule *
aymodule_load( const unsigned char *data, size_t length )
{
  void *loaded;
  aymodule *m;

  /* TurboSound first: its first module alone would match a format too */
  if( ts_check( data, length ) && ( loaded = ts_load( data, length ) ) ) {
    m = malloc( sizeof( *m ) );
    if( !m ) {
      ts_free( loaded );
      return NULL;
    }
    m->format = &ts_format;
    m->data = loaded;
    return m;
  }

  return load_single( data, length );
}

void
aymodule_free( aymodule *module )
{
  if( !module ) return;
  module->format->free( module->data );
  free( module );
}

const aymodule_format *
aymodule_format_of( const aymodule *module )
{
  return module->format;
}

void
aymodule_reset( aymodule *module )
{
  module->format->reset( module->data );
}

int
aymodule_chips( const aymodule *module )
{
  int chips;

  if( module->format == &ts_format ) {
    /* A module of the pair may play two chips by itself; at most three
       chips in all */
    const turbosound *ts = module->data;
    int i;
    for( chips = 0, i = 0; i < ts->chips; i++ )
      chips += aymodule_chips( ts->chip[i] );
    return chips > AYMODULE_CHIPS ? AYMODULE_CHIPS : chips;
  }
  return module->format->chips ? module->format->chips( module->data ) : 1;
}

void
aymodule_frame( aymodule *module,
                unsigned char registers[][ AYMODULE_REGISTERS ],
                unsigned int mask[] )
{
  int i;

  if( module->format == &ts_format ) {
    turbosound *ts = module->data;
    unsigned char spare[ AYMODULE_CHIPS ][ AYMODULE_REGISTERS ];
    unsigned int spare_mask[ AYMODULE_CHIPS ];
    int chip = 0;
    for( i = 0; i < ts->chips; i++ ) {
      int n = aymodule_chips( ts->chip[i] );
      if( chip + n <= AYMODULE_CHIPS ) {
        aymodule_frame( ts->chip[i], registers + chip, mask + chip );
      } else {
        /* No room for its chips: it goes on unheard */
        aymodule_frame( ts->chip[i], spare, spare_mask );
      }
      chip += n;
    }
    return;
  }
  if( module->format->frame_chips && module->format->chips &&
      module->format->chips( module->data ) > 1 ) {
    module->format->frame_chips( module->data, registers, mask );
    return;
  }
  module->format->frame( module->data, registers[0], &mask[0] );
}

static const char *
text( const char *( *get )( const void *module ), const aymodule *module )
{
  const char *value = get ? get( module->data ) : NULL;
  return value ? value : "";
}

const char *
aymodule_title( const aymodule *module )
{
  return text( module->format->title, module );
}

const char *
aymodule_author( const aymodule *module )
{
  return text( module->format->author, module );
}

const char *
aymodule_comment( const aymodule *module )
{
  return text( module->format->comment, module );
}

const char *
aymodule_stereo( const aymodule *module )
{
  return text( module->format->stereo, module );
}

long
aymodule_exact_length( const aymodule *module, long *loop )
{
  long frames;

  *loop = -1;
  if( !module->format->exact_length ) return 0;
  frames = module->format->exact_length( module->data, loop );
  if( frames <= 0 ) {
    *loop = -1;
    return 0;
  }
  return frames;
}

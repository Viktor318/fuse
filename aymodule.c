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

#include <stdlib.h>

#include "ascplayer.h"
#include "aymodule.h"
#include "pscplayer.h"
#include "psmplayer.h"
#include "pt1player.h"
#include "pt2player.h"
#include "psgplayer.h"
#include "pt3player.h"
#include "sqtplayer.h"
#include "stcplayer.h"
#include "stpplayer.h"
#include "ymplayer.h"

struct aymodule {
  const aymodule_format *format;
  void *data;
};

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

static const aymodule_format pt3_format = {
  "PT3", pt3_check, pt3_load_module, pt3_free_module, pt3_reset_module,
  pt3_frame_module, pt3_title_module, pt3_author_module, NULL,
  pt3_length_module, NULL
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
static const aymodule_format * const formats[] = {
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
  &pt1_format,
  &sqt_format,
  &psc_format,
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

aymodule *
aymodule_load( const unsigned char *data, size_t length )
{
  aymodule *m;
  int i;

  /* The first format that loads it: a weak check may match more than one */
  for( i = 0; formats[i]; i++ ) {
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

void
aymodule_frame( aymodule *module,
                unsigned char registers[ AYMODULE_REGISTERS ],
                unsigned int *mask )
{
  module->format->frame( module->data, registers, mask );
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

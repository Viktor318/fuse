/* aymeasure.c: Measuring the length of an AY song in the background
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

#include "config.h"

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#include "aymeasure.h"
#include "aysource.h"

#define MAX_FRAMES ( 60L * 60 * 50 )	/* an hour of music */
#define MIN_PERIOD 250			/* 5 seconds */
#define CONFIRM 3000			/* a minute */
#define CHECK_EVERY 250

/* Digital music (samples played by writing the volumes many times a frame,
   with tone and noise off) never repeats exactly: the samples fall on the
   frames a little differently each time. It is told by its volume writes
   in any DIGITAL_SEEN frames (it may start after some ordinary music),
   and measured by how it sounds instead: the mean
   level of each channel in each frame, compared in blocks of BLOCK frames
   first, then frame by frame */
#define DIGITAL_WRITES 16		/* volume writes in a frame, on average */
#define DIGITAL_SEEN 100		/* 2 seconds, to tell digital music */
#define DIGITAL_MAX_FRAMES ( 20L * 60 * 50 )	/* 20 minutes */
#define DIGITAL_CHECK_EVERY 1000	/* 20 seconds */
#define BLOCK 5				/* a tenth of a second */
#define WINDOW 3000			/* compared: a minute, as CONFIRM */
#define LEVEL_SCALE 16
/* Mean difference of the levels (in thousandths of a level, 0-15) that
   still counts as the same music: music differs some 0.4 to 1.2 levels
   from other parts of itself, and some 0.04 where it repeats. The blocks,
   averaged, differ less: they only choose the periods to check */
#define SAME 100
#define BLOCK_SAME 300
#define MAX_WRITES 8192

/* Has the music started again? The smallest period P for which the last
   max(P, a minute) frames are the same as P frames before; then back to the
   first frame S of the repetition. The song lasts until S + P and goes on
   from S; if what repeats is silence, the music ended at S (*loop = -1).
   -1: not yet */
static long
find( const unsigned int *hash, const unsigned char *silent, long n,
      long *loop )
{
  long period, need, k, start, i;
  int all_silent;

  for( period = MIN_PERIOD; ; period++ ) {
    need = period > CONFIRM ? period : CONFIRM;
    if( period + need > n ) return -1;

    for( k = 0; k < need; k++ )
      if( hash[ n - 1 - k ] != hash[ n - 1 - k - period ] ) break;
    if( k < need ) continue;

    start = n - need - period;
    while( start > 0 && hash[ start - 1 ] == hash[ start - 1 + period ] )
      start--;

    /* What repeats now is silence: the music ended where the silence
       began (the repetition may have started a little before it, with the
       last writes leaving the registers as they stay; with digital music
       even well before it, as its registers look the same at the end of
       each frame) */
    all_silent = 1;
    for( i = n - need; i < n; i++ )
      if( !silent[i] ) { all_silent = 0; break; }

    if( all_silent ) {
      long end = n;
      while( end > 0 && silent[ end - 1 ] ) end--;
      *loop = -1;
      return end;
    }
    *loop = start;
    return start + period;
  }
}

/* Mean difference of the levels of frames a..a+count and b..b+count, in
   thousandths of a level */
static long
difference( const unsigned char *level, long a, long b, long count )
{
  long i, sum = 0;
  const unsigned char *p = level + a * 3, *q = level + b * 3;

  for( i = 0; i < count * 3; i++ ) sum += abs( (int)p[i] - (int)q[i] );
  return (long)( (double)sum * 1000 / ( count * 3 * LEVEL_SCALE ) );
}

/* As find(), for digital music: the smallest period P (at least
   MIN_PERIOD) with which the last max(P, WINDOW) frames sound the same as P
   frames before, then back to the first frame S that does. The song lasts
   until S + P and goes on from S; if what sounds the same is silence, the
   music ended where it began (*loop = -1). -1: not yet */
static long
find_digital( const unsigned char *level, const unsigned char *block,
              const unsigned char *silent, long n, long *loop )
{
  long blocks = n / BLOCK, period, need, start, p, i;

  for( period = MIN_PERIOD / BLOCK; ; period++ ) {
    long found = -1, found_difference = SAME + 1;

    need = period > WINDOW / BLOCK ? period : WINDOW / BLOCK;
    if( period + need > blocks ) return -1;
    if( difference( block, blocks - need, blocks - need - period, need ) >
        BLOCK_SAME )
      continue;

    /* A period to check: frame by frame, around the block */
    for( p = period * BLOCK - BLOCK; p <= period * BLOCK + BLOCK; p++ ) {
      long d, frames_need = p > WINDOW ? p : WINDOW;
      if( p < MIN_PERIOD || p + frames_need > n ) continue;
      d = difference( level, n - frames_need, n - frames_need - p,
                      frames_need );
      if( d < found_difference ) { found_difference = d; found = p; }
    }
    if( found < 0 ) continue;

    period = found;
    need = period > WINDOW ? period : WINDOW;
    break;
  }

  /* Silence sounds the same as silence: the music ended where it began */
  for( i = n - need; i < n && silent[i]; i++ )
    ;
  if( i == n ) {
    while( n > 0 && silent[ n - 1 ] ) n--;
    *loop = -1;
    return n;
  }

  /* Back to where the repetition starts, a second at a time */
  start = n - need - period;
  while( start >= 50 &&
         difference( level, start - 50, start - 50 + period, 50 ) <= SAME )
    start -= 50;
  while( start > 0 &&
         difference( level, start - 1, start - 1 + period, 1 ) <= SAME )
    start--;

  *loop = start;
  return start + period;
}

long
aymeasure_song( const unsigned char *data, size_t length, int song,
                long *loop, volatile int *cancel )
{
  aysource *source = aysource_new( data, length, song );
  unsigned int *hash = malloc( MAX_FRAMES * sizeof( *hash ) );
  unsigned char *silent = malloc( MAX_FRAMES );
  /* Digital music: the mean level of each channel in each frame and in
     each block of frames */
  unsigned char *level = malloc( DIGITAL_MAX_FRAMES * 3 );
  unsigned char *block = malloc( DIGITAL_MAX_FRAMES / BLOCK * 3 + 3 );
  aysource_write *writes = malloc( MAX_WRITES * sizeof( *writes ) );
  long frames = 0, found = 0, volume_writes = 0;
  /* The volume writes of the last DIGITAL_SEEN frames */
  long recent[ DIGITAL_SEEN ];
  long block_sum[3] = { 0, 0, 0 };
  int i, digital = 0;

  *loop = 0;
  memset( recent, 0, sizeof( recent ) );
  if( !source || !hash || !silent || !level || !block || !writes ) {
    aysource_free( source );
    free( hash );
    free( silent );
    free( level );
    free( block );
    free( writes );
    return 0;
  }

  while( frames < ( digital ? DIGITAL_MAX_FRAMES : MAX_FRAMES ) ) {
    const unsigned char *r;
    unsigned int h = 2166136261U;
    long sum[3] = { 0, 0, 0 }, count[3] = { 0, 0, 0 };
    int written, channel;

    if( cancel && *cancel ) break;

    written = aysource_frame( source, writes, MAX_WRITES );
    r = aysource_registers( source );
    for( i = 0; i < 14; i++ ) {
      h ^= r[i];
      h *= 16777619U;
    }
    hash[ frames ] = h;

    /* The levels the volume writes set (an envelope counts as full) */
    for( i = 0; i < written; i++ ) {
      int reg = writes[i].reg;
      if( reg >= 8 && reg <= 10 ) {
        int value = writes[i].value & 0x1f;
        sum[ reg - 8 ] += value & 16 ? 15 : value;
        count[ reg - 8 ]++;
      }
    }
    /* Silent: no channel heard. A channel is heard with a volume and tone
       or noise on, or, with both off, if its volume changes (digital
       music); a volume that stays without them is no sound */
    silent[ frames ] = 1;
    for( channel = 0; channel < 3; channel++ ) {
      int tone_and_noise_off = ( ( r[7] >> channel ) & 9 ) == 9;
      if( ( r[ 8 + channel ] & 0x1f ) &&
          ( !tone_and_noise_off || count[ channel ] ) )
        silent[ frames ] = 0;
    }

    if( frames < DIGITAL_MAX_FRAMES ) {
      for( channel = 0; channel < 3; channel++ ) {
        int value = r[ 8 + channel ] & 0x1f;
        int mean = count[ channel ] ?
                   (int)( sum[ channel ] * LEVEL_SCALE / count[ channel ] ) :
                   ( value & 16 ? 15 : value ) * LEVEL_SCALE;
        level[ frames * 3 + channel ] = mean;
        block_sum[ channel ] += mean;
      }
      if( ( frames + 1 ) % BLOCK == 0 ) {
        for( channel = 0; channel < 3; channel++ ) {
          block[ frames / BLOCK * 3 + channel ] = block_sum[ channel ] / BLOCK;
          block_sum[ channel ] = 0;
        }
      }
    }
    volume_writes -= recent[ frames % DIGITAL_SEEN ];
    recent[ frames % DIGITAL_SEEN ] = count[0] + count[1] + count[2];
    volume_writes += recent[ frames % DIGITAL_SEEN ];
    frames++;

    /* The levels are kept from the start, so digital music found later
       is measured from the start too */
    if( !digital && frames <= DIGITAL_MAX_FRAMES &&
        volume_writes >= DIGITAL_WRITES * DIGITAL_SEEN )
      digital = 1;

    if( frames % CHECK_EVERY == 0 ) {
      long end = find( hash, silent, frames, loop );
      if( end >= 0 ) {
        /* Nothing but silence: no length */
        found = end < 50 ? 0 : end;
        if( !found ) *loop = 0;
        break;
      }
    }

    if( digital && frames % DIGITAL_CHECK_EVERY == 0 ) {
      long end = find_digital( level, block, silent, frames, loop );
      if( end >= 0 ) {
        found = end < 50 ? 0 : end;
        if( !found ) *loop = 0;
        break;
      }
    }
  }

  aysource_free( source );
  free( hash );
  free( silent );
  free( level );
  free( block );
  free( writes );
  return found;
}

#ifdef HAVE_PTHREAD

static pthread_t thread;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int thread_started = 0;
static volatile int cancel = 0, busy = 0;

static unsigned char *job_data = NULL;
static size_t job_length;
static int job_song;
static long job_id;

static int done = 0;
static long done_id, done_frames, done_loop;

static void *
run( void *unused )
{
  long loop, frames;

  (void)unused;
  frames = aymeasure_song( job_data, job_length, job_song, &loop, &cancel );

  pthread_mutex_lock( &lock );
  if( !cancel ) {
    done = 1;
    done_id = job_id;
    done_frames = frames;
    done_loop = loop;
  }
  busy = 0;
  pthread_mutex_unlock( &lock );
  return NULL;
}

static void
join( void )
{
  if( !thread_started ) return;
  pthread_join( thread, NULL );
  thread_started = 0;
  free( job_data );
  job_data = NULL;
}

void
aymeasure_stop( void )
{
  if( !thread_started ) return;
  cancel = 1;
  join();
  busy = 0;
}

void
aymeasure_start( const unsigned char *data, size_t length, int song,
                 long id )
{
  aymeasure_stop();

  job_data = malloc( length ? length : 1 );
  if( !job_data ) return;
  memcpy( job_data, data, length );
  job_length = length;
  job_song = song;
  job_id = id;
  cancel = 0;
  busy = 1;
  pthread_mutex_lock( &lock );
  done = 0;
  pthread_mutex_unlock( &lock );

  if( pthread_create( &thread, NULL, run, NULL ) ) {
    busy = 0;
    free( job_data );
    job_data = NULL;
    return;
  }
  thread_started = 1;
}

int
aymeasure_result( long id, long *frames, long *loop )
{
  int found = 0;

  pthread_mutex_lock( &lock );
  if( done && done_id == id ) {
    *frames = done_frames;
    *loop = done_loop;
    done = 0;
    found = 1;
  }
  pthread_mutex_unlock( &lock );

  /* Over: the thread can go */
  if( found && !busy ) join();
  return found;
}

int
aymeasure_running( void )
{
  return busy;
}

#else			/* #ifdef HAVE_PTHREAD */

/* Without threads: measured at once (it takes less than a second) */
static int done = 0;
static long done_id, done_frames, done_loop;

void
aymeasure_start( const unsigned char *data, size_t length, int song,
                 long id )
{
  done_frames = aymeasure_song( data, length, song, &done_loop, NULL );
  done_id = id;
  done = 1;
}

void aymeasure_stop( void ) { done = 0; }
int aymeasure_running( void ) { return 0; }

int
aymeasure_result( long id, long *frames, long *loop )
{
  if( !done || done_id != id ) return 0;
  *frames = done_frames;
  *loop = done_loop;
  done = 0;
  return 1;
}

#endif			/* #ifdef HAVE_PTHREAD */

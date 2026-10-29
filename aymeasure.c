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

    all_silent = 1;
    for( i = start; i < start + period; i++ )
      if( !silent[i] ) { all_silent = 0; break; }

    if( all_silent ) {
      *loop = -1;
      return start;
    }
    *loop = start;
    return start + period;
  }
}

long
aymeasure_song( const unsigned char *data, size_t length, int song,
                long *loop, volatile int *cancel )
{
  aysource *source = aysource_new( data, length, song );
  unsigned int *hash = malloc( MAX_FRAMES * sizeof( *hash ) );
  unsigned char *silent = malloc( MAX_FRAMES );
  long frames = 0, found = 0;
  int i;

  *loop = 0;
  if( !source || !hash || !silent ) {
    aysource_free( source );
    free( hash );
    free( silent );
    return 0;
  }

  while( frames < MAX_FRAMES ) {
    const unsigned char *r;
    unsigned int h = 2166136261U;

    if( cancel && *cancel ) break;

    aysource_frame( source, NULL, 0 );
    r = aysource_registers( source );
    for( i = 0; i < 14; i++ ) {
      h ^= r[i];
      h *= 16777619U;
    }
    hash[ frames ] = h;
    silent[ frames ] = !( ( r[8] | r[9] | r[10] ) & 0x1f );
    frames++;

    if( frames % CHECK_EVERY == 0 ) {
      long end = find( hash, silent, frames, loop );
      if( end >= 0 ) {
        /* Nothing but silence: no length */
        found = end < 50 ? 0 : end;
        if( !found ) *loop = 0;
        break;
      }
    }
  }

  aysource_free( source );
  free( hash );
  free( silent );
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

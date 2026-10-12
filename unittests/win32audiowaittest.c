/* win32audiowaittest.c: native progress, stop and joined-lifetime checks
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/
#include "config.h"
#include <windows.h>
#include <stdio.h>
#include "sound/audio_progress.h"

#define CHECK( condition ) do { if( !( condition ) ) { \
  fprintf( stderr, "line %d: %s\n", __LINE__, #condition ); \
  ExitProcess( 1 ); } } while( 0 )

struct lifetime {
  struct audio_progress progress;
  HANDLE stop, request, ready;
  _Atomic int terminal;
};

static DWORD WINAPI
consumer( void *opaque )
{
  struct lifetime *life = opaque;
  HANDLE events[2] = { life->stop, life->request };
  DWORD status;
  CHECK( SetEvent( life->ready ) );
  for( ;; ) {
    status = WaitForMultipleObjects( 2, events, FALSE, 2000 );
    if( status == WAIT_OBJECT_0 ) break;
    CHECK( status == WAIT_OBJECT_0 + 1 );
    audio_progress_publish( &life->progress );
  }
  atomic_store( &life->terminal, 1 );
  audio_progress_publish( &life->progress );
  return 0;
}

int
main( void )
{
  int iteration;
  CHECK( !compat_wait_uses_address ); /* Must exercise Windows semaphore path. */
  for( iteration = 0; iteration < 32; iteration++ ) {
    struct lifetime life;
    HANDLE thread;
    uint32_t generation;
    audio_progress_deadline end;
    int result;
    CHECK( !audio_progress_init( &life.progress ) );
    atomic_init( &life.terminal, 0 );
    life.stop = CreateEvent( NULL, TRUE, FALSE, NULL );
    life.request = CreateEvent( NULL, FALSE, FALSE, NULL );
    life.ready = CreateEvent( NULL, FALSE, FALSE, NULL );
    CHECK( life.stop && life.request && life.ready );
    thread = CreateThread( NULL, 0, consumer, &life, 0, NULL );
    CHECK( thread );
    CHECK( WaitForSingleObject( life.ready, 2000 ) == WAIT_OBJECT_0 );

    /* Progress before park survives even with no retained token. */
    generation = audio_progress_snapshot( &life.progress );
    audio_progress_publish( &life.progress );
    CHECK( !audio_progress_wait( &life.progress, generation,
                                compat_wait_until( 1000 ) ) );

    /* Real native worker progress, including the race with producer arming. */
    generation = audio_progress_snapshot( &life.progress );
    CHECK( SetEvent( life.request ) );
    end = compat_wait_until( 2000 );
    while( audio_progress_snapshot( &life.progress ) == generation ) {
      result = audio_progress_wait( &life.progress, generation, end );
      CHECK( result == 0 );
    }

    /* Stale/coalesced tokens are advisory, never new progress. Reuse one
       absolute deadline; insufficient wakes must not renew the budget. */
    generation = audio_progress_snapshot( &life.progress );
    CHECK( !compat_wait_wake( life.progress.wait, &life.progress.generation ) );
    CHECK( !compat_wait_wake( life.progress.wait, &life.progress.generation ) );
    end = compat_wait_until( 20 );
    CHECK( audio_progress_wait( &life.progress, generation, end ) == ETIMEDOUT );
    CHECK( audio_progress_wait( &life.progress, generation, end ) == ETIMEDOUT );
    CHECK( audio_progress_snapshot( &life.progress ) == generation );

    /* Cancellation is sticky, not permission to publish. Check it after each
       bounded wait, then join before closing/resetting any shared storage. */
    CHECK( SetEvent( life.stop ) );
    end = compat_wait_until( 2000 );
    while( !atomic_load( &life.terminal ) ) {
      result = audio_progress_wait( &life.progress, generation, end );
      CHECK( result == 0 );
      generation = audio_progress_snapshot( &life.progress );
    }
    CHECK( WaitForSingleObject( thread, 2000 ) == WAIT_OBJECT_0 );
    CHECK( !atomic_load( &life.progress.error ) );
    CHECK( CloseHandle( thread ) );
    CHECK( CloseHandle( life.ready ) );
    CHECK( CloseHandle( life.request ) );
    CHECK( CloseHandle( life.stop ) );
    audio_progress_close( &life.progress );
  }
  puts( "Win32 audio wait: 32 joined lifetimes passed" );
  return 0;
}

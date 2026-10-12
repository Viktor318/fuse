/* wasapithreadtest.c: real threaded SPSC packet chronology, no endpoint
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define YIELD() SwitchToThread()
#else
#include <pthread.h>
#include <sched.h>
#define YIELD() sched_yield()
#endif
#include "sound/wasapi_pcm.h"

#define CHECK( x ) do { if( !( x ) ) { \
  fprintf( stderr, "%d: %s\n", __LINE__, #x ); exit( 1 ); } } while( 0 )
#define TOTAL 65536
struct fixture {
  struct wasapi_service service;
  sfifo_t fifo;
  int16_t *input;
  int16_t scratch[32];
  unsigned char packet[64];
  unsigned int offset, active, begins, finishes;
};
static int32_t
padding( void *opaque, unsigned int *frames )
{ (void)opaque; *frames = 0; return 0; }
static int32_t
acquire( void *opaque, unsigned int frames, unsigned char **data )
{
  CHECK( frames == 16 );
  *data = ((struct fixture *)opaque)->packet;
  return 0;
}
static int32_t
release( void *opaque, unsigned int frames, int silent )
{
  struct fixture *f = opaque;
  unsigned int channels = f->service.channels;
  CHECK( frames == 16 && !silent && f->active );
  /* Independently decode little-endian S16 output, not another converter. */
  for( unsigned int i = 0; i < frames * channels; i++ ) {
    int value = f->packet[i * 2] | (unsigned int)f->packet[i * 2 + 1] << 8;
    if( value >= 32768 ) value -= 65536;
    CHECK( value == f->input[(size_t)f->offset * channels + i] );
  }
  f->offset += frames;
  return 0;
}
static void
begin( void *opaque )
{
  struct fixture *f = opaque;
  CHECK( !f->active ); f->active = 1; f->begins++;
}
static void
finish( void *opaque, unsigned int requested, unsigned int delivered,
        int invalid, int running )
{
  struct fixture *f = opaque;
  CHECK( requested == 16 && delivered == 16 && !invalid && running && f->active );
  f->active = 0; f->finishes++;
}
#ifdef _WIN32
static DWORD WINAPI
#else
static void *
#endif
consumer( void *opaque )
{
  struct fixture *f = opaque;
  const struct wasapi_packet_ops ops = { padding, acquire, release };
  while( f->offset < TOTAL ) {
    if( pcm_fifo_consumer_used( &f->fifo, f->service.channels * 2 ) < 16 ) {
      YIELD(); continue;
    }
    CHECK( !wasapi_service_packet( &f->service, &ops, f, 1 ) );
  }
  return 0;
}

int
main( void )
{
  for( unsigned int channels = 1; channels <= 2; channels++ )
    for( unsigned int partition = 1; partition <= 3; partition++ ) {
      struct fixture f = { 0 };
      unsigned int offset = 0;
#ifdef _WIN32
      HANDLE thread;
#else
      pthread_t thread;
#endif
      CHECK( !sfifo_init( &f.fifo, 1025 ) );
      f.input = malloc( TOTAL * channels * sizeof( *f.input ) );
      CHECK( f.input );
      for( unsigned int i = 0; i < TOTAL * channels; i++ ) {
        int value = ( i * 131u + 911u ) % 65536;
        f.input[i] = value >= 32768 ? value - 65536 : value;
      }
      f.service = (struct wasapi_service){ .fifo = &f.fifo,
        .format = { 48000, channels, 16, 16, channels * 2, 0, WASAPI_INTEGER },
        .channels = channels, .capacity = 16, .scratch = f.scratch,
        .observer = &f, .begin = begin, .finish = finish };
#ifdef _WIN32
      thread = CreateThread( NULL, 0, consumer, &f, 0, NULL ); CHECK( thread );
#else
      CHECK( !pthread_create( &thread, NULL, consumer, &f ) );
#endif
      while( offset < TOTAL ) {
        unsigned int frames = partition == 1 ? 1 : partition == 2 ? 97 :
                              ( offset * 17 + 23 ) % 97 + 1;
        if( frames > TOTAL - offset ) frames = TOTAL - offset;
        while( !pcm_fifo_producer_can_write( &f.fifo, channels * 2, frames ) ) YIELD();
        CHECK( pcm_fifo_write( &f.fifo, channels * 2,
                     f.input + (size_t)offset * channels, frames ) == (int)frames );
        offset += frames;
      }
#ifdef _WIN32
      CHECK( WaitForSingleObject( thread, 10000 ) == WAIT_OBJECT_0 );
      CHECK( CloseHandle( thread ) );
#else
      CHECK( !pthread_join( thread, NULL ) );
#endif
      CHECK( f.offset == TOTAL && f.service.submitted == TOTAL &&
             !f.service.silence && !f.service.uncertain && !f.active &&
             f.begins == f.finishes && f.finishes == TOTAL / 16 );
      sfifo_close( &f.fifo );
      free( f.input );
    }
  puts( "Threaded WASAPI packet chronology: mono/stereo, three partitions passed" );
  return 0;
}

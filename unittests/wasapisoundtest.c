/* wasapisoundtest.c: actual producer/backend with injected native transport
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
#include "machine.h"
#include "settings.h"
#include "ui/ui.h"
#include "sound/audio_progress.h"
#include "sound/wasapi_native.h"

#define CHECK( x ) do { if( !( x ) ) { \
  fprintf( stderr, "%d: %s\n", __LINE__, #x ); exit( 1 ); } } while( 0 )

struct wasapi_native {
  struct wasapi_service *service;
  unsigned int prefill, padding;
  int started;
  int32_t error;
  void (*notify)( void * );
  void *observer;
  unsigned char packet[4096];
};
static struct wasapi_native mock;
static int live, closes, waits, cancel_wait, join_failure, block_start, context = 1;
static int32_t acquisition_error, release_error, notification_error;
static unsigned int requested_rate = 48000, speed_percent = 100;

static int32_t
padding( void *opaque, unsigned int *frames )
{ *frames = ((struct wasapi_native *)opaque)->padding; return 0; }
static int32_t
acquire( void *opaque, unsigned int frames, unsigned char **data )
{
  CHECK( frames <= 480 );
  *data = ((struct wasapi_native *)opaque)->packet;
  return acquisition_error;
}
static int32_t
release( void *opaque, unsigned int frames, int silent )
{
  struct wasapi_native *n = opaque;
  (void)silent;
  if( release_error ) return release_error;
  n->padding += frames;
  return 0;
}
static void
pump( unsigned int consumed )
{
  const struct wasapi_packet_ops ops = { padding, acquire, release };
  CHECK( live && mock.service && consumed <= mock.padding );
  mock.padding -= consumed;
  mock.error = wasapi_service_packet( mock.service, &ops, &mock, mock.started );
  if( mock.error && mock.notify ) mock.notify( mock.observer );
}

int32_t
wasapi_native_open( struct wasapi_native **n, struct wasapi_geometry *g,
                    const char **operation )
{
  CHECK( !live );
  if( operation ) *operation = NULL;
  memset( &mock, 0, sizeof( mock ) );
  memset( g, 0, sizeof( *g ) );
  g->format = (struct wasapi_format){ requested_rate, 2, 32, 32, 8, 3, WASAPI_FLOAT };
  g->capacity = 480; g->period = 240; g->client3 = 1;
  *n = &mock; live = 1;
  return 0;
}
int32_t
wasapi_native_attach( struct wasapi_native *n, struct wasapi_service *s,
                      unsigned int prefill )
{
  CHECK( live && n == &mock && !n->service );
  CHECK( pcm_fifo_consumer_used( s->fifo, s->channels * 2 ) == 0 );
  n->service = s; n->prefill = prefill;
  return 0;
}
void
wasapi_native_set_notify( struct wasapi_native *n, void (*notify)( void * ), void *p )
{ n->notify = notify; n->observer = p; }
int32_t
wasapi_native_data( struct wasapi_native *n )
{
  CHECK( live && n == &mock );
  if( notification_error ) return notification_error;
  if( block_start ) return 0;
  if( !n->started && (unsigned int)pcm_fifo_consumer_used( n->service->fifo,
                    n->service->channels * 2 ) >= n->prefill ) {
    pump( 0 );
    if( !n->error ) { n->started = 1; n->notify( n->observer ); }
  }
  return 0;
}
int wasapi_native_started( struct wasapi_native *n ) { return n->started; }
int32_t wasapi_native_error( struct wasapi_native *n ) { return n->error; }
const char *wasapi_native_operation( struct wasapi_native *n )
{ (void)n; return "injected packet fault"; }
int32_t
wasapi_native_close( struct wasapi_native *n, int32_t *error )
{
  CHECK( live && n == &mock );
  if( join_failure ) return WASAPI_PACKET_FAILURE;
  if( n->service ) CHECK( n->service->fifo->buffer && n->service->scratch );
  if( error ) *error = n->error;
  live = 0; closes++;
  return 0;
}
static int
mock_wait( struct audio_progress *p, uint32_t expected, audio_progress_deadline end )
{
  (void)p; (void)expected; (void)end;
  waits++;
  if( cancel_wait ) { mock.error = WASAPI_PACKET_FAILURE; mock.notify( mock.observer ); }
  else pump( 240 );
  return 0;
}
#define audio_progress_wait mock_wait
#include "sound/wasapisound.c"
#undef audio_progress_wait

static fuse_machine_info test_machine;
fuse_machine_info *machine_current = &test_machine;
settings_info settings_current;
libspectrum_dword sound_get_effective_processor_speed( void )
{ return 3500000 * speed_percent / 100; }
int sound_normal_producer_context( void ) { return context; }
int ui_error( ui_error_level level, const char *message, ... )
{ (void)level; (void)message; return 0; }
int ui_error_console( ui_error_level level, const char *message, ... )
{ (void)level; (void)message; return 0; }

static void
start( int stereo )
{
  int rate = 44100, original = stereo;
  settings_current.sound = 1;
  CHECK( !sound_lowlevel_init( NULL, &rate, &stereo ) );
  CHECK( rate == (int)requested_rate && stereo == original );
  CHECK( !mock.started && mock.service && mock.prefill <= pacing.controller.capacity );
}
static void
publish( unsigned int frames )
{
  int16_t data[8192];
  CHECK( frames * service.channels <= 8192 );
  for( unsigned int i = 0; i < frames * service.channels; i++ ) data[i] = i - 100;
  CHECK( !sound_lowlevel_reserve( frames ) );
  sound_lowlevel_frame( data, frames * service.channels );
}

int
main( void )
{
  test_machine.timings.tstates_per_frame = 70000;
  start( SOUND_STEREO_AY_ABC );
  CHECK( !sound_lowlevel_reserve( 0 ) );
  CHECK( sound_lowlevel_reserve( pacing.controller.capacity + 1 ) == -EINVAL );
  publish( 960 ); CHECK( !mock.started );
  publish( 960 ); CHECK( mock.started && service.consumed == 480 && service.silence == 0 );
  CHECK( !pacing.normal && !atomic_load( &pacing.progress ) );
  for( int i = 0; i < 20000; i++ ) {
    publish( 240 );
    CHECK( native && !atomic_load( &pacing.invalid ) );
  }
  CHECK( pacing.normal && pacing.controller.target < pacing.controller.capacity );
  CHECK( pacing.controller.state != ADAPTIVE_STARTUP );
  uint64_t floor = pacing.controller.floor;
  /* Native empty is negative even with healthy FIFO. No imported magnitude. */
  pump( 480 );
  CHECK( atomic_load( &native_negative ) && service.native_empty == 1 );
  publish( 240 );
  CHECK( pacing.controller.target == pacing.controller.capacity );
  CHECK( pacing.controller.floor == floor );
  context = 0; publish( 960 ); context = 1; /* Frame-only exceptional batch. */
  CHECK( service.silence == 0 );
  sound_lowlevel_end(); CHECK( !live && !sound_fifo.buffer && !service.scratch );

  /* Device failure must retire even when the producer already has room. */
  start( 0 );
  mock.error = WASAPI_PACKET_FAILURE;
  CHECK( !sound_lowlevel_reserve( 1 ) && !native && !settings_current.sound );

  /* And must cancel a producer parked behind a full FIFO. */
  start( 0 ); publish( 960 ); publish( 960 );
  unsigned int space = pcm_fifo_producer_space( &sound_fifo, width );
  publish( space );
  cancel_wait = 1;
  CHECK( !sound_lowlevel_reserve( 240 ) && !native && !settings_current.sound );
  cancel_wait = 0;

  /* GetBuffer failure consumes nothing; post-consumption release failure is
     uncertain and never replayed. Exercise actual consumer observer brackets. */
  start( 0 ); publish( 960 ); publish( 960 );
  unsigned int used = pcm_fifo_consumer_used( &sound_fifo, width );
  acquisition_error = WASAPI_PACKET_FAILURE; pump( 240 );
  CHECK( pcm_fifo_consumer_used( &sound_fifo, width ) == (int)used );
  CHECK( !sound_lowlevel_reserve( 1 ) && !native );
  acquisition_error = 0;
  start( 0 ); publish( 960 ); publish( 960 );
  release_error = WASAPI_PACKET_FAILURE; pump( 240 );
  CHECK( service.uncertain == 240 && !atomic_load( &pacing.active ) );
  CHECK( atomic_load( &pacing.invalid ) );
  CHECK( !sound_lowlevel_reserve( 1 ) && !native );
  release_error = 0;

  start( 0 ); notification_error = WASAPI_PACKET_FAILURE; publish( 240 );
  CHECK( !native && !settings_current.sound ); notification_error = 0;

  /* Failed join is not permission to free FIFO/progress/scratch or reuse. */
  start( 0 ); join_failure = 1; sound_lowlevel_end();
  CHECK( native && sound_fifo.buffer && service.scratch && progress_initialized );
  int rate = 48000, stereo = 0;
  CHECK( sound_lowlevel_init( NULL, &rate, &stereo ) );
  join_failure = 0; sound_lowlevel_end(); CHECK( !native && !sound_fifo.buffer );
  for( int i = 0; i < 32; i++ ) { start( SOUND_STEREO_AY_ACB ); sound_lowlevel_end(); }
  CHECK( closes == 39 && waits > 0 );
  /* A worker stalled before Start must not trap the producer behind a stopped
     FIFO. Use an already-expired watchdog, without a wall-clock test sleep. */
  start( 0 ); block_start = 1; publish( 960 ); publish( 960 );
  stall_deadline = compat_wait_until( 0 );
  CHECK( !sound_lowlevel_reserve( 1 ) && !native && !settings_current.sound );
  block_start = 0;
  start( 0 ); publish( 960 ); publish( 960 );
  last_progress = audio_progress_snapshot( &progress );
  stall_deadline = compat_wait_until( 0 );
  CHECK( !sound_lowlevel_reserve( 1 ) && !native && !settings_current.sound );
  /* Full-frame startup and ordinary batches at both supported speed edges,
     and changed mix rates, preserve AY enums and never need stopped reads. */
  for( unsigned int rate_index = 0; rate_index < 3; rate_index++ ) {
    const unsigned int rates[] = { 44100, 48000, 96000 };
    requested_rate = rates[rate_index];
    for( speed_percent = 50; speed_percent <= 300; speed_percent += 50 ) {
      start( SOUND_STEREO_AY_ABC );
      unsigned int batches = 0;
      while( !mock.started ) {
        CHECK( batches++ < 16 );
        publish( pacing.startup_bound - 1 );
      }
      for( int batch = 0; batch < 500; batch++ )
        publish( pacing.controller.allowance - 1 );
      CHECK( native && !service.silence && !service.uncertain );
      sound_lowlevel_end();
    }
  }
  puts( "Actual WASAPI producer/packet fault/prefill/lifetime tests passed" );
  return 0;
}

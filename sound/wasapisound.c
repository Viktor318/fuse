/* wasapisound.c: native Win32 WASAPI audio and exact FIFO admission
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/
#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "sound.h"
#include "spectrum.h"
#include "audio_pacing.h"
#include "wasapi_native.h"
#include "ui/ui.h"

sfifo_t sound_fifo;
static struct wasapi_native *native;
static struct wasapi_geometry geometry;
static struct wasapi_service service;
static struct audio_pacing pacing;
static struct audio_progress progress;
static int progress_initialized, failure_reported, restart_requested;
static _Atomic bool native_negative;
static unsigned int width, prefill_goal;
/* While the AY player plays (sound_long_buffer), the FIFO is made larger when
   the output starts and about LONG_BUFFER_MS of sound is kept ahead, so the
   music goes on while the main loop is held up (menus, window moves). Games
   get the upstream geometry and pacing unchanged. */
#define LONG_BUFFER_MS 300
static unsigned int long_frames;
static uint32_t last_progress;
static audio_progress_deadline stall_deadline;

static void
notify( void *unused )
{
  (void)unused;
  audio_progress_publish( &progress );
}
static void
begin( void *unused )
{
  (void)unused;
  audio_pacing_callback_begin( &pacing );
}
static void
finish( void *unused, unsigned int requested, unsigned int delivered,
        int invalid, int running )
{
  (void)unused;
  /* Stopped native prefill is not started consumer progress and must not
     qualify startup. Still close its activity bracket on every failure. */
  audio_pacing_callback_finish( &pacing, &progress, running ? requested : 0,
                                running ? delivered : 0, invalid );
}
static void
negative( void *unused )
{
  (void)unused;
  /* A native-empty observation says nothing about an exact missing magnitude.
     Keep it separate from FIFO shortage and wake only on the sticky edge. */
  if( !atomic_exchange( &native_negative, true ) ) notify( NULL );
}

void
sound_lowlevel_end( void )
{
  int32_t error = 0, joined;
  if( native ) {
    joined = wasapi_native_close( native, &error );
    if( joined < 0 ) {
      ui_error( UI_ERROR_ERROR, "WASAPI: cannot join consumer (0x%08lx); "
                "retaining audio storage", (unsigned long)(uint32_t)joined );
      return;
    }
    native = NULL;
    if( error < 0 && !failure_reported )
      ui_error( UI_ERROR_WARNING, "WASAPI: retired stream error 0x%08lx",
                (unsigned long)(uint32_t)error );
  }
  if( progress_initialized ) {
    audio_progress_close( &progress );
    progress_initialized = 0;
  }
  sfifo_close( &sound_fifo );
  free( service.scratch );
  memset( &service, 0, sizeof( service ) );
  pacing.ready = false;
  width = prefill_goal = 0;
}

static void
retire( const char *operation, int32_t error )
{
  settings_current.sound = 0;
  failure_reported = 1;
  if( native )
    ui_error_console( UI_ERROR_INFO,
              "WASAPI: retiring after %s (0x%08lx); stopping/joining consumer",
              operation, (unsigned long)(uint32_t)error );
  sound_lowlevel_end();
  ui_error( UI_ERROR_ERROR, "WASAPI: %s failed (0x%08lx); output disabled",
            operation, (unsigned long)(uint32_t)error );
}

/* All policy changes are producer-owned and made outside a reservation. A
   native-empty observation protects at capacity without learning its magnitude. */
static void
protect_native( void )
{
  if( atomic_exchange( &native_negative, false ) ) {
    adaptive_protect( &pacing.controller, pacing.controller.capacity,
                       ADAPTIVE_HOLD );
    pacing.quarantine = true;
  }
}

static int
healthy( void )
{
  int32_t error;
  uint32_t generation;
  if( !native || restart_requested ) return 0;
  if( wasapi_native_device_changed( native ) ) {
    /* Hold production until the end of the frame, then reopen the output on
       the current default endpoint */
    restart_requested = 1;
    failure_reported = 1;
    ui_error_console( UI_ERROR_INFO, "WASAPI: output endpoint changed; "
                      "restarting on the default endpoint" );
    sound_request_restart();
    return 0;
  }
  error = wasapi_native_error( native );
  if( error < 0 ) {
    const char *operation = wasapi_native_operation( native );
    retire( operation ? operation : "consumer", error );
    return 0;
  }
  if( atomic_load( &progress.error ) ) {
    retire( "progress notification", WASAPI_PACKET_FAILURE );
    return 0;
  }
  generation = audio_progress_snapshot( &progress );
  if( ( !wasapi_native_started( native ) &&
        audio_pacing_occupancy( &sound_fifo, width ) < (int)prefill_goal ) ||
      generation != last_progress ) {
    last_progress = generation;
    stall_deadline = compat_wait_until( 2000 );
  } else if( compat_wait_expired( stall_deadline ) ) {
    retire( "consumer stalled", WASAPI_PACKET_FAILURE );
    return 0;
  }
  return 1;
}

/* Stopped worker: the producer initializes FIFO/policy before attachment.
   Failures leave partial state for sound_lowlevel_init to retire centrally. */
static int
fifo_pacing_init( unsigned int speed, uint64_t *startup,
                  unsigned int *capacity, const char **operation )
{
  unsigned int bytes;
  uint64_t allowance;
  *startup = speed ? (uint64_t)geometry.format.rate *
             machine_current->timings.tstates_per_frame / speed + 1 : 0;
  allowance = speed ? (uint64_t)geometry.format.rate *
              sound_audio_interval_tstates( speed ) / speed + 1 : 0;
  *operation = "allocate whole-frame FIFO";
  if( *startup > UINT_MAX || allowance > UINT_MAX ||
      wasapi_fifo_bytes( *startup, geometry.capacity, allowance, width, &bytes ) )
    return 1;
  long_frames = 0;
  if( sound_long_buffer ) {
    long_frames = (uint64_t)geometry.format.rate * LONG_BUFFER_MS / 1000;
    /* Room for the long buffer and a whole frame batch more */
    if( (uint64_t)( long_frames + *startup ) * width + 1 > bytes )
      bytes = ( long_frames + *startup ) * width + 1;
  }
  if( sfifo_init( &sound_fifo, bytes ) ) return 1;
  *capacity = pcm_fifo_capacity( &sound_fifo, width );
  *operation = "initialize admission geometry";
  if( *capacity < *startup ||
      *capacity < *startup + 2 * (uint64_t)geometry.capacity + 2 ||
      audio_pacing_init_rate( &pacing, &sound_fifo, width, geometry.format.rate,
        speed, machine_current->timings.tstates_per_frame, geometry.capacity ) ||
      audio_progress_init( &progress ) ) return 1;
  progress_initialized = 1;
  atomic_init( &native_negative, false );
  *operation = "initialize native evidence";
  return !atomic_is_lock_free( &native_negative );
}

static int
service_prepare( unsigned int channels, uint64_t startup,
                  unsigned int capacity, const char **operation )
{
  uint64_t goal;
  memset( &service, 0, sizeof( service ) );
  service.fifo = &sound_fifo;
  service.format = geometry.format;
  service.capacity = geometry.capacity;
  service.channels = channels;
  service.quantize = 0; /* 8-bit output was retired upstream */
  service.scratch = malloc( (size_t)geometry.capacity * width );
  service.begin = begin; service.finish = finish; service.negative = negative;
  *operation = "allocate packet scratch";
  if( !service.scratch ) return 1;
  goal = startup + geometry.capacity + pacing.controller.step;
  /* Ensure a stopped stream never requires consumption to finish prefill. A
     full next machine-frame batch must still fit while Q is below the goal. */
  *operation = "prefill geometry";
  if( goal > capacity || goal + startup - 1 > capacity ) return 1;
  prefill_goal = goal;
  return 0;
}

static void
report_startup( int requested_rate, unsigned int capacity )
{
  ui_error_console( UI_ERROR_INFO,
            "WASAPI: %d requested/%u negotiated Hz, "
            "%u input/%u endpoint channels; "
            "%s period-frames=%u, low-period result=0x%08lx, "
            "native capacity=%u, FIFO capacity=%u%s. "
            "Default endpoint followed on change",
            requested_rate, geometry.format.rate,
            service.channels, geometry.format.channels,
            geometry.client3 ? "client3 selected" : "compatibility reported-default",
            geometry.period, (unsigned long)(uint32_t)geometry.low_period_error,
            geometry.capacity, capacity,
            service.quantize ? "; 8-bit output quantization" : "" );
}

int
sound_lowlevel_init( const char *device, int *freqptr, int *stereoptr )
{
  unsigned int capacity, channels;
  uint64_t startup;
  unsigned int speed = sound_get_effective_processor_speed();
  int32_t error;
  const char *operation = NULL;
  int requested_rate = *freqptr;
  if( native || sound_fifo.buffer ) {
    settings_current.sound = 0;
    ui_error( UI_ERROR_ERROR, "WASAPI: previous audio lifetime is still live" );
    return 1;
  }
  if( device && *device && strcmp( device, "default" ) ) {
    settings_current.sound = 0;
    ui_error( UI_ERROR_ERROR, "WASAPI: unsupported device/options '%s'; "
              "use the default endpoint", device );
    return 1;
  }
  failure_reported = 0;
  restart_requested = 0;
  error = wasapi_native_open( &native, &geometry, &operation );
  if( error < 0 ) {
    retire( operation ? operation : "negotiate default endpoint", error );
    return 1;
  }
  /* Preserve the AY stereo enum; endpoint channels never alter AY ordering. */
  channels = *stereoptr ? 2 : 1;
  width = channels * sizeof( int16_t );
  if( fifo_pacing_init( speed, &startup, &capacity, &operation ) ||
      service_prepare( channels, startup, capacity, &operation ) ) {
    retire( operation, WASAPI_PACKET_FAILURE );
    return 1;
  }
  wasapi_native_set_notify( native, notify, NULL );
  error = wasapi_native_attach( native, &service, prefill_goal );
  if( error < 0 ) {
    retire( "attach FIFO transport", error );
    return 1;
  }
  *freqptr = geometry.format.rate;
  last_progress = audio_progress_snapshot( &progress );
  stall_deadline = compat_wait_until( 2000 );
  report_startup( requested_rate, capacity );
  return 0;
}

int
sound_lowlevel_reserve( unsigned int frames )
{
  int admitted, error;
  if( !healthy() ) return 0; /* Native failure is not a synthesis assertion. */
  protect_native();
  admitted = audio_pacing_pending( &pacing, &sound_fifo, width, frames,
              sound_normal_producer_context(), wasapi_native_started( native ) );
  if( admitted < 0 ) return admitted;
  for( ;; ) {
    uint32_t generation = audio_progress_snapshot( &progress );
    if( !healthy() ) return 0;
    /* Native negative evidence while waiting invalidates this transaction's
       epoch before admission, never changing FIFO contents or batch identity. */
    protect_native();
    admitted = audio_pacing_can_admit( &pacing, &sound_fifo, width, frames );
    if( admitted < 0 ) return admitted;
    if( long_frames && pacing.ready && frames && frames <= long_frames ) {
      /* AY player: fill up to the long buffer, not to the adaptive target */
      int physical = pcm_fifo_producer_can_write( &sound_fifo, width, frames );
      int q = audio_pacing_occupancy( &sound_fifo, width );
      if( physical < 0 ) return physical;
      if( q < 0 ) return q;
      admitted = physical > 0 && (unsigned int)q <= long_frames - frames;
    }
    if( admitted ) break;
    error = audio_progress_wait( &progress, generation, audio_progress_until() );
    if( error && error != ETIMEDOUT && error != EINTR ) {
      retire( "admission wait", WASAPI_PACKET_FAILURE ); return 0;
    }
  }
  if( frames ) pacing.reserved = true;
  return 0;
}

void
sound_lowlevel_frame( libspectrum_signed_word *data, int len )
{
  int written;
  int32_t error;
  unsigned int frames;
  if( !healthy() ) return;
  if( len < 0 || len % service.channels ) {
    retire( "sample geometry", WASAPI_PACKET_FAILURE ); return;
  }
  frames = len / service.channels;
  if( !frames ) return;
  /* A live backend requires exact reserve identity, never a partial fallback. */
  if( !pacing.reserved || frames != pacing.batch ) {
    retire( "publication identity", WASAPI_PACKET_FAILURE ); return;
  }
  /* A fault arriving after synthesis retires uncertain PCM rather than replaying
     it. Policy is applied between transactions, after this publication. */
  written = audio_pacing_write( &pacing, &sound_fifo, width, data, frames,
                                wasapi_native_started( native ) );
  protect_native();
  if( written != (int)frames ) {
    retire( "whole-frame publication", WASAPI_PACKET_FAILURE ); return;
  }
  error = wasapi_native_data( native );
  if( error < 0 ) retire( "data notification", error );
}

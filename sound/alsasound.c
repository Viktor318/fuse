/* alsasound.c: ALSA (Linux) sound I/O
   Copyright (c) 2006-2008 Gergely Szasz
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
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

/* This is necessary to prevent warnings from the calls to
   snd_pcm_[hs]w_params_alloca() */
#ifndef NDEBUG
#define NDEBUG
#endif

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <alsa/asoundlib.h>

#include "compat.h"
#include "settings.h"
#include "sound.h"
#include "spectrum.h"
#include "ui/ui.h"
#include "alsa_pacing.h"

/* Default storage in legacy Spectrum-frame-sized units. */
#define NUM_FRAMES 3

static snd_pcm_t *pcm_handle;
static snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;
static int ch, framesize;
static const char *pcm_name = NULL;
static int verb = 0;
static snd_pcm_uframes_t exact_periodsize, exact_bsize;

static snd_output_t *output = NULL;
static struct alsa_pacing pacing;
static snd_pcm_uframes_t prefill_goal, wait_threshold;
static bool waiting_configured, interval_max_supported;
static FILE *trace;
static unsigned int trace_rows, trace_waits, admitted_limit;
static unsigned int xruns, suspends;

static void alsa_fail( const char *operation, int error );

void
sound_lowlevel_end( void )
{
  /* Stop PCM device and drop pending frames */
  if( pcm_handle ) {
    snd_pcm_drop( pcm_handle );
    snd_pcm_close( pcm_handle );
    pcm_handle = NULL;
  }
  memset( &pacing, 0, sizeof( pacing ) );
  waiting_configured = false;
  if( trace ) {
    FILE *closing = trace;
    trace = NULL;
    if( fclose( closing ) )
      ui_error( UI_ERROR_WARNING, "ALSA: closing diagnostic trace: %s",
                strerror( errno ) );
  }
}

int
sound_lowlevel_init( const char *device, int *freqptr, int *stereoptr )
{
  unsigned int exact_rate, periods;
  unsigned int val, n;
  snd_pcm_hw_params_t *hw_params;
  snd_pcm_sw_params_t *sw_params;
  snd_pcm_uframes_t avail_min = 0, sound_periodsize, bsize = 0;
  static int first_init = 1;
  static int init_running = 0;
  const char *option, *trace_path;
  char tmp;
  int err, dir, nperiods = NUM_FRAMES;
  unsigned int target = 0;
  enum alsa_pacing_mode mode = ALSA_PACING_ADAPTIVE;
  snd_pcm_uframes_t boundary;
  libspectrum_dword processor_speed, interval;
  uint64_t allowance, startup, requested_capacity;
  bool explicit_geometry = false;

  float hz;

  if( init_running )
    return 0;

  if( pcm_handle ) return 1;
  init_running = 1;
  pcm_name = NULL;
  verb = 0;
  waiting_configured = false;
  /* select a default device if we weren't explicitly given one */

  option = device;
  while( option && *option ) {
    tmp = '*';
    if( ( err = sscanf( option, " buffer=%u %n%c", &val, &n, &tmp ) > 0 ) &&
        ( tmp == ',' || strlen( option ) == n ) ) {
      if( val < 1 ) {
        fprintf( stderr, "Bad value for ALSA buffer size %u, using default\n",
                 val );
      } else {
        bsize = val;
        explicit_geometry = true;
      }
    } else if( ( err = sscanf( option, " frames=%u %n%c", &val, &n,
                               &tmp ) > 0 ) &&
               ( tmp == ',' || strlen( option ) == n ) ) {
      if( val < 1 || val > INT_MAX ) {
        fprintf( stderr,
                 "Bad value for ALSA buffer size %u frames, using default (%d)\n",
                 val, NUM_FRAMES );
      } else {
        nperiods = val;
        explicit_geometry = true;
      }
    } else if( ( err = sscanf( option, " avail=%u %n%c", &val, &n,
                               &tmp ) > 0 ) &&
               ( tmp == ',' || strlen( option ) == n ) ) {
      if( val < 1 ) {
        fprintf( stderr,
                 "Bad value for ALSA avail_min size %u frames, using default\n",
                 val );
      } else {
        avail_min = val;
      }
    } else if( sscanf( option, " target=%u %n%c", &val, &n, &tmp ) > 0 &&
               ( tmp == ',' || strlen( option ) == n ) ) {
      target = val;
      mode = val ? ALSA_PACING_FIXED : ALSA_PACING_CAPACITY;
    } else if( ( err = sscanf( option, " verbose %n%c", &n, &tmp ) == 1 ) &&
               ( tmp == ','  || strlen( option ) == n ) ) {
      verb = 1;
    } else {                                    /* try as device name */
      n = strlen( option );
      pcm_name = option;
      while( isspace( (unsigned char)*pcm_name ) ) pcm_name++;
      if( *pcm_name == '\'' ) pcm_name++;   /* force device */
    }
    option += n + ( tmp == ',' );
  }

  /* Open the sound device
   */
  if( pcm_name == NULL || *pcm_name == '\0' )
    pcm_name = "default";
  if( snd_pcm_open( &pcm_handle, pcm_name, stream, SND_PCM_NONBLOCK ) < 0 ) {
    if( strcmp( pcm_name, "default" ) == 0 ) {
      /* we try a last one: plughw:0,0 but what a weired ALSA conf.... */
      if( snd_pcm_open( &pcm_handle, "plughw:0,0", stream,
                        SND_PCM_NONBLOCK ) < 0 ) {
        settings_current.sound = 0;
        ui_error( UI_ERROR_ERROR,
                  "couldn't open sound device 'default' and 'plughw:0,0' check ALSA configuration."
                  );
        init_running = 0;
        return 1;
      } else {
        pcm_name = "plughw:0,0";
        if( first_init )
          fprintf( stderr,
                   "Couldn't open sound device 'default', using 'plughw:0,0' check ALSA configuration.\n"
                   );
      }
    }
    if( !pcm_handle ) {
      settings_current.sound = 0;
      ui_error( UI_ERROR_ERROR, "couldn't open sound device '%s'.", pcm_name );
      init_running = 0;
      return 1;
    }
  }

  /* Allocate the snd_pcm_hw_params_t structure on the stack. */
  snd_pcm_hw_params_alloca( &hw_params );

  /* Init hw_params with full configuration space */
  if( snd_pcm_hw_params_any( pcm_handle, hw_params ) < 0 ) {
    settings_current.sound = 0;
    ui_error( UI_ERROR_ERROR,
              "couldn't get configuration space on sound device '%s'.",
              pcm_name );
    sound_lowlevel_end();
    init_running = 0;
    return 1;
  }

  if( snd_pcm_hw_params_set_access( pcm_handle, hw_params,
                                    SND_PCM_ACCESS_RW_INTERLEAVED ) < 0 ) {
    settings_current.sound = 0;
    ui_error( UI_ERROR_ERROR, "couldn't set access interleaved on '%s'.",
              pcm_name );
    sound_lowlevel_end();
    init_running = 0;
    return 1;
  }

  /* Set sample format */
  if( snd_pcm_hw_params_set_format( pcm_handle, hw_params,
#if defined WORDS_BIGENDIAN
                                    SND_PCM_FORMAT_S16_BE
#else
                                    SND_PCM_FORMAT_S16_LE
#endif
                                    ) < 0 ) {
    settings_current.sound = 0;
    ui_error( UI_ERROR_ERROR, "couldn't set format on '%s'.", pcm_name );
    sound_lowlevel_end();
    init_running = 0;
    return 1;
  }

  ch = *stereoptr ? 2 : 1;

  if( snd_pcm_hw_params_set_channels( pcm_handle, hw_params, ch )
      < 0 ) {
    fprintf( stderr, "Couldn't set %s to '%s'.\n", pcm_name,
             (*stereoptr ? "stereo" : "mono" ) );
    ch = *stereoptr ? 1 : 2;            /* try with opposite */
    if( snd_pcm_hw_params_set_channels( pcm_handle, hw_params, ch )
        < 0 ) {
      ui_error( UI_ERROR_ERROR, "couldn't set %s to '%s'.", pcm_name,
                (*stereoptr ? "stereo" : "mono" ) );
      settings_current.sound = 0;
      sound_lowlevel_end();
      init_running = 0;
      return 1;
    }
    *stereoptr = *stereoptr ? 0 : 1;            /* write back */
  }

  framesize = ch << 1;                  /* we always use 16 bit sorry :-( */
  /* Set sample rate. If the exact rate is not supported */
  /* by the hardware, use nearest possible rate.         */
  exact_rate = *freqptr;
  if( snd_pcm_hw_params_set_rate_near( pcm_handle, hw_params, &exact_rate,
                                       NULL ) < 0 ) {
    settings_current.sound = 0;
    ui_error( UI_ERROR_ERROR, "couldn't set rate %d on '%s'.",
              *freqptr, pcm_name );
    sound_lowlevel_end();
    init_running = 0;
    return 1;
  }
  if( first_init && *freqptr != exact_rate ) {
    fprintf( stderr,
             "The rate %d Hz is not supported by your hardware. "
             "Using %d Hz instead.\n", *freqptr, exact_rate );
  }
  *freqptr = exact_rate;

  if( bsize != 0 ) {
    exact_periodsize = sound_periodsize = bsize / nperiods;
    if( bsize < 1 ) {
      fprintf( stderr,
               "bad value for ALSA buffer size %i, using default.\n",
               val );
      bsize = 0;
    }
  }

  if( bsize == 0 ) {
    /* Adjust relative processor speed to deal with adjusting sound generation
       frequency against emulation speed (more flexible than adjusting generated
       sample rate) */
    hz = (float)sound_get_effective_processor_speed() /
         machine_current->timings.tstates_per_frame;
    /* Retain the legacy reference-frame storage size at high emulator speeds.
       This no longer determines the default service/wakeup period. */
    if( hz > 100.0 ) hz = 100.0;
    exact_periodsize = sound_periodsize = *freqptr / hz;
  }

  if( !explicit_geometry ) {
    /* Storage still covers legacy whole-frame/catch-up production. Only the
       service quantum follows the ordinary cadence; shrinking three periods
       to 5 ms each would make some exact whole-frame batches inadmissible. */
    requested_capacity = (uint64_t)sound_periodsize * nperiods;
    processor_speed = sound_get_effective_processor_speed();
    interval = sound_audio_interval_tstates( processor_speed );
    if( !processor_speed || !requested_capacity ||
        requested_capacity > UINT_MAX ) {
      ui_error( UI_ERROR_ERROR, "ALSA: invalid requested capacity on '%s'",
                pcm_name );
      sound_lowlevel_end();
      settings_current.sound = 0;
      init_running = 0;
      return 1;
    }
    bsize = requested_capacity;
    sound_periodsize = (uint64_t)*freqptr * interval / processor_speed;
    if( !sound_periodsize ) sound_periodsize = 1;
    exact_periodsize = sound_periodsize;
  }

  dir = -1;
  if( snd_pcm_hw_params_set_period_size_near( pcm_handle, hw_params,
                                              &exact_periodsize, &dir ) < 0 ) {
    settings_current.sound = 0;
    ui_error( UI_ERROR_ERROR, "couldn't set period size %d on '%s'.",
              (int)sound_periodsize, pcm_name );
    sound_lowlevel_end();
    init_running = 0;
    return 1;
  }

  if( first_init && ( exact_periodsize < sound_periodsize / 1.5 ||
                      exact_periodsize > sound_periodsize * 1.5    ) ) {
    fprintf( stderr,
             "The period size %d is not supported by your hardware. "
             "Using %d instead.\n", (int)sound_periodsize,
             (int)exact_periodsize );
  }

  if( explicit_geometry ) {
    /* Explicit buffer/frames retain their previous period-count semantics. */
    periods = nperiods;
    err = snd_pcm_hw_params_set_periods_near( pcm_handle, hw_params,
                                              &periods, NULL );
    if( !err && first_init && periods != (unsigned int)nperiods )
      fprintf( stderr, "%d periods is not supported by your hardware. "
               "Using %u instead.\n", nperiods, periods );
  } else {
    err = snd_pcm_hw_params_set_buffer_size_near( pcm_handle, hw_params,
                                                  &bsize );
  }
  if( err < 0 ) {
    settings_current.sound = 0;
    ui_error( UI_ERROR_ERROR, "couldn't set buffer geometry on '%s': %s",
              pcm_name, snd_strerror( err ) );
    sound_lowlevel_end();
    init_running = 0;
    return 1;
  }

  snd_pcm_hw_params_get_buffer_size( hw_params, &exact_bsize );

  /* Apply HW parameter settings to */
  /* PCM device and prepare device  */

  if( snd_pcm_hw_params( pcm_handle, hw_params ) < 0 ) {
    settings_current.sound = 0;
    ui_error( UI_ERROR_ERROR, "couldn't set hw_params on %s", pcm_name );
    sound_lowlevel_end();
    init_running = 0;
    return 1;
  }

  snd_pcm_sw_params_alloca( &sw_params );
  if( ( err = snd_pcm_sw_params_current( pcm_handle, sw_params ) ) < 0 ) {
    ui_error( UI_ERROR_ERROR, "couldn't get sw_params from %s: %s", pcm_name,
              snd_strerror( err ) );
    sound_lowlevel_end();
    init_running = 0;
    return 1;
  }

  /* Explicit prefill/start: a shallow target must never wait for a prepared
     stream to consume. Use installed geometry, not requested period counts. */
  snd_pcm_sw_params_get_boundary( sw_params, &boundary );
  prefill_goal = exact_bsize > exact_periodsize ?
                 exact_bsize - exact_periodsize : exact_bsize;
  if( ( err = snd_pcm_sw_params_set_start_threshold( pcm_handle,
                                                     sw_params,
                                                     boundary ) ) < 0 ) {
    ui_error( UI_ERROR_ERROR, "couldn't set start_treshold on %s: %s", pcm_name,
              snd_strerror( err ) );
    sound_lowlevel_end();
    init_running = 0;
    return 1;
  }

  if( !avail_min )
    avail_min = exact_periodsize >> 1;
  if( ( err = snd_pcm_sw_params_set_avail_min( pcm_handle,
                                               sw_params, avail_min ) ) < 0 ) {
#if SND_LIB_VERSION < 0x10010
    if( ( err = snd_pcm_sw_params_set_sleep_min( pcm_handle,
                                                 sw_params, 1 ) ) < 0 ) {
      fprintf( stderr, "Unable to set minimal sleep 1 for %s: %s\n", pcm_name,
               snd_strerror( err ) );
    }
#else
    fprintf( stderr, "Unable to set avail min %s: %s\n", pcm_name,
             snd_strerror( err ) );
#endif
  }

#if SND_LIB_VERSION < 0x10010
  if( ( err = snd_pcm_sw_params_set_xfer_align( pcm_handle, sw_params,
                                                1 ) ) < 0 ) {
    ui_error( UI_ERROR_ERROR, "couldn't set xfer_allign on %s: %s", pcm_name,
              snd_strerror( err ) );
    sound_lowlevel_end();
    init_running = 0;
    return 1;
  }
#endif

  if( ( err = snd_pcm_sw_params( pcm_handle, sw_params ) ) < 0 ) {
    ui_error( UI_ERROR_ERROR, "couldn't set sw_params on %s: %s", pcm_name,
              snd_strerror( err ) );
    sound_lowlevel_end();
    init_running = 0;
    return 1;
  }

  if( snd_pcm_hw_params_current( pcm_handle, hw_params ) < 0 ||
      snd_pcm_hw_params_get_buffer_size( hw_params, &exact_bsize ) < 0 ||
      snd_pcm_hw_params_get_period_size( hw_params, &exact_periodsize,
                                         &dir ) < 0 ) {
    ui_error( UI_ERROR_ERROR, "ALSA: querying installed geometry on '%s'",
              pcm_name );
    sound_lowlevel_end();
    settings_current.sound = 0;
    init_running = 0;
    return 1;
  }
  prefill_goal = exact_bsize > exact_periodsize ?
                 exact_bsize - exact_periodsize : exact_bsize;
  processor_speed = sound_get_effective_processor_speed();
  interval = sound_audio_interval_tstates( processor_speed );
  allowance = processor_speed ?
              (uint64_t)*freqptr * interval / processor_speed + 1 : 0;
  startup = processor_speed ? (uint64_t)*freqptr *
            machine_current->timings.tstates_per_frame / processor_speed +
            1 : 0;
  if( !startup || exact_bsize > UINT_MAX || exact_periodsize > UINT_MAX ||
      allowance > UINT_MAX || startup > UINT_MAX ||
      alsa_pacing_init( &pacing, exact_bsize, allowance, exact_periodsize,
                        startup, mode, target ) ) {
    ui_error( UI_ERROR_ERROR, "ALSA: unusable PCM admission geometry on '%s'",
              pcm_name );
    snd_pcm_close( pcm_handle );
    pcm_handle = NULL;
    settings_current.sound = 0;
    init_running = 0;
    return 1;
  }
  /* ioplug's avail_max is cumulative until reset, unlike the documented
     per-status hardware interval. Do not turn that into perpetual warnings. */
  interval_max_supported = snd_pcm_type( pcm_handle ) == SND_PCM_TYPE_HW;
  if( verb )
    fprintf( stdout, "ALSA: %s, %u Hz, %d channels, capacity=%lu, "
             "period=%lu, batch-device=%d, pacing=%s, target=%u\n",
             pcm_name, exact_rate, ch, (unsigned long)exact_bsize,
             (unsigned long)exact_periodsize,
             snd_pcm_hw_params_is_batch( hw_params ),
             mode == ALSA_PACING_ADAPTIVE ? "adaptive" :
             mode == ALSA_PACING_FIXED ? "fixed" : "capacity",
             mode == ALSA_PACING_FIXED ? target : (unsigned int)exact_bsize );
  trace_path = getenv( "FUSE_ALSA_TRACE" );
  if( trace_path && *trace_path ) {
    trace = fopen( trace_path, "w" );
    if( !trace ) {
      ui_error( UI_ERROR_WARNING, "ALSA: opening trace '%s': %s",
                trace_path, strerror( errno ) );
    } else {
      fprintf( trace, "# PCM=%s rate=%u channels=%d C=%lu period=%lu "
               "batch=%d mode=%d interval-max=%d\n", pcm_name, exact_rate, ch,
               (unsigned long)exact_bsize, (unsigned long)exact_periodsize,
               snd_pcm_hw_params_is_batch( hw_params ), mode,
               interval_max_supported );
      fprintf( trace, "time_us,B,before,after,effective,progress,ordinary,"
               "complete,running,waits,target,accepted,floor,state,epoch,mode,"
               "limit,xruns,suspends,error\n" );
    }
  }
  trace_rows = trace_waits = xruns = suspends = 0;
  if( first_init ) snd_output_stdio_attach( &output, stdout, 0 );

  first_init = 0;
  init_running = 0;
  return 0;     /* success */
}

/* Device failures are not synthesis assertion failures. Retire output locally:
   reserve's negative return is reserved for programming/geometry errors. */
static void
alsa_fail( const char *operation, int error )
{
  settings_current.sound = 0;
  sound_lowlevel_end();
  ui_error( UI_ERROR_ERROR, "ALSA: %s on '%s': %s", operation,
            pcm_name ? pcm_name : "default", snd_strerror( error ) );
}

static int
alsa_recover( int error )
{
  int result;
  long long deadline;
  if( error == -EINTR || error == -EAGAIN ) return 0;
  if( error != -EPIPE && error != -ESTRPIPE ) return error;
  if( error == -EPIPE ) xruns++;
  else suspends++;
  if( error == -ESTRPIPE ) {
    deadline = compat_monotonic_time_us() + 100000;
    do {
      result = snd_pcm_resume( pcm_handle );
      if( result != -EAGAIN ) break;
      compat_timer_sleep( 1 );
    } while( compat_monotonic_time_us() < deadline );
    if( !result ) {
      /* Resume retains queued frames, but does not retain adaptive evidence. */
      alsa_pacing_restart( &pacing );
      waiting_configured = false;
      return 0;
    }
  }
  result = snd_pcm_prepare( pcm_handle );
  if( result < 0 ) return result;
  alsa_pacing_restart( &pacing );
  waiting_configured = false;
  return 0;
}

/* Status resets avail_max: each successful call ends one labelled observation
   window. It is reported low water, not a continuously measured physical queue. */
static int
alsa_snapshot( unsigned int *q, bool *running, bool observe_minimum )
{
  snd_pcm_status_t *status;
  snd_pcm_uframes_t available, maximum;
  snd_pcm_state_t state;
  int error;
  snd_pcm_status_alloca( &status );
  error = snd_pcm_status( pcm_handle, status );
  if( error < 0 ) return error;
  state = snd_pcm_status_get_state( status );
  if( state == SND_PCM_STATE_XRUN ) return -EPIPE;
  if( state == SND_PCM_STATE_SUSPENDED ) return -ESTRPIPE;
  if( state == SND_PCM_STATE_DISCONNECTED ) return -ENODEV;
  if( state != SND_PCM_STATE_RUNNING && state != SND_PCM_STATE_PREPARED )
    return -EBADFD;
  available = snd_pcm_status_get_avail( status );
  maximum = snd_pcm_status_get_avail_max( status );
  if( available > exact_bsize ) return -ERANGE;
  *q = exact_bsize - available;
  *running = state == SND_PCM_STATE_RUNNING;
  if( alsa_pacing_snapshot( &pacing, *q ) ) {
    /* A plugin may expose a different accounting domain. Keep playback, but
       do not keep inventing progress or qualifying shallow targets. */
    adaptive_fallback( &pacing.controller );
    pacing.mode = ALSA_PACING_CAPACITY;
    pacing.have_queue = false;
    pacing.accepted = 0;
    pacing.quarantine = true;
  }
  if( observe_minimum && *running && interval_max_supported )
    alsa_pacing_low_water( &pacing,
                           maximum <= exact_bsize ? exact_bsize - maximum : 0,
                           maximum >= available && maximum <= exact_bsize );
  return 0;
}

/* Development-only bounded trace. No callback, normal logging or policy
   side effects; all I/O is on the producer and explicitly opted in. */
static void
alsa_trace( unsigned int batch, unsigned int before, unsigned int after,
            bool complete, bool running, int error )
{
  if( trace && trace_rows < 65536 ) {
    trace_rows++;
    if( fprintf( trace,
                 "%lld,%u,%u,%u,%lld,%u,%d,%d,%d,%u,%u,%u,%llu,%d,%llu,%d,%u,%u,%u,%d\n",
                 compat_monotonic_time_us(), batch, before, after,
                 (long long)after - batch, pacing.progress, pacing.ordinary,
                 complete, running, trace_waits, alsa_pacing_limit( &pacing ),
                 pacing.mode == ALSA_PACING_FIXED && pacing.normal ?
                 pacing.fixed_target : pacing.controller.accepted,
                 (unsigned long long)pacing.controller.floor,
                 pacing.controller.state,
                 (unsigned long long)pacing.controller.epoch, pacing.mode,
                 admitted_limit, xruns, suspends, error ) < 0 ) {
      int saved_errno = errno;
      fclose( trace );
      trace = NULL;
      ui_error( UI_ERROR_WARNING, "ALSA: writing diagnostic trace: %s",
                strerror( saved_errno ) );
    }
  }
}

static int
alsa_wait( unsigned int needed )
{
  snd_pcm_sw_params_t *params;
  int error;
  long long before;
  if( !waiting_configured || wait_threshold != needed ) {
    snd_pcm_sw_params_alloca( &params );
    error = snd_pcm_sw_params_current( pcm_handle, params );
    if( error < 0 ) return error;
    error = snd_pcm_sw_params_set_avail_min( pcm_handle, params, needed );
    if( error < 0 ) return error;
    error = snd_pcm_sw_params( pcm_handle, params );
    if( error < 0 ) return error;
    wait_threshold = needed;
    waiting_configured = true;
  }
  trace_waits++;
  before = compat_monotonic_time_us();
  error = snd_pcm_wait( pcm_handle, 10 );
  /* Some plugins signal readiness below the requested threshold. Rate-limit
     immediate returns; the caller still has to recheck its predicate. */
  if( error >= 0 && compat_monotonic_time_us() - before < 1000 )
    compat_timer_sleep( 1 );
  return error < 0 ? error : 0;
}

int
sound_lowlevel_reserve( unsigned int frames )
{
  unsigned int q, limit;
  bool running;
  int error;
  if( !pcm_handle || !frames ) return 0;
  if( pacing.reserved || frames > pacing.controller.capacity ) return -EINVAL;
  trace_waits = 0;
  for( ;; ) {
    error = alsa_snapshot( &q, &running, true );
    if( error < 0 ) {
      error = alsa_recover( error );
      if( error < 0 ) {
        alsa_fail( "query/recovery", error );
        return 0;
      }
      continue;
    }
    if( alsa_pacing_pending( &pacing, frames,
                             sound_normal_producer_context(), running ) )
      return -EINVAL;
    limit = alsa_pacing_limit( &pacing );
    if( frames <= limit && q <= limit - frames ) {
      admitted_limit = limit;
      pacing.reserved = true;
      return 0;
    }
    /* A prepared buffer cannot drain. Initialization and prefill must ensure
       this is unreachable; fail output instead of waiting forever. */
    if( !running ) {
      if( q ) {
        error = snd_pcm_start( pcm_handle );
        if( !error ) continue;
        alsa_fail( "prefill start", error );
      } else {
        alsa_fail( "prefill capacity", -ENOSPC );
      }
      return 0;
    }
    error = alsa_wait( pacing.controller.capacity - limit + frames );
    if( error < 0 ) {
      error = alsa_recover( error );
      if( error < 0 ) {
        alsa_fail( "wait/recovery", error );
        return 0;
      }
    }
  }
}

void
sound_lowlevel_frame( libspectrum_signed_word *data, int len )
{
  unsigned int before = 0, after = 0, remaining, batch;
  bool running = false, complete = true;
  int error;
  snd_pcm_sframes_t written;
  if( !pcm_handle ) return;
  if( len <= 0 || len % ch || !pacing.reserved ||
      (unsigned int)( len / ch ) != pacing.batch ) {
    alsa_fail( "publication identity", -EINVAL );
    return;
  }
  batch = remaining = len / ch;
  error = alsa_snapshot( &before, &running, true );
  if( error < 0 ) {
    error = alsa_recover( error );
    complete = false;
    if( error < 0 ) goto failed;
  }
  while( remaining ) {
    written = snd_pcm_writei( pcm_handle, data, remaining );
    if( written > 0 ) {
      if( (snd_pcm_uframes_t)written > remaining ) {
        error = -ERANGE;
        goto failed;
      }
      alsa_pacing_accept( &pacing, written );
      data += written * ch;
      remaining -= written;
      /* Explicit start also works when a recovered tail is being prefilled. */
      error = alsa_snapshot( &after, &running, false );
      if( !error && !running && after >= prefill_goal )
        error = snd_pcm_start( pcm_handle );
      if( error < 0 ) {
        complete = false;
        error = alsa_recover( error );
        if( error < 0 ) goto failed;
      }
      continue;
    }
    error = written < 0 ? written : -EAGAIN;
    if( error == -EINTR ) continue;
    if( error == -EAGAIN ) {
      error = alsa_snapshot( &after, &running, false );
      if( !error && !running ) {
        if( !after ) {
          error = -ENOSPC;
          goto failed;
        }
        error = snd_pcm_start( pcm_handle );
      }
      if( !error ) error = alsa_wait( 1 );
      if( !error ) continue;
    }
    complete = false;
    error = alsa_recover( error );
    if( error < 0 ) goto failed;
  }
  error = alsa_snapshot( &after, &running, true );
  if( error < 0 ) {
    error = alsa_recover( error );
    complete = false;
    if( error < 0 ) goto failed;
  }
  if( complete && pacing.reserved ) {
    if( alsa_pacing_publication( &pacing, before, after, running,
                                 sound_normal_producer_context() ) ) {
      adaptive_fallback( &pacing.controller );
      pacing.mode = ALSA_PACING_CAPACITY;
      pacing.quarantine = true;
      complete = false;
    }
  }
  alsa_trace( batch, before, after, complete, running, 0 );
  pacing.reserved = false;
  return;
failed:
  alsa_trace( batch, before, after, false, running, error );
  alsa_fail( "write/recovery", error );
}

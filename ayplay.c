/* ayplay.c: Playing an AY song apart from the emulated machine
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   The sound generator is that of sound/ay_engine.c (Copyright (c)
   2000-2026 Russell Marks, Matan Ziv-Av, Philip Kendall, Fredrick Meunier,
   Patrik Rak; GPL 2 or later), with a state of its own.

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

#include "ayplay.h"
#include "aysource.h"
#include "options.h"
#include "settings.h"
#include "sound.h"
#include "sound/blipbuffer.h"
#include "sound/dc_filter.h"

#ifdef HAVE_PTHREAD
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock( &lock )
#define UNLOCK() pthread_mutex_unlock( &lock )
#else
#define LOCK()
#define UNLOCK()
#endif

#define CHANNELS 3
#define ENV_STEPS 16
#define ENV_CONT 8
#define ENV_ATTACK 4
#define ENV_ALT 2
#define ENV_HOLD 1
/* The sound generator is clocked every 16 AY clocks, 32 processor clocks */
#define AY_STEP 32
#define AMPL_AY_TONE ( 24 * 256 )
#define MAX_WRITES 8000
#define PENDING 8192

/* As in ayplayer.c: fading out at the end of a song whose music goes round,
   when the player leaves it */
#define FADE_FRAMES ( 5 * 50 )
#define LOOP_FADE_FRAMES ( 3 * 50 )

typedef struct chip {
  unsigned char registers[ 16 ];
  unsigned int tone_tick[ CHANNELS ], tone_high[ CHANNELS ];
  unsigned int tone_period[ CHANNELS ], noise_period, env_period;
  unsigned int noise_tick, tone_cycles, env_cycles;
  unsigned int env_internal_tick, env_tick;
  int rng, noise_toggle, env_first, env_reverse, env_counter;
  int previous[ CHANNELS ][ 2 ];
} chip;

/* The song */
static unsigned char *song_data = NULL;
static size_t song_length = 0;
static int song_number = 0;
static aysource *source = NULL;
static aysource_write writes[ MAX_WRITES ];

static volatile int active = 0, paused = 0, ended = 0;
static long frames_played = 0, song_frames = 0, song_loop = -1;
static int length_default = 0, repeat = 0, fade = 100;

/* The sound */
static chip ay;
static unsigned int levels[ ENV_STEPS ];
static unsigned int next_tick;
static Blip_Buffer *buffers[ 2 ] = { NULL, NULL };
static Blip_Synth *synths[ CHANNELS ][ 2 ];
static int buffer_rate = 0;
static dc_filter_t dc[ 2 ];
static blip_sample_t pending[ 2 ][ PENDING ];
static long pending_count = 0, pending_position = 0;

/* How it is heard: set from the settings by ayplay_update_mix() */
static int mix_volume = 100;
static int mix_channel_volume[ CHANNELS ] = { 100, 100, 100 };
static int mix_muted[ CHANNELS ];
/* How loud each channel is on the left and the right side, in percent
   (from its panorama) */
static int mix_sides[ CHANNELS ][ 2 ] =
  { { 100, 100 }, { 100, 100 }, { 100, 100 } };

static volatile int peak[ CHANNELS ];

/* ------------------------------------------------------------------------
   The sound generator (as sound/ay_engine.c)
   ------------------------------------------------------------------------ */

static void
levels_init( void )
{
  static const int measured_levels[ ENV_STEPS ] = {
    0x0000, 0x0385, 0x053D, 0x0770, 0x0AD7, 0x0FD5, 0x15B0, 0x230C,
    0x2B4C, 0x43C1, 0x5A4B, 0x732F, 0x9204, 0xAFF1, 0xD921, 0xFFFF
  };
  int i;

  for( i = 0; i < ENV_STEPS; i++ )
    levels[i] = ( measured_levels[i] * AMPL_AY_TONE + 0x8000 ) / 0xffff;
}

static void
chip_reset( chip *c )
{
  int i;

  memset( c, 0, sizeof( *c ) );
  for( i = 0; i < CHANNELS; i++ ) c->tone_period[i] = 1;
  c->rng = 1;
  c->env_first = 1;
  c->env_counter = ENV_STEPS - 1;
}

static void
chip_write( chip *c, int reg, int val )
{
  int channel;

  reg &= 15;
  c->registers[ reg ] = val;
  if( reg <= 5 ) {
    channel = reg >> 1;
    c->tone_period[ channel ] = c->registers[ reg & ~1 ] |
                                ( c->registers[ reg | 1 ] & 15 ) << 8;
    if( !c->tone_period[ channel ] ) c->tone_period[ channel ] = 1;
    if( c->tone_tick[ channel ] >= c->tone_period[ channel ] * 2 )
      c->tone_tick[ channel ] %= c->tone_period[ channel ] * 2;
  } else if( reg == 6 ) {
    c->noise_tick = 0;
    c->noise_period = c->registers[6] & 31;
  } else if( reg == 11 || reg == 12 ) {
    c->env_period = c->registers[11] | ( c->registers[12] << 8 );
  } else if( reg == 13 ) {
    c->env_internal_tick = c->env_tick = c->env_cycles = 0;
    c->env_first = 1;
    c->env_reverse = 0;
    c->env_counter = ( c->registers[13] & ENV_ATTACK ) ? 0 : ENV_STEPS - 1;
  }
}

static void
finish_envelope_cycle( chip *c, int shape )
{
  if( !( shape & ENV_CONT ) ) {
    c->env_counter = 0;
  } else if( shape & ENV_HOLD ) {
    if( c->env_first && ( shape & ENV_ALT ) )
      c->env_counter = c->env_counter ? 0 : ENV_STEPS - 1;
  } else if( shape & ENV_ALT ) {
    c->env_reverse = !c->env_reverse;
  } else {
    c->env_counter = ( shape & ENV_ATTACK ) ? 0 : ENV_STEPS - 1;
  }
  c->env_first = 0;
}

static void
advance_envelope( chip *c, int shape )
{
  if( c->env_first || ( ( shape & ENV_CONT ) && !( shape & ENV_HOLD ) ) ) {
    c->env_counter += c->env_reverse ?
                      ( ( shape & ENV_ATTACK ) ? -1 : 1 ) :
                      ( ( shape & ENV_ATTACK ) ? 1 : -1 );
    if( c->env_counter < 0 ) c->env_counter = 0;
    if( c->env_counter >= ENV_STEPS ) c->env_counter = ENV_STEPS - 1;
  }

  c->env_internal_tick++;
  while( c->env_internal_tick >= ENV_STEPS ) {
    c->env_internal_tick -= ENV_STEPS;
    finish_envelope_cycle( c, shape );
  }
}

static int
clock_envelope( chip *c, int shape )
{
  int noise_count = 0;

  c->env_cycles += 16;
  while( c->env_cycles >= 16 ) {
    c->env_cycles -= 16;
    noise_count++;
    c->env_tick++;
    while( c->env_tick >= c->env_period ) {
      c->env_tick -= c->env_period;
      advance_envelope( c, shape );
      if( !c->env_period ) break;
    }
  }
  return noise_count;
}

static void
clock_noise( chip *c, int count )
{
  c->noise_tick += count;
  while( c->noise_tick >= c->noise_period ) {
    c->noise_tick -= c->noise_period;
    c->noise_toggle ^= ( c->rng ^ ( c->rng >> 1 ) ) & 1;
    c->rng = ( c->rng >> 1 ) ^ ( 0x12000 & -( c->rng & 1 ) );
    if( !c->noise_period ) break;
  }
}

static int
channel_output( chip *c, int channel, int envelope_level,
                unsigned int tone_count )
{
  int volume = c->registers[ 8 + channel ];
  int output = volume & 16 ? envelope_level : (int)levels[ volume & 15 ];
  int mixer = c->registers[7];

  if( !( mixer & ( 1 << channel ) ) ) {
    c->tone_tick[ channel ] += tone_count;
    if( c->tone_tick[ channel ] >= c->tone_period[ channel ] ) {
      c->tone_tick[ channel ] -= c->tone_period[ channel ];
      c->tone_high[ channel ] = !c->tone_high[ channel ];
    }
    if( !c->tone_high[ channel ] ) output = 0;
  }
  if( !( mixer & ( 0x08 << channel ) ) && c->noise_toggle ) output = 0;
  if( mix_muted[ channel ] ) return 0;
  return output * mix_volume * fade / 10000 *
         mix_channel_volume[ channel ] / 100;
}

/* ------------------------------------------------------------------------
   Producing the samples
   ------------------------------------------------------------------------ */

static void
buffers_free( void )
{
  int channel, side;

  for( channel = 0; channel < CHANNELS; channel++ )
    for( side = 0; side < 2; side++ )
      if( synths[ channel ][ side ] )
        delete_Blip_Synth( &synths[ channel ][ side ] );
  for( side = 0; side < 2; side++ )
    if( buffers[ side ] ) delete_Blip_Buffer( &buffers[ side ] );
  buffer_rate = 0;
}

static int
buffers_new( int rate )
{
  int channel, side;

  buffers_free();
  for( side = 0; side < 2; side++ ) {
    buffers[ side ] = new_Blip_Buffer();
    if( !buffers[ side ] ) return 1;
    blip_buffer_set_clock_rate( buffers[ side ], AYSOURCE_CLOCK );
    if( blip_buffer_set_sample_rate( buffers[ side ], rate, 200 ) ) return 1;
    blip_buffer_set_bass_freq( buffers[ side ], 0 );
    dc_filter_configure( &dc[ side ], rate );
    dc_filter_reset( &dc[ side ] );
  }
  for( channel = 0; channel < CHANNELS; channel++ )
    for( side = 0; side < 2; side++ ) {
      Blip_Synth *synth = new_Blip_Synth();
      if( !synth ) return 1;
      blip_synth_set_volume( synth, 1.0 );
      blip_synth_set_treble_eq( synth, 0.0 );
      blip_synth_set_output( synth, buffers[ side ] );
      synths[ channel ][ side ] = synth;
    }
  buffer_rate = rate;
  pending_count = pending_position = 0;
  memset( ay.previous, 0, sizeof( ay.previous ) );
  next_tick = 0;
  return 0;
}

/* The counting of the frames, the end of the song and fading out */
static void
song_frame( void )
{
  long fade_frames = length_default ? FADE_FRAMES : LOOP_FADE_FRAMES;

  frames_played++;
  if( song_frames && frames_played >= song_frames ) {
    if( repeat && song_loop >= 0 ) frames_played = song_loop;
    else ended = 1;
  }

  if( !repeat && song_loop >= 0 && song_frames &&
      frames_played > song_frames - fade_frames ) {
    long left = song_frames - frames_played;
    fade = left > 0 ? left * 100 / fade_frames : 0;
  } else {
    fade = 100;
  }
}

/* A frame of the song into the buffers */
static void
render_frame( void )
{
  int count, position = 0, channel, side;
  unsigned int f;
  chip *c = &ay;
  long avail, n0, n1;
  /* Channels with tone and noise off: the lowest and highest level in the
     frame (digital music changes it all the time) */
  int dc_low[ CHANNELS ], dc_high[ CHANNELS ];

  for( channel = 0; channel < CHANNELS; channel++ ) {
    dc_low[ channel ] = 16;
    dc_high[ channel ] = -1;
  }

  count = aysource_frame( source, writes, MAX_WRITES );
  song_frame();

  for( f = next_tick; f < AYSOURCE_FRAME_TSTATES; f += AY_STEP ) {
    int envelope_level, noise_count;
    unsigned int tone_count;

    while( position < count && writes[ position ].tstates <= f ) {
      chip_write( c, writes[ position ].reg, writes[ position ].value );
      position++;
    }

    envelope_level = levels[ c->env_counter ];
    noise_count = clock_envelope( c, c->registers[13] );
    c->tone_cycles += 16;
    tone_count = c->tone_cycles >> 3;
    c->tone_cycles &= 7;

    for( channel = 0; channel < CHANNELS; channel++ ) {
      int volume = c->registers[ 8 + channel ];
      int level = volume & 16 ? c->env_counter : volume & 15;
      int output = channel_output( c, channel, envelope_level, tone_count );

      if( ( ( c->registers[7] >> channel ) & 9 ) == 9 ) {
        if( level < dc_low[ channel ] ) dc_low[ channel ] = level;
        if( level > dc_high[ channel ] ) dc_high[ channel ] = level;
        level = 0;
      }
      if( level > peak[ channel ] ) peak[ channel ] = level;

      for( side = 0; side < 2; side++ ) {
        int heard = output * mix_sides[ channel ][ side ] / 100;
        if( heard != c->previous[ channel ][ side ] ) {
          blip_synth_update( synths[ channel ][ side ], f, heard );
          c->previous[ channel ][ side ] = heard;
        }
      }
    }

    clock_noise( c, noise_count );
  }

  /* Writes after the last step */
  while( position < count ) {
    chip_write( c, writes[ position ].reg, writes[ position ].value );
    position++;
  }
  next_tick = f - AYSOURCE_FRAME_TSTATES;

  /* A level that keeps changing without tone or noise is heard (digital
     music), one that stays is not: the meters see how much it changed */
  for( channel = 0; channel < CHANNELS; channel++ )
    if( dc_high[ channel ] - dc_low[ channel ] > peak[ channel ] )
      peak[ channel ] = dc_high[ channel ] - dc_low[ channel ];

  for( side = 0; side < 2; side++ )
    blip_buffer_end_frame( buffers[ side ], AYSOURCE_FRAME_TSTATES );

  avail = blip_buffer_samples_avail( buffers[0] );
  if( avail > PENDING ) avail = PENDING;
  n0 = blip_buffer_read_samples( buffers[0], pending[0], avail, 0 );
  n1 = blip_buffer_read_samples( buffers[1], pending[1], avail, 0 );
  pending_count = n0 < n1 ? n0 : n1;
  pending_position = 0;
}

void
ayplay_render( short *dest, int frames, int channels, int rate )
{
  int i, side;

  if( !active || paused || rate <= 0 ) return;

  LOCK();
  if( !active || paused || !source ) {
    UNLOCK();
    return;
  }
  if( rate != buffer_rate && buffers_new( rate ) ) {
    buffers_free();
    UNLOCK();
    return;
  }

  for( i = 0; i < frames; i++ ) {
    double out[ 2 ];

    if( pending_position >= pending_count ) render_frame();
    for( side = 0; side < 2; side++ ) {
      double in = pending_position < pending_count ?
                  pending[ side ][ pending_position ] : 0;
      out[ side ] = dc_filter_apply( &dc[ side ], in );
    }
    if( pending_position < pending_count ) pending_position++;

    if( channels == 1 ) {
      double v = *dest + ( out[0] + out[1] ) / 2;
      *dest++ = v > 32767 ? 32767 : v < -32768 ? -32768 : (short)v;
    } else {
      for( side = 0; side < 2; side++ ) {
        double v = *dest + out[ side ];
        *dest++ = v > 32767 ? 32767 : v < -32768 ? -32768 : (short)v;
      }
      for( side = 2; side < channels; side++ ) dest++;
    }
  }
  UNLOCK();
}

/* Without sound output: the song goes on silently, a frame at a time */
void
ayplay_silent_frame( void )
{
  if( !active || paused ) return;
  LOCK();
  if( active && !paused && source ) {
    int count = aysource_frame( source, writes, MAX_WRITES ), i, channel;
    int dc_low[ CHANNELS ], dc_high[ CHANNELS ];

    for( channel = 0; channel < CHANNELS; channel++ ) {
      dc_low[ channel ] = 16;
      dc_high[ channel ] = -1;
    }
    for( i = 0; i < count; i++ ) {
      chip_write( &ay, writes[i].reg, writes[i].value );
      /* Without tone and noise: how much the level changes (digital
         music), as when it is played */
      channel = writes[i].reg - 8;
      if( channel >= 0 && channel < CHANNELS &&
          ( ( ay.registers[7] >> channel ) & 9 ) == 9 ) {
        int volume = ay.registers[ 8 + channel ];
        int level = volume & 16 ? 15 : volume & 15;
        if( level < dc_low[ channel ] ) dc_low[ channel ] = level;
        if( level > dc_high[ channel ] ) dc_high[ channel ] = level;
      }
    }
    /* The meters see the levels set */
    for( channel = 0; channel < CHANNELS; channel++ ) {
      int volume = ay.registers[ 8 + channel ];
      int level = volume & 16 ? 15 : volume & 15;
      if( ( ( ay.registers[7] >> channel ) & 9 ) == 9 ) level = 0;
      if( dc_high[ channel ] - dc_low[ channel ] > level )
        level = dc_high[ channel ] - dc_low[ channel ];
      if( level > peak[ channel ] ) peak[ channel ] = level;
    }
    song_frame();
    pending_count = pending_position = 0;
  }
  UNLOCK();
}

/* ------------------------------------------------------------------------
   Controlling it
   ------------------------------------------------------------------------ */

void
ayplay_update_mix( void )
{
  int pan[ CHANNELS ], channel;
  int stereo = sound_ay_stereo_setting();
  int volume[ CHANNELS ];

  volume[0] = settings_current.volume_ay_a;
  volume[1] = settings_current.volume_ay_b;
  volume[2] = settings_current.volume_ay_c;
  for( channel = 0; channel < CHANNELS; channel++ )
    pan[ channel ] = sound_ay_channel_pan( stereo, channel );

  LOCK();
  mix_volume = settings_current.volume_ay < 0 ? 0 :
               settings_current.volume_ay > 100 ? 100 :
               settings_current.volume_ay;
  for( channel = 0; channel < CHANNELS; channel++ ) {
    mix_channel_volume[ channel ] = volume[ channel ] < 0 ? 0 :
                                    volume[ channel ] > 100 ? 100 :
                                    volume[ channel ];
    /* The side away from the panorama gets quieter */
    mix_sides[ channel ][0] = pan[ channel ] <= 0 ? 100 :
                              100 - pan[ channel ];
    mix_sides[ channel ][1] = pan[ channel ] >= 0 ? 100 :
                              100 + pan[ channel ];
  }
  UNLOCK();
}

static void
free_song( void )
{
  aysource_free( source );
  source = NULL;
  free( song_data );
  song_data = NULL;
  song_length = 0;
}

int
ayplay_start( const unsigned char *data, size_t length, int song,
              long frames, long loop, int is_default )
{
  aysource *new_source = aysource_new( data, length, song );
  unsigned char *copy = malloc( length ? length : 1 );
  int channel;

  if( !new_source || !copy ) {
    aysource_free( new_source );
    free( copy );
    return 1;
  }
  memcpy( copy, data, length );
  ayplay_update_mix();

  LOCK();
  free_song();
  source = new_source;
  song_data = copy;
  song_length = length;
  song_number = song;
  song_frames = frames;
  song_loop = loop;
  length_default = is_default;
  frames_played = 0;
  fade = 100;
  ended = 0;
  paused = 0;
  levels_init();
  chip_reset( &ay );
  next_tick = 0;
  pending_count = pending_position = 0;
  for( channel = 0; channel < CHANNELS; channel++ ) peak[ channel ] = 0;
  /* New buffers at the first samples: nothing of the last song is left */
  buffers_free();
  active = 1;
  UNLOCK();

  ayplay_output_start();
  return 0;
}

void
ayplay_stop( void )
{
  ayplay_output_stop();

  LOCK();
  active = 0;
  paused = 0;
  ended = 0;
  free_song();
  buffers_free();
  UNLOCK();
}

int ayplay_active( void ) { return active; }
int ayplay_paused( void ) { return active && paused; }
int ayplay_ended( void ) { return active && ended; }

void
ayplay_set_paused( int pause )
{
  paused = pause ? 1 : 0;
}

void
ayplay_set_length( long frames, long loop, int is_default )
{
  LOCK();
  song_frames = frames;
  song_loop = loop;
  length_default = is_default;
  if( song_frames && frames_played >= song_frames ) {
    if( repeat && song_loop >= 0 ) frames_played = song_loop;
    else ended = 1;
  }
  UNLOCK();
}

void
ayplay_set_repeat( int on )
{
  LOCK();
  repeat = on ? 1 : 0;
  UNLOCK();
}

long
ayplay_frames_played( void )
{
  long frames;

  LOCK();
  frames = active ? frames_played : 0;
  UNLOCK();
  return frames;
}

int
ayplay_seek( long frame )
{
  aysource *new_source;
  const unsigned char *registers;
  unsigned char *data;
  size_t length;
  int song, reg;
  long i;

  if( !active ) return 1;
  if( frame < 0 ) frame = 0;

  /* The song from the start, run there apart (while the music goes on),
     then put in place of the one playing */
  LOCK();
  data = malloc( song_length ? song_length : 1 );
  if( data ) memcpy( data, song_data, song_length );
  length = song_length;
  song = song_number;
  if( song_frames && frame >= song_frames ) frame = song_frames - 1;
  UNLOCK();
  if( !data ) return 1;

  new_source = aysource_new( data, length, song );
  free( data );
  if( !new_source ) return 1;
  for( i = 0; i < frame; i++ ) aysource_frame( new_source, NULL, 0 );

  LOCK();
  if( !active ) {
    UNLOCK();
    aysource_free( new_source );
    return 1;
  }
  aysource_free( source );
  source = new_source;
  frames_played = frame;
  ended = 0;
  fade = 100;
  /* The sound generator from the registers the song has there */
  chip_reset( &ay );
  registers = aysource_registers( source );
  for( reg = 0; reg < 14; reg++ ) chip_write( &ay, reg, registers[ reg ] );
  pending_count = pending_position = 0;
  UNLOCK();
  return 0;
}

void
ayplay_set_muted( int channel, int muted )
{
  if( channel < 0 || channel >= CHANNELS ) return;
  mix_muted[ channel ] = muted ? 1 : 0;
}

int
ayplay_muted( int channel )
{
  return channel >= 0 && channel < CHANNELS && mix_muted[ channel ];
}

int
ayplay_channel_peak( int channel )
{
  int level;

  if( channel < 0 || channel >= CHANNELS ) return 0;
  level = peak[ channel ];
  peak[ channel ] = 0;
  return level;
}

#ifndef HAVE_WASAPI_AUDIO

/* The music is mixed into the sound of the emulator (sound.c) */
int ayplay_output_start( void ) { return 0; }
void ayplay_output_stop( void ) { }
int ayplay_output_independent( void ) { return 0; }

#endif

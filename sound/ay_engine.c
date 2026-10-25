/* ay_engine.c: AY-3-8912 sound generation
   Copyright (c) 2000-2026 Russell Marks, Matan Ziv-Av, Philip Kendall,
                           Fredrick Meunier, Patrik Rak

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/

#include "config.h"

#include <string.h>

#include "machine.h"
#include "periph.h"
#include "peripherals/sound/ay.h"
#include "sound.h"
#include "sound/audio_timeline.h"
#include "sound/ay_engine.h"

/* A frame cannot normally contain this many AY writes, but retaining a large
 * fixed queue avoids dropping deliberately dense sample playback. */
#define AY_CHANGE_MAX 8000
#define AY_CHANNELS 3
/* The chip has 16 distinct volume levels and 16 envelope steps. */
#define AY_ENV_STEPS 16
#define AY_ENV_CONT 8
#define AY_ENV_ATTACK 4
#define AY_ENV_ALT 2
#define AY_ENV_HOLD 1
/* Tone and noise divide the AY clock by 16. Spectrum machines and clones
 * normally derive that clock by dividing the processor clock by two. */
#define AY_CLOCK_DIVISOR 16
#define AY_CLOCK_RATIO 2
/* All three channels together approximately match the ULA beeper. The total
 * must fit the historical 7-bit mix: 50 + 2 + 3 * 24 = 124. */
#define AMPL_AY_TONE ( 24 * 256 )

struct ay_change {
  libspectrum_dword tstates;
  unsigned char reg, val;
};

static struct ay_change changes[AY_CHANGE_MAX];
static int change_count, change_position;
/* Next tick in the current frame; carry its phase across video boundaries. */
static libspectrum_dword next_tick;
static int previous[AY_CHANNELS];
static libspectrum_byte registers[AY_REGISTERS];
static unsigned int levels[AY_ENV_STEPS];
static unsigned int tone_tick[AY_CHANNELS], tone_high[AY_CHANNELS];
static unsigned int tone_period[AY_CHANNELS], noise_period, env_period;
static unsigned int noise_tick, tone_cycles, env_cycles;
static unsigned int env_internal_tick, env_tick;
static int rng = 1, noise_toggle, env_first = 1, env_reverse;
static int env_counter = AY_ENV_STEPS - 1;
static Blip_Synth *synths[AY_CHANNELS];
static Blip_Synth *right_synths[AY_CHANNELS];
/* Channels heard on both sides (the middle one of a stereo mode) */
static int both_sides[AY_CHANNELS];
/* AY volume in percent; applied to the amplitudes so that it can be changed
   while playing (changing a Blip_Synth's volume would leave a DC step) */
static int ay_volume = 100;
/* Fading out, in percent of the volume */
static int ay_fade = 100;
/* Channels not heard (the level meters still see them) */
static int channel_muted[AY_CHANNELS];
/* Highest level (0-15) of each channel since the last query */
static int meter_peak[AY_CHANNELS];

static void
ay_state_reset( void )
{
  int i;

  noise_tick = noise_period = 0;
  env_internal_tick = env_tick = env_period = 0;
  tone_cycles = env_cycles = 0;
  for( i = 0; i < AY_CHANNELS; i++ ) {
    tone_tick[i] = tone_high[i] = 0;
    tone_period[i] = 1;
  }
  change_count = change_position = 0;
  next_tick = 0;
  memset( previous, 0, sizeof( previous ) );
}

static void
ay_levels_init( void )
{
  /* Based on Matthew Westcott's December 2001 comp.sys.sinclair
   * measurements, with Russell Marks's follow-up adjustment intended
   * to model the AY as used in a Spectrum, then scaled to the internal
   * amplitude range. */
  static const int measured_levels[AY_ENV_STEPS] = {
    0x0000, 0x0385, 0x053D, 0x0770, 0x0AD7, 0x0FD5, 0x15B0, 0x230C,
    0x2B4C, 0x43C1, 0x5A4B, 0x732F, 0x9204, 0xAFF1, 0xD921, 0xFFFF
  };
  int i;

  for( i = 0; i < AY_ENV_STEPS; i++ )
    levels[i] = ( measured_levels[i] * AMPL_AY_TONE + 0x8000 ) / 0xffff;
}

int
ay_engine_init( int volume, int stereo )
{
  int i;

  change_position = 0;
  next_tick = 0;
  memset( previous, 0, sizeof( previous ) );
  ay_levels_init();
  ay_engine_set_volume( volume );
  /* Every channel has a right side synth too, so the stereo mode can be
     changed while playing */
  for( i = 0; i < AY_CHANNELS; i++ ) {
    synths[i] = new_Blip_Synth();
    blip_synth_set_volume( synths[i], 1.0 );
    blip_synth_set_treble_eq( synths[i], 0.0 );
    right_synths[i] = new_Blip_Synth();
    blip_synth_set_volume( right_synths[i], 1.0 );
    blip_synth_set_treble_eq( right_synths[i], 0.0 );
    both_sides[i] = 0;
    meter_peak[i] = 0;
  }
  ( void )stereo;
  return 0;
}

void
ay_engine_set_fade( int percent )
{
  ay_fade = percent < 0 ? 0 : percent > 100 ? 100 : percent;
}

void
ay_engine_set_volume( int volume )
{
  ay_volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
}

/* The channels on the left, in the middle and on the right */
void
ay_engine_stereo_order( int stereo, int order[ AY_CHANNELS ] )
{
  switch( stereo ) {
  case SOUND_STEREO_AY_ACB: order[0] = 0; order[1] = 2; order[2] = 1; break;
  case SOUND_STEREO_AY_BAC: order[0] = 1; order[1] = 0; order[2] = 2; break;
  default:                  order[0] = 0; order[1] = 1; order[2] = 2; break;
  }
}

void
ay_engine_set_channel_muted( int channel, int muted )
{
  if( channel >= 0 && channel < AY_CHANNELS )
    channel_muted[ channel ] = muted ? 1 : 0;
}

int
ay_engine_channel_muted( int channel )
{
  return channel >= 0 && channel < AY_CHANNELS && channel_muted[ channel ];
}

int
ay_engine_channel_peak( int channel )
{
  int peak;

  if( channel < 0 || channel >= AY_CHANNELS ) return 0;
  peak = meter_peak[ channel ];
  meter_peak[ channel ] = 0;
  return peak;
}

void
ay_engine_end( void )
{
  int i;
  for( i = 0; i < AY_CHANNELS; i++ ) {
    delete_Blip_Synth( &synths[i] );
    delete_Blip_Synth( &right_synths[i] );
  }
}

void
ay_engine_set_outputs( Blip_Buffer *left, Blip_Buffer *right, int stereo )
{
  int order[ AY_CHANNELS ];
  int i;

  /* Take every channel back to zero where it was, at the start of the
     current interval, so that moving it leaves no DC step behind */
  for( i = 0; i < AY_CHANNELS; i++ ) {
    if( synths[i] && synths[i]->impl.buf && synths[i]->impl.last_amp )
      blip_synth_update( synths[i], 0, 0 );
    if( right_synths[i] && right_synths[i]->impl.buf &&
        right_synths[i]->impl.last_amp )
      blip_synth_update( right_synths[i], 0, 0 );
    both_sides[i] = 0;
  }
  memset( previous, 0, sizeof( previous ) );

  if( stereo == SOUND_STEREO_AY_NONE || !right ) {
    for( i = 0; i < AY_CHANNELS; i++ ) {
      blip_synth_set_output( synths[i], left );
      blip_synth_set_output( right_synths[i], left );
    }
    return;
  }

  ay_engine_stereo_order( stereo, order );
  blip_synth_set_output( synths[ order[0] ], left );
  blip_synth_set_output( right_synths[ order[0] ], right );
  blip_synth_set_output( synths[ order[1] ], left );
  blip_synth_set_output( right_synths[ order[1] ], right );
  both_sides[ order[1] ] = 1;
  blip_synth_set_output( synths[ order[2] ], right );
  blip_synth_set_output( right_synths[ order[2] ], left );
}

static void
ay_apply_change( const struct ay_change *change )
{
  int reg = change->reg;
  int channel;

  registers[reg] = change->val;
  if( reg <= 5 ) {
    channel = reg >> 1;
    tone_period[channel] = registers[reg & ~1] |
                           ( registers[reg | 1] & 15 ) << 8;
    if( !tone_period[channel] ) tone_period[channel] = 1;
    /* Keep phase bounded when a period changes. Getting this wrong makes the
     * vibrato in games such as Ghouls 'n' Ghosts sound scratchy. */
    if( tone_tick[channel] >= tone_period[channel] * 2 )
      tone_tick[channel] %= tone_period[channel] * 2;
  } else if( reg == 6 ) {
    noise_tick = 0;
    noise_period = registers[6] & 31;
  } else if( reg == 11 || reg == 12 ) {
    env_period = registers[11] | ( registers[12] << 8 );
  } else if( reg == 13 ) {
    env_internal_tick = env_tick = env_cycles = 0;
    env_first = 1;
    env_reverse = 0;
    env_counter = ( registers[13] & AY_ENV_ATTACK ) ? 0 : AY_ENV_STEPS - 1;
  }
}

static void
ay_finish_envelope_cycle( int shape )
{
  if( !( shape & AY_ENV_CONT ) ) {
    env_counter = 0;
  } else if( shape & AY_ENV_HOLD ) {
    if( env_first && ( shape & AY_ENV_ALT ) )
      env_counter = env_counter ? 0 : AY_ENV_STEPS - 1;
  } else if( shape & AY_ENV_ALT ) {
    env_reverse = !env_reverse;
  } else {
    env_counter = ( shape & AY_ENV_ATTACK ) ? 0 : AY_ENV_STEPS - 1;
  }
  env_first = 0;
}

static void
ay_advance_envelope( int shape )
{
  if( env_first || ( ( shape & AY_ENV_CONT ) &&
                     !( shape & AY_ENV_HOLD ) ) ) {
    env_counter += env_reverse ?
                   ( ( shape & AY_ENV_ATTACK ) ? -1 : 1 ) :
                   ( ( shape & AY_ENV_ATTACK ) ? 1 : -1 );
    if( env_counter < 0 ) env_counter = 0;
    if( env_counter >= AY_ENV_STEPS ) env_counter = AY_ENV_STEPS - 1;
  }

  env_internal_tick++;
  while( env_internal_tick >= AY_ENV_STEPS ) {
    env_internal_tick -= AY_ENV_STEPS;
    ay_finish_envelope_cycle( shape );
  }
}

static int
ay_clock_envelope( int shape )
{
  int noise_count = 0;

  env_cycles += AY_CLOCK_DIVISOR;
  while( env_cycles >= AY_CLOCK_DIVISOR ) {
    env_cycles -= AY_CLOCK_DIVISOR;
    noise_count++;
    env_tick++;
    while( env_tick >= env_period ) {
      env_tick -= env_period;
      ay_advance_envelope( shape );
      if( !env_period ) break;
    }
  }
  return noise_count;
}

static int
ay_channel_output( int channel, int envelope_level, unsigned int tone_count )
{
  /* Volume bit 4 selects the envelope; otherwise the low nibble selects a
   * fixed level. With both tone and noise disabled, the selected level passes
   * through unchanged, which software uses for sample playback. */
  int volume = registers[8 + channel];
  int output = volume & 16 ? envelope_level : levels[volume & 15];
  int mixer = registers[7];

  if( !( mixer & ( 1 << channel ) ) ) {
    tone_tick[channel] += tone_count;
    if( tone_tick[channel] >= tone_period[channel] ) {
      tone_tick[channel] -= tone_period[channel];
      tone_high[channel] = !tone_high[channel];
    }
    if( !tone_high[channel] ) output = 0;
  }
  if( !( mixer & ( 0x08 << channel ) ) && noise_toggle ) output = 0;
  if( channel_muted[channel] ) return 0;
  return output * ay_volume * ay_fade / 10000;
}

static void
ay_emit_channel( int channel, libspectrum_dword at_tstates, int output,
                 int *previous )
{
  if( *previous == output ) return;
  blip_synth_update( synths[channel], at_tstates, output );
  if( both_sides[channel] )
    blip_synth_update( right_synths[channel], at_tstates, output );
  *previous = output;
}

static void
ay_clock_noise( int count )
{
  noise_tick += count;
  while( noise_tick >= noise_period ) {
    noise_tick -= noise_period;
    /* The 17-bit LFSR exposes bit 0 and feeds back bit 0 XOR bit 1. The mask
     * applies the taps at bits 14 and 17 without unpredictable branches. */
    noise_toggle ^= ( rng ^ ( rng >> 1 ) ) & 1;
    rng = ( rng >> 1 ) ^ ( 0x12000 & -( rng & 1 ) );
    if( !noise_period ) break;
  }
}

void
ay_engine_render( libspectrum_dword endpoint )
{
  libspectrum_dword f;

  if( !( periph_is_active( PERIPH_TYPE_FULLER ) ||
         periph_is_active( PERIPH_TYPE_MELODIK ) ||
         machine_current->capabilities & LIBSPECTRUM_MACHINE_CAPABILITY_AY ) )
    return;

  for( f = next_tick; f < endpoint;
       f += AY_CLOCK_DIVISOR * AY_CLOCK_RATIO ) {
    unsigned int tone_count;
    int noise_count, channel;
    int envelope_level;

    while( change_position < change_count &&
           f >= changes[change_position].tstates )
      ay_apply_change( &changes[change_position++] );

    envelope_level = levels[env_counter];
    noise_count = ay_clock_envelope( registers[13] );
    tone_cycles += AY_CLOCK_DIVISOR;
    tone_count = tone_cycles >> 3;
    tone_cycles &= 7;

    for( channel = 0; channel < AY_CHANNELS; channel++ ) {
      /* Level meter: the selected level, nothing when both tone and noise
         are off for the channel */
      int volume = registers[8 + channel];
      int level = volume & 16 ? env_counter : volume & 15;
      if( ( ( registers[7] >> channel ) & 9 ) == 9 ) level = 0;
      if( level > meter_peak[channel] ) meter_peak[channel] = level;
    }

    for( channel = 0; channel < AY_CHANNELS; channel++ )
      ay_emit_channel(
        channel, sound_interval_time( f ),
        ay_channel_output( channel, envelope_level, tone_count ),
        &previous[channel] );
    ay_clock_noise( noise_count );
  }
  next_tick = f;
}

void
ay_engine_write( int reg, int val, libspectrum_dword now )
{
  /* Writes are queued with their tstate and applied by ay_engine_render(), so
   * sub-frame register changes retain their original timing. */
  if( change_count >= AY_CHANGE_MAX ) return;
  changes[change_count].tstates = now;
  changes[change_count].reg = reg & 15;
  changes[change_count].val = val;
  change_count++;
}

void
ay_engine_reset( void )
{
  int i;
  ay_state_reset();
  /* A reset must not replay time whose PCM has already been extracted.
     Retain the legacy frame-local tick lattice, not a lattice at the cut. */
  next_tick = ( ( sound_audio_position() +
                 AY_CLOCK_DIVISOR * AY_CLOCK_RATIO - 1 ) /
               ( AY_CLOCK_DIVISOR * AY_CLOCK_RATIO ) ) *
             ( AY_CLOCK_DIVISOR * AY_CLOCK_RATIO );
  memset( registers, 0, sizeof( registers ) );
  for( i = 0; i < AY_REGISTERS; i++ ) ay_engine_write( i, 0, 0 );
}

/* Test-support accessors used by the regression tests in
 * unittests/unittests.c: the frame-relative offset of the next AY
 * rendering tick and the engine's register copy. */
libspectrum_dword
ay_engine_next_tick_offset( void )
{
  return next_tick;
}

libspectrum_byte
ay_engine_register_value( int reg )
{
  return registers[ reg & 15 ];
}

void
ay_engine_end_frame( void )
{
  libspectrum_dword frame = machine_current->timings.tstates_per_frame;
  int i;
  /* Preserve master's continuous AY grid and unconsumed tail writes while
     rebasing the resumable renderer to the following video frame. */
  next_tick = next_tick >= frame ? next_tick - frame : 0;
  change_count -= change_position;
  if( change_count ) {
    memmove( changes, changes + change_position,
             change_count * sizeof( changes[0] ) );
    for( i = 0; i < change_count; i++ )
      changes[i].tstates = changes[i].tstates > frame ?
                          changes[i].tstates - frame : 0;
  }
  change_position = 0;
  memset( previous, 0, sizeof( previous ) );
}

/* source_synths.c: Peripheral sound source synthesis
   Copyright (c) 2000-2026 Russell Marks, Matan Ziv-Av, Philip Kendall,
                           Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/

#include "config.h"

#include <limits.h>

#include "fuse.h"
#include "machine.h"
#include "periph.h"
#include "sound.h"
#include "sound/source_synths.h"
#include "sound/audio_timeline.h"

static Blip_Synth *left_specdrum, *right_specdrum;
static Blip_Synth *left_covox, *right_covox;
static Blip_Synth *left_sp0256, *right_sp0256;
/* The volume (percent) each synth plays at: SpecDrum, Covox, uSpeech */
enum { SYNTH_SPECDRUM, SYNTH_COVOX, SYNTH_SP0256, SYNTHS };
static int synth_volume[ SYNTHS ];
/* The lowest and highest level of each since the last frame, for the
   level meters (a level that stays is not heard) */
static int level_low[ SYNTHS ], level_high[ SYNTHS ];
static const int synth_source[ SYNTHS ] = {
  SOUND_SOURCE_SPECDRUM, SOUND_SOURCE_COVOX, SOUND_SOURCE_USPEECH
};

static void
level_reset( void )
{
  int i;

  for( i = 0; i < SYNTHS; i++ ) {
    level_low[i] = INT_MAX;
    level_high[i] = INT_MIN;
  }
}

static void
level_seen( int synth, int level )
{
  if( level < level_low[ synth ] ) level_low[ synth ] = level;
  if( level > level_high[ synth ] ) level_high[ synth ] = level;
}

double
source_volume( int volume )
{
  if( volume < 0 ) return 0.0;
  if( volume > 100 ) return 1.0;
  return volume / 100.0;
}

static Blip_Synth *
source_synth_init( Blip_Buffer *output, int volume )
{
  Blip_Synth *synth = new_Blip_Synth();
  blip_synth_set_volume( synth, source_volume( volume ) );
  blip_synth_set_output( synth, output );
  blip_synth_set_treble_eq( synth, 0.0 );
  return synth;
}

void
source_synths_init( Blip_Buffer *left, Blip_Buffer *right, int stereo,
                    int specdrum_volume, int covox_volume,
                    int uspeech_volume )
{
  synth_volume[ SYNTH_SPECDRUM ] = specdrum_volume;
  synth_volume[ SYNTH_COVOX ] = covox_volume;
  synth_volume[ SYNTH_SP0256 ] = uspeech_volume;
  level_reset();

  left_specdrum = source_synth_init( left, specdrum_volume );
  left_covox = source_synth_init( left, covox_volume );
  left_sp0256 = source_synth_init( left, uspeech_volume );

  right_specdrum = right_covox = right_sp0256 = NULL;
  if( !stereo ) return;
  right_specdrum = source_synth_init( right, specdrum_volume );
  right_covox = source_synth_init( right, covox_volume );
  right_sp0256 = source_synth_init( right, uspeech_volume );
}

void
source_synths_end( void )
{
  delete_Blip_Synth( &left_specdrum );
  delete_Blip_Synth( &right_specdrum );
  delete_Blip_Synth( &left_covox );
  delete_Blip_Synth( &right_covox );
  delete_Blip_Synth( &left_sp0256 );
  delete_Blip_Synth( &right_sp0256 );
}

/* A synth to another volume at `time': its level goes back to zero at the
   old volume and up again at the new one, so no step is left behind */
static void
synth_volume_change( Blip_Synth *synth, blip_time_t time, int volume )
{
  int level;

  if( !synth ) return;
  level = synth->impl.last_amp;
  if( synth->impl.buf && level ) blip_synth_update( synth, time, 0 );
  blip_synth_set_volume( synth, source_volume( volume ) );
  if( synth->impl.buf && level ) blip_synth_update( synth, time, level );
}

void
source_synths_update_volumes( blip_time_t time )
{
  int i;

  for( i = 0; i < SYNTHS; i++ ) {
    int volume = sound_source_volume( synth_source[i] );
    if( volume == synth_volume[i] ) continue;
    synth_volume[i] = volume;
    switch( i ) {
    case SYNTH_SPECDRUM:
      synth_volume_change( left_specdrum, time, volume );
      synth_volume_change( right_specdrum, time, volume );
      break;
    case SYNTH_COVOX:
      synth_volume_change( left_covox, time, volume );
      synth_volume_change( right_covox, time, volume );
      break;
    default:
      synth_volume_change( left_sp0256, time, volume );
      synth_volume_change( right_sp0256, time, volume );
      break;
    }
  }
}

/* The level meters: how far each level went, as heard after the volume */
void
source_synths_end_frame( void )
{
  int i;

  for( i = 0; i < SYNTHS; i++ )
    if( level_high[i] > level_low[i] )
      sound_meter_level( synth_source[i],
                         ( level_high[i] - level_low[i] ) / 2 *
                         synth_volume[i] / 100 );
  level_reset();
}

void
source_synths_set_speech_output( Blip_Buffer *left, Blip_Buffer *right )
{
  blip_synth_set_output( left_sp0256, left );
  if( right_sp0256 ) blip_synth_set_output( right_sp0256, right );
}

/* SpecDrum, Covox and SP0256 provide already digitized waveforms; their
 * writes update simple level synths without an additional source model. */
void
sound_specdrum_write( libspectrum_word port GCC_UNUSED, libspectrum_byte val )
{
  if( periph_is_active( PERIPH_TYPE_SPECDRUM ) ) {
    machine_current->specdrum.specdrum_dac = val - 128;

    /* The synths don't exist while the sound is off (e.g. during
       fastloading), but loaders may still write to the port */
    if( sound_enabled ) {
      libspectrum_dword time = sound_interval_time( tstates );
      level_seen( SYNTH_SPECDRUM, ( val - 128 ) * 128 );
      blip_synth_update( left_specdrum, time, ( val - 128 ) * 128 );
      if( right_specdrum )
        blip_synth_update( right_specdrum, time, ( val - 128 ) * 128 );
    }
  }
}

void
sound_covox_write( libspectrum_word port GCC_UNUSED, libspectrum_byte val )
{
  if( periph_is_active( PERIPH_TYPE_COVOX_FB ) ||
      periph_is_active( PERIPH_TYPE_COVOX_DD ) ) {
    machine_current->covox.covox_dac = val;

    /* As for the SpecDrum: no synths while the sound is off */
    if( sound_enabled ) {
      libspectrum_dword time = sound_interval_time( tstates );
      level_seen( SYNTH_COVOX, val * 128 );
      blip_synth_update( left_covox, time, val * 128 );
      if( right_covox ) blip_synth_update( right_covox, time, val * 128 );
    }
  }
}

void
source_synths_sp0256_write( libspectrum_dword at_tstates,
                            libspectrum_signed_word val )
{
  libspectrum_dword time = sound_interval_time( at_tstates );
  level_seen( SYNTH_SP0256, val );
  blip_synth_update( left_sp0256, time, val );
  if( right_sp0256 ) blip_synth_update( right_sp0256, time, val );
}

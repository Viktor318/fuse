/* ay_engine.h: AY sound generation */

#ifndef FUSE_AY_ENGINE_H
#define FUSE_AY_ENGINE_H

#include "libspectrum.h"
#include "sound/blipbuffer.h"

int ay_engine_init( int volume, int stereo );
void ay_engine_end( void );
void ay_engine_set_outputs( Blip_Buffer *left, Blip_Buffer *right, int stereo );
/* Ordered frame-relative endpoints; end_frame rebases the continuous lattice. */
void ay_engine_render( libspectrum_dword endpoint );
void ay_engine_end_frame( void );
void ay_engine_write( int reg, int val, libspectrum_dword now );
void ay_engine_reset( void );

/* AY volume in percent, can be changed while playing */
void ay_engine_set_volume( int volume );
/* Fade out: percent of the volume (100: none) */
void ay_engine_set_fade( int percent );
/* The channels (0=A, 1=B, 2=C) on the left, in the middle and on the right
   in a stereo mode (in order A, B, C without stereo) */
void ay_engine_stereo_order( int stereo, int order[ 3 ] );
/* Silence a channel (0=A, 1=B, 2=C) while playing */
void ay_engine_set_channel_muted( int channel, int muted );
int ay_engine_channel_muted( int channel );
/* Volume of a channel (0=A, 1=B, 2=C) in percent */
void ay_engine_set_channel_volume( int channel, int percent );
/* A level (0-15) of a channel as heard after its volume */
int ay_engine_channel_level_heard( int channel, int level );
/* Highest level (0-15) of a channel since the last call, after the volume
   of the channel */
int ay_engine_channel_peak( int channel );

/* Test-support accessors used by the regression tests in
 * unittests/unittests.c. */
libspectrum_dword ay_engine_next_tick_offset( void );
libspectrum_byte ay_engine_register_value( int reg );

#endif

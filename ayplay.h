/* ayplay.h: Playing an AY song apart from the emulated machine
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

#ifndef FUSE_AYPLAY_H
#define FUSE_AYPLAY_H

#include <stddef.h>

/* The song runs on a processor of its own (aysource.c), frame by frame,
   whenever the sound output wants more samples, and has an AY sound
   generator of its own. The emulated machine goes on with what it was
   doing; its sound and the music are heard together.

   With WASAPI the music has a sound stream of its own (fed by a thread), so
   it goes on while the emulation is paused or held up (menus, dialogs,
   moving the window). Otherwise it is mixed into the sound of the emulator,
   frame by frame. */

/* Start song `song' (0 based) of the file (the data is copied). Length in
   frames (0: none), where the music goes on from after the end (-1: it
   ends), and whether the length is the default one */
int ayplay_start( const unsigned char *data, size_t length, int song,
                  long frames, long loop, int length_default );
void ayplay_stop( void );
int ayplay_active( void );

void ayplay_set_paused( int paused );
int ayplay_paused( void );

/* A length found meanwhile (by measuring) */
void ayplay_set_length( long frames, long loop, int length_default );
/* Songs that go round go on playing at the end, without a fade-out */
void ayplay_set_repeat( int repeat );

long ayplay_frames_played( void );
int ayplay_ended( void );

/* Go to a frame of the song (the song runs there without sound) */
int ayplay_seek( long frame );

/* Highest level (0-15) of a channel since the last call, before the volume
   of the channel */
int ayplay_channel_peak( int channel );

/* Silence a channel (0=A, 1=B, 2=C) of the music */
void ayplay_set_muted( int channel, int muted );
int ayplay_muted( int channel );

/* The volumes and the stereo places changed (in the settings) */
void ayplay_update_mix( void );

/* Add `frames' frames of the music (`channels' samples each, 1 or 2) at
   `rate' Hz to `dest' */
void ayplay_render( short *dest, int frames, int channels, int rate );

/* Without sound output: a frame of the song, silently */
void ayplay_silent_frame( void );

/* The output of the music: with WASAPI the stream of its own, otherwise the
   sound of the emulator */
int ayplay_output_start( void );
void ayplay_output_stop( void );
/* The music is heard even while the emulation does not run */
int ayplay_output_independent( void );

#endif			/* #ifndef FUSE_AYPLAY_H */

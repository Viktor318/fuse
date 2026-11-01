/* ymplayer.h: YM (ST-Sound) and VTX (Vortex) register dump player
   Copyright (c) 2026 Viktor (GitHub: Viktor318)
   The formats as ZXTune (https://zxtune.bitbucket.io/), Copyright (c)
   Vitamin/CAIG <vitamin.caig@gmail.com>, reads them
   (src/formats/chiptune/aym/ym_vtx.cpp, src/module/players/aym/ymvtx.cpp).

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

#ifndef FUSE_YMPLAYER_H
#define FUSE_YMPLAYER_H

#include <stddef.h>

/* A YM file (YM2!, YM3!, YM3b, YM5!, YM6!, usually packed into an LHA
   archive) or a VTX file (packed with -lh5-) holds the AY/YM registers of
   every frame, recorded on an Atari ST, an Amstrad CPC, a Spectrum...
   The frames are played at 50 Hz and the periods for the clock of the
   Spectrum's AY, whatever the chip clock and frame rate of the file.
   The digital drums and special effects of YM5/YM6 are not played. */

typedef struct ymplayer_module ymplayer_module;

#define YMPLAYER_REGISTERS 14

/* Whether the data looks like a YM (packed or not) / VTX file */
int ymplayer_check( const unsigned char *data, size_t length );

/* NULL if the data is not a YM or VTX file that can be played */
ymplayer_module *ymplayer_load( const unsigned char *data, size_t length );
void ymplayer_free( ymplayer_module *module );

/* The length in (50 Hz) frames, and the frame the music goes on from */
long ymplayer_frames( const ymplayer_module *module );
long ymplayer_loop( const ymplayer_module *module );

const char *ymplayer_title( const ymplayer_module *module );
const char *ymplayer_author( const ymplayer_module *module );
const char *ymplayer_comment( const ymplayer_module *module );

/* The stereo layout stored in a VTX file, as the names of the AY stereo
   modes of Fuse ("Mono", "ABC", "ACB", "BAC", "BCA", "CAB", "CBA"); "" for
   YM files */
const char *ymplayer_stereo( const ymplayer_module *module );

/* Back to the start */
void ymplayer_reset( ymplayer_module *module );

/* The registers of the next frame; *mask tells which ones to write
   (register 13 only when the file writes it). After the end the music
   goes on from the loop frame */
void ymplayer_frame( ymplayer_module *module,
                     unsigned char registers[ YMPLAYER_REGISTERS ],
                     unsigned int *mask );

#endif			/* #ifndef FUSE_YMPLAYER_H */

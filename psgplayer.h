/* psgplayer.h: PSG (Programmable Sound Generator) register dump player
   Copyright (c) 2026 Viktor (GitHub: Viktor318)
   The format as ZXTune (https://zxtune.bitbucket.io/), Copyright (c)
   Vitamin/CAIG <vitamin.caig@gmail.com>, reads it
   (src/formats/chiptune/aym/psg.cpp, src/module/players/aym/psg.cpp).

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

#ifndef FUSE_PSGPLAYER_H
#define FUSE_PSGPLAYER_H

#include <stddef.h>

/* A .psg file holds the AY register writes of every frame, as an emulator
   recorded them: "PSG", 1Ah, a 16 byte header, then FFh (a new frame),
   FEh n (4 * n frames), FDh (the end), or a register (0-15) and its
   value. It has no title or author, and its length is known exactly. */

typedef struct psgplayer_module psgplayer_module;

#define PSGPLAYER_REGISTERS 14

/* Whether the data looks like a PSG file */
int psgplayer_check( const unsigned char *data, size_t length );

/* NULL if the data is not a PSG file with any frames. The data is copied */
psgplayer_module *psgplayer_load( const unsigned char *data, size_t length );
void psgplayer_free( psgplayer_module *module );

/* The number of frames */
long psgplayer_frames( const psgplayer_module *module );

/* Back to the start */
void psgplayer_reset( psgplayer_module *module );

/* The registers of the next frame; *mask tells which ones were written
   (register 13 only when the file writes it, as writing it starts the
   envelope again). After the last frame nothing is written */
void psgplayer_frame( psgplayer_module *module,
                      unsigned char registers[ PSGPLAYER_REGISTERS ],
                      unsigned int *mask );

#endif			/* #ifndef FUSE_PSGPLAYER_H */

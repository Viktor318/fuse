/* ascplayer.h: ASC Sound Master (.asc, .as0) module player
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   Based on ZXTune (https://zxtune.bitbucket.io/), Copyright (c)
   Vitamin/CAIG <vitamin.caig@gmail.com>, GNU LGPL version 3.

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

#ifndef FUSE_ASCPLAYER_H
#define FUSE_ASCPLAYER_H

#include <stddef.h>

/* Both versions of the format share the module type and the player: v1.x
   (.asc, with a loop position in its header) and v0.x (.as0, without) */
typedef struct asc_module asc_module;

/* Whether the data looks like an ASC Sound Master v1.x / v0.x module (by
   its structure: there is no signature) */
int asc_check( const unsigned char *data, size_t length );
int asc0_check( const unsigned char *data, size_t length );

/* NULL if the data is not a module of that version that can be played */
asc_module *asc_load( const unsigned char *data, size_t length );
asc_module *asc0_load( const unsigned char *data, size_t length );
void asc_free( asc_module *module );

/* "" if the module has none */
const char *asc_title( const asc_module *module );
const char *asc_author( const asc_module *module );

/* The length in frames and the loop frame, as ZXTune tells them; the
   module is left at its start */
long asc_length( asc_module *module, long *loop );

void asc_reset( asc_module *module );

/* The registers of the next frame; *mask tells which ones to write */
void asc_frame( asc_module *module, unsigned char *registers,
                unsigned int *mask );

#endif			/* #ifndef FUSE_ASCPLAYER_H */

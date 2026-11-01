/* pt2player.h: Pro Tracker 2 (.pt2) module player
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

#ifndef FUSE_PT2PLAYER_H
#define FUSE_PT2PLAYER_H

#include <stddef.h>

typedef struct pt2_module pt2_module;

/* Whether the data looks like a Pro Tracker 2 module (by its header:
   the bytes ZXTune expects and the layout of the areas) */
int pt2_check( const unsigned char *data, size_t length );

/* NULL if the data is not a module that can be played */
pt2_module *pt2_load( const unsigned char *data, size_t length );
void pt2_free( pt2_module *module );

const char *pt2_title( const pt2_module *module );

/* The length in frames and the loop frame, as ZXTune tells them; the
   module is left at its start */
long pt2_length( pt2_module *module, long *loop );

void pt2_reset( pt2_module *module );

/* The registers of the next frame; *mask tells which ones to write */
void pt2_frame( pt2_module *module, unsigned char *registers,
                unsigned int *mask );

#endif			/* #ifndef FUSE_PT2PLAYER_H */

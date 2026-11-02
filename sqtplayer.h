/* sqtplayer.h: SQ-Tracker (compiled, .sqt) module player
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

#ifndef FUSE_SQTPLAYER_H
#define FUSE_SQTPLAYER_H

#include <stddef.h>

typedef struct sqt_module sqt_module;

/* Whether the data looks like a compiled SQ-Tracker module (by its
   header: the bytes ZXTune expects and the layout of the areas) */
int sqt_check( const unsigned char *data, size_t length );

/* NULL if the data is not a module that can be played */
sqt_module *sqt_load( const unsigned char *data, size_t length );
void sqt_free( sqt_module *module );

/* The format has no title: always "" */
const char *sqt_title( const sqt_module *module );

/* The length in frames and the loop frame, as ZXTune tells them; the
   module is left at its start */
long sqt_length( sqt_module *module, long *loop );

void sqt_reset( sqt_module *module );

/* The registers of the next frame; *mask tells which ones to write */
void sqt_frame( sqt_module *module, unsigned char *registers,
                unsigned int *mask );

#endif			/* #ifndef FUSE_SQTPLAYER_H */

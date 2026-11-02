/* gtrplayer.h: Global Tracker (.gtr) module player
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

#ifndef FUSE_GTRPLAYER_H
#define FUSE_GTRPLAYER_H

#include <stddef.h>

typedef struct gtr_module gtr_module;

/* Whether the data looks like a Global Tracker module (by the bytes of its
   header and the places of its parts) */
int gtr_check( const unsigned char *data, size_t length );

/* NULL if the data is not a module that can be played */
gtr_module *gtr_load( const unsigned char *data, size_t length );
void gtr_free( gtr_module *module );

/* The title ("" if none); the format has no author */
const char *gtr_title( const gtr_module *module );

/* The length in frames and the loop frame, as ZXTune tells them; the
   module is left at its start */
long gtr_length( gtr_module *module, long *loop );

void gtr_reset( gtr_module *module );

/* The registers of the next frame; *mask tells which ones to write */
void gtr_frame( gtr_module *module, unsigned char *registers,
                unsigned int *mask );

#endif			/* #ifndef FUSE_GTRPLAYER_H */

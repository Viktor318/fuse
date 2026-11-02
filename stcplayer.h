/* stcplayer.h: Sound Tracker module player (compiled .stc, uncompiled
   Sound Tracker 1 and Sound Tracker 3 modules)
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

#ifndef FUSE_STCPLAYER_H
#define FUSE_STCPLAYER_H

#include <stddef.h>

typedef struct stc_module stc_module;

/* Whether the data looks like a Sound Tracker module (by its structure:
   there is no signature) */
int stc_check( const unsigned char *data, size_t length );

/* NULL if the data is not a module that can be played */
stc_module *stc_load( const unsigned char *data, size_t length );

/* The same for an uncompiled Sound Tracker 1 module (as saved by the
   editor: no signature, a fixed layout) and a Sound Tracker 3 module
   (no signature either). The modules are played, told and freed by the
   stc_ functions below */
int st1_check( const unsigned char *data, size_t length );
stc_module *st1_load( const unsigned char *data, size_t length );
int st3_check( const unsigned char *data, size_t length );
stc_module *st3_load( const unsigned char *data, size_t length );

void stc_free( stc_module *module );

const char *stc_title( const stc_module *module );

/* The length in frames and the loop frame, as ZXTune tells them; the
   module is left at its start */
long stc_length( stc_module *module, long *loop );

void stc_reset( stc_module *module );

/* The registers of the next frame; *mask tells which ones to write */
void stc_frame( stc_module *module, unsigned char *registers,
                unsigned int *mask );

#endif			/* #ifndef FUSE_STCPLAYER_H */

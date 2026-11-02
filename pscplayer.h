/* pscplayer.h: Pro Sound Creator (.psc) module player
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

#ifndef FUSE_PSCPLAYER_H
#define FUSE_PSCPLAYER_H

#include <stddef.h>

typedef struct psc_module psc_module;

/* Whether the data looks like a Pro Sound Creator module (by the bytes of
   its header and the places of its parts) */
int psc_check( const unsigned char *data, size_t length );

/* NULL if the data is not a module that can be played */
psc_module *psc_load( const unsigned char *data, size_t length );
void psc_free( psc_module *module );

const char *psc_title( const psc_module *module );
const char *psc_author( const psc_module *module );

/* The length in frames and the loop frame, as ZXTune tells them; the
   module is left at its start */
long psc_length( psc_module *module, long *loop );

void psc_reset( psc_module *module );

/* The registers of the next frame; *mask tells which ones to write */
void psc_frame( psc_module *module, unsigned char *registers,
                unsigned int *mask );

#endif			/* #ifndef FUSE_PSCPLAYER_H */

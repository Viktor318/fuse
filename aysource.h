/* aysource.h: The AY registers of a song, frame by frame, without the
   emulated machine
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

#ifndef FUSE_AYSOURCE_H
#define FUSE_AYSOURCE_H

#include <stddef.h>

/* A song of an .ay file runs on a Z80 of its own (only memory and the AY
   ports, no screen, no contention); a Pro Tracker 3 module gives the
   registers itself. The frame is that of a Spectrum 128K */
#define AYSOURCE_FRAME_TSTATES 70908
#define AYSOURCE_CLOCK 3546900		/* processor clock; the AY: half */

typedef struct aysource aysource;

typedef struct aysource_write {
  unsigned int tstates;		/* in the frame */
  unsigned char reg, value;
} aysource_write;

/* Is this an .ay file / a module that can be played? */
int aysource_is_ay( const unsigned char *data, size_t length );

/* Song `song' (0 based) of the file; the data is not needed afterwards.
   NULL if it cannot be played */
aysource *aysource_new( const unsigned char *data, size_t length, int song );
void aysource_free( aysource *source );

/* Run a frame: the AY register writes with their time (at most `max'; NULL:
   none kept). Returns how many were kept */
int aysource_frame( aysource *source, aysource_write *writes, int max );

/* The registers (0-13) as the song left them */
const unsigned char *aysource_registers( const aysource *source );

#endif			/* #ifndef FUSE_AYSOURCE_H */

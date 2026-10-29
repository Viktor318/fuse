/* aymeasure.h: Measuring the length of an AY song in the background
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

#ifndef FUSE_AYMEASURE_H
#define FUSE_AYMEASURE_H

#include <stddef.h>

/* The song runs on a processor of its own (aysource.c) as fast as it can,
   in a thread of its own, while the AY registers of every frame are kept,
   until the music repeats (at most an hour of music) */

/* Start measuring song `song' (0 based) of the file (the data is copied);
   a measuring going on is given up. `id' tells the result apart */
void aymeasure_start( const unsigned char *data, size_t length, int song,
                      long id );
void aymeasure_stop( void );
int aymeasure_running( void );

/* Measuring `id' is over: 1, the length in frames (0: no repetition found)
   and where the music goes on from after the end (-1: it ends) */
int aymeasure_result( long id, long *frames, long *loop );

/* Measure now, without a thread. Returns the length (0: none found) */
long aymeasure_song( const unsigned char *data, size_t length, int song,
                     long *loop, volatile int *cancel );

#endif			/* #ifndef FUSE_AYMEASURE_H */

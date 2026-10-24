/* ayplayer.h: Play the songs of .ay (ZXAYEMUL) music files
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

#ifndef FUSE_AYPLAYER_H
#define FUSE_AYPLAYER_H

#include <stddef.h>

/* Is this the name of an .ay file? */
int ayplayer_is_ay_file( const char *filename );

/* Open a file and play its first song */
int ayplayer_open( const char *filename );

/* Stop, forget the file and go back to the previous machine */
void ayplayer_close( void );

/* Play song `song' (0 based) / stop playing */
int ayplayer_play( int song );
void ayplayer_stop( void );

/* Called once every frame */
void ayplayer_frame( void );

int ayplayer_is_open( void );
int ayplayer_song_count( void );
int ayplayer_current_song( void );	/* -1: stopped */
long ayplayer_song_frames( void );	/* 0: length not known */
long ayplayer_frames_played( void );
int ayplayer_song_ended( void );
const char *ayplayer_filename( void );
const char *ayplayer_author( void );
const char *ayplayer_misc( void );
void ayplayer_song_name( int song, char *text, size_t length );

#endif

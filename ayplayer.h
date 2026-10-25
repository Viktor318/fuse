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

/* Go to a frame of the current song: the emulation runs there without
   sound and without waiting (backwards: from the start of the song) */
int ayplayer_seek( long frame );
int ayplayer_seeking( void );
long ayplayer_seek_target( void );	/* -1: not seeking */

/* Called once every frame */
void ayplayer_frame( void );

/* The user interface can be called every frame (NULL: not) */
void ayplayer_set_frame_hook( void ( *hook )( void ) );

int ayplayer_is_open( void );
int ayplayer_song_count( void );
int ayplayer_current_song( void );	/* -1: stopped */
long ayplayer_song_frames( void );	/* 0: length not known */
long ayplayer_frames_played( void );
int ayplayer_song_ended( void );
/* The file gives no length: the default one (5 minutes) is used */
int ayplayer_length_is_default( void );
/* The length was measured (by the repetition of the music) */
int ayplayer_length_is_measured( void );

/* Measuring the length of a song, the first time it is played: the user
   interface shows the progress (0-100) and, when it is over, plays the
   song again with ayplayer_play() */
int ayplayer_scanning( void );
int ayplayer_scan_progress( void );
long ayplayer_scan_frames( void );
int ayplayer_scan_take_finished( void );	/* over since the last call? */
void ayplayer_scan_cancel( void );		/* plays without measuring */

/* The same song again at the end: a song whose music goes round just goes
   on playing (the elapsed time back to where the round starts); otherwise
   ayplayer_song_ended() and the user interface plays it again. When not
   repeating, songs that go round fade out at the end */
void ayplayer_set_repeat_song( int repeat );
const char *ayplayer_filename( void );
const char *ayplayer_author( void );
const char *ayplayer_misc( void );
void ayplayer_song_name( int song, char *text, size_t length );

#endif

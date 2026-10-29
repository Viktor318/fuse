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

#include <libspectrum.h>

/* Load a file for playing (does not play anything) */
int ayplayer_load( const char *filename );
const char *ayplayer_path( void );	/* "" if none */

/* The songs of a file, without loading it for playing */
typedef char ayplayer_song_title[ 128 ];
typedef struct ayplayer_info {
  int songs;
  libspectrum_dword checksum;
  char author[ 256 ];
  ayplayer_song_title *names;
} ayplayer_info;
int ayplayer_file_info( const char *filename, ayplayer_info *info );
void ayplayer_file_info_free( ayplayer_info *info );

/* The length measured earlier of a song (by file checksum): 1 if known;
   frames 0: no repetition found (default length) */
int ayplayer_known_length( libspectrum_dword checksum, int song,
                           long *frames );

/* Stop and forget the file */
void ayplayer_close( void );

/* Play song `song' (0 based) / stop playing. The song plays apart from the
   emulated machine (ayplay.c), which goes on with what it was doing */
int ayplayer_play( int song );
void ayplayer_stop( void );
void ayplayer_set_paused( int paused );
int ayplayer_paused( void );

/* Go to a frame of the current song */
int ayplayer_seek( long frame );

/* The music is mixed into the sound of the emulator and plays: the
   emulation has to go on (in menus, while moving the window...) */
int ayplayer_needs_emulation( void );

/* Called once every frame */
void ayplayer_frame( void );

/* The user interface can be called every frame (NULL: not) */
void ayplayer_set_frame_hook( void ( *hook )( void ) );

/* Measuring the length of a song, the first time it is played, in the
   background while it plays. ayplayer_poll() is called often (it takes the
   result): 1 when a measuring is over */
int ayplayer_poll( void );
int ayplayer_measuring( void );
int ayplayer_measuring_current( void );	/* the song playing */
/* Measure a song of a file (playing nothing): 1 if it is known already or
   cannot be read */
int ayplayer_measure( const char *filename, int song );

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

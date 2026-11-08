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

/* The file types of the player, for file dialogs (.txt files are music
   only if they are Vortex Tracker modules) */
#define AY_MUSIC_TYPES \
  "*.ay;*.pt3;*.vt2;*.pt2;*.pt1;*.stc;*.st1;*.st3;*.stp;*.asc;*.as0;*.sqt;*.psc;*.psm;*.gtr;*.ftc;" \
  "*.psg;*.ym;*.vtx;*.ts"

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
  char misc[ 256 ];		/* as ayplayer_misc() */
  char comment[ 256 ];		/* as ayplayer_comment() */
  char format[ 64 ];		/* modules: as ayplayer_format(); "" */
  int chips;			/* modules: the AY chips (TurboSound); .ay: 1 */
  ayplayer_song_title *names;
} ayplayer_info;
int ayplayer_file_info( const char *filename, ayplayer_info *info );
void ayplayer_file_info_free( ayplayer_info *info );

/* The AY chips a file plays, without messages: a module (TurboSound: 2 or
   3) tells it, an .ay file counts as 1 (its songs may select a second one
   as they play); 0 if it cannot be read */
int ayplayer_file_chips( const char *filename );

/* The length measured earlier of a song (by file checksum): 1 if known;
   frames 0: no repetition found (default length) */
int ayplayer_known_length( libspectrum_dword checksum, int song,
                           long *frames );

/* The chips a song used, as measured with its length (1-3); 0 if not
   measured, or measured by a version that did not count them (one chip) */
int ayplayer_known_chips( libspectrum_dword checksum, int song );

/* Stop and forget the file */
void ayplayer_close( void );

/* Play song `song' (0 based) / stop playing. The song plays apart from the
   emulated machine (ayplay.c), which goes on with what it was doing */
int ayplayer_play( int song );
void ayplayer_stop( void );

/* The song is played for the TurboSound player (1) or the AY player (0):
   with the settings of its mixer */
void ayplayer_set_turbosound( int turbosound );
int ayplayer_turbosound( void );
/* The .mix file of the song playing, if they are loaded automatically
   (settings_current.ayplayer_mix_auto), else the settings of the user */
void ayplayer_song_mix( void );
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

/* Measure song `song' of the file again, also if its length is known; 0 if
   it started. The measuring going on is given up */
int ayplayer_remeasure( const char *filename, int song );
void ayplayer_measure_cancel( void );
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
/* The comment of the file (misc without the TurboSound chips) */
const char *ayplayer_comment( void );
/* The format of the loaded file: "AY", "PT3", "PT3 + PT3"... */
const char *ayplayer_format( void );
/* The chips the loaded module plays (TurboSound); .ay files: 1 */
int ayplayer_chips( void );
/* The stereo mode stored in the file (VTX), as a stereo setting value; ""
   if none */
const char *ayplayer_file_stereo( void );
void ayplayer_song_name( int song, char *text, size_t length );

#endif

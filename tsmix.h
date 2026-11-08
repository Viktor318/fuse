/* tsmix.h: The TurboSound mixer settings, and their .mix files
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

#ifndef FUSE_TSMIX_H
#define FUSE_TSMIX_H

#include <stddef.h>

#include "settings.h"

/* The songs of the TurboSound player are heard with settings of their own,
   apart from those of the AY mixer (which the AY player and the games
   use): a master volume, one stereo mode for all the chips, and for each
   channel of each chip whether it is heard, its volume and its panorama.
   They are settings of Fuse (tsmixer_* in fuse.cfg); a .mix file next to a
   song keeps them for that song, as with the AY mixer */

#define TSMIX_CHIPS 3
#define TSMIX_CHANNELS 3

/* The stereo mode, as SOUND_STEREO_AY_... and as its name ("ABC") */
int tsmix_stereo( void );
const char *tsmix_stereo_name( void );
int tsmix_master( void );
int tsmix_volume( int chip, int channel );
/* Where the channel is heard: -100 left .. 100 right */
int tsmix_pan( int chip, int channel );
int tsmix_on( int chip, int channel );

/* Changes; each is heard at once. A stereo mode other than Custom puts
   the panoramas of every chip where it puts the channels A, B and C;
   Custom keeps them where they are, to be set one by one */
void tsmix_set_stereo( const char *name );
void tsmix_set_master( int volume );
void tsmix_set_volume( int chip, int channel, int volume );
void tsmix_set_pan( int chip, int channel, int pan );
void tsmix_set_on( int chip, int channel, int on );
/* Every channel heard at 100%, the panoramas where the mode puts them */
void tsmix_reset( void );

/* Called after a change from anywhere */
void tsmix_set_changed_hook( void ( *hook )( void ) );

/* As aymix_song_started(), for a song of the TurboSound player */
void tsmix_song_started( const char *path, int song, int songs,
                         int automatic, const char *file_stereo );
void tsmix_restore_user( void );
/* A .mix file chosen by the user, used as that of a song would be */
int tsmix_load( const char *filename );
/* The name of the .mix file in use ("": the user's settings) */
const char *tsmix_loaded_name( void );
/* For saving the settings: the values of the user, not those of a .mix */
void tsmix_user_settings( settings_info *settings );

/* The settings as a .mix file: the master volume, and of each channel of
   the three chips whether it is heard, its volume and panorama; the keys
   of the first chip are those of the AY mixer */
int tsmix_save( const char *filename );

#endif			/* #ifndef FUSE_TSMIX_H */

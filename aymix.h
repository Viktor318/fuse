/* aymix.h: The AY mixer settings, and the .mix files of AY songs
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

#ifndef FUSE_AYMIX_H
#define FUSE_AYMIX_H

#include <stddef.h>

#include "settings.h"

/* The mixer settings are the stereo mode, the places of the channels in the
   custom mode, and the volume and panorama of each channel. They are
   settings of Fuse (games hear them too). A .mix file next to a song keeps
   them for that song; loaded, they are used until a song without one, and
   do not change the saved settings */

/* Changes; each is applied at once. Changing the stereo mode (or a place)
   puts the panoramas where the mode puts the channels */
void aymix_set_stereo( const char *name );
void aymix_set_place( int channel, int place );	/* SOUND_AY_LEFT... */
void aymix_set_volume( int channel, int volume );
void aymix_set_pan( int channel, int pan );
/* Volumes 100%, panoramas where the stereo mode puts the channels */
void aymix_reset( void );

/* Called after a change from anywhere, so that every window shows it */
void aymix_set_changed_hook( void ( *hook )( void ) );
void aymix_changed( void );

/* A song started: its .mix file is loaded if `automatic', otherwise (or
   without one) the settings of the user come back */
void aymix_song_started( const char *path, int song, int songs,
                         int automatic );
/* The settings of the user again (the player is closed, or automatic
   loading is turned off) */
void aymix_restore_user( void );
/* The name of the .mix file in use ("": none) */
const char *aymix_loaded_name( void );
/* For saving the settings: the values of the user, not those of a .mix */
void aymix_user_settings( settings_info *settings );

/* The .mix file offered for a song (the folder of the song, or the mix
   folder of Fuse when that one cannot be written) */
void aymix_default_path( const char *path, char *buffer, size_t length );
int aymix_save( const char *filename );

#endif			/* #ifndef FUSE_AYMIX_H */

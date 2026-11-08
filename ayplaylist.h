/* ayplaylist.h: The playlist of the AY player
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

#ifndef FUSE_AYPLAYLIST_H
#define FUSE_AYPLAYLIST_H

#include <libspectrum.h>

/* One song of an AY file */
typedef struct ayplaylist_entry {
  char *path;			/* full path of the file */
  int song;			/* 0 based */
  char title[ 128 ];
  char author[ 128 ];
  char misc[ 128 ];		/* as the player shows them */
  char comment[ 128 ];
  char format[ 32 ];		/* modules: "PT3", "PT3 + PT3"... */
  int chips;			/* TurboSound modules: 2 or 3; else 1 */
  libspectrum_dword checksum;	/* of the file */
} ayplaylist_entry;

/* There are two lists, each with a player window of its own: that of the
   AY player (music of one AY chip, and every .ay file) and that of the
   TurboSound player (modules playing two or three chips). The player
   (ayplayer.c) plays the song of one of them at a time */
typedef struct ayplaylist ayplaylist;

ayplaylist *ayplaylist_ay( void );
ayplaylist *ayplaylist_ts( void );
int ayplaylist_is_turbosound( const ayplaylist *list );

/* The list a music file belongs to */
ayplaylist *ayplaylist_for_file( const char *path );

/* A list is kept in fuse-ay-playlist.m3u (fuse-ts-playlist.m3u) in the
   settings folder, read the first time it is used and written whenever it
   changes */
int ayplaylist_count( ayplaylist *list );
const ayplaylist_entry *ayplaylist_get( ayplaylist *list, int index );

/* Add every song of a file at the end; returns the index of the first one
   or -1 (also if the file belongs to the other list, with a message) */
int ayplaylist_add_file( ayplaylist *list, const char *path );
/* Add the music files of a folder (those of the other list are left out);
   returns how many songs were added */
int ayplaylist_add_folder( ayplaylist *list, const char *folder );

void ayplaylist_remove( ayplaylist *list, int index );
void ayplaylist_clear( ayplaylist *list );
/* Move an entry one place up (-1) or down (+1); returns its new index */
int ayplaylist_move( ayplaylist *list, int index, int direction );

/* .m3u files: lines "path::AY,song" (as NEZplug), or just "path" for all
   the songs of a file */
int ayplaylist_load_m3u( ayplaylist *list, const char *path, int append );
int ayplaylist_save_m3u( ayplaylist *list, const char *path );

/* The entry being played, or chosen to be played (-1: none) */
int ayplaylist_current( ayplaylist *list );
void ayplaylist_set_current( ayplaylist *list, int index );

/* Play an entry: loads its file if needed (a song of the other list
   stops) */
int ayplaylist_play( ayplaylist *list, int index );

/* The player has the file of a song of this list (playing or stopped) /
   plays a song of this list */
int ayplaylist_owns_player( const ayplaylist *list );
int ayplaylist_playing( const ayplaylist *list );
/* The window of the list closes: the player stops and forgets its file if
   it is a song of this list */
void ayplaylist_close_player( ayplaylist *list );

/* The length of an entry if it was measured earlier (frames; 0: no
   repetition found, the default length); 0 if not known yet */
int ayplaylist_length( ayplaylist *list, int index, long *frames );

/* The channels of an entry: 3 for each AY chip (an .ay song using a second
   chip: once measured) */
int ayplaylist_channels( ayplaylist *list, int index );

/* File > Open of a music file: add its songs and play the first one */
int ayplaylist_open_file( ayplaylist *list, const char *path );

#endif

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
  libspectrum_dword checksum;	/* of the file */
} ayplaylist_entry;

/* The list is kept in fuse-ay-playlist.m3u in the settings folder, read
   the first time it is used and written whenever it changes */
int ayplaylist_count( void );
const ayplaylist_entry *ayplaylist_get( int index );

/* Add every song of a file at the end; returns the index of the first one
   or -1 */
int ayplaylist_add_file( const char *path );
/* Add the .ay files of a folder; returns how many songs were added */
int ayplaylist_add_folder( const char *folder );

void ayplaylist_remove( int index );
void ayplaylist_clear( void );
/* Move an entry one place up (-1) or down (+1); returns its new index */
int ayplaylist_move( int index, int direction );

/* .m3u files: lines "path::AY,song" (as NEZplug), or just "path" for all
   the songs of a file */
int ayplaylist_load_m3u( const char *path, int append );
int ayplaylist_save_m3u( const char *path );

/* The entry being played (-1: none) */
int ayplaylist_current( void );
void ayplaylist_set_current( int index );

/* Play an entry: loads its file if needed */
int ayplaylist_play( int index );

/* The length of an entry if it was measured earlier (frames; 0: no
   repetition found, the default length); 0 if not known yet */
int ayplaylist_length( int index, long *frames );

/* File > Open of an .ay file: add its songs and play the first one; if
   the file is in the list already, play it from there */
int ayplaylist_open_file( const char *path );

#endif

/* recent_files.h: The files opened lately (File > Recent files)
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

#ifndef FUSE_RECENT_FILES_H
#define FUSE_RECENT_FILES_H

/* The last files opened, newest first, kept in the recent_files setting
   (fuse.cfg; the paths apart by "|", which a Windows path can't contain).
   Each change is written to fuse.cfg at once, without the other settings
   changed since they were last saved */
#define RECENT_FILES_MAX 10

/* A file was opened: it goes to the top of the list */
void recent_files_add( const char *path );

int recent_files_count( void );
/* The path of entry `index' (0: the newest); NULL if there is none. Valid
   until the list changes */
const char *recent_files_get( int index );

void recent_files_clear( void );

/* Open entry `index' as File > Open would; a file that is not there any
   more is dropped from the list, with a message. 0 if it was opened */
int recent_files_open( int index );

/* Called after every change of the list (the user interface shows it) */
void recent_files_set_changed_hook( void ( *hook )( void ) );

#endif			/* #ifndef FUSE_RECENT_FILES_H */

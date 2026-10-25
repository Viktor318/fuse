/* winstate.h: What the Win32 user interface remembers by itself
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

#ifndef FUSE_WIN32_WINSTATE_H
#define FUSE_WIN32_WINSTATE_H

/* Places of the windows, kept in fuse-windows.ini (written at once when
   changed) */
int winstate_get( const char *section, const char *key, long *value );
void winstate_set( const char *section, const char *key, long value );
void winstate_remove( const char *section, const char *key );

#endif

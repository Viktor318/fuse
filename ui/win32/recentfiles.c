/* recentfiles.c: Win32 File > Recent files menu
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

/* The submenu comes from menu_data.dat with its last item, Clear list;
   the files above it are put in here whenever the list changes (and the
   texts translated here, not by win32i18n.c, as the items come and go) */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "i18n.h"
#include "ui/win32/menu_data.h"
#include "recent_files.h"
#include "win32internals.h"

/* The menu ids of the files (menu_data.h uses 100 and up, much fewer than
   these) */
#define IDM_RECENT_FIRST 880

/* A path longer than this is shortened in the middle */
#define SHOWN_LENGTH 60

static int
is_separator( char c )
{
  return c == '\\' || c == '/';
}

/* "C:\Spectrum\Games\Some folder\game.tzx" as "C:\Spectrum\...\game.tzx":
   the root and first folder, then as many of the last parts as fit (the
   file name always). Cut at separators only, so UTF-8 letters stay whole */
static void
shorten( const char *path, char *text, size_t size )
{
  size_t length = strlen( path ), head = 0, tail;
  const char *p;
  int separators = 0;

  if( length <= SHOWN_LENGTH ) {
    snprintf( text, size, "%s", path );
    return;
  }

  /* The head: up to the second separator after the root (\\server\share\
     counts as the root of a network path) */
  if( is_separator( path[0] ) && is_separator( path[1] ) ) separators = -2;
  for( p = path; *p; p++ )
    if( is_separator( *p ) && ++separators == 2 ) {
      head = p - path + 1;
      break;
    }

  /* The tail: the last parts that fit, at least the file name */
  tail = length;
  for( p = path + length - 1; p > path + head; p-- ) {
    if( !is_separator( *p ) ) continue;
    if( head + 4 + ( path + length - p ) > SHOWN_LENGTH &&
        tail != length )
      break;
    tail = p - path;
    if( head + 4 + ( length - tail ) > SHOWN_LENGTH ) break;
  }

  if( !head || tail <= head || tail == length ) {
    snprintf( text, size, "%s", path );
    return;
  }
  snprintf( text, size, "%.*s...%s", (int)head, path, path + tail );
}

/* A menu text: & as && (or it would be a mnemonic) */
static void
escape_ampersands( const char *text, char *menu_text, size_t size )
{
  size_t used = 0;

  for( ; *text && used + 2 < size; text++ ) {
    if( *text == '&' ) menu_text[ used++ ] = '&';
    menu_text[ used++ ] = *text;
  }
  menu_text[ used ] = '\0';
}

/* The submenu: the one ending with Clear list */
static HMENU
recent_menu( void )
{
  HMENU bar = GetMenu( fuse_hWnd ), file;
  int i, count;

  if( !bar || !( file = GetSubMenu( bar, 0 ) ) ) return NULL;
  count = GetMenuItemCount( file );
  for( i = 0; i < count; i++ ) {
    HMENU sub = GetSubMenu( file, i );
    if( sub && win32_recent_files_is_menu( sub ) ) return sub;
  }
  return NULL;
}

int
win32_recent_files_is_menu( HMENU menu )
{
  int count = GetMenuItemCount( menu );

  return count > 0 && GetMenuItemID( menu, count - 1 ) ==
                        IDM_MENU_FILE_RECENTFILES_CLEARLIST;
}

void
win32_recent_files_update( void )
{
  HMENU menu = recent_menu();
  int i, count = recent_files_count();
  char shown[ 512 ], escaped[ 1024 ], text[ 1100 ];

  if( !menu ) return;

  while( GetMenuItemCount( menu ) > 0 ) DeleteMenu( menu, 0, MF_BYPOSITION );

  /* &1 ... &9, 1&0 */
  for( i = 0; i < count; i++ ) {
    shorten( recent_files_get( i ), shown, sizeof( shown ) );
    escape_ampersands( shown, escaped, sizeof( escaped ) );
    if( i < 9 )
      _snprintf( text, sizeof( text ), "&%d  %s", i + 1, escaped );
    else
      _snprintf( text, sizeof( text ), "1&0  %s", escaped );
    text[ sizeof( text ) - 1 ] = '\0';
    AppendMenuA( menu, MF_STRING, IDM_RECENT_FIRST + i, text );
  }
  if( !count )
    AppendMenuA( menu, MF_STRING | MF_GRAYED, IDM_RECENT_FIRST,
                 i18n( "(empty)" ) );

  AppendMenuA( menu, MF_SEPARATOR, 0, NULL );
  AppendMenuA( menu, MF_STRING | ( count ? 0 : MF_GRAYED ),
               IDM_MENU_FILE_RECENTFILES_CLEARLIST, i18n( "&Clear list" ) );
}

void
win32_recent_files_init( void )
{
  recent_files_set_changed_hook( win32_recent_files_update );
  win32_recent_files_update();
}

int
win32_recent_files_command( WORD id )
{
  if( id < IDM_RECENT_FIRST || id >= IDM_RECENT_FIRST + RECENT_FILES_MAX )
    return 0;
  recent_files_open( id - IDM_RECENT_FIRST );
  return 1;
}

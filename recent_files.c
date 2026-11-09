/* recent_files.c: The files opened lately (File > Recent files)
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef WIN32
#include <direct.h>
#endif

#ifdef ENABLE_AUTOMATION
#include "automation/automation.h"
#endif
#include "compat.h"
#include "display.h"
#include "fuse.h"
#include "recent_files.h"
#include "settings.h"
#include "tape.h"
#include "ui/ui.h"
#include "utils.h"

#define SEPARATOR '|'

/* The list as read from the setting, made again when it changes */
static char *paths[ RECENT_FILES_MAX ];
static int count = 0;
static char *parsed = NULL;	/* the setting the list was made from */

static void ( *changed_hook )( void ) = NULL;

void
recent_files_set_changed_hook( void ( *hook )( void ) )
{
  changed_hook = hook;
}

static void
free_list( void )
{
  int i;

  for( i = 0; i < count; i++ ) libspectrum_free( paths[i] );
  count = 0;
}

/* The list from the setting, if that changed (it is read from fuse.cfg
   after Fuse has started) */
static void
parse( void )
{
  const char *setting = settings_current.recent_files ?
                        settings_current.recent_files : "";
  const char *start, *end;

  if( parsed && !strcmp( parsed, setting ) ) return;

  free_list();
  libspectrum_free( parsed );
  parsed = utils_safe_strdup( setting );

  for( start = setting; *start && count < RECENT_FILES_MAX; start = end ) {
    size_t length;

    end = strchr( start, SEPARATOR );
    if( !end ) end = start + strlen( start );
    length = end - start;
    if( length ) {
      paths[ count ] = libspectrum_new( char, length + 1 );
      memcpy( paths[ count ], start, length );
      paths[ count ][ length ] = '\0';
      count++;
    }
    if( *end ) end++;
  }
}

/* Write the list to the setting, and to fuse.cfg: into the settings as
   they were saved, so that other settings changed meanwhile are not saved
   by this */
static void
store( void )
{
  settings_info saved;
  size_t length = 1;
  char *setting;
  int i;

  for( i = 0; i < count; i++ ) length += strlen( paths[i] ) + 1;
  setting = libspectrum_new( char, length );
  setting[0] = '\0';
  for( i = 0; i < count; i++ ) {
    if( i ) strcat( setting, "|" );
    strcat( setting, paths[i] );
  }

  settings_set_string( &settings_current.recent_files, setting );
  libspectrum_free( parsed );
  parsed = setting;

  memset( &saved, 0, sizeof( saved ) );
  if( !settings_read_config( &saved ) ) {
    settings_set_string( &saved.recent_files, setting );
    settings_write_config( &saved );
  }
  settings_free( &saved );

  if( changed_hook ) changed_hook();
}

static int
same_path( const char *a, const char *b )
{
#ifdef WIN32
  /* Windows paths: case does not matter, \ and / are the same */
  for( ; *a && *b; a++, b++ ) {
    char x = *a, y = *b;
    if( x >= 'A' && x <= 'Z' ) x += 'a' - 'A';
    if( y >= 'A' && y <= 'Z' ) y += 'a' - 'A';
    if( x == '/' ) x = '\\';
    if( y == '/' ) y = '\\';
    if( x != y ) return 0;
  }
  return !*a && !*b;
#else
  return !strcmp( a, b );
#endif
}

void
recent_files_add( const char *path )
{
  char full[ PATH_MAX ];
  char *entry;
  int i;

  if( !path || !*path || strchr( path, SEPARATOR ) ) return;
  /* Not for test runs */
#ifdef ENABLE_AUTOMATION
  if( automation_active() ) return;
#endif
#ifdef UI_NULL
  if( settings_current.unittests ) return;
#endif

  /* A path relative to the current folder (the command line) is kept as
     a full one */
  if( !compat_is_absolute_path( path ) && getcwd( full, sizeof( full ) ) ) {
    size_t length = strlen( full );
    snprintf( full + length, sizeof( full ) - length, "%s%s",
              length && ( full[ length - 1 ] == '/' ||
                          full[ length - 1 ] == '\\' ) ?
                "" : FUSE_DIR_SEP_STR, path );
  } else {
    snprintf( full, sizeof( full ), "%s", path );
  }

  parse();

  /* Already there: moved to the top */
  for( i = 0; i < count; i++ )
    if( same_path( paths[i], full ) ) break;
  if( i < count ) {
    entry = paths[i];
  } else {
    if( count == RECENT_FILES_MAX ) libspectrum_free( paths[ --count ] );
    entry = utils_safe_strdup( full );
    i = count++;
  }
  memmove( &paths[1], &paths[0], i * sizeof( paths[0] ) );
  paths[0] = entry;

  store();
}

int
recent_files_count( void )
{
  parse();
  return count;
}

const char *
recent_files_get( int index )
{
  parse();
  return index >= 0 && index < count ? paths[ index ] : NULL;
}

void
recent_files_clear( void )
{
  parse();
  free_list();
  store();
}

static void
remove_entry( int index )
{
  libspectrum_free( paths[ index ] );
  memmove( &paths[ index ], &paths[ index + 1 ],
           ( count - index - 1 ) * sizeof( paths[0] ) );
  count--;
  store();
}

int
recent_files_open( int index )
{
  char *path;
  int error;

  parse();
  if( index < 0 || index >= count ) return 1;

  if( !compat_file_exists( paths[ index ] ) ) {
    ui_error( UI_ERROR_ERROR,
              "'%s' no longer exists; it was removed from the list",
              paths[ index ] );
    remove_entry( index );
    return 1;
  }

  /* Opening it changes the list (it goes to the top) */
  path = utils_safe_strdup( paths[ index ] );
  fuse_emulation_pause();
  error = utils_open_file( path, tape_can_autoload(), NULL );
  display_refresh_all();
  fuse_emulation_unpause();
  libspectrum_free( path );
  return error;
}

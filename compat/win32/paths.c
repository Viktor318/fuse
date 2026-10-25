/* paths.c: Path-related compatibility routines
   Copyright (c) 1999-2012 Philip Kendall

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

   Author contact information:

   E-mail: philip-fuse@shadowmagic.org.uk

*/

#include "config.h"

#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#endif				/* #ifdef HAVE_LIBGEN_H */
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "compat.h"
#include "fuse.h"
#include "ui/ui.h"

const char*
compat_get_temp_path( void )
{
  const char *dir;

  /* Something close to this algorithm specified at
     http://msdn.microsoft.com/en-us/library/windows/desktop/aa364992%28v=vs.85%29.aspx
  */
  dir = getenv( "TMP" ); if( dir ) return dir;
  dir = getenv( "TEMP" ); if( dir ) return dir;
  return ".";
}

/* Where earlier versions kept their files */
static const char*
old_config_path( void )
{
  const char *dir;
  dir = getenv( "USERPROFILE" ); if( dir ) return dir;
  dir = getenv( "WINDIR" ); if( dir ) return dir;
  return ".";
}

static void
move_file( const char *from_dir, const char *to_dir, const char *name )
{
  char from[ MAX_PATH ], to[ MAX_PATH ];

  snprintf( from, sizeof( from ), "%s\\%s", from_dir, name );
  snprintf( to, sizeof( to ), "%s\\%s", to_dir, name );
  if( GetFileAttributesA( from ) == INVALID_FILE_ATTRIBUTES ) return;
  if( GetFileAttributesA( to ) != INVALID_FILE_ATTRIBUTES ) return;
  MoveFileExA( from, to, MOVEFILE_COPY_ALLOWED );
}

/* The files of earlier versions from the user's folder into the new one */
static void
move_old_files( const char *to_dir )
{
  static const char * const names[] = {
    "fuse.cfg", "fuse-windows.ini", "fuse-window.txt",
    "fuse-keyboard-window.txt", "fuse_ay_lengths.txt",
    "fuse-ay-playlist.m3u", NULL
  };
  const char *from_dir = old_config_path();
  char pattern[ MAX_PATH ];
  WIN32_FIND_DATAA found;
  HANDLE find;
  int i;

  for( i = 0; names[i]; i++ ) move_file( from_dir, to_dir, names[i] );

  /* The log of a test version is not needed any more */
  snprintf( pattern, sizeof( pattern ), "%s\\fuse-ay-debug.log", from_dir );
  DeleteFileA( pattern );

  snprintf( pattern, sizeof( pattern ), "%s\\fuse-last-session.*",
            from_dir );
  find = FindFirstFileA( pattern, &found );
  if( find != INVALID_HANDLE_VALUE ) {
    do {
      if( !( found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) )
        move_file( from_dir, to_dir, found.cFileName );
    } while( FindNextFileA( find, &found ) );
    FindClose( find );
  }
}

/* The settings and every file Fuse keeps go to the Fuse folder in the
   user's Roaming folder (%APPDATA%\Fuse) */
const char*
compat_get_config_path( void )
{
  static char path[ MAX_PATH ];
  static int found = 0;
  const char *appdata;
  DWORD attributes;

  if( found ) return path;

  appdata = getenv( "APPDATA" );
  if( !appdata || !*appdata ) return old_config_path();

  snprintf( path, sizeof( path ), "%s\\Fuse", appdata );
  CreateDirectoryA( path, NULL );
  attributes = GetFileAttributesA( path );
  if( attributes == INVALID_FILE_ATTRIBUTES ||
      !( attributes & FILE_ATTRIBUTE_DIRECTORY ) )
    return old_config_path();

  move_old_files( path );
  found = 1;
  return path;
}

const char*
compat_get_fallback_config_path( void )
{
  return NULL;
}

int
compat_is_absolute_path( const char *path )
{
  if( path[0] == '\\' ) return 1;
  if( path[0] && path[1] == ':' ) return 1;
  return 0;
}

int
compat_get_next_path( path_context *ctx )
{
  char buffer[ PATH_MAX ];
  const char *path_segment, *path2;

  switch( ctx->type ) {
  case UTILS_AUXILIARY_LIB: path_segment = "lib"; break;
  case UTILS_AUXILIARY_ROM: path_segment = "roms"; break;
  case UTILS_AUXILIARY_WIDGET: path_segment = "ui/widget"; break;
  default:
    ui_error( UI_ERROR_ERROR, "unknown auxiliary file type %d", ctx->type );
    return 0;
  }

  switch( (ctx->state)++ ) {

    /* First look relative to the Fuse executable */
  case 0:
    if( compat_is_absolute_path( fuse_progname ) ) {
      strncpy( buffer, fuse_progname, PATH_MAX );
      buffer[ PATH_MAX - 1 ] = '\0';
    } else {
      DWORD retval; 
      retval = GetModuleFileName( NULL, buffer, PATH_MAX );
      if( !retval ) return 0;
    }

    path2 = dirname( buffer );
    snprintf( ctx->path, PATH_MAX, "%s" FUSE_DIR_SEP_STR "%s", path2,
              path_segment );
    return 1;

    /* Then relative to %APPDATA%/Fuse directory */
  case 1:
    path2 = getenv( "APPDATA" );
    if( !path2 ) return 0;
    snprintf( ctx->path, PATH_MAX, "%s" FUSE_DIR_SEP_STR "Fuse" 
              FUSE_DIR_SEP_STR "%s", path2, path_segment );

    return 1;

    /* Then relative to the current directory */
  case 2:
    snprintf( ctx->path, PATH_MAX, "." FUSE_DIR_SEP_STR "%s",
              path_segment );
    return 1;

  case 3: return 0;
  }

  ui_error( UI_ERROR_ERROR, "unknown path_context state %d", ctx->state );
  fuse_abort();
}

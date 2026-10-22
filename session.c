/* session.c: save the emulated machine on exit and restore it on start
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

/* The session is kept next to the settings file:

     fuse-last-session.szx   the machine
     fuse-last-session.tzx   the tape, if one was inserted
     fuse-last-session.txt   the tape's current block

   Snapshots don't hold the tape, so it is written separately. */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <libspectrum.h>

#include "compat.h"
#include "periph.h"
#include "session.h"
#include "settings.h"
#include "snapshot.h"
#include "tape.h"
#include "utils.h"
#include "ui/ui.h"

#define SESSION_NAME "fuse-last-session"

static int
session_path( char *path, size_t length, const char *extension )
{
  const char *dir = compat_get_config_path();

  if( !dir ) return 1;
  snprintf( path, length, "%s/" SESSION_NAME ".%s", dir, extension );
  return 0;
}

int
session_save( void )
{
  char snapshot[ PATH_MAX ], tape[ PATH_MAX ], info[ PATH_MAX ];
  FILE *f;

  if( !settings_current.resume_last_session ) return 0;

  if( session_path( snapshot, sizeof( snapshot ), "szx" ) ||
      session_path( tape, sizeof( tape ), "tzx" ) ||
      session_path( info, sizeof( info ), "txt" ) )
    return 1;

  if( snapshot_write( snapshot ) ) return 1;

  remove( tape );
  remove( info );

  if( tape_present() ) {
    int block = tape_get_current_block();

    if( tape_write( tape ) ) return 1;

    f = fopen( info, "w" );
    if( f ) {
      fprintf( f, "tape_block=%d\n", block < 0 ? 0 : block );
      fclose( f );
    }
  }

  return 0;
}

/* Read the session snapshot, but keep the peripherals the user has chosen.
   SZX snapshots don't record every peripheral (e.g. the Fuller Box,
   Melodik, Covox or an empty Interface 2), so a plain snapshot_read()
   would switch those off. */
static int
session_read_snapshot( const char *filename )
{
  utils_file file;
  libspectrum_snap *snap;
  int interface2 = settings_current.interface2;
  int error;

  if( utils_read_file( filename, &file ) ) return 1;

  snap = libspectrum_snap_alloc();
  error = libspectrum_snap_read( snap, file.buffer, file.length,
                                 LIBSPECTRUM_ID_UNKNOWN, filename );
  utils_close_file( &file );
  if( error ) { libspectrum_snap_free( snap ); return error; }

  libspectrum_snap_set_fuller_box_active( snap, settings_current.fuller );
  libspectrum_snap_set_melodik_active( snap, settings_current.melodik );
  libspectrum_snap_set_specdrum_active( snap, settings_current.specdrum );
  libspectrum_snap_set_covox_active( snap, settings_current.covox );
  libspectrum_snap_set_uspeech_active( snap, settings_current.uspeech );

  error = snapshot_copy_from( snap );
  libspectrum_snap_free( snap );
  if( error ) return error;

  if( interface2 && !settings_current.interface2 ) {
    settings_current.interface2 = 1;
    periph_update();
  }

  return 0;
}

int
session_restore( void )
{
  char snapshot[ PATH_MAX ], tape[ PATH_MAX ], info[ PATH_MAX ];
  FILE *f;

  if( !settings_current.resume_last_session ) return 0;

  if( session_path( snapshot, sizeof( snapshot ), "szx" ) ||
      session_path( tape, sizeof( tape ), "tzx" ) ||
      session_path( info, sizeof( info ), "txt" ) )
    return 0;

  if( !compat_file_exists( snapshot ) ) return 0;

  if( session_read_snapshot( snapshot ) ) {
    ui_error( UI_ERROR_WARNING, "The last session could not be restored" );
    return 1;
  }

  if( compat_file_exists( tape ) && !tape_open( tape, 0 ) ) {
    int block = 0;

    f = fopen( info, "r" );
    if( f ) {
      if( fscanf( f, "tape_block=%d", &block ) != 1 ) block = 0;
      fclose( f );
    }
    if( block > 0 ) tape_select_block( block );
  }

  return 0;
}

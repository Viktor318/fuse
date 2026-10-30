/* snapshot.c: snapshot handling routines
   Copyright (c) 1999-2012 Philip Kendall
   Copyright (c) 2026 Fredrick Meunier

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

#include "libspectrum.h"

#include "fuse.h"
#include "keyboard.h"
#include "machine.h"
#include "memory_pages.h"
#include "module.h"
#include "settings.h"
#include "snapshot.h"
#include "sound.h"
#include "ui/ui.h"
#include "utils.h"

/* Some .z80 files have junk after the last memory block (e.g. 64 stray
   bytes from the program that saved them). libspectrum reads it as one
   more block, which runs past the end of the file, and refuses the whole
   snapshot. Return the length up to the end of the last whole block, so
   the junk is left out; otherwise return the length unchanged. */
static size_t
snapshot_z80_usable_length( const unsigned char *buffer, size_t length )
{
  size_t offset, extra, block, blocks = 0;

  /* Only version 2 and 3 files (PC zero in the main header) have blocks */
  if( length < 32 || buffer[6] || buffer[7] ) return length;

  extra = buffer[30] | ( buffer[31] << 8 );
  offset = 32 + extra;
  if( offset > length ) return length;

  while( offset + 3 <= length ) {
    block = buffer[ offset ] | ( buffer[ offset + 1 ] << 8 );

    /* An empty block for page 0 starts SLT data; leave the rest alone */
    if( !block && !buffer[ offset + 2 ] ) return length;

    if( block == 0xffff ) block = 0x4000;	/* Uncompressed page */

    if( offset + 3 + block > length ) break;

    offset += 3 + block;
    blocks++;
  }

  /* Keep the length if nothing was readable or nothing is left over */
  if( !blocks || offset == length ) return length;

  return offset;
}

/* Keep the sound peripherals the user has switched on. Most snapshot
   formats can't record them (.z80 has no SpecDrum, Covox or uSpeech, .szx
   no Fuller Box, Melodik or Covox), so loading one would switch them off.
   A peripheral the snapshot itself asks for is still switched on. */
static void
snapshot_keep_sound_peripherals( libspectrum_snap *snap )
{
  if( settings_current.fuller )
    libspectrum_snap_set_fuller_box_active( snap, 1 );
  if( settings_current.melodik )
    libspectrum_snap_set_melodik_active( snap, 1 );
  if( settings_current.specdrum )
    libspectrum_snap_set_specdrum_active( snap, 1 );
  if( settings_current.covox )
    libspectrum_snap_set_covox_active( snap, 1 );
  if( settings_current.uspeech )
    libspectrum_snap_set_uspeech_active( snap, 1 );
}

int snapshot_read( const char *filename )
{
  utils_file file;
  libspectrum_snap *snap = libspectrum_snap_alloc();
  libspectrum_id_t type;
  size_t length;
  int error;

  error = utils_read_file( filename, &file );
  if( error ) { libspectrum_snap_free( snap ); return error; }

  /* Raw: a compressed .z80 is left as it is */
  error = libspectrum_identify_file_raw( &type, filename, file.buffer,
                                        file.length );
  if( error ) {
    utils_close_file( &file ); libspectrum_snap_free( snap );
    return error;
  }

  length = file.length;
  if( type == LIBSPECTRUM_ID_SNAPSHOT_Z80 )
    length = snapshot_z80_usable_length( file.buffer, length );

  /* libspectrum still identifies the file itself (e.g. compressed ones) */
  error = libspectrum_snap_read( snap, file.buffer, length,
                                 LIBSPECTRUM_ID_UNKNOWN, filename );
  if( error ) {
    utils_close_file( &file ); libspectrum_snap_free( snap );
    return error;
  }

  utils_close_file( &file );

  snapshot_keep_sound_peripherals( snap );

  error = snapshot_copy_from( snap );
  if( error ) { libspectrum_snap_free( snap ); return error; }

  error = libspectrum_snap_free( snap ); if( error ) return error;

  return 0;
}

int
snapshot_read_buffer( const unsigned char *buffer, size_t length,
		      libspectrum_id_t type )
{
  libspectrum_snap *snap = libspectrum_snap_alloc();
  int error;

  if( type == LIBSPECTRUM_ID_SNAPSHOT_Z80 )
    length = snapshot_z80_usable_length( buffer, length );

  error = libspectrum_snap_read( snap, buffer, length, type, NULL );
  if( error ) { libspectrum_snap_free( snap ); return error; }

  snapshot_keep_sound_peripherals( snap );

  error = snapshot_copy_from( snap );
  if( error ) { libspectrum_snap_free( snap ); return error; }

  error = libspectrum_snap_free( snap ); if( error ) return error;

  return 0;
}

int
snapshot_copy_from( libspectrum_snap *snap )
{
  int error;
  libspectrum_machine machine;

  /* Restored source state/time is not a continuation of consumed audio. */
  sound_suspend_subframe();
  periph_disable_optional();
  module_snapshot_enabled( snap );

  machine = libspectrum_snap_machine( snap );

  settings_current.late_timings = libspectrum_snap_late_timings( snap );

  /* Drop stale banks; register this snapshot's ROMs between machine setup
     and peripheral reset, before ROM files are consulted. */
  machine_clear_snapshot_rom_banks();

  if( machine != machine_current->machine ) {
    error = machine_select_from_snapshot( machine, snap );
    if( error ) {
      ui_error( UI_ERROR_ERROR,
		"Loading a %s snapshot, but that's not available",
		libspectrum_machine_name( machine ) );
    }
  } else {
    error = machine_reset_from_snapshot( 0, snap );
    if( error ) return error;
  }


  keyboard_release_all();

  module_snapshot_from( snap );

  /* Need to reset memory_map_[read|write] after all modules have had a turn
     initialising from the snapshot */
  machine_current->memory_map();

  return 0;
}

int snapshot_write( const char *filename )
{
  libspectrum_id_t type;
  libspectrum_class_t class;
  libspectrum_snap *snap;
  unsigned char *buffer; size_t length;
  int flags;

  int error;

  /* Work out what sort of file we want from the filename; default to
     .szx if we couldn't guess */
  error = libspectrum_identify_file_with_class( &type, &class, filename, NULL,
						0 );
  if( error ) return error;

  if( class != LIBSPECTRUM_CLASS_SNAPSHOT || type == LIBSPECTRUM_ID_UNKNOWN )
    type = LIBSPECTRUM_ID_SNAPSHOT_SZX;

  snap = libspectrum_snap_alloc();

  error = snapshot_copy_to( snap );
  if( error ) { libspectrum_snap_free( snap ); return error; }

  flags = 0;
  length = 0;
  buffer = NULL;
  error = libspectrum_snap_write( &buffer, &length, &flags, snap, type,
				  fuse_creator, 0 );
  if( error ) { libspectrum_snap_free( snap ); return error; }

  if( flags & LIBSPECTRUM_FLAG_SNAPSHOT_MAJOR_INFO_LOSS ) {
    ui_error(
      UI_ERROR_WARNING,
      "A large amount of information has been lost in conversion; the snapshot probably won't work"
    );
  } else if( flags & LIBSPECTRUM_FLAG_SNAPSHOT_MINOR_INFO_LOSS ) {
    ui_error(
      UI_ERROR_WARNING,
      "Some information has been lost in conversion; the snapshot may not work"
    );
  }

  error = libspectrum_snap_free( snap );
  if( error ) { libspectrum_free( buffer ); return 1; }

  error = utils_write_file( filename, buffer, length );
  if( error ) { libspectrum_free( buffer ); return error; }

  libspectrum_free( buffer );

  return 0;

}

int
snapshot_copy_to( libspectrum_snap *snap )
{
  libspectrum_snap_set_machine( snap, machine_current->machine );
  libspectrum_snap_set_late_timings( snap, settings_current.late_timings );

  module_snapshot_to( snap );

  return 0;
}

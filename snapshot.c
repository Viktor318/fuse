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

#include <stdio.h>
#include <string.h>

#include "libspectrum.h"

#include "compat.h"
#include "fuse.h"
#include "i18n.h"
#include "keyboard.h"
#include "machine.h"
#include "memory_pages.h"
#include "machines/specplus3.h"
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
  int error, keypad128;
  libspectrum_machine machine;

  /* Restored source state/time is not a continuation of consumed audio. */
  sound_suspend_subframe();
  keypad128 = settings_current.keypad128;
  specplus3_bus_invalidate();
  periph_disable_optional();

  /* No snapshot format records the Spectrum 128 keypad: it stays plugged
     in, as it was */
  settings_current.keypad128 = keypad128;

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
      return error;
    }
  } else {
    error = machine_reset_from_snapshot( 0, snap );
    if( error ) return error;
  }


  keyboard_release_all();

  specplus3_bus_invalidate();
  module_snapshot_from( snap );

  /* Need to reset memory_map_[read|write] after all modules have had a turn
     initialising from the snapshot */
  machine_current->memory_map();
  specplus3_bus_rebase();

  return 0;
}

/* The devices whose state a snapshot format can't keep (the losses that
   libspectrum counts as major), by their names */
typedef struct snapshot_loss {
  int ( *active )( libspectrum_snap *snap );
  const char *name;
  int sna, z80;			/* lost in .sna / .z80 */
} snapshot_loss;

static int
custom_rom_active( libspectrum_snap *snap )
{
  return libspectrum_snap_custom_rom( snap );
}

static const snapshot_loss snapshot_losses[] = {
  { libspectrum_snap_interface2_active, "Interface 2", 1, 1 },
  { libspectrum_snap_dock_active, "Timex Dock", 1, 1 },
  { custom_rom_active, NULL, 1, 1 },	/* the custom ROM, named below */
  { libspectrum_snap_fuller_box_active, "Fuller Box", 1, 0 },
  { libspectrum_snap_melodik_active, "Melodik", 1, 0 },
  { libspectrum_snap_specdrum_active, "SpecDrum", 1, 1 },
  { libspectrum_snap_covox_active, "Covox", 1, 1 },
  { libspectrum_snap_uspeech_active, "uSpeech", 1, 1 },
  { libspectrum_snap_ulaplus_active, "ULAplus", 0, 1 },
  { libspectrum_snap_plusd_active, "+D", 1, 0 },
  { libspectrum_snap_disciple_active, "DISCiPLE", 1, 1 },
  { libspectrum_snap_beta_active, "Beta 128", 1, 1 },
  { libspectrum_snap_opus_active, "Opus Discovery", 1, 1 },
  { libspectrum_snap_didaktik80_active, "Didaktik 80", 1, 1 },
  { libspectrum_snap_zxatasp_active, "ZXATASP", 1, 1 },
  { libspectrum_snap_zxcf_active, "ZXCF", 1, 1 },
  { libspectrum_snap_simpleide_active, "Simple IDE", 1, 1 },
  { libspectrum_snap_divide_active, "DivIDE", 1, 1 },
  { libspectrum_snap_divmmc_active, "DivMMC", 1, 1 },
  { libspectrum_snap_zxmmc_active, "ZXMMC", 1, 1 },
  { libspectrum_snap_multiface_active, "Multiface", 1, 1 },
  { libspectrum_snap_spectranet_active, "Spectranet", 1, 1 },
  { libspectrum_snap_usource_active, "uSource", 1, 1 },
  { libspectrum_snap_ttx2000s_active, "TTX2000S", 1, 1 },
};

/* The Hungarian article for a name: "az" before a vowel sound */
static const char*
hungarian_article( const char *name )
{
  static const char *vowels = "AEIOUaeiou";
  unsigned char first = (unsigned char)name[0];

  /* Á É Í Ó Ö Ő Ú Ü Ű (and lower case) start with 0xc3 or 0xc5 in UTF-8 */
  if( strchr( vowels, first ) || first == 0xc3 || first == 0xc5 ) return "az";
  return "a";
}

/* The machine a snapshot of this machine loads as, if the format can't
   keep it, or LIBSPECTRUM_MACHINE_UNKNOWN */
static libspectrum_machine
snapshot_machine_lost( libspectrum_machine machine, libspectrum_id_t type )
{
  if( type == LIBSPECTRUM_ID_SNAPSHOT_SNA ) {
    switch( machine ) {
    case LIBSPECTRUM_MACHINE_48_NTSC:
    case LIBSPECTRUM_MACHINE_TC2048:
    case LIBSPECTRUM_MACHINE_TC2068:
    case LIBSPECTRUM_MACHINE_TS2068:
      return LIBSPECTRUM_MACHINE_48;
    case LIBSPECTRUM_MACHINE_128:
    case LIBSPECTRUM_MACHINE_128E:
    case LIBSPECTRUM_MACHINE_PENT512:
    case LIBSPECTRUM_MACHINE_PENT1024:
    case LIBSPECTRUM_MACHINE_PLUS2:
    case LIBSPECTRUM_MACHINE_PLUS2A:
    case LIBSPECTRUM_MACHINE_PLUS3:
    case LIBSPECTRUM_MACHINE_PLUS3E:
    case LIBSPECTRUM_MACHINE_SCORP:
    case LIBSPECTRUM_MACHINE_SE:
      return LIBSPECTRUM_MACHINE_PENT;
    default:
      break;
    }
  } else if( type == LIBSPECTRUM_ID_SNAPSHOT_Z80 ) {
    switch( machine ) {
    case LIBSPECTRUM_MACHINE_48_NTSC: return LIBSPECTRUM_MACHINE_48;
    case LIBSPECTRUM_MACHINE_SE:
    case LIBSPECTRUM_MACHINE_128E: return LIBSPECTRUM_MACHINE_128;
    default: break;
    }
  }

  return LIBSPECTRUM_MACHINE_UNKNOWN;
}

/* After a snapshot was written in a format which couldn't keep everything:
   say what is missing. Minor losses (the T-state count, a HALT...) don't
   matter in practice and are not mentioned */
static void
snapshot_loss_warning( libspectrum_snap *snap, libspectrum_id_t type )
{
  const char *extension = type == LIBSPECTRUM_ID_SNAPSHOT_SNA ? ".sna" :
                          type == LIBSPECTRUM_ID_SNAPSHOT_Z80 ? ".z80" : NULL;
  int hungarian = i18n_hungarian();
  const char *names[ ARRAY_SIZE( snapshot_losses ) ];
  size_t i, count = 0;
  char message[ 1024 ], *end = message;
  size_t left = sizeof( message );
  libspectrum_machine machine, loaded_as;
  int n;

#define ADD( ... ) do { \
    n = snprintf( end, left, __VA_ARGS__ ); \
    if( n > 0 && (size_t)n < left ) { end += n; left -= n; } \
  } while( 0 )

  if( !extension ) {
    ui_error( UI_ERROR_WARNING, hungarian ?
      "A pillanatkép formátuma nem tud mindent megőrizni. Ha mindent meg akarsz őrizni, ments SZX formátumban." :
      "This snapshot format can't keep everything. To keep everything, save in SZX format." );
    return;
  }

  for( i = 0; i < ARRAY_SIZE( snapshot_losses ); i++ ) {
    const snapshot_loss *loss = &snapshot_losses[i];
    if( !( type == LIBSPECTRUM_ID_SNAPSHOT_SNA ? loss->sna : loss->z80 ) )
      continue;
    if( !loss->active( snap ) ) continue;
    names[ count++ ] = loss->name ? loss->name :
                       hungarian ? "saját ROM" : "custom ROM";
  }

  machine = libspectrum_snap_machine( snap );
  loaded_as = snapshot_machine_lost( machine, type );

  message[0] = '\0';

  if( count ) {
    for( i = 0; i < count; i++ ) {
      const char *separator = i == 0 ? "" : i == count - 1 ?
                              ( hungarian ? " és " : " and " ) : ", ";
      if( hungarian ) {
        const char *article = hungarian_article( names[i] );
        if( i == 0 ) ADD( "%c%s %s", article[0] - 'a' + 'A', article + 1,
                          names[i] );
        else ADD( "%s%s %s", separator, article, names[i] );
      } else {
        ADD( "%s%s%s", i == 0 ? "The state of the " : separator,
             i == 0 ? "" : "the ", names[i] );
      }
    }
    ADD( hungarian ? " állapota nem menthető %s fájlba." :
                     " can't be saved in a %s file.", extension );
  }

  if( loaded_as != LIBSPECTRUM_MACHINE_UNKNOWN ) {
    if( count ) ADD( " " );
    ADD( hungarian ?
           "A gép típusa (%s) nem menthető %s fájlba: betöltéskor %s gépként indul." :
           "The machine type (%s) can't be saved in a %s file: it loads as a %s.",
         libspectrum_machine_name( machine ), extension,
         libspectrum_machine_name( loaded_as ) );
  }

  if( !count && loaded_as == LIBSPECTRUM_MACHINE_UNKNOWN )
    ADD( hungarian ? "A %s formátum nem tud mindent megőrizni." :
                     "The %s format can't keep everything.", extension );

  ADD( hungarian ? "\n\nHa mindent meg akarsz őrizni, ments SZX formátumban." :
                   "\n\nTo keep everything, save in SZX format." );

#undef ADD

  ui_error( UI_ERROR_WARNING, "%s", message );
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

  error = utils_write_file( filename, buffer, length );
  libspectrum_free( buffer );
  if( error ) { libspectrum_snap_free( snap ); return error; }

  /* Saved; what the format couldn't keep */
  if( flags & LIBSPECTRUM_FLAG_SNAPSHOT_MAJOR_INFO_LOSS )
    snapshot_loss_warning( snap, type );

  error = libspectrum_snap_free( snap );
  if( error ) return 1;

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

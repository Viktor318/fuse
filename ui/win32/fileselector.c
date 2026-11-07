/* fileselector.c: Win32 fileselector routines
   Copyright (c) 2008 Marek Januszewski
   Copyright (c) 2016-2021 Sergio Baldoví

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

#include <string.h>
#include <windows.h>

#include "../../ayplayer.h"
#include "compat.h"
#include "i18n.h"
#include "ui/ui.h"
#include "ui/uimedia.h"
#include "utils.h"
#include "win32internals.h"
#include "win32text.h"

/* FIXME: remember the last directory when opening/saving
static char *current_folder;
*/

/* TODO: poll libspectrum for supported file extensions and avoid duplication
   between UIs */
static LPCTSTR file_filter = TEXT(
"Supported Files\0"
"*.ezx;*.mgtsnp;*.slt;*.sna;*.snapshot;*.snp;*.sp;*.szx;*.z80;*.zx-state;*.s;"
"*.csw;*.ltp;*.pzx;*.raw;*.spc;*.sta;*.tzx;*.tap"
#ifdef LIBSPECTRUM_SUPPORTS_WAV
";*.wav"
#endif
";"
"*.d40;*.d80;*.dsk;*.fdi;*.img;*.mgt;*.opd;*.opu;*.sad;*.scl;*.td0;*.trd;*.udi;"
"*.dck;*.rom;*.hdf;*.mdr;*.fmf;*.rzx;"
"*.bin;*.log;*.mlt;*.png;*.pok;*.scr;*.svg;" AY_MUSIC_TYPES
#ifdef LIBSPECTRUM_SUPPORTS_ZLIB_COMPRESSION
";*.gz;*.zip"
#endif
#ifdef LIBSPECTRUM_SUPPORTS_BZ2_COMPRESSION
";*.bz2"
#endif
"\0"
"All Files (*.*)\0"
"*.*\0"
"BASIC Text Files (*.bas;*.txt)\0"
"*.bas;*.txt\0"
"Auxiliary Files (*.scr;*.mlt;*.pok;*.png;*.svg;...)\0"
"*.bin;*.log;*.mlt;*.png;*.pok;*.scr;*.svg\0"
"AY Music Files (*.ay;*.pt3;*.stc;*.ym;...)\0"
AY_MUSIC_TYPES ";*.txt\0"
#ifdef LIBSPECTRUM_SUPPORTS_ZLIB_COMPRESSION
"Compressed files (*.gz;*.zip;...)\0"
"*.gz;*.zip"
#ifdef LIBSPECTRUM_SUPPORTS_BZ2_COMPRESSION
";*.bz2"
#endif
"\0"
#endif
"Disk Files (*.dsk;*.udi;*.scl;*.trd;*.fdi;...)\0"
"*.d40;*.d80;*.dsk;*.fdi;*.img;*.mgt;*.opd;*.opu;*.sad;*.scl;*.td0;*.trd;*.udi\0"
"Dock Files (*.dck;*.rom)\0"
"*.dck;*.rom\0"
"Harddisk Files (*.hdf)\0"
"*.hdf\0"
"Microdrive Files (*.mdr)\0"
"*.mdr\0"
"Movie Files (*.fmf)\0"
"*.fmf\0"
"Recording Files (*.rzx)\0"
"*.rzx\0"
"ROM Files (*.rom)\0"
"*.rom\0"
"Snapshot Files (*.szx;*.z80;*.sna;...)\0"
"*.ezx;*.mgtsnp;*.slt;*.sna;*.snapshot;*.snp;*.sp;*.szx;*.z80;*.zx-state;*.s\0"
"Tape Files (*.tap;*.tzx;*.pzx"
#ifdef LIBSPECTRUM_SUPPORTS_WAV
";*.wav"
#endif
";*.csw;...)\0"
"*.csw;*.ltp;*.pzx;*.raw;*.spc;*.sta;*.tzx;*.tap"
#ifdef LIBSPECTRUM_SUPPORTS_WAV
";*.wav"
#endif
"\0"
"\0" );

/* The file types each dialog shows. A save dialog lists the types it can
   write, one by one, and the extension of the type chosen (or the default
   one) is added to a name typed without one, or with one of another kind
   of file; an open dialog shows every type, with its own preselected. Each
   dialog remembers the type chosen last time (while Fuse runs). */

#define ALL_FILES "All Files (*.*)\0*.*\0"

#define SNAPSHOT_TYPES \
  "SZX Snapshots (*.szx)\0*.szx\0" \
  "Z80 Snapshots (*.z80)\0*.z80\0" \
  "SNA Snapshots (*.sna)\0*.sna\0"

#define TAPE_TYPES \
  "TZX Tapes (*.tzx)\0*.tzx\0" \
  "TAP Tapes (*.tap)\0*.tap\0" \
  "PZX Tapes (*.pzx)\0*.pzx\0" \
  "CSW Tapes (*.csw)\0*.csw\0"

#define DISK_DSK "DSK Disk Images (*.dsk)\0*.dsk\0"
#define DISK_UDI "UDI Disk Images (*.udi)\0*.udi\0"
#define DISK_TRD "TRD Disk Images (*.trd)\0*.trd\0"
#define DISK_SCL "SCL Disk Images (*.scl)\0*.scl\0"
#define DISK_MGT "MGT Disk Images (*.mgt)\0*.mgt\0"
#define DISK_IMG "IMG Disk Images (*.img)\0*.img\0"
#define DISK_OPD "OPD Disk Images (*.opd)\0*.opd\0"
#define DISK_D80 "D80 Disk Images (*.d80)\0*.d80\0"
#define DISK_D40 "D40 Disk Images (*.d40)\0*.d40\0"

typedef struct dialog_profile {
  /* The (English) title, or its start if it ends with '*' */
  const char *title;
  /* Save: the types written ("name\0pattern\0..."); open: NULL */
  const char *save_types;
  /* Open: the name of the type of file_filter to preselect */
  const char *open_type;
  /* Save: added to a name without a known extension; NULL: none */
  const char *default_extension;
  /* The type chosen last time (1-based; 0: none yet) */
  DWORD last_index;
} dialog_profile;

static dialog_profile profiles[] = {
  /* Save dialogs */
  { "Fuse - Save Snapshot", SNAPSHOT_TYPES ALL_FILES, NULL, "szx", 0 },
  { "Fuse - Write Tape", TAPE_TYPES ALL_FILES, NULL, "tzx", 0 },
  { "Fuse - Write Microdrive*", "Microdrive Cartridges (*.mdr)\0*.mdr\0"
    ALL_FILES, NULL, "mdr", 0 },
  { "Fuse - Write +3 Disk*", DISK_DSK DISK_UDI ALL_FILES, NULL, "dsk", 0 },
  { "Fuse - Write Beta Disk*", DISK_TRD DISK_SCL DISK_UDI ALL_FILES, NULL,
    "trd", 0 },
  { "Fuse - Write +D Disk*", DISK_MGT DISK_IMG DISK_UDI ALL_FILES, NULL,
    "mgt", 0 },
  { "Fuse - Write DISCiPLE Disk*", DISK_MGT DISK_IMG DISK_UDI ALL_FILES, NULL,
    "mgt", 0 },
  { "Fuse - Write Opus Disk*", DISK_OPD DISK_UDI ALL_FILES, NULL, "opd", 0 },
  { "Fuse - Write Didaktik Disk*", DISK_D80 DISK_D40 DISK_UDI ALL_FILES, NULL,
    "d80", 0 },
  { "Fuse - Save Screenshot as SCR", "SCR Screenshots (*.scr)\0*.scr\0"
    ALL_FILES, NULL, "scr", 0 },
  { "Fuse - Save Screenshot as MLT", "MLT Screenshots (*.mlt)\0*.mlt\0"
    ALL_FILES, NULL, "mlt", 0 },
  { "Fuse - Save Screenshot as PNG", "PNG Images (*.png)\0*.png\0"
    ALL_FILES, NULL, "png", 0 },
  { "Fuse - Capture to SVG File", "SVG Images (*.svg)\0*.svg\0" ALL_FILES,
    NULL, "svg", 0 },
  { "Fuse - Start Recording", "RZX Recordings (*.rzx)\0*.rzx\0" ALL_FILES,
    NULL, "rzx", 0 },
  { "Fuse - Record Movie File", "FMF Movies (*.fmf)\0*.fmf\0" ALL_FILES,
    NULL, "fmf", 0 },
  { "Fuse - Start AY Log", "PSG AY Logs (*.psg)\0*.psg\0" ALL_FILES, NULL,
    "psg", 0 },
  { "Fuse - Save Profile Data", "Text Files (*.txt)\0*.txt\0" ALL_FILES,
    NULL, "txt", 0 },
  { "Fuse - Save BASIC as Text", "BASIC Text Files (*.bas)\0*.bas\0"
    "Text Files (*.txt)\0*.txt\0" ALL_FILES, NULL, "bas", 0 },
  { "Fuse - Save Binary Data", ALL_FILES, NULL, NULL, 0 },

  /* Open dialogs */
  { "Fuse - Open Tape", NULL, "Tape Files*", NULL, 0 },
  { "Fuse - Load Snapshot*", NULL, "Snapshot Files*", NULL, 0 },
  { "Fuse - Start Replay", NULL, "Recording Files*", NULL, 0 },
  { "Fuse - Load RZX", NULL, "Recording Files*", NULL, 0 },
  { "Fuse - Continue Recording", NULL, "Recording Files*", NULL, 0 },
  { "Fuse - Finalise Recording", NULL, "Recording Files*", NULL, 0 },
  { "Fuse - Insert Microdrive*", NULL, "Microdrive Files*", NULL, 0 },
  { "Fuse - Insert Timex Dock Cartridge", NULL, "Dock Files*", NULL, 0 },
  { "Fuse - Insert Interface 2 Cartridge", NULL, "ROM Files*", NULL, 0 },
  { "Fuse - Insert Hard Disk File", NULL, "Harddisk Files*", NULL, 0 },
  { "Fuse - Insert *", NULL, "Disk Files*", NULL, 0 },
  { "Fuse - Open SCR Screenshot", NULL, "Auxiliary Files*", NULL, 0 },
  { "Fuse - Open MLT Screenshot", NULL, "Auxiliary Files*", NULL, 0 },
  { "Fuse - Select ROM", NULL, "ROM Files*", NULL, 0 },
  { "Fuse - Load BASIC Text", NULL, "BASIC Text Files*", NULL, 0 },
  { "Fuse - Load Binary Data", NULL, "All Files*", NULL, 0 },
  { "Fuse - Select File for Communication", NULL, "All Files*", NULL, 0 },
};

/* Any other dialog (e.g. File > Open): every type, the one chosen last */
static dialog_profile other_dialogs = { "", NULL, NULL, NULL, 0 };

/* Does a text match a pattern which may end with '*'? */
static int
matches( const char *text, const char *pattern )
{
  size_t length = strlen( pattern );

  if( length && pattern[ length - 1 ] == '*' )
    return !strncmp( text, pattern, length - 1 );

  return !strcmp( text, pattern );
}

static dialog_profile*
find_profile( const char *title )
{
  size_t i;

  for( i = 0; i < ARRAY_SIZE( profiles ); i++ )
    if( matches( title, profiles[i].title ) ) return &profiles[i];

  return &other_dialogs;
}

/* The position (1-based) of the type whose name matches, 0 if none */
static DWORD
filter_position( const char *filter, const char *name )
{
  DWORD position = 1;

  while( *filter ) {
    if( matches( filter, name ) ) return position;
    filter += strlen( filter ) + 1;	/* the name */
    filter += strlen( filter ) + 1;	/* the patterns */
    position++;
  }

  return 0;
}

/* The patterns of the type at a position (1-based), or NULL */
static const char*
filter_patterns( const char *filter, DWORD position )
{
  DWORD i;

  for( i = 1; *filter; i++ ) {
    filter += strlen( filter ) + 1;
    if( i == position ) return filter;
    filter += strlen( filter ) + 1;
  }

  return NULL;
}

/* A filter with the names of the file types translated */
static const char*
translated_filter( const char *filter )
{
  static char buffer[ 4096 ];
  const char *in = filter;
  size_t used = 0;
  int name = 1;

  while( *in ) {
    const char *text = name ? i18n( in ) : in;
    size_t length = strlen( text ) + 1;

    if( used + length + 1 > sizeof( buffer ) ) return filter;
    memcpy( buffer + used, text, length );
    used += length;
    in += strlen( in ) + 1;
    name = !name;
  }
  buffer[ used ] = '\0';

  return buffer;
}

/* The extension of a file name (without the dot), or NULL */
static const char*
extension_of( const char *filename )
{
  const char *name, *dot;

  if( !filename ) return NULL;

  name = strrchr( filename, '\\' );
  name = name ? name + 1 : filename;
  if( strrchr( name, '/' ) ) name = strrchr( name, '/' ) + 1;

  dot = strrchr( name, '.' );
  return dot && dot != name && dot[1] ? dot + 1 : NULL;
}

/* The extension of a single "*.ext" pattern, or NULL */
static const char*
pattern_extension( const char *patterns )
{
  if( !patterns || strncmp( patterns, "*.", 2 ) ) return NULL;
  if( strchr( patterns, ';' ) || strchr( patterns + 2, '*' ) ) return NULL;
  return patterns + 2;
}

/* Is the extension one of the types the dialog writes? */
static int
known_extension( const char *types, const char *extension )
{
  DWORD i;
  const char *patterns, *own;

  if( !extension ) return 0;

  for( i = 1; ( patterns = filter_patterns( types, i ) ) != NULL; i++ ) {
    own = pattern_extension( patterns );
    if( own && !strcasecmp( own, extension ) ) return 1;
  }

  return 0;
}

/* The extension a name gets in a save dialog, if it needs one: that of the
   type chosen, else the one of the file being written over (disks), else
   the default one */
static const char*
extension_to_add( const dialog_profile *profile, DWORD chosen )
{
  const char *extension;

  extension = pattern_extension( filter_patterns( profile->save_types,
                                                  chosen ) );
  if( extension ) return extension;

  extension = extension_of( ui_media_save_suggestion );
  if( known_extension( profile->save_types, extension ) ) return extension;

  return profile->default_extension;
}

static char*
run_dialog( const char *title, int is_saving )
{
  OPENFILENAME ofn;
  char szFile[512];
  int result;
  dialog_profile *profile = find_profile( title );
  const char *filter;
  const char *extension;
  const char *suggested;

  if( is_saving && profile->save_types ) {
    filter = profile->save_types;
  } else {
    filter = file_filter;
    if( profile->save_types ) profile = &other_dialogs;
  }

  szFile[0] = '\0';

  while( 1 ) {

    memset( &ofn, 0, sizeof( ofn ) );
    ofn.lStructSize = sizeof( ofn );
    ofn.hwndOwner = fuse_hWnd;
    ofn.lpstrFilter = translated_filter( filter );
    ofn.lpstrCustomFilter = NULL;

    /* The type chosen last time; else the dialog's own, the type of the
       file being written over (disks), or the first one */
    ofn.nFilterIndex = profile->last_index;
    if( !ofn.nFilterIndex && profile->open_type )
      ofn.nFilterIndex = filter_position( filter, profile->open_type );
    if( !ofn.nFilterIndex && is_saving && profile->save_types ) {
      suggested = extension_of( ui_media_save_suggestion );
      if( known_extension( filter, suggested ) ) {
        DWORD i;
        for( i = 1; filter_patterns( filter, i ); i++ ) {
          const char *own = pattern_extension( filter_patterns( filter, i ) );
          if( own && !strcasecmp( own, suggested ) ) ofn.nFilterIndex = i;
        }
      }
    }
    if( !ofn.nFilterIndex ) ofn.nFilterIndex = 1;

    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof( szFile );
    ofn.lpstrFileTitle = NULL;
    ofn.lpstrInitialDir = NULL;
    ofn.lpstrTitle = i18n( title );
    ofn.Flags = /* OFN_DONTADDTORECENT | */ OFN_PATHMUSTEXIST |
                OFN_HIDEREADONLY;
    if( is_saving ) {
      ofn.Flags |= OFN_OVERWRITEPROMPT | OFN_NOREADONLYRETURN;
    } else {
      ofn.Flags |= OFN_FILEMUSTEXIST;
    }
    ofn.nFileOffset = 0;
    ofn.nFileExtension = 0;

    /* Windows adds it to a name typed without an extension (and changes it
       with the type chosen) */
    ofn.lpstrDefExt = is_saving && profile->save_types ?
                      extension_to_add( profile, ofn.nFilterIndex ) : NULL;

    result = win32text_file_dialog( &ofn, is_saving );

    profile->last_index = ofn.nFilterIndex;

    if( !result ) return NULL;

    if( !is_saving || !profile->save_types ) break;

    /* A name without an extension of the types written gets one */
    extension = extension_to_add( profile, ofn.nFilterIndex );
    if( !extension ||
        known_extension( profile->save_types, extension_of( szFile ) ) )
      break;

    if( strlen( szFile ) + strlen( extension ) + 2 > sizeof( szFile ) ) break;
    strcat( szFile, "." );
    strcat( szFile, extension );

    /* Windows asked about the name typed, not this one */
    if( !compat_file_exists( szFile ) ) break;
    {
      const char *name = strrchr( szFile, '\\' );
      if( ui_query( "%s already exists.\nDo you want to overwrite it?",
                    name ? name + 1 : szFile ) )
        break;
    }
    /* Not overwritten: choose again, with this name */
  }

  return utils_safe_strdup( szFile );
}

char*
ui_get_open_filename( const char *title )
{
  return run_dialog( title, 0 );
}

char*
ui_get_save_filename( const char *title )
{
  return run_dialog( title, 1 );
}

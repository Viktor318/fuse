/* ayplaylist.c: The playlist of the AY player
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

#ifdef WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

#include "ayplayer.h"
#include "ayplaylist.h"
#include "compat.h"
#include "i18n.h"
#include "ui/ui.h"
#include "utils.h"

/* Two lists: that of the AY player (music of one AY chip, and every .ay
   file) and that of the TurboSound player (modules playing two or three
   chips). Each is kept in a file of its own */
struct ayplaylist {
  int turbosound;
  const char *filename;		/* in the settings folder */

  ayplaylist_entry *entries;
  int entry_count, entry_size;
  int current;
  int loaded;
  /* Do not write the file while it is being read */
  int loading;
  /* Reading the saved list when the player starts: no messages */
  int quiet;
  /* Files of a list being loaded that couldn't be read (left out) */
  int missing_files;
};

static ayplaylist ay_list = {
  0, "fuse-ay-playlist.m3u", NULL, 0, 0, -1, 0, 0, 0, 0
};
static ayplaylist ts_list = {
  1, "fuse-ts-playlist.m3u", NULL, 0, 0, -1, 0, 0, 0, 0
};

/* The list whose song the player (ayplayer.c, one file at a time) has
   loaded; NULL: none */
static ayplaylist *player_owner = NULL;

ayplaylist *ayplaylist_ay( void ) { return &ay_list; }
ayplaylist *ayplaylist_ts( void ) { return &ts_list; }

int
ayplaylist_is_turbosound( const ayplaylist *list )
{
  return list->turbosound;
}

/* A file is TurboSound music if its module plays more than one chip; .ay
   files are always played by the AY player, also if they use a second
   chip */
static int
is_turbosound_file( const char *path )
{
  return ayplayer_file_chips( path ) > 1;
}

ayplaylist*
ayplaylist_for_file( const char *path )
{
  return is_turbosound_file( path ) ? &ts_list : &ay_list;
}

static int save_m3u( ayplaylist *list, const char *path );

static void
autosave( ayplaylist *list )
{
  char path[ 1024 ];

  if( list->loading ) return;
  snprintf( path, sizeof( path ), "%s" FUSE_DIR_SEP_STR "%s",
            compat_get_config_path(), list->filename );
  save_m3u( list, path );
}

static void
load_saved( ayplaylist *list )
{
  char path[ 1024 ];
  FILE *f;

  if( list->loaded ) return;
  list->loaded = 1;

  snprintf( path, sizeof( path ), "%s" FUSE_DIR_SEP_STR "%s",
            compat_get_config_path(), list->filename );
  f = fopen( path, "r" );
  if( !f ) return;
  fclose( f );
  list->quiet = 1;
  ayplaylist_load_m3u( list, path, 1 );
  list->quiet = 0;
}

static int
file_readable( const char *path )
{
  FILE *f = fopen( path, "rb" );

  if( !f ) return 0;
  fclose( f );
  return 1;
}

static ayplaylist_entry*
new_entry( ayplaylist *list, int index )
{
  if( list->entry_count == list->entry_size ) {
    list->entry_size = list->entry_size ? list->entry_size * 2 : 32;
    list->entries = libspectrum_renew( ayplaylist_entry, list->entries,
                                       list->entry_size );
  }
  if( index < list->entry_count )
    memmove( &list->entries[ index + 1 ], &list->entries[ index ],
             ( list->entry_count - index ) * sizeof( list->entries[0] ) );
  list->entry_count++;
  if( list->current >= index ) list->current++;
  memset( &list->entries[ index ], 0, sizeof( list->entries[0] ) );
  return &list->entries[ index ];
}

static void
free_entries( ayplaylist *list )
{
  int i;

  for( i = 0; i < list->entry_count; i++ )
    libspectrum_free( list->entries[i].path );
  list->entry_count = 0;
  list->current = -1;
}

/* Add songs of a file: song -1 for all of them */
static int
add_songs( ayplaylist *list, const char *path, int song )
{
  ayplayer_info info;
  int i, first = -1;

  /* A file of a list that isn't there (any more) is left out, without a
     message for every one of them */
  if( list->loading && !file_readable( path ) ) {
    list->missing_files++;
    return -1;
  }

  if( ayplayer_file_info( path, &info ) ) return -1;

  /* Each list takes its own kind of music: a file added on its own is
     refused with a message, the files of a folder or a list are just left
     out */
  if( ( info.chips > 1 ) != list->turbosound ) {
    if( !list->loading && !list->quiet ) {
      if( list->turbosound )
        ui_error( UI_ERROR_WARNING,
                  "'%s' is not a TurboSound file: add it in the AY player",
                  path );
      else
        ui_error( UI_ERROR_WARNING,
                  "'%s' is a TurboSound file: add it in the TurboSound "
                  "player", path );
    }
    ayplayer_file_info_free( &info );
    return -1;
  }

  for( i = 0; i < info.songs; i++ ) {
    ayplaylist_entry *e;

    if( song >= 0 && i != song ) continue;

    e = new_entry( list, list->entry_count );
    e->path = utils_safe_strdup( path );
    e->song = i;
    e->checksum = info.checksum;
    e->chips = info.chips;
    snprintf( e->author, sizeof( e->author ), "%s", info.author );
    snprintf( e->misc, sizeof( e->misc ), "%s", info.misc );
    snprintf( e->comment, sizeof( e->comment ), "%s", info.comment );
    snprintf( e->format, sizeof( e->format ), "%s", info.format );
    if( info.names[i][0] ) {
      snprintf( e->title, sizeof( e->title ), "%s", info.names[i] );
    } else {
      const char *name = path, *p;
      for( p = path; *p; p++ )
        if( *p == '/' || *p == '\\' ) name = p + 1;
      snprintf( e->title, sizeof( e->title ), "%s", name );
    }
    if( first < 0 ) first = list->entry_count - 1;
  }

  ayplayer_file_info_free( &info );
  return first;
}

int
ayplaylist_count( ayplaylist *list )
{
  load_saved( list );
  return list->entry_count;
}

const ayplaylist_entry*
ayplaylist_get( ayplaylist *list, int index )
{
  load_saved( list );
  if( index < 0 || index >= list->entry_count ) return NULL;
  return &list->entries[ index ];
}

int
ayplaylist_add_file( ayplaylist *list, const char *path )
{
  int first;

  load_saved( list );
  first = add_songs( list, path, -1 );
  if( first >= 0 ) autosave( list );
  return first;
}

static int
compare_names( const void *a, const void *b )
{
  return strcmp( *(char * const *)a, *(char * const *)b );
}

int
ayplaylist_add_folder( ayplaylist *list, const char *folder )
{
  char **names = NULL, path[ 1024 ];
  size_t count = 0, size = 0, i;
  int before;

  load_saved( list );
  before = list->entry_count;

#ifdef WIN32
  {
    WIN32_FIND_DATAA found;
    HANDLE find;

    snprintf( path, sizeof( path ), "%s\\*.*", folder );
    find = FindFirstFileA( path, &found );
    if( find != INVALID_HANDLE_VALUE ) {
      do {
        if( found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) continue;
        if( !ayplayer_is_ay_file( found.cFileName ) ) continue;
        if( count == size ) {
          size = size ? size * 2 : 64;
          names = libspectrum_renew( char *, names, size );
        }
        names[ count++ ] = utils_safe_strdup( found.cFileName );
      } while( FindNextFileA( find, &found ) );
      FindClose( find );
    }
  }
#else
  {
    DIR *dir = opendir( folder );
    struct dirent *de;

    if( dir ) {
      while( ( de = readdir( dir ) ) ) {
        if( !ayplayer_is_ay_file( de->d_name ) ) continue;
        if( count == size ) {
          size = size ? size * 2 : 64;
          names = libspectrum_renew( char *, names, size );
        }
        names[ count++ ] = utils_safe_strdup( de->d_name );
      }
      closedir( dir );
    }
  }
#endif

  /* In the order of their names */
  if( count ) qsort( names, count, sizeof( names[0] ), compare_names );

  /* The files of the other player are left out quietly */
  list->loading = 1;
  for( i = 0; i < count; i++ ) {
    snprintf( path, sizeof( path ), "%s" FUSE_DIR_SEP_STR "%s", folder,
              names[i] );
    add_songs( list, path, -1 );
    libspectrum_free( names[i] );
  }
  list->loading = 0;
  libspectrum_free( names );

  if( list->entry_count != before ) autosave( list );
  return list->entry_count - before;
}

void
ayplaylist_remove( ayplaylist *list, int index )
{
  load_saved( list );
  if( index < 0 || index >= list->entry_count ) return;

  libspectrum_free( list->entries[ index ].path );
  memmove( &list->entries[ index ], &list->entries[ index + 1 ],
           ( list->entry_count - index - 1 ) * sizeof( list->entries[0] ) );
  list->entry_count--;

  if( list->current == index ) list->current = -1;
  else if( list->current > index ) list->current--;

  autosave( list );
}

void
ayplaylist_clear( ayplaylist *list )
{
  load_saved( list );
  free_entries( list );
  autosave( list );
}

int
ayplaylist_move( ayplaylist *list, int index, int direction )
{
  int other = index + direction;
  ayplaylist_entry swap;

  load_saved( list );
  if( index < 0 || index >= list->entry_count || other < 0 ||
      other >= list->entry_count )
    return index;

  swap = list->entries[ index ];
  list->entries[ index ] = list->entries[ other ];
  list->entries[ other ] = swap;

  if( list->current == index ) list->current = other;
  else if( list->current == other ) list->current = index;

  autosave( list );
  return other;
}

int
ayplaylist_load_m3u( ayplaylist *list, const char *path, int append )
{
  char line[ 2048 ];
  FILE *f;

  f = fopen( path, "r" );
  if( !f ) {
    ui_error( UI_ERROR_ERROR, "couldn't open '%s'", path );
    return 1;
  }

  list->loaded = 1;
  if( !append ) free_entries( list );

  list->loading = 1;
  list->missing_files = 0;
  while( fgets( line, sizeof( line ), f ) ) {
    char *end = line + strlen( line ), *mark;
    int song = -1;

    while( end > line && ( end[-1] == '\n' || end[-1] == '\r' ||
                           end[-1] == ' ' ) )
      *--end = '\0';
    if( !line[0] || line[0] == '#' ) continue;

    /* A list saved by an older Fuse or another player may be in the
       legacy code page */
    {
      char *utf8 = i18n_legacy_to_utf8( line );
      if( utf8 ) {
        snprintf( line, sizeof( line ), "%s", utf8 );
        libspectrum_free( utf8 );
      }
    }

    /* path::AY,song (the song may also be written $hex) */
    mark = strstr( line, "::" );
    if( mark ) {
      char *comma = strchr( mark, ',' );
      if( comma ) {
        comma++;
        song = *comma == '$' ? (int)strtol( comma + 1, NULL, 16 ) :
                               atoi( comma );
      }
      *mark = '\0';
    }

    if( !ayplayer_is_ay_file( line ) ) continue;

    /* Relative to the folder of the list */
    if( !compat_is_absolute_path( line ) ) {
      char full[ 2048 ];
      const char *p, *slash = NULL;
      for( p = path; *p; p++ ) if( *p == '/' || *p == '\\' ) slash = p;
      if( slash ) {
        snprintf( full, sizeof( full ), "%.*s" FUSE_DIR_SEP_STR "%s",
                  (int)( slash - path ), path, line );
        add_songs( list, full, song );
        continue;
      }
    }
    add_songs( list, line, song );
  }
  list->loading = 0;
  fclose( f );

  /* The saved list: the missing files just go; a list loaded by the user:
     one message */
  if( list->missing_files && !list->quiet )
    ui_error( UI_ERROR_WARNING,
              "%d files of the list are not available and were left out",
              list->missing_files );

  autosave( list );
  return 0;
}

static int
save_m3u( ayplaylist *list, const char *path )
{
  FILE *f;
  int i;

  f = fopen( path, "w" );
  if( !f ) return 1;

  fprintf( f, "#EXTM3U\n" );
  for( i = 0; i < list->entry_count; i++ ) {
    const ayplaylist_entry *e = &list->entries[i];
    long frames;

    if( !ayplayer_known_length( e->checksum, e->song, &frames ) || !frames )
      frames = -50;
    fprintf( f, "#EXTINF:%ld,%s%s%s\n", frames / 50, e->author,
             e->author[0] ? " - " : "", e->title );
    fprintf( f, "%s::AY,%d\n", e->path, e->song );
  }

  fclose( f );
  return 0;
}

int
ayplaylist_save_m3u( ayplaylist *list, const char *path )
{
  load_saved( list );
  if( save_m3u( list, path ) ) {
    ui_error( UI_ERROR_ERROR, "couldn't write '%s'", path );
    return 1;
  }
  return 0;
}

int
ayplaylist_current( ayplaylist *list )
{
  return list->current;
}

void
ayplaylist_set_current( ayplaylist *list, int index )
{
  list->current = index >= 0 && index < list->entry_count ? index : -1;
}

int
ayplaylist_play( ayplaylist *list, int index )
{
  const ayplaylist_entry *e = ayplaylist_get( list, index );

  if( !e ) return 1;

  /* The player plays for one list at a time: the song of the other one
     stops */
  if( player_owner != list || strcmp( ayplayer_path(), e->path ) ) {
    if( ayplayer_load( e->path ) ) return 1;
  }
  player_owner = list;
  list->current = index;
  return ayplayer_play( e->song );
}

int
ayplaylist_owns_player( const ayplaylist *list )
{
  return player_owner == list && ayplayer_is_open();
}

int
ayplaylist_playing( const ayplaylist *list )
{
  return ayplaylist_owns_player( list ) && ayplayer_current_song() >= 0;
}

void
ayplaylist_close_player( ayplaylist *list )
{
  if( player_owner != list ) return;
  ayplayer_close();
  player_owner = NULL;
}

int
ayplaylist_length( ayplaylist *list, int index, long *frames )
{
  const ayplaylist_entry *e = ayplaylist_get( list, index );

  if( !e ) return 0;
  return ayplayer_known_length( e->checksum, e->song, frames );
}

int
ayplaylist_channels( ayplaylist *list, int index )
{
  const ayplaylist_entry *e = ayplaylist_get( list, index );
  int chips;

  if( !e ) return 0;
  chips = e->chips;
  /* An .ay song: a second chip is found by measuring it */
  if( chips <= 1 ) {
    chips = ayplayer_known_chips( e->checksum, e->song );
    if( chips < 1 ) chips = 1;
  }
  return chips * 3;
}

int
ayplaylist_open_file( ayplaylist *list, const char *path )
{
  int first = ayplaylist_add_file( list, path );

  if( first < 0 ) return 1;
  return ayplaylist_play( list, first );
}

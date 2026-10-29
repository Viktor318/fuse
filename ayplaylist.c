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
#include "ui/ui.h"
#include "utils.h"

#define PLAYLIST_FILE "fuse-ay-playlist.m3u"

static ayplaylist_entry *entries = NULL;
static int entry_count = 0, entry_size = 0;
static int current = -1;
static int loaded = 0;
/* Do not write the file while it is being read */
static int loading = 0;
/* Reading the saved list when the player starts: no messages */
static int quiet = 0;
/* Files of a list being loaded that couldn't be read (left out) */
static int missing_files = 0;

static int save_m3u( const char *path );

static void
autosave( void )
{
  char path[ 1024 ];

  if( loading ) return;
  snprintf( path, sizeof( path ), "%s" FUSE_DIR_SEP_STR "%s",
            compat_get_config_path(), PLAYLIST_FILE );
  save_m3u( path );
}

static void
load_saved( void )
{
  char path[ 1024 ];
  FILE *f;

  if( loaded ) return;
  loaded = 1;

  snprintf( path, sizeof( path ), "%s" FUSE_DIR_SEP_STR "%s",
            compat_get_config_path(), PLAYLIST_FILE );
  f = fopen( path, "r" );
  if( !f ) return;
  fclose( f );
  quiet = 1;
  ayplaylist_load_m3u( path, 1 );
  quiet = 0;
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
new_entry( int index )
{
  if( entry_count == entry_size ) {
    entry_size = entry_size ? entry_size * 2 : 32;
    entries = libspectrum_renew( ayplaylist_entry, entries, entry_size );
  }
  if( index < entry_count )
    memmove( &entries[ index + 1 ], &entries[ index ],
             ( entry_count - index ) * sizeof( entries[0] ) );
  entry_count++;
  if( current >= index ) current++;
  memset( &entries[ index ], 0, sizeof( entries[0] ) );
  return &entries[ index ];
}

/* Add songs of a file: song -1 for all of them */
static int
add_songs( const char *path, int song )
{
  ayplayer_info info;
  int i, first = -1;

  /* A file of a list that isn't there (any more) is left out, without a
     message for every one of them */
  if( loading && !file_readable( path ) ) {
    missing_files++;
    return -1;
  }

  if( ayplayer_file_info( path, &info ) ) return -1;

  for( i = 0; i < info.songs; i++ ) {
    ayplaylist_entry *e;

    if( song >= 0 && i != song ) continue;

    e = new_entry( entry_count );
    e->path = utils_safe_strdup( path );
    e->song = i;
    e->checksum = info.checksum;
    snprintf( e->author, sizeof( e->author ), "%s", info.author );
    if( info.names[i][0] ) {
      snprintf( e->title, sizeof( e->title ), "%s", info.names[i] );
    } else {
      const char *name = path, *p;
      for( p = path; *p; p++ )
        if( *p == '/' || *p == '\\' ) name = p + 1;
      snprintf( e->title, sizeof( e->title ), "%s", name );
    }
    if( first < 0 ) first = entry_count - 1;
  }

  ayplayer_file_info_free( &info );
  return first;
}

int
ayplaylist_count( void )
{
  load_saved();
  return entry_count;
}

const ayplaylist_entry*
ayplaylist_get( int index )
{
  load_saved();
  if( index < 0 || index >= entry_count ) return NULL;
  return &entries[ index ];
}

int
ayplaylist_add_file( const char *path )
{
  int first;

  load_saved();
  first = add_songs( path, -1 );
  if( first >= 0 ) autosave();
  return first;
}

static int
compare_names( const void *a, const void *b )
{
  return strcmp( *(char * const *)a, *(char * const *)b );
}

int
ayplaylist_add_folder( const char *folder )
{
  char **names = NULL, path[ 1024 ];
  size_t count = 0, size = 0, i;
  int before;

  load_saved();
  before = entry_count;

#ifdef WIN32
  {
    WIN32_FIND_DATAA found;
    HANDLE find;

    snprintf( path, sizeof( path ), "%s\\*.ay", folder );
    find = FindFirstFileA( path, &found );
    if( find != INVALID_HANDLE_VALUE ) {
      do {
        if( found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) continue;
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

  loading = 1;
  for( i = 0; i < count; i++ ) {
    snprintf( path, sizeof( path ), "%s" FUSE_DIR_SEP_STR "%s", folder,
              names[i] );
    add_songs( path, -1 );
    libspectrum_free( names[i] );
  }
  loading = 0;
  libspectrum_free( names );

  if( entry_count != before ) autosave();
  return entry_count - before;
}

void
ayplaylist_remove( int index )
{
  load_saved();
  if( index < 0 || index >= entry_count ) return;

  libspectrum_free( entries[ index ].path );
  memmove( &entries[ index ], &entries[ index + 1 ],
           ( entry_count - index - 1 ) * sizeof( entries[0] ) );
  entry_count--;

  if( current == index ) current = -1;
  else if( current > index ) current--;

  autosave();
}

void
ayplaylist_clear( void )
{
  int i;

  load_saved();
  for( i = 0; i < entry_count; i++ ) libspectrum_free( entries[i].path );
  entry_count = 0;
  current = -1;
  autosave();
}

int
ayplaylist_move( int index, int direction )
{
  int other = index + direction;
  ayplaylist_entry swap;

  load_saved();
  if( index < 0 || index >= entry_count || other < 0 ||
      other >= entry_count )
    return index;

  swap = entries[ index ];
  entries[ index ] = entries[ other ];
  entries[ other ] = swap;

  if( current == index ) current = other;
  else if( current == other ) current = index;

  autosave();
  return other;
}

int
ayplaylist_load_m3u( const char *path, int append )
{
  char line[ 2048 ];
  FILE *f;
  int before;

  f = fopen( path, "r" );
  if( !f ) {
    ui_error( UI_ERROR_ERROR, "couldn't open '%s'", path );
    return 1;
  }

  loaded = 1;
  if( !append ) {
    int i;
    for( i = 0; i < entry_count; i++ ) libspectrum_free( entries[i].path );
    entry_count = 0;
    current = -1;
  }
  before = entry_count;

  loading = 1;
  missing_files = 0;
  while( fgets( line, sizeof( line ), f ) ) {
    char *end = line + strlen( line ), *mark;
    int song = -1;

    while( end > line && ( end[-1] == '\n' || end[-1] == '\r' ||
                           end[-1] == ' ' ) )
      *--end = '\0';
    if( !line[0] || line[0] == '#' ) continue;

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
        add_songs( full, song );
        continue;
      }
    }
    add_songs( line, song );
  }
  loading = 0;
  fclose( f );

  /* The saved list: the missing files just go; a list loaded by the user:
     one message */
  if( missing_files && !quiet )
    ui_error( UI_ERROR_WARNING,
              "%d files of the list are not available and were left out",
              missing_files );

  autosave();
  ( void )before;
  return 0;
}

static int
save_m3u( const char *path )
{
  FILE *f;
  int i;

  f = fopen( path, "w" );
  if( !f ) return 1;

  fprintf( f, "#EXTM3U\n" );
  for( i = 0; i < entry_count; i++ ) {
    const ayplaylist_entry *e = &entries[i];
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
ayplaylist_save_m3u( const char *path )
{
  load_saved();
  if( save_m3u( path ) ) {
    ui_error( UI_ERROR_ERROR, "couldn't write '%s'", path );
    return 1;
  }
  return 0;
}

int
ayplaylist_current( void )
{
  return current;
}

void
ayplaylist_set_current( int index )
{
  current = index >= 0 && index < entry_count ? index : -1;
}

int
ayplaylist_play( int index )
{
  const ayplaylist_entry *e = ayplaylist_get( index );

  if( !e ) return 1;

  if( strcmp( ayplayer_path(), e->path ) ) {
    if( ayplayer_load( e->path ) ) return 1;
  }
  current = index;
  return ayplayer_play( e->song );
}

int
ayplaylist_length( int index, long *frames )
{
  const ayplaylist_entry *e = ayplaylist_get( index );

  if( !e ) return 0;
  return ayplayer_known_length( e->checksum, e->song, frames );
}

int
ayplaylist_open_file( const char *path )
{
  int first = ayplaylist_add_file( path );

  if( first < 0 ) return 1;
  return ayplaylist_play( first );
}

/* winstate.c: What the Win32 user interface remembers by itself
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

/* The places of the windows are kept in fuse-windows.ini in the settings
   folder, written whenever one changes: unlike the settings (fuse.cfg),
   they are remembered without saving the settings.

     [main]
     x=120
     y=80

   The files fuse-window.txt and fuse-keyboard-window.txt of earlier
   versions are read once, then removed. */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"
#include "winstate.h"

#define WINSTATE_FILE "fuse-windows.ini"
#define MAX_ENTRIES 64

typedef struct entry {
  char section[ 32 ];
  char key[ 32 ];
  long value;
} entry;

static entry entries[ MAX_ENTRIES ];
static int entry_count = 0;
static int loaded = 0;

static int
state_path( char *path, size_t size, const char *name )
{
  const char *dir = compat_get_config_path();

  if( !dir ) return 1;
  snprintf( path, size, "%s" FUSE_DIR_SEP_STR "%s", dir, name );
  return 0;
}

static entry*
find( const char *section, const char *key )
{
  int i;

  for( i = 0; i < entry_count; i++ )
    if( !strcmp( entries[i].section, section ) &&
        !strcmp( entries[i].key, key ) )
      return &entries[i];
  return NULL;
}

static void
put( const char *section, const char *key, long value )
{
  entry *e = find( section, key );

  if( !e ) {
    if( entry_count >= MAX_ENTRIES ) return;
    e = &entries[ entry_count++ ];
    snprintf( e->section, sizeof( e->section ), "%s", section );
    snprintf( e->key, sizeof( e->key ), "%s", key );
  }
  e->value = value;
}

static void
save( void )
{
  char path[ 1024 ];
  const char *section = NULL;
  int i, j;
  FILE *f;

  if( state_path( path, sizeof( path ), WINSTATE_FILE ) ) return;
  f = fopen( path, "w" );
  if( !f ) return;

  fprintf( f, "; Fuse: places of the windows\n" );

  /* Section by section, in the order they first appeared */
  for( i = 0; i < entry_count; i++ ) {
    for( j = 0; j < i; j++ )
      if( !strcmp( entries[j].section, entries[i].section ) ) break;
    if( j < i ) continue;

    section = entries[i].section;
    fprintf( f, "\n[%s]\n", section );
    for( j = i; j < entry_count; j++ )
      if( !strcmp( entries[j].section, section ) )
        fprintf( f, "%s=%ld\n", entries[j].key, entries[j].value );
  }

  fclose( f );
}

/* An old file "x=...\ny=...[\nwidth=...\nheight=...]" into a section */
static int
migrate( const char *name, const char *section )
{
  char path[ 1024 ], line[ 128 ], key[ 32 ];
  long value;
  int found = 0;
  FILE *f;

  if( state_path( path, sizeof( path ), name ) ) return 0;
  f = fopen( path, "r" );
  if( !f ) return 0;

  while( fgets( line, sizeof( line ), f ) ) {
    if( sscanf( line, "%31[a-z]=%ld", key, &value ) == 2 ) {
      put( section, key, value );
      found = 1;
    }
  }
  fclose( f );
  remove( path );
  return found;
}

static void
load( void )
{
  char path[ 1024 ], line[ 256 ], section[ 32 ] = "", key[ 32 ];
  long value;
  int migrated = 0;
  FILE *f;

  if( loaded ) return;
  loaded = 1;

  if( !state_path( path, sizeof( path ), WINSTATE_FILE ) &&
      ( f = fopen( path, "r" ) ) ) {
    while( fgets( line, sizeof( line ), f ) ) {
      if( line[0] == '[' ) {
        if( sscanf( line, "[%31[^]]]", section ) != 1 ) section[0] = '\0';
      } else if( section[0] &&
                 sscanf( line, "%31[^=]=%ld", key, &value ) == 2 ) {
        put( section, key, value );
      }
    }
    fclose( f );
  }

  migrated |= migrate( "fuse-window.txt", "main" );
  migrated |= migrate( "fuse-keyboard-window.txt", "keyboard" );
  if( migrated ) save();
}

int
winstate_get( const char *section, const char *key, long *value )
{
  entry *e;

  load();
  e = find( section, key );
  if( !e ) return 0;
  *value = e->value;
  return 1;
}

void
winstate_remove( const char *section, const char *key )
{
  entry *e;

  load();
  e = find( section, key );
  if( !e ) return;
  *e = entries[ --entry_count ];
  save();
}

void
winstate_set( const char *section, const char *key, long value )
{
  entry *e;

  load();
  e = find( section, key );
  if( e && e->value == value ) return;
  put( section, key, value );
  save();
}

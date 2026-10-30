/* aymix.c: The AY mixer settings, and the .mix files of AY songs
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

/* A .mix file is text, one setting a line ("; " starts a comment):

     stereo=ACB
     place_a=Left       (the custom mode: Left, Centre or Right)
     volume_a=100       (0-100)
     pan_a=-100         (-100: left, 0: middle, 100: right)

   and the same for the channels B and C. Song n (1 based) of a file with
   several songs may have one of its own: Song.ay -> Song.3.mix, used
   before Song.mix */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef WIN32
#include <direct.h>
#endif

#include "aymix.h"
#include "compat.h"
#include "fuse.h"
#include "settings.h"
#include "sound.h"
#include "ui/ui.h"
#include "utils.h"

#define MIX_FOLDER "mix"
#define CHANNELS 3

typedef struct mix {
  char stereo[ 16 ];
  int place[ CHANNELS ];
  int volume[ CHANNELS ];
  int pan[ CHANNELS ];		/* SOUND_AY_PAN_MODE: where the mode puts it */
} mix;

static const char * const place_names[] = { "Left", "Centre", "Right" };

/* The settings of the user, while those of a .mix file are used */
static mix user;
static int temporary = 0;
static char loaded_name[ 256 ] = "";

static void ( *changed_hook )( void ) = NULL;

static char **
place_setting( int channel )
{
  switch( channel ) {
  case 0: return &settings_current.stereo_ay_a;
  case 1: return &settings_current.stereo_ay_b;
  default: return &settings_current.stereo_ay_c;
  }
}

static int *
volume_setting( int channel )
{
  switch( channel ) {
  case 0: return &settings_current.volume_ay_a;
  case 1: return &settings_current.volume_ay_b;
  default: return &settings_current.volume_ay_c;
  }
}

static int *
pan_setting( int channel )
{
  switch( channel ) {
  case 0: return &settings_current.pan_ay_a;
  case 1: return &settings_current.pan_ay_b;
  default: return &settings_current.pan_ay_c;
  }
}

void
aymix_set_changed_hook( void ( *hook )( void ) )
{
  changed_hook = hook;
}

void
aymix_changed( void )
{
  if( changed_hook ) changed_hook();
}

/* Apply the settings: the sound generators hear them */
static void
apply( void )
{
  sound_ay_stereo_changed();
  aymix_changed();
}

static void
current( mix *m )
{
  static const int defaults[ CHANNELS ] =
    { SOUND_AY_LEFT, SOUND_AY_CENTRE, SOUND_AY_RIGHT };
  int channel;

  snprintf( m->stereo, sizeof( m->stereo ), "%s",
            settings_current.stereo_ay ? settings_current.stereo_ay : "Mono" );
  for( channel = 0; channel < CHANNELS; channel++ ) {
    m->place[ channel ] = sound_ay_place_setting( *place_setting( channel ),
                                                  defaults[ channel ] );
    m->volume[ channel ] = *volume_setting( channel );
    m->pan[ channel ] = *pan_setting( channel );
  }
}

static void
use( const mix *m )
{
  int channel;

  settings_set_string( &settings_current.stereo_ay, m->stereo );
  for( channel = 0; channel < CHANNELS; channel++ ) {
    settings_set_string( place_setting( channel ),
                         place_names[ m->place[ channel ] ] );
    *volume_setting( channel ) = m->volume[ channel ];
    *pan_setting( channel ) = m->pan[ channel ];
  }
  apply();
}

/* ------------------------------------------------------------------------
   Changes from the windows
   ------------------------------------------------------------------------ */

void
aymix_set_stereo( const char *name )
{
  settings_set_string( &settings_current.stereo_ay, name );
  sound_ay_pans_reset();
  apply();
}

void
aymix_set_place( int channel, int place )
{
  if( channel < 0 || channel >= CHANNELS || place < SOUND_AY_LEFT ||
      place > SOUND_AY_RIGHT ) return;
  settings_set_string( place_setting( channel ), place_names[ place ] );
  sound_ay_pan_set( channel, SOUND_AY_PAN_MODE );
  apply();
}

void
aymix_set_volume( int channel, int volume )
{
  if( channel < 0 || channel >= CHANNELS ) return;
  *volume_setting( channel ) = volume < 0 ? 0 : volume > 100 ? 100 : volume;
  sound_ay_volume_changed();
  aymix_changed();
}

void
aymix_set_pan( int channel, int pan )
{
  if( channel < 0 || channel >= CHANNELS ) return;
  sound_ay_pan_set( channel, pan < -100 ? -100 : pan > 100 ? 100 : pan );
  sound_ay_volume_changed();
  aymix_changed();
}

void
aymix_reset( void )
{
  int channel;

  for( channel = 0; channel < CHANNELS; channel++ )
    *volume_setting( channel ) = 100;
  sound_ay_pans_reset();
  sound_ay_volume_changed();
  aymix_changed();
}

/* ------------------------------------------------------------------------
   .mix files
   ------------------------------------------------------------------------ */

static void
mix_folder( char *buffer, size_t length )
{
  snprintf( buffer, length, "%s" FUSE_DIR_SEP_STR MIX_FOLDER,
            compat_get_config_path() );
}

/* The folder of `path' and its name without the extension */
static void
split( const char *path, char *folder, size_t folder_length, char *base,
       size_t base_length )
{
  const char *name = path, *p, *dot = NULL;

  for( p = path; *p; p++ )
    if( *p == '/' || *p == '\\' ) name = p + 1;
  for( p = name; *p; p++ )
    if( *p == '.' ) dot = p;

  snprintf( folder, folder_length, "%.*s", (int)( name - path ), path );
  snprintf( base, base_length, "%.*s",
            (int)( dot ? dot - name : (int)strlen( name ) ), name );
}

static int
folder_writable( const char *folder )
{
  char test[ 1100 ];
  FILE *f;

  snprintf( test, sizeof( test ), "%s.fuse-mix-test", folder );
  f = fopen( test, "w" );
  if( !f ) return 0;
  fclose( f );
  remove( test );
  return 1;
}

void
aymix_default_path( const char *path, char *buffer, size_t length )
{
  char folder[ 1024 ], base[ 256 ], mixes[ 1024 ];

  if( !path || !*path ) {
    mix_folder( mixes, sizeof( mixes ) );
    snprintf( buffer, length, "%s" FUSE_DIR_SEP_STR "mixer.mix", mixes );
    return;
  }
  split( path, folder, sizeof( folder ), base, sizeof( base ) );
  if( !folder_writable( folder ) ) {
    mix_folder( mixes, sizeof( mixes ) );
#ifdef WIN32
    _mkdir( mixes );
#else
    mkdir( mixes, 0755 );
#endif
    snprintf( buffer, length, "%s" FUSE_DIR_SEP_STR "%s.mix", mixes, base );
    return;
  }
  snprintf( buffer, length, "%s%s.mix", folder, base );
}

int
aymix_save( const char *filename )
{
  mix m;
  FILE *f;
  int channel, stereo = sound_ay_stereo_setting();

  current( &m );
  f = fopen( filename, "w" );
  if( !f ) {
    ui_error( UI_ERROR_ERROR, "Could not write '%s'", filename );
    return 1;
  }
  fprintf( f, "; Fuse AY mixer\n" );
  fprintf( f, "stereo=%s\n", m.stereo );
  for( channel = 0; channel < CHANNELS; channel++ ) {
    fprintf( f, "place_%c=%s\n", 'a' + channel, place_names[ m.place[ channel ] ] );
    fprintf( f, "volume_%c=%d\n", 'a' + channel, m.volume[ channel ] );
    fprintf( f, "pan_%c=%d\n", 'a' + channel,
             sound_ay_channel_pan( stereo, channel ) );
  }
  if( fclose( f ) ) {
    ui_error( UI_ERROR_ERROR, "Could not write '%s'", filename );
    return 1;
  }

  /* What is heard now is this file */
  if( temporary ) {
    const char *name = filename, *p;
    for( p = filename; *p; p++ )
      if( *p == '/' || *p == '\\' ) name = p + 1;
    snprintf( loaded_name, sizeof( loaded_name ), "%s", name );
    aymix_changed();
  }
  return 0;
}

/* Read a .mix file over the settings in `m' */
static int
load( const char *filename, mix *m )
{
  char line[ 256 ], key[ 64 ], value[ 64 ];
  FILE *f = fopen( filename, "r" );

  if( !f ) return 1;
  while( fgets( line, sizeof( line ), f ) ) {
    int channel, number;

    if( sscanf( line, " %63[^= \t] = %63s", key, value ) != 2 ) continue;
    if( key[0] == ';' ) continue;
    if( !strcmp( key, "stereo" ) ) {
      snprintf( m->stereo, sizeof( m->stereo ), "%s", value );
      continue;
    }
    if( strlen( key ) < 3 ) continue;
    channel = key[ strlen( key ) - 1 ] - 'a';
    if( channel < 0 || channel >= CHANNELS ||
        key[ strlen( key ) - 2 ] != '_' ) continue;
    key[ strlen( key ) - 2 ] = '\0';
    number = atoi( value );
    if( !strcmp( key, "place" ) )
      m->place[ channel ] = sound_ay_place_setting( value, m->place[ channel ] );
    else if( !strcmp( key, "volume" ) && number >= 0 && number <= 100 )
      m->volume[ channel ] = number;
    else if( !strcmp( key, "pan" ) && number >= -100 && number <= 100 )
      m->pan[ channel ] = number;
  }
  fclose( f );
  return 0;
}

/* The .mix file of a song: of the song itself, then of the file; next to
   the song, then in the mix folder of Fuse */
static int
find( const char *path, int song, int songs, char *found, size_t length )
{
  char folder[ 1024 ], base[ 256 ], mixes[ 1024 ];
  const char *folders[ 2 ];
  int i, own;

  split( path, folder, sizeof( folder ), base, sizeof( base ) );
  mix_folder( mixes, sizeof( mixes ) );
  strncat( mixes, FUSE_DIR_SEP_STR, sizeof( mixes ) - strlen( mixes ) - 1 );
  folders[0] = folder;
  folders[1] = mixes;

  for( i = 0; i < 2; i++ )
    for( own = songs > 1; own >= 0; own-- ) {
      FILE *f;
      if( own )
        snprintf( found, length, "%s%s.%d.mix", folders[i], base, song + 1 );
      else
        snprintf( found, length, "%s%s.mix", folders[i], base );
      f = fopen( found, "r" );
      if( f ) {
        fclose( f );
        return 1;
      }
    }
  return 0;
}

void
aymix_restore_user( void )
{
  if( !temporary ) return;
  temporary = 0;
  loaded_name[0] = '\0';
  use( &user );
}

void
aymix_song_started( const char *path, int song, int songs, int automatic )
{
  char filename[ 1100 ];
  const char *name, *p;
  mix m;

  if( !automatic || !path || !*path ||
      !find( path, song, songs, filename, sizeof( filename ) ) ) {
    aymix_restore_user();
    return;
  }

  /* The settings of the user are kept while a .mix file is in use */
  if( !temporary ) current( &user );
  m = user;
  if( load( filename, &m ) ) {
    aymix_restore_user();
    return;
  }
  temporary = 1;
  name = filename;
  for( p = filename; *p; p++ )
    if( *p == '/' || *p == '\\' ) name = p + 1;
  snprintf( loaded_name, sizeof( loaded_name ), "%s", name );
  use( &m );
}

const char *
aymix_loaded_name( void )
{
  return temporary ? loaded_name : "";
}

void
aymix_user_settings( settings_info *settings )
{
  if( !temporary ) return;
  settings_set_string( &settings->stereo_ay, user.stereo );
  settings_set_string( &settings->stereo_ay_a, place_names[ user.place[0] ] );
  settings_set_string( &settings->stereo_ay_b, place_names[ user.place[1] ] );
  settings_set_string( &settings->stereo_ay_c, place_names[ user.place[2] ] );
  settings->volume_ay_a = user.volume[0];
  settings->volume_ay_b = user.volume[1];
  settings->volume_ay_c = user.volume[2];
  settings->pan_ay_a = user.pan[0];
  settings->pan_ay_b = user.pan[1];
  settings->pan_ay_c = user.pan[2];
}

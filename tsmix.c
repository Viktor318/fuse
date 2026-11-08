/* tsmix.c: The TurboSound mixer settings, and their .mix files
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

/* The settings are kept in fuse.cfg as five settings: the stereo mode
   (tsmixer_stereo), the master volume (tsmixer_volume), and for the nine
   channels (A, B, C of the first chip, then of the second and the third)
   the volumes, the panoramas (SOUND_AY_PAN_MODE: where the mode puts the
   channel) and whether they are heard, as numbers apart by spaces
   (tsmixer_volumes, tsmixer_pans) and a string of 0s and 1s
   (tsmixer_channels).

   A .mix file of the TurboSound mixer is that of the AY mixer (aymix.c)
   with more lines:

     stereo=ABC
     volume=80          (the master volume)
     volume_a=100       (channel A of the first chip, as in the AY mixer)
     pan_a=-100
     on_a=1             (heard: 1, switched off: 0)
     volume_2a=85       (channel A of the second chip; _3a: of the third)
     pan_2a=-100
     on_2a=1

   The AY mixer reads the keys of the first chip and the stereo mode of
   such a file, and leaves the others. A file of the AY mixer gives its
   A, B and C to the channels of every chip */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ayplay.h"
#include "aymix.h"
#include "settings.h"
#include "sound.h"
#include "tsmix.h"
#include "ui/ui.h"

#define CHIPS TSMIX_CHIPS
#define CHANNELS TSMIX_CHANNELS
#define DEFAULT_STEREO "ABC"

typedef struct mix {
  char stereo[ 16 ];
  int master;
  int volume[ CHIPS ][ CHANNELS ];
  int pan[ CHIPS ][ CHANNELS ];	/* SOUND_AY_PAN_MODE: where the mode puts it */
  int on[ CHIPS ][ CHANNELS ];
} mix;

/* The settings of the user, while those of a .mix file are used */
static mix user;
static int temporary = 0;
static char loaded_name[ 256 ] = "";

static void ( *changed_hook )( void ) = NULL;

void
tsmix_set_changed_hook( void ( *hook )( void ) )
{
  changed_hook = hook;
}

/* ------------------------------------------------------------------------
   The settings
   ------------------------------------------------------------------------ */

static int
clamp( int value, int low, int high )
{
  return value < low ? low : value > high ? high : value;
}

/* Up to CHIPS * CHANNELS numbers apart by spaces; those missing or out of
   range: `otherwise' */
static void
read_numbers( const char *text, int values[ CHIPS ][ CHANNELS ],
              int otherwise, int low, int high, int also )
{
  const char *p = text;
  int i;

  for( i = 0; i < CHIPS * CHANNELS; i++ ) {
    char *end;
    long value = otherwise;

    if( p && *p ) {
      value = strtol( p, &end, 10 );
      if( end == p ) {
        value = otherwise;
        p = NULL;
      } else {
        p = end;
        if( ( value < low || value > high ) && value != also )
          value = otherwise;
      }
    }
    values[ i / CHANNELS ][ i % CHANNELS ] = value;
  }
}

static void
write_numbers( char **setting, int values[ CHIPS ][ CHANNELS ] )
{
  char text[ CHIPS * CHANNELS * 8 ];
  size_t used = 0;
  int i;

  text[0] = '\0';
  for( i = 0; i < CHIPS * CHANNELS; i++ ) {
    snprintf( text + used, sizeof( text ) - used, "%s%d", i ? " " : "",
              values[ i / CHANNELS ][ i % CHANNELS ] );
    used += strlen( text + used );
  }
  settings_set_string( setting, text );
}

static void
read_settings( const settings_info *settings, mix *m )
{
  const char *on = settings->tsmixer_channels;
  int i;

  snprintf( m->stereo, sizeof( m->stereo ), "%s",
            settings->tsmixer_stereo ? settings->tsmixer_stereo :
                                       DEFAULT_STEREO );
  m->master = clamp( settings->tsmixer_volume, 0, 100 );
  read_numbers( settings->tsmixer_volumes, m->volume, 100, 0, 100, 100 );
  read_numbers( settings->tsmixer_pans, m->pan, SOUND_AY_PAN_MODE, -100, 100,
                SOUND_AY_PAN_MODE );
  for( i = 0; i < CHIPS * CHANNELS; i++ )
    m->on[ i / CHANNELS ][ i % CHANNELS ] =
      !( on && (int)strlen( on ) > i && on[i] == '0' );
}

static void
write_settings( settings_info *settings, mix *m )
{
  char on[ CHIPS * CHANNELS + 1 ];
  int i;

  settings_set_string( &settings->tsmixer_stereo, m->stereo );
  settings->tsmixer_volume = m->master;
  write_numbers( &settings->tsmixer_volumes, m->volume );
  write_numbers( &settings->tsmixer_pans, m->pan );
  for( i = 0; i < CHIPS * CHANNELS; i++ )
    on[i] = m->on[ i / CHANNELS ][ i % CHANNELS ] ? '1' : '0';
  on[ CHIPS * CHANNELS ] = '\0';
  settings_set_string( &settings->tsmixer_channels, on );
}

static void
current( mix *m )
{
  read_settings( &settings_current, m );
}

/* The settings are heard at once, and every window shows them */
static void
use( mix *m )
{
  write_settings( &settings_current, m );
  ayplay_update_mix();
  if( changed_hook ) changed_hook();
}

/* Where a stereo mode puts a channel; the custom mode starts from ABC */
static int
mode_pan( int stereo, int channel )
{
  int position[ 3 ];

  if( stereo == SOUND_STEREO_AY_NONE ) return 0;
  if( stereo == SOUND_STEREO_AY_CUSTOM ) stereo = SOUND_STEREO_AY_ABC;
  sound_ay_channel_positions( stereo, position );
  return position[ channel ] == SOUND_AY_LEFT ? -100 :
         position[ channel ] == SOUND_AY_RIGHT ? 100 : 0;
}

static int
heard_pan( const mix *m, int chip, int channel )
{
  int stereo = sound_ay_stereo_from_name( m->stereo );
  int pan = m->pan[ chip ][ channel ];

  if( stereo == SOUND_STEREO_AY_NONE ) return 0;
  if( pan >= -100 && pan <= 100 ) return pan;
  return mode_pan( stereo, channel );
}

static int
valid( int chip, int channel )
{
  return chip >= 0 && chip < CHIPS && channel >= 0 && channel < CHANNELS;
}

int
tsmix_stereo( void )
{
  mix m;

  current( &m );
  return sound_ay_stereo_from_name( m.stereo );
}

const char *
tsmix_stereo_name( void )
{
  return settings_current.tsmixer_stereo ? settings_current.tsmixer_stereo :
                                           DEFAULT_STEREO;
}

int
tsmix_master( void )
{
  return clamp( settings_current.tsmixer_volume, 0, 100 );
}

int
tsmix_volume( int chip, int channel )
{
  mix m;

  if( !valid( chip, channel ) ) return 0;
  current( &m );
  return m.volume[ chip ][ channel ];
}

int
tsmix_pan( int chip, int channel )
{
  mix m;

  if( !valid( chip, channel ) ) return 0;
  current( &m );
  return heard_pan( &m, chip, channel );
}

int
tsmix_on( int chip, int channel )
{
  mix m;

  if( !valid( chip, channel ) ) return 0;
  current( &m );
  return m.on[ chip ][ channel ];
}

/* ------------------------------------------------------------------------
   Changes from the windows
   ------------------------------------------------------------------------ */

void
tsmix_set_stereo( const char *name )
{
  mix m;
  int chip, channel;

  current( &m );
  if( sound_ay_stereo_from_name( name ) == SOUND_STEREO_AY_CUSTOM ) {
    /* Each panorama stays where it is heard now */
    for( chip = 0; chip < CHIPS; chip++ )
      for( channel = 0; channel < CHANNELS; channel++ )
        m.pan[ chip ][ channel ] =
          sound_ay_stereo_from_name( m.stereo ) == SOUND_STEREO_AY_NONE ?
          mode_pan( SOUND_STEREO_AY_CUSTOM, channel ) :
          heard_pan( &m, chip, channel );
  } else {
    for( chip = 0; chip < CHIPS; chip++ )
      for( channel = 0; channel < CHANNELS; channel++ )
        m.pan[ chip ][ channel ] = SOUND_AY_PAN_MODE;
  }
  snprintf( m.stereo, sizeof( m.stereo ), "%s", name );
  use( &m );
}

void
tsmix_set_master( int volume )
{
  mix m;

  current( &m );
  m.master = clamp( volume, 0, 100 );
  use( &m );
}

void
tsmix_set_volume( int chip, int channel, int volume )
{
  mix m;

  if( !valid( chip, channel ) ) return;
  current( &m );
  m.volume[ chip ][ channel ] = clamp( volume, 0, 100 );
  use( &m );
}

void
tsmix_set_pan( int chip, int channel, int pan )
{
  mix m;

  if( !valid( chip, channel ) ) return;
  current( &m );
  m.pan[ chip ][ channel ] = clamp( pan, -100, 100 );
  use( &m );
}

void
tsmix_set_on( int chip, int channel, int on )
{
  mix m;

  if( !valid( chip, channel ) ) return;
  current( &m );
  m.on[ chip ][ channel ] = on ? 1 : 0;
  use( &m );
}

void
tsmix_reset( void )
{
  mix m;
  int chip, channel;

  current( &m );
  for( chip = 0; chip < CHIPS; chip++ )
    for( channel = 0; channel < CHANNELS; channel++ ) {
      m.volume[ chip ][ channel ] = 100;
      m.pan[ chip ][ channel ] = SOUND_AY_PAN_MODE;
      m.on[ chip ][ channel ] = 1;
    }
  use( &m );
}

/* ------------------------------------------------------------------------
   .mix files
   ------------------------------------------------------------------------ */

int
tsmix_save( const char *filename )
{
  mix m;
  FILE *f;
  int chip, channel;

  current( &m );
  f = fopen( filename, "w" );
  if( !f ) {
    ui_error( UI_ERROR_ERROR, "Could not write '%s'", filename );
    return 1;
  }
  fprintf( f, "; Fuse TurboSound mixer\n" );
  fprintf( f, "stereo=%s\n", m.stereo );
  fprintf( f, "volume=%d\n", m.master );
  for( chip = 0; chip < CHIPS; chip++ )
    for( channel = 0; channel < CHANNELS; channel++ ) {
      char key[ 8 ];
      /* The first chip as in the AY mixer: _a; the others _2a, _3a */
      if( chip ) snprintf( key, sizeof( key ), "%d%c", chip + 1,
                           'a' + channel );
      else snprintf( key, sizeof( key ), "%c", 'a' + channel );
      fprintf( f, "volume_%s=%d\n", key, m.volume[ chip ][ channel ] );
      fprintf( f, "pan_%s=%d\n", key, heard_pan( &m, chip, channel ) );
      fprintf( f, "on_%s=%d\n", key, m.on[ chip ][ channel ] );
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
    if( changed_hook ) changed_hook();
  }
  return 0;
}

enum { KEY_VOLUME, KEY_PAN, KEY_ON, KEY_PLACE, KEYS };

/* Read a .mix file over the settings in `m'. A key without a chip (_a) is
   that of the first chip, and of the others too unless they have their
   own (_2a, _3a): so a file of the AY mixer is heard on every chip */
static int
load( const char *filename, mix *m )
{
  static const char * const names[ KEYS ] = { "volume", "pan", "on", "place" };
  char line[ 256 ], key[ 64 ], value[ 64 ];
  int values[ KEYS ][ CHIPS ][ CHANNELS ], set[ KEYS ][ CHIPS ][ CHANNELS ];
  int chip, channel, k, stereo;
  FILE *f = fopen( filename, "r" );

  if( !f ) return 1;
  memset( set, 0, sizeof( set ) );
  memset( values, 0, sizeof( values ) );

  while( fgets( line, sizeof( line ), f ) ) {
    size_t length;
    int number;

    if( sscanf( line, " %63[^= \t] = %63s", key, value ) != 2 ) continue;
    if( key[0] == ';' ) continue;
    if( !strcmp( key, "stereo" ) ) {
      snprintf( m->stereo, sizeof( m->stereo ), "%s", value );
      continue;
    }
    number = atoi( value );
    if( !strcmp( key, "volume" ) ) {
      if( number >= 0 && number <= 100 ) m->master = number;
      continue;
    }

    /* name_a, name_2a */
    length = strlen( key );
    if( length < 3 ) continue;
    channel = key[ length - 1 ] - 'a';
    if( channel < 0 || channel >= CHANNELS ) continue;
    chip = 0;
    if( key[ length - 2 ] >= '1' && key[ length - 2 ] <= '0' + CHIPS &&
        length >= 4 && key[ length - 3 ] == '_' ) {
      chip = key[ length - 2 ] - '1';
      key[ length - 3 ] = '\0';
    } else if( key[ length - 2 ] == '_' ) {
      key[ length - 2 ] = '\0';
    } else {
      continue;
    }

    for( k = 0; k < KEYS; k++ )
      if( !strcmp( key, names[k] ) ) break;
    if( k == KEYS ) continue;
    if( k == KEY_PLACE ) {
      number = sound_ay_place_setting( value, -1 );
      if( number < 0 ) continue;
    } else if( k == KEY_VOLUME ? number < 0 || number > 100 :
               k == KEY_PAN ? number < -100 || number > 100 : 0 ) {
      continue;
    }
    values[k][ chip ][ channel ] = number;
    set[k][ chip ][ channel ] = 1;
  }
  fclose( f );

  stereo = sound_ay_stereo_from_name( m->stereo );
  for( chip = 0; chip < CHIPS; chip++ )
    for( channel = 0; channel < CHANNELS; channel++ ) {
      int got[ KEYS ], v[ KEYS ];
      for( k = 0; k < KEYS; k++ ) {
        int from = set[k][ chip ][ channel ] ? chip : 0;
        got[k] = set[k][ from ][ channel ];
        v[k] = values[k][ from ][ channel ];
      }
      if( got[ KEY_VOLUME ] ) m->volume[ chip ][ channel ] = v[ KEY_VOLUME ];
      if( got[ KEY_ON ] ) m->on[ chip ][ channel ] = v[ KEY_ON ] != 0;
      if( got[ KEY_PAN ] ) {
        m->pan[ chip ][ channel ] = v[ KEY_PAN ];
      } else if( got[ KEY_PLACE ] && stereo == SOUND_STEREO_AY_CUSTOM ) {
        /* An old file of the custom mode: the place of the channel */
        m->pan[ chip ][ channel ] = v[ KEY_PLACE ] == SOUND_AY_LEFT ? -100 :
                                    v[ KEY_PLACE ] == SOUND_AY_RIGHT ? 100 :
                                    0;
      } else {
        m->pan[ chip ][ channel ] = SOUND_AY_PAN_MODE;
      }
    }
  return 0;
}

void
tsmix_restore_user( void )
{
  if( !temporary ) return;
  temporary = 0;
  loaded_name[0] = '\0';
  use( &user );
}

static void
set_loaded_name( const char *path, const char *more )
{
  const char *name = path, *p;

  for( p = path; *p; p++ )
    if( *p == '/' || *p == '\\' ) name = p + 1;
  if( more )
    snprintf( loaded_name, sizeof( loaded_name ), "%s (%s)", name, more );
  else
    snprintf( loaded_name, sizeof( loaded_name ), "%s", name );
}

/* A .mix file over the settings of the user, until the next song without
   one */
static int
use_file( const char *filename )
{
  mix m;

  if( !temporary ) current( &user );
  m = user;
  if( load( filename, &m ) ) return 1;
  temporary = 1;
  set_loaded_name( filename, NULL );
  use( &m );
  return 0;
}

int
tsmix_load( const char *filename )
{
  if( use_file( filename ) ) {
    ui_error( UI_ERROR_ERROR, "couldn't open '%s'", filename );
    return 1;
  }
  return 0;
}

void
tsmix_song_started( const char *path, int song, int songs, int automatic,
                    const char *file_stereo )
{
  char filename[ 1100 ];
  int chip, channel;
  mix m;

  if( automatic && path && *path &&
      aymix_find( path, song, songs, filename, sizeof( filename ) ) ) {
    if( !use_file( filename ) ) return;
  } else if( automatic && path && *path && file_stereo && *file_stereo ) {
    /* The stereo mode the music file asks for, with the user's volumes */
    if( !temporary ) current( &user );
    m = user;
    snprintf( m.stereo, sizeof( m.stereo ), "%s", file_stereo );
    for( chip = 0; chip < CHIPS; chip++ )
      for( channel = 0; channel < CHANNELS; channel++ )
        m.pan[ chip ][ channel ] = SOUND_AY_PAN_MODE;
    temporary = 1;
    set_loaded_name( path, file_stereo );
    use( &m );
    return;
  }
  tsmix_restore_user();
}

const char *
tsmix_loaded_name( void )
{
  return temporary ? loaded_name : "";
}

void
tsmix_user_settings( settings_info *settings )
{
  if( temporary ) write_settings( settings, &user );
}

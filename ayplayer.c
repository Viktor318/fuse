/* ayplayer.c: Play the songs of .ay (ZXAYEMUL) music files
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   Starting a song follows the AY player of ZEsarUX (audio_ayplayer.c,
   Copyright (C) 2013 Cesar Hernandez Bano, GPL 3 or later), so this file
   is distributed under the GPL version 3 or later.

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

/* An .ay file holds the music player code of a game and its data, not a
   recording. A song runs on a processor of its own (aysource.c), apart from
   the emulated machine, which goes on with what it was doing (ayplay.c).
   Pro Tracker 3 modules give the AY registers of every frame themselves.

   All relative pointers in the file are big endian words, counted from the
   position of the pointer itself. */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "ayplay.h"
#include "ayplayer.h"
#include "aymeasure.h"
#include "aymix.h"
#include "fuse.h"
#include "pt3player.h"
#include "compat.h"
#include "sound.h"
#include "ui/ui.h"
#include "utils.h"

/* The loaded file */
static libspectrum_byte *ay_data = NULL;
static size_t ay_length = 0;

static char ay_filename[ 256 ];
static char ay_author[ 256 ];
static char ay_misc[ 256 ];

/* A Pro Tracker 3 module instead of an AY file (only for its texts here:
   aysource.c plays it) */
static pt3_module *ay_pt3 = NULL;

static int ay_songs = 0;		/* number of songs */
static int ay_first_song = 0;		/* 0 based */

/* Song being played, 0 based; -1 when stopped */
static int ay_current = -1;

/* Length of the current song in frames (0: not known) */
static long ay_song_frames = 0;

/* The current song ended (its length was reached) */
static int ay_song_ended = 0;

/* Songs without a length in the file go on for ever: as NEZplug does,
   they get a default length (5 minutes) and fade out in the last
   5 seconds (ayplay.c), when the player goes on to the next song */
#define AY_DEFAULT_LENGTH ( 5 * 60 * 50 )
static int ay_length_default = 0;

/* Where the music goes on from after the end of the song (-1: the music
   ends there; songs of default length: 0) */
static long ay_song_loop = -1;
/* Repeat the song: songs that go round just go on playing */
static int ay_repeat_song = 0;

/* Measuring the length of a song: the first time a song is played, it is
   measured in the background (aymeasure.c) while it plays, and the length
   is kept in a file next to the settings, by file checksum */
#define AY_LENGTHS_FILE "fuse_ay_lengths.txt"
/* Lengths found by the measuring of this version. Version 1 gave up after
   15 seconds, so its "no repetition found" lines are dropped and measured
   again. Lines of the versions before 4 (which did not know digital music
   or a standing volume as silence) are kept and used, but measured again
   when the song is played or the lengths of the list are measured */
#define AY_LENGTHS_VERSION 5

static int ay_length_measured = 0;
static libspectrum_dword ay_file_checksum = 0;

/* The song being measured (one at a time) */
static int measure_busy = 0;
static long measure_id = 0;
static libspectrum_dword measure_checksum;
static int measure_song;

/* Full path of the loaded file */
static char ay_path[ 1024 ] = "";

/* ------------------------------------------------------------------------
   Reading the file
   ------------------------------------------------------------------------ */

static int
byte_at( size_t offset )
{
  if( offset >= ay_length ) return -1;
  return ay_data[ offset ];
}

static int
word_at( size_t offset )
{
  if( offset + 1 >= ay_length ) return -1;
  return ( ay_data[ offset ] << 8 ) | ay_data[ offset + 1 ];
}

/* Follow the relative pointer at `offset'; -1 if it points outside */
static long
pointer_at( size_t offset )
{
  int value = word_at( offset );
  long target;

  if( value < 0 ) return -1;
  /* As in ZEsarUX: the offset is unsigned */
  target = (long)offset + value;
  if( target < 0 || (size_t)target >= ay_length ) return -1;
  return target;
}

/* Copy the zero terminated string at the relative pointer */
static void
string_at( size_t offset, char *text, size_t length )
{
  long p = pointer_at( offset );
  size_t i = 0;

  if( p >= 0 ) {
    while( i + 1 < length && (size_t)p + i < ay_length &&
           ay_data[ p + i ] ) {
      libspectrum_byte c = ay_data[ p + i ];
      text[ i ] = ( c >= 32 && c < 127 ) ? c : '?';
      i++;
    }
  }
  text[ i ] = '\0';
}

/* Offset of the song data of song `song' (0 based), -1 if not valid */
static long
song_data( int song )
{
  long songs = pointer_at( 18 );
  if( songs < 0 ) return -1;
  return pointer_at( songs + song * 4 + 2 );
}

static int
has_extension( const char *filename, const char *extension )
{
  size_t length = strlen( filename ), ext = strlen( extension ), i;

  if( length <= ext + 1 || filename[ length - ext - 1 ] != '.' ) return 0;
  for( i = 0; i < ext; i++ ) {
    char c = filename[ length - ext + i ];
    if( c >= 'A' && c <= 'Z' ) c += 'a' - 'A';
    if( c != extension[i] ) return 0;
  }
  return 1;
}

/* The files of the player: AY files and Pro Tracker 3 modules */
int
ayplayer_is_ay_file( const char *filename )
{
  return has_extension( filename, "ay" ) || has_extension( filename, "pt3" );
}

static int
is_ay_data( const libspectrum_byte *data, size_t length )
{
  return length >= 20 && !memcmp( data, "ZXAYEMUL", 8 );
}

/* ------------------------------------------------------------------------
   Opening and closing
   ------------------------------------------------------------------------ */

static void
free_file( void )
{
  libspectrum_free( ay_data );
  ay_data = NULL;
  ay_length = 0;
  if( ay_pt3 ) pt3_free( ay_pt3 );
  ay_pt3 = NULL;
  ay_songs = 0;
  ay_current = -1;
}

/* CRC-32 of the file: identifies it in the lengths file */
static libspectrum_dword
checksum( const libspectrum_byte *data, size_t length )
{
  libspectrum_dword crc = 0xffffffff;
  size_t i;
  int bit;

  for( i = 0; i < length; i++ ) {
    crc ^= data[i];
    for( bit = 0; bit < 8; bit++ )
      crc = ( crc >> 1 ) ^ ( 0xedb88320 & -( crc & 1 ) );
  }
  return ~crc;
}

static void
lengths_path( char *path, size_t size )
{
  snprintf( path, size, "%s" FUSE_DIR_SEP_STR "%s", compat_get_config_path(),
            AY_LENGTHS_FILE );
}

/* Lines: checksum (hex), song (0 based), length in frames (0: no repetition
   found, the default length is used), the frame the music goes on from
   after the end (-1: the music ends), the version of the measuring (none:
   the first one). The last line of a song counts.

   Read once, then kept in memory, with a hash table to find a song at once
   (the playlist looks up all of its songs). New lengths are added to the end
   of the file; when a song is in it more than once, it is written again
   when read, with one line for each song */
typedef struct length_entry {
  libspectrum_dword sum;
  int song, version;
  long frames, loop;
} length_entry;

static length_entry *lengths = NULL;
static size_t lengths_count = 0, lengths_size = 0;
static int lengths_read = 0;

/* Open addressing: entry index + 1, 0 for an empty slot. Kept at most half
   full */
static size_t *lengths_table = NULL;
static size_t lengths_table_size = 0;

static size_t
lengths_hash( libspectrum_dword sum, int song )
{
  libspectrum_dword h = sum ^ ( (libspectrum_dword)song * 0x9e3779b9U );
  h ^= h >> 16;
  h *= 0x85ebca6bU;
  h ^= h >> 13;
  return h;
}

/* The slot of the song: where it is, or the empty one where it goes */
static size_t
lengths_slot( libspectrum_dword sum, int song )
{
  size_t mask = lengths_table_size - 1;
  size_t slot = lengths_hash( sum, song ) & mask;

  while( lengths_table[ slot ] ) {
    const length_entry *e = &lengths[ lengths_table[ slot ] - 1 ];
    if( e->sum == sum && e->song == song ) break;
    slot = ( slot + 1 ) & mask;
  }
  return slot;
}

static void
lengths_table_grow( void )
{
  size_t i;

  lengths_table_size = lengths_table_size ? lengths_table_size * 2 : 256;
  libspectrum_free( lengths_table );
  lengths_table = libspectrum_new0( size_t, lengths_table_size );
  for( i = 0; i < lengths_count; i++ )
    lengths_table[ lengths_slot( lengths[i].sum, lengths[i].song ) ] = i + 1;
}

/* 1 if the song was there already */
static int
lengths_remember( libspectrum_dword sum, int song, long frames, long loop,
                  int version )
{
  size_t slot, i;
  int found;

  if( ( lengths_count + 1 ) * 2 > lengths_table_size ) lengths_table_grow();
  slot = lengths_slot( sum, song );
  found = lengths_table[ slot ] != 0;

  if( found ) {
    i = lengths_table[ slot ] - 1;
  } else {
    if( lengths_count == lengths_size ) {
      lengths_size = lengths_size ? lengths_size * 2 : 64;
      lengths = libspectrum_renew( length_entry, lengths, lengths_size );
    }
    i = lengths_count++;
    lengths_table[ slot ] = i + 1;
  }
  lengths[i].sum = sum;
  lengths[i].song = song;
  lengths[i].frames = frames;
  lengths[i].loop = loop;
  lengths[i].version = version;
  return found;
}

/* The file again, one line for each song. Written next to it first, so
   that nothing is lost if writing fails */
static void
lengths_rewrite( const char *path )
{
  char new_path[ 1040 ];
  size_t i;
  FILE *f;

  snprintf( new_path, sizeof( new_path ), "%s.new", path );
  f = fopen( new_path, "w" );
  if( !f ) return;
  for( i = 0; i < lengths_count; i++ ) {
    const length_entry *e = &lengths[i];
    if( e->version > 1 )
      fprintf( f, "%08lx %d %ld %ld %d\n", (unsigned long)e->sum, e->song,
               e->frames, e->loop, e->version );
    else
      fprintf( f, "%08lx %d %ld %ld\n", (unsigned long)e->sum, e->song,
               e->frames, e->loop );
  }
  if( fclose( f ) ) {
    remove( new_path );
    return;
  }
  remove( path );
  rename( new_path, path );
}

static void
lengths_load( void )
{
  char path[ 1024 ], new_path[ 1040 ], line[ 128 ];
  unsigned long file_sum;
  int file_song, version, fields, rewrite = 0;
  long file_frames, file_loop;
  FILE *f;

  lengths_read = 1;
  lengths_table_grow();
  lengths_path( path, sizeof( path ) );
  f = fopen( path, "r" );
  if( !f ) {
    /* Stopped while it was written again: the new one is complete */
    snprintf( new_path, sizeof( new_path ), "%s.new", path );
    if( rename( new_path, path ) ) return;
    f = fopen( path, "r" );
    if( !f ) return;
  }

  while( fgets( line, sizeof( line ), f ) ) {
    version = 1;
    fields = sscanf( line, "%lx %d %ld %ld %d", &file_sum, &file_song,
                     &file_frames, &file_loop, &version );
    if( fields < 4 ) continue;
    /* Given up by an earlier measuring: measured again */
    if( version < AY_LENGTHS_VERSION && file_frames <= 0 ) {
      rewrite = 1;
      continue;
    }
    if( lengths_remember( file_sum, file_song, file_frames, file_loop,
                          version ) )
      rewrite = 1;
  }
  fclose( f );

  if( rewrite ) lengths_rewrite( path );
}

static int
lengths_lookup( libspectrum_dword sum, int song, long *frames, long *loop )
{
  size_t slot;

  if( !lengths_read ) lengths_load();

  slot = lengths_slot( sum, song );
  if( !lengths_table[ slot ] ) return 0;
  *frames = lengths[ lengths_table[ slot ] - 1 ].frames;
  *loop = lengths[ lengths_table[ slot ] - 1 ].loop;
  return 1;
}

/* Known and measured by this version: no need to measure it again */
static int
lengths_current( libspectrum_dword sum, int song )
{
  size_t slot;

  if( !lengths_read ) lengths_load();

  slot = lengths_slot( sum, song );
  return lengths_table[ slot ] &&
         lengths[ lengths_table[ slot ] - 1 ].version >= AY_LENGTHS_VERSION;
}

static void
lengths_store( libspectrum_dword sum, int song, long frames, long loop )
{
  char path[ 1024 ];
  FILE *f;

  if( !lengths_read ) lengths_load();
  lengths_remember( sum, song, frames, loop, AY_LENGTHS_VERSION );

  lengths_path( path, sizeof( path ) );
  f = fopen( path, "a" );
  if( !f ) return;
  fprintf( f, "%08lx %d %ld %ld %d\n", (unsigned long)sum, song, frames, loop,
           AY_LENGTHS_VERSION );
  fclose( f );
}

int
ayplayer_known_length( libspectrum_dword checksum, int song, long *frames )
{
  long loop;
  return lengths_lookup( checksum, song, frames, &loop );
}

/* Check and copy the file; on success it belongs to the caller */
static int
read_ay_file( const char *filename, libspectrum_byte **data, size_t *length )
{
  utils_file file;

  if( utils_read_file( filename, &file ) ) return 1;

  if( !is_ay_data( file.buffer, file.length ) &&
      !pt3_check( file.buffer, file.length ) ) {
    ui_error( UI_ERROR_ERROR, "'%s' is not an AY music file", filename );
    utils_close_file( &file );
    return 1;
  }

  *data = libspectrum_new( libspectrum_byte, file.length );
  memcpy( *data, file.buffer, file.length );
  *length = file.length;
  utils_close_file( &file );
  return 0;
}

int
ayplayer_load( const char *filename )
{
  libspectrum_byte *data;
  size_t length;
  const char *name, *p;

  if( read_ay_file( filename, &data, &length ) ) return 1;

  ayplayer_stop();
  free_file();

  ay_data = data;
  ay_length = length;
  ay_file_checksum = checksum( ay_data, ay_length );

  if( !is_ay_data( ay_data, ay_length ) ) {
    ay_pt3 = pt3_load( ay_data, ay_length );
    if( !ay_pt3 ) {
      ui_error( UI_ERROR_ERROR, "'%s' is not a valid PT3 music file",
                filename );
      free_file();
      return 1;
    }
  } else if( song_data( 0 ) < 0 ) {
    ui_error( UI_ERROR_ERROR, "'%s' is not a valid AY music file", filename );
    free_file();
    return 1;
  }

  snprintf( ay_path, sizeof( ay_path ), "%s", filename );
  name = filename;
  for( p = filename; *p; p++ )
    if( *p == '/' || *p == '\\' ) name = p + 1;
  snprintf( ay_filename, sizeof( ay_filename ), "%s", name );

  if( ay_pt3 ) {
    snprintf( ay_author, sizeof( ay_author ), "%s", pt3_author( ay_pt3 ) );
    ay_misc[0] = '\0';
    ay_songs = 1;
    ay_first_song = 0;
    return 0;
  }

  string_at( 12, ay_author, sizeof( ay_author ) );
  string_at( 14, ay_misc, sizeof( ay_misc ) );

  ay_songs = byte_at( 16 ) + 1;
  ay_first_song = byte_at( 17 );
  if( ay_first_song < 0 || ay_first_song >= ay_songs ) ay_first_song = 0;

  return 0;
}

const char *
ayplayer_path( void )
{
  return ay_data ? ay_path : "";
}

/* The songs of a file, without loading it for playing */
int
ayplayer_file_info( const char *filename, ayplayer_info *info )
{
  libspectrum_byte *data, *saved_data = ay_data;
  size_t length, saved_length = ay_length;
  int saved_songs = ay_songs;
  int i, error = 0;

  memset( info, 0, sizeof( *info ) );
  if( read_ay_file( filename, &data, &length ) ) return 1;

  if( !is_ay_data( data, length ) ) {
    pt3_module *module = pt3_load( data, length );

    if( !module ) {
      ui_error( UI_ERROR_ERROR, "'%s' is not a valid PT3 music file",
                filename );
      error = 1;
    } else {
      info->checksum = checksum( data, length );
      snprintf( info->author, sizeof( info->author ), "%s",
                pt3_author( module ) );
      info->songs = 1;
      info->names = libspectrum_new( ayplayer_song_title, 1 );
      snprintf( info->names[0], sizeof( info->names[0] ), "%s",
                pt3_title( module ) );
      pt3_free( module );
    }
    libspectrum_free( data );
    return error;
  }

  /* The reading functions work on the loaded file: lend them this one */
  ay_data = data;
  ay_length = length;

  if( song_data( 0 ) < 0 ) {
    ui_error( UI_ERROR_ERROR, "'%s' is not a valid AY music file", filename );
    error = 1;
  } else {
    info->checksum = checksum( data, length );
    string_at( 12, info->author, sizeof( info->author ) );
    info->songs = byte_at( 16 ) + 1;
    ay_songs = info->songs;
    info->names = libspectrum_new( ayplayer_song_title, info->songs );
    for( i = 0; i < info->songs; i++ )
      ayplayer_song_name( i, info->names[i], sizeof( info->names[i] ) );
  }

  ay_data = saved_data;
  ay_length = saved_length;
  ay_songs = saved_songs;
  libspectrum_free( data );
  return error;
}

void
ayplayer_file_info_free( ayplayer_info *info )
{
  libspectrum_free( info->names );
  info->names = NULL;
}

void
ayplayer_close( void )
{
  ayplayer_stop();
  /* The settings of the user, not those of a .mix file, are kept */
  aymix_restore_user();
  aymeasure_stop();
  measure_busy = 0;
  free_file();
}

/* ------------------------------------------------------------------------
   Measuring the lengths
   ------------------------------------------------------------------------ */

static void
measure_start( const libspectrum_byte *data, size_t length,
               libspectrum_dword sum, int song )
{
  measure_id++;
  measure_checksum = sum;
  measure_song = song;
  measure_busy = 1;
  aymeasure_start( data, length, song, measure_id );
}

/* A length found: kept, and used by the song if it is playing */
static void
measure_finished( long frames, long loop )
{
  measure_busy = 0;
  lengths_store( measure_checksum, measure_song, frames, loop );

  if( ay_current != measure_song || measure_checksum != ay_file_checksum ||
      !ay_data )
    return;

  ay_length_measured = frames > 0;
  ay_length_default = frames <= 0;
  ay_song_frames = frames > 0 ? frames : AY_DEFAULT_LENGTH;
  ay_song_loop = frames > 0 ? loop : 0;
  if( ay_song_loop >= ay_song_frames ) ay_song_loop = 0;
  if( ayplay_active() )
    ayplay_set_length( ay_song_frames, ay_song_loop, ay_length_default );
}

int
ayplayer_poll( void )
{
  long frames, loop;

  if( !measure_busy ) return 0;
  if( !aymeasure_result( measure_id, &frames, &loop ) ) {
    /* Given up (could not be started) */
    if( !aymeasure_running() ) {
      measure_busy = 0;
      return 1;
    }
    return 0;
  }
  measure_finished( frames, loop );
  return 1;
}

int
ayplayer_measuring( void )
{
  return measure_busy;
}

int
ayplayer_measuring_current( void )
{
  return measure_busy && ay_current == measure_song &&
         measure_checksum == ay_file_checksum;
}

/* The measuring going on (if any) is given up */
void
ayplayer_measure_cancel( void )
{
  aymeasure_stop();
  measure_busy = 0;
}

/* As ayplayer_measure(), also if the length is known: the new one
   replaces it */
int
ayplayer_remeasure( const char *filename, int song )
{
  libspectrum_byte *data;
  size_t length;

  if( read_ay_file( filename, &data, &length ) ) return 1;
  measure_start( data, length, checksum( data, length ), song );
  libspectrum_free( data );
  return 0;
}

int
ayplayer_measure( const char *filename, int song )
{
  libspectrum_byte *data;
  size_t length;
  libspectrum_dword sum;

  if( read_ay_file( filename, &data, &length ) ) return 1;
  sum = checksum( data, length );
  if( lengths_current( sum, song ) ) {
    libspectrum_free( data );
    return 1;
  }
  measure_start( data, length, sum, song );
  libspectrum_free( data );
  return 0;
}

/* ------------------------------------------------------------------------
   Playing a song
   ------------------------------------------------------------------------ */

int
ayplayer_play( int song )
{
  long measured, loop;
  int length = 0;

  if( !ay_data || song < 0 || song >= ay_songs ) return 1;

  /* The length from the file (0: not known), or the measured one */
  if( !ay_pt3 ) {
    long data = song_data( song );
    if( data < 0 ) return 1;
    length = word_at( data + 4 );
    if( length < 0 ) length = 0;
  }
  ay_length_default = length <= 0;
  ay_length_measured = 0;
  ay_song_frames = length > 0 ? length : AY_DEFAULT_LENGTH;
  ay_song_loop = length > 0 ? -1 : 0;

  if( lengths_lookup( ay_file_checksum, song, &measured, &loop ) ) {
    ay_length_measured = measured > 0;
    ay_length_default = measured <= 0;
    ay_song_frames = measured > 0 ? measured : AY_DEFAULT_LENGTH;
    ay_song_loop = measured > 0 ? loop : 0;
    if( ay_song_loop >= ay_song_frames ) ay_song_loop = 0;
  }

  /* The .mix file of the song, if it is to be loaded */
  aymix_song_started( ay_path, song, ay_songs,
                      settings_current.ayplayer_mix_auto );

  if( ayplay_start( ay_data, ay_length, song, ay_song_frames, ay_song_loop,
                    ay_length_default ) ) {
    ui_error( UI_ERROR_ERROR, "Song %d of the AY file is damaged", song + 1 );
    ayplayer_stop();
    return 1;
  }
  ayplay_set_repeat( ay_repeat_song );
  ay_current = song;
  ay_song_ended = 0;

  /* Mixed into the sound of the emulator: more sound is kept ahead, so the
     music goes on while the emulation is held up a little */
  sound_set_long_buffer( !ayplay_output_independent() );

  /* Not measured yet, or by an earlier version: measured now, while it
     plays */
  if( !lengths_current( ay_file_checksum, song ) ) {
    if( !ayplayer_measuring_current() )
      measure_start( ay_data, ay_length, ay_file_checksum, song );
  }
  return 0;
}

void
ayplayer_stop( void )
{
  if( ay_current < 0 ) return;
  ay_current = -1;
  ay_song_ended = 0;
  ayplay_stop();
  sound_set_long_buffer( 0 );
}

void
ayplayer_set_paused( int paused )
{
  if( ay_current >= 0 ) ayplay_set_paused( paused );
}

int
ayplayer_paused( void )
{
  return ay_current >= 0 && ayplay_paused();
}

int
ayplayer_seek( long frame )
{
  if( ay_current < 0 || !ay_song_frames ) return 1;
  if( frame >= ay_song_frames ) frame = ay_song_frames - 1;
  return ayplay_seek( frame );
}

int
ayplayer_needs_emulation( void )
{
  return ay_current >= 0 && !ayplay_paused() && !ayplay_output_independent();
}

static void ( *frame_hook )( void ) = NULL;

void
ayplayer_set_frame_hook( void ( *hook )( void ) )
{
  frame_hook = hook;
}

void
ayplayer_frame( void )
{
  /* Mixed into the sound of the emulator, but there is no sound: the song
     goes on silently with the emulation */
  if( ay_current >= 0 && !ayplay_output_independent() && !sound_enabled )
    ayplay_silent_frame();
  if( frame_hook ) frame_hook();
}

/* ------------------------------------------------------------------------
   Information for the user interface
   ------------------------------------------------------------------------ */

int ayplayer_is_open( void ) { return ay_data != NULL; }
int ayplayer_song_count( void ) { return ay_songs; }
int ayplayer_current_song( void ) { return ay_current; }
long ayplayer_song_frames( void ) { return ay_song_frames; }
int ayplayer_length_is_default( void ) { return ay_length_default; }
int ayplayer_length_is_measured( void ) { return ay_length_measured; }

long
ayplayer_frames_played( void )
{
  return ay_current >= 0 ? ayplay_frames_played() : 0;
}

int
ayplayer_song_ended( void )
{
  if( ay_current >= 0 && ayplay_ended() ) ay_song_ended = 1;
  return ay_song_ended;
}

void
ayplayer_set_repeat_song( int repeat )
{
  ay_repeat_song = repeat;
  ayplay_set_repeat( repeat );
}

const char *ayplayer_filename( void ) { return ay_filename; }
const char *ayplayer_author( void ) { return ay_author; }
const char *ayplayer_misc( void ) { return ay_misc; }

void
ayplayer_song_name( int song, char *text, size_t length )
{
  long songs;

  text[0] = '\0';
  if( ay_pt3 ) {
    if( song == 0 ) snprintf( text, length, "%s", pt3_title( ay_pt3 ) );
    return;
  }
  songs = pointer_at( 18 );
  if( !ay_data || songs < 0 || song < 0 || song >= ay_songs ) return;
  string_at( songs + song * 4, text, length );
}

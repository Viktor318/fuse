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
   recording. Each song says where its blocks go in memory, the initial
   register values, and the addresses of its init and interrupt routines.

   The file is played on a Spectrum +2A (it has the AY chip, and with the
   special paging of port 1FFD all 64K are RAM, as the format expects). The
   memory is filled and a small player is put at address 0, as described in
   the format specification:

     no interrupt routine:   DI; CALL init; loop: IM 2; EI; HALT; JR loop
     with interrupt routine: DI; CALL init; loop: IM 1; EI; HALT;
                             CALL interrupt; JR loop

   All relative pointers in the file are signed big endian words, counted
   from the position of the pointer itself. */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "ayplayer.h"
#include "fuse.h"
#include "machine.h"
#include "machines/specplus3.h"
#include "memory_pages.h"
#include "periph.h"
#include "compat.h"
#include "settings.h"
#include "snapshot.h"
#include "sound.h"
#include "sound/ay_engine.h"
#include "timer/timer.h"
#include "ui/ui.h"
#include "utils.h"
#include "z80/z80.h"
#include "z80/z80_macros.h"

/* The loaded file */
static libspectrum_byte *ay_data = NULL;
static size_t ay_length = 0;

static char ay_filename[ 256 ];
static char ay_author[ 256 ];
static char ay_misc[ 256 ];

static int ay_songs = 0;		/* number of songs */
static int ay_first_song = 0;		/* 0 based */

/* Song being played, 0 based; -1 when stopped */
static int ay_current = -1;

/* Length of the current song in frames (0: not known) */
static long ay_song_frames = 0;

/* Frames played of the current song */
static long ay_frames_played = 0;

/* The current song ended (its length was reached) */
static int ay_song_ended = 0;

/* Songs without a length in the file go on for ever: as NEZplug does,
   they get a default length (5 minutes) and fade out in the last
   5 seconds, when the player goes on to the next song at the end */
#define AY_DEFAULT_LENGTH ( 5 * 60 * 50 )
#define AY_FADE_FRAMES ( 5 * 50 )
/* Songs that go round (measured) fade out in 3 seconds when left */
#define AY_LOOP_FADE_FRAMES ( 3 * 50 )
static int ay_length_default = 0;

/* Where the music goes on from after the end of the song (-1: the music
   ends there; songs of default length: 0) */
static long ay_song_loop = -1;
/* Repeat the song: songs that go round just go on playing */
static int ay_repeat_song = 0;

/* Measuring the length of a song: the first time a song is played, it runs
   without sound and without waiting (and without drawing the screen) while
   the AY registers of every frame are kept, until the music repeats. The
   lengths are kept in a file next to the settings, by file checksum */
#define AY_SCAN_SECONDS 15.0
#define AY_SCAN_MAX_FRAMES ( 60L * 60 * 50 )	/* an hour of music */
#define AY_SCAN_MIN_PERIOD 250			/* 5 seconds */
#define AY_SCAN_CONFIRM 3000			/* a minute */
#define AY_SCAN_CHECK_EVERY 250
#define AY_LENGTHS_FILE "fuse_ay_lengths.txt"

static int ay_scanning = 0;
static int ay_scan_finished = 0;
static int ay_scan_skip_once = 0;
static long ay_scan_count;
static double ay_scan_start;
static int ay_scan_frame_rate;
static libspectrum_dword *ay_scan_hash = NULL;
static libspectrum_byte *ay_scan_silent = NULL;
static int ay_length_measured = 0;
static libspectrum_dword ay_file_checksum = 0;

/* Machine before the player was opened, to go back to it */
/* Seeking: the emulation runs without sound and without waiting until this
   frame of the song is reached (-1: not seeking) */
static long ay_seek_target = -1;
static int ay_seek_frame_rate;

/* The machine as it was before the player took it over (the program that
   was running, e.g. a game), restored when the player is closed */
static libspectrum_snap *ay_saved_snap = NULL;

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

int
ayplayer_is_ay_file( const char *filename )
{
  size_t length = strlen( filename );

  return length > 3 &&
         ( filename[ length - 3 ] == '.' ) &&
         ( filename[ length - 2 ] == 'a' || filename[ length - 2 ] == 'A' ) &&
         ( filename[ length - 1 ] == 'y' || filename[ length - 1 ] == 'Y' );
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
  ay_songs = 0;
  ay_current = -1;
  sound_long_buffer = 0;
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
   after the end (-1: the music ends). The last line of a song counts.
   Read once, then kept in memory too */
typedef struct length_entry {
  libspectrum_dword sum;
  int song;
  long frames, loop;
} length_entry;

static length_entry *lengths = NULL;
static size_t lengths_count = 0, lengths_size = 0;
static int lengths_read = 0;

static void
lengths_remember( libspectrum_dword sum, int song, long frames, long loop )
{
  size_t i;

  for( i = 0; i < lengths_count; i++ )
    if( lengths[i].sum == sum && lengths[i].song == song ) break;

  if( i == lengths_count ) {
    if( lengths_count == lengths_size ) {
      lengths_size = lengths_size ? lengths_size * 2 : 64;
      lengths = libspectrum_renew( length_entry, lengths, lengths_size );
    }
    lengths_count++;
  }
  lengths[i].sum = sum;
  lengths[i].song = song;
  lengths[i].frames = frames;
  lengths[i].loop = loop;
}

static int
lengths_lookup( libspectrum_dword sum, int song, long *frames, long *loop )
{
  size_t i;

  if( !lengths_read ) {
    char path[ 1024 ], line[ 128 ];
    unsigned long file_sum;
    int file_song;
    long file_frames, file_loop;
    FILE *f;

    lengths_read = 1;
    lengths_path( path, sizeof( path ) );
    f = fopen( path, "r" );
    if( f ) {
      while( fgets( line, sizeof( line ), f ) ) {
        if( sscanf( line, "%lx %d %ld %ld", &file_sum, &file_song,
                    &file_frames, &file_loop ) == 4 )
          lengths_remember( file_sum, file_song, file_frames, file_loop );
      }
      fclose( f );
    }
  }

  for( i = 0; i < lengths_count; i++ ) {
    if( lengths[i].sum == sum && lengths[i].song == song ) {
      *frames = lengths[i].frames;
      *loop = lengths[i].loop;
      return 1;
    }
  }
  return 0;
}

static void
lengths_store( libspectrum_dword sum, int song, long frames, long loop )
{
  char path[ 1024 ];
  long old_frames, old_loop;
  FILE *f;

  lengths_lookup( sum, song, &old_frames, &old_loop );	/* read it first */
  lengths_remember( sum, song, frames, loop );

  lengths_path( path, sizeof( path ) );
  f = fopen( path, "a" );
  if( !f ) return;
  fprintf( f, "%08lx %d %ld %ld\n", (unsigned long)sum, song, frames, loop );
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

  if( file.length < 20 || memcmp( file.buffer, "ZXAYEMUL", 8 ) ) {
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

  if( song_data( 0 ) < 0 ) {
    ui_error( UI_ERROR_ERROR, "'%s' is not a valid AY music file", filename );
    free_file();
    return 1;
  }

  snprintf( ay_path, sizeof( ay_path ), "%s", filename );
  name = filename;
  for( p = filename; *p; p++ )
    if( *p == '/' || *p == '\\' ) name = p + 1;
  snprintf( ay_filename, sizeof( ay_filename ), "%s", name );

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

/* Keep the running program before the player first takes the machine */
static void
take_over( void )
{
  if( ay_saved_snap ) return;
  ay_saved_snap = libspectrum_snap_alloc();
  if( snapshot_copy_to( ay_saved_snap ) ) {
    libspectrum_snap_free( ay_saved_snap );
    ay_saved_snap = NULL;
  }
}

int
ayplayer_has_machine( void )
{
  return ay_saved_snap != NULL;
}

/* Give the machine back: the program that was running goes on */
void
ayplayer_release( void )
{
  ayplayer_stop();
  if( !ay_saved_snap ) return;

  snapshot_copy_from( ay_saved_snap );
  libspectrum_snap_free( ay_saved_snap );
  ay_saved_snap = NULL;
}

void
ayplayer_close( void )
{
  ayplayer_release();
  free_file();
}

/* ------------------------------------------------------------------------
   Playing a song
   ------------------------------------------------------------------------ */

static void
poke( libspectrum_word address, libspectrum_byte value )
{
  writebyte_internal( address, value );
}

static void
seek_finish( void )
{
  int reg;

  if( ay_seek_target < 0 ) return;
  ay_seek_target = -1;
  settings_current.frame_rate = ay_seek_frame_rate;

  sound_unpause();
  timer_estimate_reset();

  /* The sound generator did not see the register writes while seeking */
  if( sound_enabled )
    for( reg = 0; reg < 14; reg++ )
      sound_ay_write( reg, machine_current->ay.registers[ reg ], 0 );
}

int
ayplayer_seeking( void )
{
  /* Measuring runs without waiting too */
  return ay_seek_target >= 0 || ay_scanning;
}

static void
silence( void )
{
  int reg;

  for( reg = 0; reg < 14; reg++ ) {
    writeport_internal( 0xfffd, reg );
    writeport_internal( 0xbffd, 0 );
  }
  poke( 0x0000, 0xf3 );
  poke( 0x0001, 0x76 );
  PC = 0x0000;
  IFF1 = IFF2 = 0;
  z80.halted = 0;
}

static void
scan_begin( void )
{
  if( !ay_scan_hash ) {
    ay_scan_hash = libspectrum_new( libspectrum_dword, AY_SCAN_MAX_FRAMES );
    ay_scan_silent = libspectrum_new( libspectrum_byte, AY_SCAN_MAX_FRAMES );
  }

  ay_scanning = 1;
  ay_scan_finished = 0;
  ay_scan_count = 0;
  ay_scan_start = timer_get_time();

  /* No screen drawing meanwhile */
  ay_scan_frame_rate = settings_current.frame_rate;
  settings_current.frame_rate = 1000;

  sound_pause();
}

/* store: keep the length (0: default) and where the music goes on from */
static void
scan_end( int store, long frames, long loop )
{
  if( !ay_scanning ) return;

  ay_scanning = 0;
  ay_scan_finished = 1;
  settings_current.frame_rate = ay_scan_frame_rate;

  if( store && ay_current >= 0 )
    lengths_store( ay_file_checksum, ay_current, frames, loop );

  /* Quiet until the song starts again */
  silence();
  sound_unpause();
  timer_estimate_reset();
}

/* Has the music started again? The smallest period P for which the last
   max(P, a minute) frames are the same as P frames before; then back to the
   first frame S of the repetition. The song lasts until S + P and goes on
   from S; if what repeats is silence, the music ended at S (*loop = -1).
   -1: not yet */
static long
scan_find( long *loop )
{
  long n = ay_scan_count, period, need, k, start, i;
  int silent;

  for( period = AY_SCAN_MIN_PERIOD; ; period++ ) {
    need = period > AY_SCAN_CONFIRM ? period : AY_SCAN_CONFIRM;
    if( period + need > n ) return -1;

    for( k = 0; k < need; k++ )
      if( ay_scan_hash[ n - 1 - k ] != ay_scan_hash[ n - 1 - k - period ] )
        break;
    if( k < need ) continue;

    start = n - need - period;
    while( start > 0 &&
           ay_scan_hash[ start - 1 ] == ay_scan_hash[ start - 1 + period ] )
      start--;

    silent = 1;
    for( i = start; i < start + period; i++ )
      if( !ay_scan_silent[ i ] ) { silent = 0; break; }

    if( silent ) {
      *loop = -1;
      return start;
    }
    *loop = start;
    return start + period;
  }
}

static void
scan_frame( void )
{
  const libspectrum_byte *r = machine_current->ay.registers;
  libspectrum_dword hash = 2166136261U;
  long length, loop;
  int i;

  for( i = 0; i < 14; i++ ) {
    hash ^= r[i];
    hash *= 16777619U;
  }
  ay_scan_hash[ ay_scan_count ] = hash;
  ay_scan_silent[ ay_scan_count ] = !( ( r[8] | r[9] | r[10] ) & 0x1f );
  ay_scan_count++;

  if( sound_enabled ) sound_pause();

  if( ay_scan_count % AY_SCAN_CHECK_EVERY == 0 ) {
    length = scan_find( &loop );
    if( length >= 0 ) {
      /* Nothing but silence: no length */
      if( length < 50 ) scan_end( 1, 0, 0 );
      else scan_end( 1, length, loop );
      return;
    }
  }

  if( ay_scan_count >= AY_SCAN_MAX_FRAMES ||
      timer_get_time() - ay_scan_start > AY_SCAN_SECONDS )
    scan_end( 1, 0, 0 );
}

int
ayplayer_scanning( void )
{
  return ay_scanning;
}

int
ayplayer_scan_progress( void )
{
  double elapsed;

  if( !ay_scanning ) return 100;
  elapsed = timer_get_time() - ay_scan_start;
  if( elapsed < 0 ) elapsed = 0;
  if( elapsed > AY_SCAN_SECONDS ) elapsed = AY_SCAN_SECONDS;
  return elapsed * 100 / AY_SCAN_SECONDS;
}

int
ayplayer_scan_take_finished( void )
{
  int finished = ay_scan_finished;
  ay_scan_finished = 0;
  return finished;
}

void
ayplayer_scan_cancel( void )
{
  if( !ay_scanning ) return;
  scan_end( 0, 0, 0 );
  /* The song starts again: this time without measuring */
  ay_scan_skip_once = 1;
}

long
ayplayer_scan_frames( void )
{
  return ay_scan_count;
}

int
ayplayer_seek( long frame )
{
  if( ay_current < 0 || !ay_song_frames ) return 1;

  if( frame < 0 ) frame = 0;
  if( frame >= ay_song_frames ) frame = ay_song_frames - 1;

  /* Backwards: from the start of the song again */
  if( frame < ay_frames_played ) {
    if( ayplayer_play( ay_current ) ) return 1;
  }

  if( frame > ay_frames_played ) {
    /* No screen drawing meanwhile */
    if( ay_seek_target < 0 ) {
      ay_seek_frame_rate = settings_current.frame_rate;
      settings_current.frame_rate = 1000;
    }
    ay_seek_target = frame;
    sound_pause();
  }

  return 0;
}

long
ayplayer_seek_target( void )
{
  return ay_seek_target;
}

int
ayplayer_play( int song )
{
  long data, points, blocks;
  int hi_reg, lo_reg, stack, init, interrupt, length, block;
  libspectrum_dword address;
  libspectrum_byte player[ 18 ];
  int i;

  if( !ay_data || song < 0 || song >= ay_songs ) return 1;

  seek_finish();
  if( ay_scanning ) scan_end( 0, 0, 0 );
  ay_scan_finished = 0;

  data = song_data( song );
  if( data < 0 ) return 1;

  /* As in ZEsarUX: player versions 0-3 have the pointers to the points and
     the blocks after the registers, the later ones further on */
  if( byte_at( 9 ) <= 3 ) {
    points = pointer_at( data + 10 );
    blocks = pointer_at( data + 12 );
  } else {
    points = pointer_at( data + 28 );
    blocks = pointer_at( data + 30 );
  }
  if( points < 0 || blocks < 0 ) {
    ui_error( UI_ERROR_ERROR, "Song %d of the AY file is damaged", song + 1 );
    return 1;
  }

  take_over();

  /* A Spectrum +2A, all memory RAM (special paging 0,1,2,3) */
  if( machine_current->machine != LIBSPECTRUM_MACHINE_PLUS2A ) {
    if( machine_select( LIBSPECTRUM_MACHINE_PLUS2A ) ) return 1;
  } else {
    machine_reset( 0 );
  }
  specplus3_memoryport2_write( 0x1ffd, 0x01 );

  stack = word_at( points );
  init = word_at( points + 2 );
  interrupt = word_at( points + 4 );
  if( stack < 0 ) stack = 0;
  if( init < 0 ) init = 0;
  if( interrupt < 0 ) interrupt = 0;

  /* Memory: 0000-00FF RET, 0100-3FFF FFh, 4000-FFFF 0, 0038 EI */
  for( address = 0x0000; address < 0x0100; address++ ) poke( address, 0xc9 );
  for( address = 0x0100; address < 0x4000; address++ ) poke( address, 0xff );
  for( address = 0x4000; address < 0x10000; address++ ) poke( address, 0x00 );
  poke( 0x0038, 0xfb );

  /* The player at address 0 (as in ZEsarUX):
       no interrupt routine:   DI; CALL init; loop: IM 2; EI; HALT; JR loop
       with interrupt routine: DI; CALL init; loop: IM 1; EI; HALT;
                               CALL interrupt; JR loop
     then DI; JR $ and EI; RET */
  memset( player, 0xc9, sizeof( player ) );
  player[0] = 0xf3;
  player[1] = 0xcd;
  player[2] = init & 0xff;
  player[3] = init >> 8;
  if( !interrupt ) {
    player[4] = 0xed; player[5] = 0x5e;
    player[6] = 0xfb;
    player[7] = 0x76;
    player[8] = 0x18; player[9] = 256 - 6;
  } else {
    player[4] = 0xed; player[5] = 0x56;
    player[6] = 0xfb;
    player[7] = 0x76;
    player[8] = 0xcd;
    player[9] = interrupt & 0xff;
    player[10] = interrupt >> 8;
    player[11] = 0x18; player[12] = 256 - 9;
  }
  player[13] = 0xf3; player[14] = 0x18; player[15] = 256 - 2;
  player[16] = 0xfb; player[17] = 0xc9;
  for( i = 0; i < 18; i++ ) poke( i, player[i] );

  /* The blocks of the song */
  for( block = 0; ; block++ ) {
    long entry = blocks + block * 6;
    int block_address = word_at( entry );
    int block_length = word_at( entry + 2 );
    int block_offset = word_at( entry + 4 );
    long block_data;
    int j;

    if( block_address <= 0 ) break;
    if( block_length < 0 || block_offset < 0 ) break;

    /* As DeliAY and ZEsarUX: not beyond the end of the memory */
    if( block_address + block_length > 0x10000 )
      block_length = 0x10000 - block_address;

    block_data = entry + 4 + block_offset;
    for( j = 0; j < block_length; j++ ) {
      if( (size_t)( block_data + j ) >= ay_length ) break;
      poke( block_address + j, ay_data[ block_data + j ] );
    }

    /* No init routine: the player calls the first block instead (as the
       AY format describes; ZEsarUX started the processor there, which
       then returned to nowhere) */
    if( !init && block == 0 ) {
      poke( 0x0002, block_address & 0xff );
      poke( 0x0003, block_address >> 8 );
    }
  }

  /* Registers, as in ZEsarUX: the cold start values, A and F from the
     song, SP from the song, I=3, interrupts disabled, IM 0 */
  hi_reg = byte_at( data + 8 );
  lo_reg = byte_at( data + 9 );
  if( hi_reg < 0 ) hi_reg = 0xff;
  if( lo_reg < 0 ) lo_reg = 0xff;

  AF = AF_ = BC = BC_ = DE = DE_ = HL = HL_ = IX = IY = 0xffff;
  A = hi_reg;
  F = lo_reg;
  SP = stack;
  I = 3;
  R = R7 = 0;
  IFF1 = IFF2 = 0;
  IM = 0;
  PC = 0x0000;
  z80.halted = 0;

  length = word_at( data + 4 );
  ay_length_default = length <= 0;
  ay_length_measured = 0;
  ay_song_frames = length > 0 ? length : AY_DEFAULT_LENGTH;
  ay_song_loop = length > 0 ? -1 : 0;
  ay_frames_played = 0;
  ay_engine_set_fade( 100 );
  ay_song_ended = 0;
  ay_current = song;
  sound_long_buffer = 1;

  /* The measured length, or measure it now */
  {
    long measured, loop;

    if( lengths_lookup( ay_file_checksum, song, &measured, &loop ) ) {
      ay_length_measured = measured > 0;
      ay_length_default = measured <= 0;
      ay_song_frames = measured > 0 ? measured : AY_DEFAULT_LENGTH;
      ay_song_loop = measured > 0 ? loop : 0;
      if( ay_song_loop >= ay_song_frames ) ay_song_loop = 0;
    } else if( ay_scan_skip_once ) {
      ay_scan_skip_once = 0;
    } else {
      scan_begin();
    }
  }

  return 0;
}

void
ayplayer_stop( void )
{
  int reg;

  seek_finish();
  if( ay_scanning ) scan_end( 0, 0, 0 );
  ay_scan_finished = 0;
  ay_engine_set_fade( 100 );
  if( ay_current < 0 ) return;
  ay_current = -1;
  sound_long_buffer = 0;

  /* Silence: all AY registers to 0 (volumes too), and the processor waits
     with DI; HALT. The memory stays as it was */
  for( reg = 0; reg < 14; reg++ ) {
    writeport_internal( 0xfffd, reg );
    writeport_internal( 0xbffd, 0 );
  }
  poke( 0x0000, 0xf3 );
  poke( 0x0001, 0x76 );
  PC = 0x0000;
  IFF1 = IFF2 = 0;
  z80.halted = 0;
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
  if( ay_scanning ) {
    if( ay_current >= 0 ) scan_frame();
    if( frame_hook ) frame_hook();
    return;
  }

  if( ay_current >= 0 ) {
    long fade = ay_length_default ? AY_FADE_FRAMES : AY_LOOP_FADE_FRAMES;

    ay_frames_played++;
    if( ay_song_frames && ay_frames_played >= ay_song_frames ) {
      if( ay_repeat_song && ay_song_loop >= 0 ) {
        /* The music goes round by itself: on it goes */
        ay_frames_played = ay_song_loop;
      } else {
        ay_song_ended = 1;
      }
    }

    /* Fade out at the end of a song whose music goes round, when the
       player leaves it */
    if( !ay_repeat_song && ay_song_loop >= 0 &&
        ay_frames_played > ay_song_frames - fade ) {
      long left = ay_song_frames - ay_frames_played;
      ay_engine_set_fade( left > 0 ? left * 100 / fade : 0 );
    } else {
      ay_engine_set_fade( 100 );
    }
  }

  if( ay_seek_target >= 0 ) {
    if( ay_current < 0 || ay_frames_played >= ay_seek_target )
      seek_finish();
    else if( sound_enabled )
      sound_pause();	/* someone turned it on again meanwhile */
  }

  if( frame_hook ) frame_hook();
}

/* ------------------------------------------------------------------------
   Information for the user interface
   ------------------------------------------------------------------------ */

int ayplayer_is_open( void ) { return ay_data != NULL; }
int ayplayer_song_count( void ) { return ay_songs; }
int ayplayer_current_song( void ) { return ay_current; }
long ayplayer_song_frames( void ) { return ay_song_frames; }
long ayplayer_frames_played( void ) { return ay_frames_played; }
int ayplayer_song_ended( void ) { return ay_song_ended; }
int ayplayer_length_is_default( void ) { return ay_length_default; }
int ayplayer_length_is_measured( void ) { return ay_length_measured; }

void
ayplayer_set_repeat_song( int repeat )
{
  ay_repeat_song = repeat;
}
const char *ayplayer_filename( void ) { return ay_filename; }
const char *ayplayer_author( void ) { return ay_author; }
const char *ayplayer_misc( void ) { return ay_misc; }

void
ayplayer_song_name( int song, char *text, size_t length )
{
  long songs = pointer_at( 18 );

  text[0] = '\0';
  if( !ay_data || songs < 0 || song < 0 || song >= ay_songs ) return;
  string_at( songs + song * 4, text, length );
}

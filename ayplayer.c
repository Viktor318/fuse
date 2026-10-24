/* ayplayer.c: Play the songs of .ay (ZXAYEMUL) music files
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
#include "settings.h"
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

/* Machine before the player was opened, to go back to it */
static libspectrum_machine ay_previous_machine = LIBSPECTRUM_MACHINE_UNKNOWN;

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
  if( value >= 0x8000 ) value -= 0x10000;
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
}

int
ayplayer_open( const char *filename )
{
  utils_file file;
  const char *name, *p;

  if( utils_read_file( filename, &file ) ) return 1;

  if( file.length < 20 || memcmp( file.buffer, "ZXAYEMUL", 8 ) ) {
    ui_error( UI_ERROR_ERROR, "'%s' is not an AY music file", filename );
    utils_close_file( &file );
    return 1;
  }

  ayplayer_stop();
  free_file();

  ay_data = libspectrum_new( libspectrum_byte, file.length );
  memcpy( ay_data, file.buffer, file.length );
  ay_length = file.length;
  utils_close_file( &file );

  name = filename;
  for( p = filename; *p; p++ )
    if( *p == '/' || *p == '\\' ) name = p + 1;
  snprintf( ay_filename, sizeof( ay_filename ), "%s", name );

  string_at( 12, ay_author, sizeof( ay_author ) );
  string_at( 14, ay_misc, sizeof( ay_misc ) );

  ay_songs = byte_at( 16 ) + 1;
  ay_first_song = byte_at( 17 );
  if( ay_first_song < 0 || ay_first_song >= ay_songs ) ay_first_song = 0;

  if( song_data( 0 ) < 0 ) {
    ui_error( UI_ERROR_ERROR, "'%s' is not a valid AY music file", filename );
    free_file();
    return 1;
  }

  if( ay_previous_machine == LIBSPECTRUM_MACHINE_UNKNOWN )
    ay_previous_machine = machine_current->machine;

  return ayplayer_play( ay_first_song );
}

void
ayplayer_close( void )
{
  int was_open = ay_data != NULL;

  ayplayer_stop();
  free_file();

  /* Back to the machine used before the player */
  if( was_open && ay_previous_machine != LIBSPECTRUM_MACHINE_UNKNOWN ) {
    if( machine_current->machine != ay_previous_machine )
      machine_select( ay_previous_machine );
    else
      machine_reset( 0 );
  }
  ay_previous_machine = LIBSPECTRUM_MACHINE_UNKNOWN;
}

/* ------------------------------------------------------------------------
   Playing a song
   ------------------------------------------------------------------------ */

static void
poke( libspectrum_word address, libspectrum_byte value )
{
  writebyte_internal( address, value );
}

int
ayplayer_play( int song )
{
  long data, points, blocks;
  int hi_reg, lo_reg, stack, init, interrupt, length, i;
  libspectrum_word regs;
  libspectrum_dword address;

  if( !ay_data || song < 0 || song >= ay_songs ) return 1;

  data = song_data( song );
  if( data < 0 ) return 1;
  points = pointer_at( data + 10 );
  blocks = pointer_at( data + 12 );
  if( points < 0 || blocks < 0 ) {
    ui_error( UI_ERROR_ERROR, "Song %d of the AY file is damaged", song + 1 );
    return 1;
  }

  if( fuse_emulation_paused ) fuse_emulation_unpause();

  /* A Spectrum +2A, all memory RAM (special paging 0,1,2,3) */
  if( machine_current->machine != LIBSPECTRUM_MACHINE_PLUS2A ) {
    if( machine_select( LIBSPECTRUM_MACHINE_PLUS2A ) ) return 1;
  } else {
    machine_reset( 0 );
  }
  specplus3_memoryport2_write( 0x1ffd, 0x01 );

  /* Memory as the specification asks */
  for( address = 0x0000; address < 0x0100; address++ ) poke( address, 0xc9 );
  for( address = 0x0100; address < 0x4000; address++ ) poke( address, 0xff );
  for( address = 0x4000; address < 0x10000; address++ ) poke( address, 0x00 );
  poke( 0x0038, 0xfb );

  /* The blocks of the song */
  init = word_at( points + 2 );
  interrupt = word_at( points + 4 );
  stack = word_at( points );

  for( i = 0; ; i++ ) {
    long entry = blocks + i * 6;
    int block_address = word_at( entry );
    int block_length;
    long block_data;
    int j;

    if( block_address <= 0 ) break;
    block_length = word_at( entry + 2 );
    block_data = pointer_at( entry + 4 );
    if( block_length <= 0 || block_data < 0 ) continue;

    if( !init && i == 0 ) init = block_address;

    /* Not beyond the end of the memory or of the file */
    if( block_address + block_length > 0x10000 )
      block_length = 0x10000 - block_address;
    if( (size_t)( block_data + block_length ) > ay_length )
      block_length = ay_length - block_data;

    for( j = 0; j < block_length; j++ )
      poke( block_address + j, ay_data[ block_data + j ] );
  }

  if( init <= 0 ) {
    ui_error( UI_ERROR_ERROR, "Song %d of the AY file has no init routine",
              song + 1 );
    return 1;
  }

  /* The player at address 0 */
  poke( 0x0000, 0xf3 );				/* DI */
  poke( 0x0001, 0xcd );				/* CALL init */
  poke( 0x0002, init & 0xff );
  poke( 0x0003, init >> 8 );
  if( interrupt <= 0 ) {
    poke( 0x0004, 0xed ); poke( 0x0005, 0x5e );	/* IM 2 */
    poke( 0x0006, 0xfb );			/* EI */
    poke( 0x0007, 0x76 );			/* HALT */
    poke( 0x0008, 0x18 ); poke( 0x0009, 0xfa );	/* JR loop */
  } else {
    poke( 0x0004, 0xed ); poke( 0x0005, 0x56 );	/* IM 1 */
    poke( 0x0006, 0xfb );			/* EI */
    poke( 0x0007, 0x76 );			/* HALT */
    poke( 0x0008, 0xcd );			/* CALL interrupt */
    poke( 0x0009, interrupt & 0xff );
    poke( 0x000a, interrupt >> 8 );
    poke( 0x000b, 0x18 ); poke( 0x000c, 0xf7 );	/* JR loop */
  }

  /* Registers */
  hi_reg = byte_at( data + 8 );
  lo_reg = byte_at( data + 9 );
  if( hi_reg < 0 ) hi_reg = 0;
  if( lo_reg < 0 ) lo_reg = 0;
  regs = ( hi_reg << 8 ) | lo_reg;

  AF = AF_ = HL = HL_ = DE = DE_ = BC = BC_ = IX = IY = regs;
  I = hi_reg;
  R = R7 = 0;
  SP = stack > 0 ? stack : 0;
  PC = 0x0000;
  IFF1 = IFF2 = 0;
  IM = 0;
  z80.halted = 0;

  length = word_at( data + 4 );
  ay_song_frames = length > 0 ? length : 0;
  ay_frames_played = 0;
  ay_song_ended = 0;
  ay_current = song;

  return 0;
}

void
ayplayer_stop( void )
{
  int reg;

  if( ay_current < 0 ) return;
  ay_current = -1;
  if( fuse_emulation_paused ) fuse_emulation_unpause();

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
  if( ay_current >= 0 && !fuse_emulation_paused ) {
    ay_frames_played++;
    if( ay_song_frames && ay_frames_played >= ay_song_frames )
      ay_song_ended = 1;
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

/* aysource.c: The AY registers of a song, frame by frame, without the
   emulated machine
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   Starting a song follows the AY player of ZEsarUX (audio_ayplayer.c,
   Copyright (C) 2013 Cesar Hernandez Bano, GPL 3 or later).

   The processor is z80.h of the chips project by Andre Weissflog
   (https://github.com/floooh/chips, zlib/libpng license), unchanged in
   sound/chips_z80.h.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

/* An .ay file holds the music player code of a game and its data. Each song
   says where its blocks go in the 64K of memory, the initial register
   values, and the addresses of its init and interrupt routines. A small
   player is put at address 0, as described in the format specification:

     no interrupt routine:   DI; CALL init; loop: IM 2; EI; HALT; JR loop
     with interrupt routine: DI; CALL init; loop: IM 1; EI; HALT;
                             CALL interrupt; JR loop

   All relative pointers in the file are big endian words, counted from the
   position of the pointer itself. */

#include "config.h"

#include <stdlib.h>
#include <string.h>

/* Not the names of the processor of the emulator */
#define z80_init aysource_z80_init
#define z80_reset aysource_z80_reset
#define z80_tick aysource_z80_tick
#define z80_prefetch aysource_z80_prefetch
#define z80_opdone aysource_z80_opdone
#define CHIPS_IMPL
#define CHIPS_ASSERT( x )
#include "sound/chips_z80.h"

#include "aysource.h"
#include "pt3player.h"

/* The interrupt is held this long at the start of the frame (128K) */
#define INT_TSTATES 36

struct aysource {
  /* .ay: the processor and its memory */
  z80_t cpu;
  uint64_t pins;
  unsigned char mem[ 0x10000 ];
  int selected, chip;
  /* .pt3: the module */
  pt3_module *pt3;
  unsigned char registers[ 16 ];
};

typedef struct file {
  const unsigned char *data;
  size_t length;
} file;

static int
byte_at( const file *f, long offset )
{
  if( offset < 0 || (size_t)offset >= f->length ) return -1;
  return f->data[ offset ];
}

static int
word_at( const file *f, long offset )
{
  if( offset < 0 || (size_t)offset + 1 >= f->length ) return -1;
  return ( f->data[ offset ] << 8 ) | f->data[ offset + 1 ];
}

/* Follow the relative pointer at `offset'; -1 if it points outside. As in
   ZEsarUX, the offset is unsigned */
static long
pointer_at( const file *f, long offset )
{
  int value = word_at( f, offset );
  long target;

  if( value < 0 ) return -1;
  target = offset + value;
  if( (size_t)target >= f->length ) return -1;
  return target;
}

int
aysource_is_ay( const unsigned char *data, size_t length )
{
  return length >= 20 && !memcmp( data, "ZXAYEMUL", 8 );
}

static int
setup_ay( aysource *s, const file *f, int song )
{
  long songs, data, points, blocks;
  int stack, init, interrupt, hi_reg, lo_reg, block;
  unsigned char *mem = s->mem;
  unsigned char player[ 18 ];
  z80_t *cpu = &s->cpu;

  if( song < 0 || song > byte_at( f, 16 ) ) return 1;
  songs = pointer_at( f, 18 );
  if( songs < 0 ) return 1;
  data = pointer_at( f, songs + song * 4 + 2 );
  if( data < 0 ) return 1;

  /* As in ZEsarUX: player versions 0-3 have the pointers to the points and
     the blocks after the registers, the later ones further on */
  if( byte_at( f, 9 ) <= 3 ) {
    points = pointer_at( f, data + 10 );
    blocks = pointer_at( f, data + 12 );
  } else {
    points = pointer_at( f, data + 28 );
    blocks = pointer_at( f, data + 30 );
  }
  if( points < 0 || blocks < 0 ) return 1;

  stack = word_at( f, points );
  init = word_at( f, points + 2 );
  interrupt = word_at( f, points + 4 );
  if( stack < 0 ) stack = 0;
  if( init < 0 ) init = 0;
  if( interrupt < 0 ) interrupt = 0;

  /* Memory: 0000-00FF RET, 0100-3FFF FFh, 4000-FFFF 0, 0038 EI */
  memset( mem, 0xc9, 0x100 );
  memset( mem + 0x100, 0xff, 0x3f00 );
  memset( mem + 0x4000, 0x00, 0xc000 );
  mem[ 0x38 ] = 0xfb;

  /* The player, then DI; JR $ and EI; RET */
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
  memcpy( mem, player, sizeof( player ) );

  /* The blocks of the song */
  for( block = 0; ; block++ ) {
    long entry = blocks + block * 6;
    int address = word_at( f, entry );
    int length = word_at( f, entry + 2 );
    int offset = word_at( f, entry + 4 );
    long origin;
    int j;

    if( address <= 0 ) break;
    if( length < 0 || offset < 0 ) break;

    /* As DeliAY and ZEsarUX: not beyond the end of the memory */
    if( address + length > 0x10000 ) length = 0x10000 - address;

    origin = entry + 4 + offset;
    for( j = 0; j < length; j++ ) {
      if( (size_t)( origin + j ) >= f->length ) break;
      mem[ address + j ] = f->data[ origin + j ];
    }

    /* No init routine: the player calls the first block instead */
    if( !init && block == 0 ) {
      mem[2] = address & 0xff;
      mem[3] = address >> 8;
    }
  }

  /* Registers, as in ZEsarUX: the cold start values, A and F from the
     song, SP from the song, I=3, interrupts disabled, IM 0 */
  hi_reg = byte_at( f, data + 8 );
  lo_reg = byte_at( f, data + 9 );
  if( hi_reg < 0 ) hi_reg = 0xff;
  if( lo_reg < 0 ) lo_reg = 0xff;

  s->pins = z80_init( cpu );
  cpu->af = cpu->bc = cpu->de = cpu->hl = cpu->ix = cpu->iy = 0xffff;
  cpu->af2 = cpu->bc2 = cpu->de2 = cpu->hl2 = 0xffff;
  cpu->a = hi_reg;
  cpu->f = lo_reg;
  cpu->sp = stack;
  cpu->i = 3;
  cpu->r = 0;
  cpu->im = 0;
  cpu->iff1 = cpu->iff2 = false;
  s->pins = z80_prefetch( cpu, 0 );
  return 0;
}

aysource *
aysource_new( const unsigned char *data, size_t length, int song )
{
  aysource *s = calloc( 1, sizeof( *s ) );
  file f;

  if( !s ) return NULL;
  f.data = data;
  f.length = length;

  if( aysource_is_ay( data, length ) ) {
    if( setup_ay( s, &f, song ) ) {
      free( s );
      return NULL;
    }
    return s;
  }

  if( song != 0 || !( s->pt3 = pt3_load( data, length ) ) ) {
    free( s );
    return NULL;
  }
  pt3_reset( s->pt3 );
  return s;
}

void
aysource_free( aysource *source )
{
  if( !source ) return;
  if( source->pt3 ) pt3_free( source->pt3 );
  free( source );
}

const unsigned char *
aysource_registers( const aysource *source )
{
  return source->registers;
}

static int
keep( aysource_write *writes, int max, int count, unsigned int tstates,
      int reg, int value )
{
  if( !writes || count >= max ) return count;
  writes[ count ].tstates = tstates;
  writes[ count ].reg = reg;
  writes[ count ].value = value;
  return count + 1;
}

static int
frame_pt3( aysource *s, aysource_write *writes, int max )
{
  unsigned char registers[ PT3_REGISTERS ];
  unsigned int mask;
  int reg, count = 0;

  pt3_frame( s->pt3, registers, &mask );
  for( reg = 0; reg < PT3_REGISTERS; reg++ ) {
    if( !( mask & ( 1u << reg ) ) ) continue;
    s->registers[ reg ] = registers[ reg ];
    count = keep( writes, max, count, 0, reg, registers[ reg ] );
  }
  return count;
}

int
aysource_frame( aysource *s, aysource_write *writes, int max )
{
  z80_t *cpu = &s->cpu;
  uint64_t pins = s->pins;
  unsigned char *mem = s->mem;
  int count = 0;
  unsigned int t;

  if( s->pt3 ) return frame_pt3( s, writes, max );

  for( t = 0; t < AYSOURCE_FRAME_TSTATES; t++ ) {
    if( t < INT_TSTATES ) pins |= Z80_INT;
    else pins &= ~Z80_INT;

    pins = z80_tick( cpu, pins );

    if( pins & Z80_MREQ ) {
      uint16_t addr = Z80_GET_ADDR( pins );
      if( pins & Z80_RD ) {
        Z80_SET_DATA( pins, mem[ addr ] );
      } else if( pins & Z80_WR ) {
        mem[ addr ] = Z80_GET_DATA( pins );
      }
    } else if( pins & Z80_IORQ ) {
      uint16_t port = Z80_GET_ADDR( pins );
      if( pins & Z80_M1 ) {
        /* Interrupt acknowledge: FFh on the bus */
        Z80_SET_DATA( pins, 0xff );
      } else if( pins & Z80_WR ) {
        uint8_t value = Z80_GET_DATA( pins );
        if( ( port & 0xc002 ) == 0xc000 ) {
          /* Register select; TurboSound: FFh the first chip, FEh the
             second (only the first one is played). Any other value of 16
             or more selects no register, as on the AY itself: digital
             music writes its samples with OUT (FDh),A, whose port is
             FFFDh-like for values from C0h */
          if( value == 0xff || value == 0xfe ) s->chip = value == 0xff ? 0 : 1;
          else s->selected = value < 16 ? value : -1;
        } else if( ( port & 0xc002 ) == 0x8000 && !s->chip &&
                   s->selected >= 0 ) {
          s->registers[ s->selected ] = value;
          if( s->selected < 14 )
            count = keep( writes, max, count, t, s->selected, value );
        }
      } else if( pins & Z80_RD ) {
        /* The selected register can be read back, as on the 128K */
        if( ( port & 0xc002 ) == 0xc000 && !s->chip && s->selected >= 0 ) {
          Z80_SET_DATA( pins, s->registers[ s->selected ] );
        } else {
          Z80_SET_DATA( pins, 0xff );
        }
      }
    }

    /* Waiting with HALT for the next interrupt: on to the end of the
       frame */
    if( ( pins & Z80_HALT ) && t >= INT_TSTATES ) break;
  }

  s->pins = pins;
  return count;
}

/* keypad128.h: Spectrum 128 keypad emulation
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

#ifndef FUSE_KEYPAD128_H
#define FUSE_KEYPAD128_H

#include <libspectrum.h>

#include "input.h"

/* The keys of the keypad: bit numbers of the 20 bit key state, rows 5, 4,
   1, 2, 3 from the lowest bits up, as the keypad sends them. The editing
   functions of the 128 BASIC editor in brackets */
typedef enum keypad128_key {
  KEYPAD128_PERIOD = 1,		/* (last line of the program) */
  KEYPAD128_0 = 3,		/* (SHIFT) */
  KEYPAD128_ENTER = 4,
  KEYPAD128_3 = 5,		/* (first line of the program) */
  KEYPAD128_2 = 6,		/* (end of line) */
  KEYPAD128_1 = 7,		/* (start of line) */
  KEYPAD128_RPAREN = 8,		/* (TOGGLE) */
  KEYPAD128_LPAREN = 9,		/* (DEL right) */
  KEYPAD128_MULTIPLY = 10,	/* (cursor up) */
  KEYPAD128_DIVIDE = 11,	/* (DEL left) */
  KEYPAD128_MINUS = 12,		/* (CMND) */
  KEYPAD128_9 = 13,		/* (cursor right) */
  KEYPAD128_8 = 14,		/* (cursor down) */
  KEYPAD128_7 = 15,		/* (cursor left) */
  KEYPAD128_PLUS = 16,		/* (10 lines down) */
  KEYPAD128_6 = 17,		/* (10 lines up) */
  KEYPAD128_5 = 18,		/* (next word) */
  KEYPAD128_4 = 19,		/* (previous word) */
} keypad128_key;

void keypad128_register_startup( void );

/* Is the keypad plugged in (and the machine able to talk to it)? */
int keypad128_active( void );

/* The keypad socket: AY register 14 written in output mode, and the input
   line (bit 5 of register 14) */
void keypad128_write( libspectrum_byte r14 );
libspectrum_byte keypad128_read( void );

/* Host keys as keypad keys (Home, End, Page Up...). Returns 1 if the key
   was taken by the keypad, so it mustn't act on the keyboard too */
int keypad128_host_key( input_key key, int press );

/* All keys up (e.g. on losing the focus) */
void keypad128_release_all( void );

#endif			/* #ifndef FUSE_KEYPAD128_H */

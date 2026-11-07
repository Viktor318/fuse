/* keypad128.c: Spectrum 128 keypad emulation
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   The keypad protocol follows the keypad emulation of EightyOne
   (Source/Keypad/keypad.cpp, Copyright (c) 2003-2025 Michael D Wynne,
   GPL v2 or later).

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

/* The keypad of the Spectrum 128 is plugged into the KEYPAD socket, which
   is wired to the I/O port of the AY chip (register 14): bit 0 is the line
   to the keypad, bit 5 the line from it. The 128 ROM polls the keypad from
   its interrupt routine: each change of its line moves the keypad a step
   on. The keypad first tells it is there, then sends its ID (4 bits) and
   then its five rows of four keys, each as a flag (has the row changed?)
   and, if so, the four key bits. Each bit is a low pulse, then the bit,
   then high. The keypad gives up and starts again when the ROM leaves it
   waiting too long (a time in T-states).

   The host keys used as keypad keys are the editing keys of a PC
   keyboard, for the editing functions of the 128 BASIC editor; see
   keypad128_host_key() */

#include "config.h"

#include <libspectrum.h>

#include "compat.h"
#include "infrastructure/startup_manager.h"
#include "keypad128.h"
#include "machine.h"
#include "module.h"
#include "periph.h"
#include "settings.h"
#include "spectrum.h"

/* The ROM may be interrupted between two bits (the frame interrupt takes
   about 1900 T-states), so the keypad waits longer than that for the next
   step of a bit (EightyOne waits 1500 or 2000 T-states) */
#define BIT_TIMEOUT 5000

#define LINE_IN_LOW  0x00
#define LINE_IN_HIGH 0x20

typedef enum keypad_state {
  AWAITING_POLL_REQUEST,
  AWAITING_POLL_REQUEST_END,
  AWAITING_SCAN_REQUEST,
  AWAITING_READ_KEYPAD_ID,
  AWAITING_READ_ROW_FLAG,
  AWAITING_READ_ROW_BITS,
} keypad_state;

typedef enum keypad_substate {
  AWAITING_READ_BIT_SYNC,
  AWAITING_READ_BIT_SYNC_END,
  AWAITING_READ_BIT,
  AWAITING_READ_BIT_END,
} keypad_substate;

static keypad_state state;
static keypad_substate substate;
static int row;
static int line_out;		/* bit 0 of register 14 last written */
static libspectrum_byte line_in;
static int bits_to_send, key_bits_to_send;
static int row_bits[5];		/* the rows as last sent */

/* The keypad gives up at this time (absolute T-states), if not 0 */
static libspectrum_qword timeout_at;

/* Keys held: KEYPAD128_* bit numbers */
static libspectrum_dword keys_held;

static void keypad128_reset( int hard_reset );

static module_info_t keypad128_module_info = {

  /* .reset = */ keypad128_reset,
  /* .romcs = */ NULL,
  /* .snapshot_enabled = */ NULL,
  /* .snapshot_from = */ NULL,
  /* .snapshot_to = */ NULL,

};

static libspectrum_qword
now( void )
{
  return (libspectrum_qword)spectrum_get_frame_count() *
         machine_current->timings.tstates_per_frame + tstates;
}

static void
set_timeout( int tstates_from_now )
{
  timeout_at = tstates_from_now > 0 ? now() + tstates_from_now : 0;
}

static void
protocol_reset( void )
{
  int i;

  state = AWAITING_POLL_REQUEST;
  substate = AWAITING_READ_BIT_SYNC;
  line_in = LINE_IN_HIGH;
  timeout_at = 0;
  for( i = 0; i < 5; i++ ) row_bits[i] = 0x0f;
}

/* Has the keypad given up waiting? */
static void
check_timeout( void )
{
  if( timeout_at && now() >= timeout_at ) protocol_reset();
}

static void
keypad128_reset( int hard_reset GCC_UNUSED )
{
  protocol_reset();
}

/* No ports of its own: it is on the I/O port of the AY chip. The machines
   with the KEYPAD socket (the 128 and the +2) make it optional, so the
   option is greyed out on the others */
static const periph_t keypad128_periph = {
  /* .option = */ &settings_current.keypad128,
  /* .ports = */ NULL,
  /* .hard_reset = */ 0,
  /* .activate = */ NULL,
};

static int
keypad128_init( void *context GCC_UNUSED )
{
  module_register( &keypad128_module_info );
  periph_register( PERIPH_TYPE_KEYPAD128, &keypad128_periph );
  protocol_reset();
  return 0;
}

void
keypad128_register_startup( void )
{
  startup_manager_module dependencies[] = { STARTUP_MANAGER_MODULE_SETUID };
  startup_manager_register( STARTUP_MANAGER_MODULE_KEYPAD128, dependencies,
                            ARRAY_SIZE( dependencies ), keypad128_init, NULL,
                            NULL );
}

/* Only the 128 and the +2 have the KEYPAD socket and a ROM which polls the
   keypad (the +2A and +3 ROMs do not): they make the peripheral optional */
int
keypad128_active( void )
{
  return periph_is_active( PERIPH_TYPE_KEYPAD128 );
}

/* The four keys of the row being sent, as the keypad sends them (bit 3 is
   the leftmost key) */
static int
row_keys( int row_number )
{
  /* Rows 5, 4, 1, 2, 3 in this order */
  return ( keys_held >> ( row_number * 4 ) ) & 0x0f;
}

static void
handle_row_read( void )
{
  switch( substate ) {

  case AWAITING_READ_BIT_SYNC:
    line_in = LINE_IN_LOW;
    set_timeout( 19000 );
    substate = AWAITING_READ_BIT_SYNC_END;

    if( state == AWAITING_READ_ROW_FLAG ) {
      int bits = row_keys( row );

      if( bits != row_bits[ row ] ) {
        /* The flag (1), then the keys */
        row_bits[ row ] = bits;
        key_bits_to_send = ( bits << 1 ) | 1;
        bits_to_send = 5;
      } else {
        key_bits_to_send = 0;
        bits_to_send = 1;
      }
    }
    break;

  case AWAITING_READ_BIT_SYNC_END:
    line_in = ( key_bits_to_send & 0x01 ) ? LINE_IN_HIGH : LINE_IN_LOW;
    key_bits_to_send >>= 1;
    substate = AWAITING_READ_BIT;
    set_timeout( BIT_TIMEOUT );
    break;

  case AWAITING_READ_BIT:
    line_in = LINE_IN_HIGH;
    substate = AWAITING_READ_BIT_END;
    set_timeout( BIT_TIMEOUT );
    break;

  case AWAITING_READ_BIT_END:
    bits_to_send--;

    if( bits_to_send > 0 ) {
      if( state == AWAITING_READ_ROW_FLAG ) state = AWAITING_READ_ROW_BITS;
      substate = AWAITING_READ_BIT_SYNC;
      set_timeout( BIT_TIMEOUT );
    } else if( state == AWAITING_READ_KEYPAD_ID ) {
      state = AWAITING_READ_ROW_FLAG;
      substate = AWAITING_READ_BIT_SYNC;
      set_timeout( BIT_TIMEOUT );
      row = 0;
    } else {
      row++;
      if( row == 5 ) {
        row = 0;
        set_timeout( 0 );
      } else {
        set_timeout( 5000 );
      }
      state = AWAITING_READ_ROW_FLAG;
      substate = AWAITING_READ_BIT_SYNC;
    }
    break;
  }
}

void
keypad128_write( libspectrum_byte r14 )
{
  if( !keypad128_active() ) return;

  check_timeout();

  /* Each change of the line moves the keypad on */
  if( line_out == ( r14 & 0x01 ) ) return;
  line_out = r14 & 0x01;

  switch( state ) {

  case AWAITING_POLL_REQUEST:
    line_in = LINE_IN_LOW;
    state = AWAITING_POLL_REQUEST_END;
    set_timeout( 5000 );
    break;

  case AWAITING_POLL_REQUEST_END:
    line_in = LINE_IN_HIGH;
    state = AWAITING_SCAN_REQUEST;
    set_timeout( 5000 );
    break;

  case AWAITING_SCAN_REQUEST:
    state = AWAITING_READ_KEYPAD_ID;
    substate = AWAITING_READ_BIT_SYNC;
    set_timeout( 5500 );
    key_bits_to_send = 0x4;
    bits_to_send = 4;
    break;

  case AWAITING_READ_KEYPAD_ID:
  case AWAITING_READ_ROW_FLAG:
  case AWAITING_READ_ROW_BITS:
    handle_row_read();
    break;
  }
}

libspectrum_byte
keypad128_read( void )
{
  if( !keypad128_active() ) return LINE_IN_HIGH;

  check_timeout();
  return line_in;
}

/* Host keys as keypad keys. The editing keys of a PC keyboard give the
   editing functions of the 128 BASIC editor, which only the keypad has:

     Home / End                start / end of the line
     Ctrl + Left / Right       previous / next word
     Page Up / Page Down       10 lines up / down
     Delete                    delete the character under the cursor
     Ctrl + Delete             delete to the end of the line
     Ctrl + Backspace          delete to the start of the line
     Insert                    CMND (the editor menu)

   The deletions of more than one character need the keypad SHIFT too. */

#define KEY( k ) ( (libspectrum_dword)1 << ( k ) )

#define MAX_HELD 8
static struct { input_key key; libspectrum_dword bits; } held[ MAX_HELD ];
static int held_count = 0;

/* Ctrl keys held: bit 0 left, bit 1 right */
static int ctrl_held = 0;

static libspectrum_dword
keypad_keys_for( input_key key )
{
  int ctrl = ctrl_held != 0;

  switch( key ) {
  case INPUT_KEY_Home:      return KEY( KEYPAD128_1 );
  case INPUT_KEY_End:       return KEY( KEYPAD128_2 );
  case INPUT_KEY_Page_Up:   return KEY( KEYPAD128_6 );
  case INPUT_KEY_Page_Down: return KEY( KEYPAD128_PLUS );
  case INPUT_KEY_Insert:    return KEY( KEYPAD128_MINUS );
  case INPUT_KEY_Delete:
    return ctrl ? KEY( KEYPAD128_0 ) | KEY( KEYPAD128_2 )
                : KEY( KEYPAD128_LPAREN );
  case INPUT_KEY_BackSpace:
    return ctrl ? KEY( KEYPAD128_0 ) | KEY( KEYPAD128_1 ) : 0;
  case INPUT_KEY_Left:      return ctrl ? KEY( KEYPAD128_4 ) : 0;
  case INPUT_KEY_Right:     return ctrl ? KEY( KEYPAD128_5 ) : 0;
  default:                  return 0;
  }
}

static void
update_keys( void )
{
  int i;

  keys_held = 0;
  for( i = 0; i < held_count; i++ ) keys_held |= held[i].bits;
}

int
keypad128_host_key( input_key key, int press )
{
  int i;
  libspectrum_dword bits;

  if( key == INPUT_KEY_Control_L || key == INPUT_KEY_Control_R ) {
    int bit = key == INPUT_KEY_Control_L ? 1 : 2;
    if( press ) ctrl_held |= bit; else ctrl_held &= ~bit;
    return 0;
  }

  for( i = 0; i < held_count; i++ )
    if( held[i].key == key ) break;

  if( !press ) {
    /* Released as the keypad keys it was pressed as */
    if( i == held_count ) return 0;
    held[i] = held[ --held_count ];
    update_keys();
    return 1;
  }

  /* Autorepeat of a key held as keypad keys */
  if( i < held_count ) return 1;

  if( !keypad128_active() ) return 0;

  bits = keypad_keys_for( key );
  if( !bits || held_count == MAX_HELD ) return 0;

  held[ held_count ].key = key;
  held[ held_count++ ].bits = bits;
  update_keys();
  return 1;
}

void
keypad128_release_all( void )
{
  held_count = 0;
  ctrl_held = 0;
  update_keys();
}

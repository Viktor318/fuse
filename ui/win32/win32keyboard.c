/* win32keyboard.c: routines for dealing with the Win32 keyboard
   Copyright (c) 2003-2008 Marek Januszewski, Philip Kendall

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

   Author contact information:

   E-mail: philip-fuse@shadowmagic.org.uk

*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#ifdef HAVE_LIB_GLIB
#include <glib.h>
#endif				/* #ifdef HAVE_LIB_GLIB */

#include "libspectrum.h"

#include "display.h"
#include "fuse.h"
#include "hungarian_keyboard.h"
#include "keyboard.h"
#include "machine.h"
#include "settings.h"
#include "snapshot.h"
#include "spectrum.h"
#include "tape.h"
#include "utils.h"
#include "win32internals.h"

/* Map OEM unshifted keys to Fuse input layer keysym */
extern const keysyms_map_t oem_keysyms_map[];

static GHashTable *oem_keysyms_hash;

static input_key
oem_keysyms_remap( WPARAM wParam )
{
  const input_key *ptr;
  unsigned int mapped_code;
  libspectrum_dword unshifted_char;

  mapped_code = MapVirtualKey( wParam, MAPVK_VK_TO_CHAR );
  if( !mapped_code ) return INPUT_KEY_NONE;

  unshifted_char = LOWORD( mapped_code );

  /* On some layouts a digit is on an OEM key (e.g. 0 left of 1 on the
     Hungarian layout); it is still that digit, also with Caps Shift */
  if( unshifted_char >= '0' && unshifted_char <= '9' )
    return keysyms_remap( unshifted_char );

  ptr = g_hash_table_lookup( oem_keysyms_hash, &unshifted_char );

  return ptr ? *ptr : INPUT_KEY_NONE;
}

/* Identifier of the physical key, the same on press and release: the
   scan code, plus the extended key flag */
static int
physical_key( LPARAM lParam )
{
  return ( lParam >> 16 ) & 0x1ff;
}

/* Keypad keys held, by physical key: Windows may report the release with
   another virtual key (e.g. VK_END instead of VK_NUMPAD1 if Shift or Num
   Lock changed meanwhile), but the same key must be released */
#define MAX_KEYPAD_HELD 16
static struct { int physical; input_key key; } keypad_held[ MAX_KEYPAD_HELD ];
static int keypad_held_count = 0;

static void
keypad_remember( int physical, input_key key )
{
  int i;

  for( i = 0; i < keypad_held_count; i++ )
    if( keypad_held[i].physical == physical ) {
      keypad_held[i].key = key;
      return;
    }

  if( keypad_held_count < MAX_KEYPAD_HELD ) {
    keypad_held[ keypad_held_count ].physical = physical;
    keypad_held[ keypad_held_count++ ].key = key;
  }
}

static input_key
keypad_forget( int physical )
{
  int i;
  input_key key;

  for( i = 0; i < keypad_held_count; i++ )
    if( keypad_held[i].physical == physical ) {
      key = keypad_held[i].key;
      keypad_held[i] = keypad_held[ --keypad_held_count ];
      return key;
    }

  return INPUT_KEY_NONE;
}

/* The numeric keypad with Num Lock on: digits and the operators, in every
   typing mode */
static input_key
numpad_keysym( WPARAM wParam )
{
  if( wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9 )
    return keysyms_remap( '0' + ( wParam - VK_NUMPAD0 ) );

  switch( wParam ) {
  case VK_ADD:      return INPUT_KEY_plus;
  case VK_SUBTRACT: return INPUT_KEY_minus;
  case VK_MULTIPLY: return INPUT_KEY_asterisk;
  case VK_DIVIDE:   return INPUT_KEY_slash;
  case VK_DECIMAL:  return INPUT_KEY_period;
  }

  return INPUT_KEY_NONE;
}


/* For character based typing (Hungarian keyboard): the character produced
   by this key press. TranslateMessage() has already posted the WM_CHAR
   message for it, right behind the WM_KEYDOWN being handled, so take it from
   the queue. Asking for it with the W variant gives Unicode even for an ANSI
   window, so no code page handling is needed. Returns 0 if none (keys like
   cursors, dead keys, or Ctrl/Alt combinations, which don't produce
   WM_CHAR, or produce a control character) */
static libspectrum_dword
get_character( void )
{
  MSG msg;
  libspectrum_dword character;

  if( !settings_current.hungarian_keyboard ) return 0;

  if( !PeekMessageW( &msg, fuse_hWnd, WM_CHAR, WM_CHAR, PM_REMOVE ) )
    return 0;

  character = (WCHAR)msg.wParam;

  /* Characters outside the Basic Multilingual Plane come as a surrogate
     pair; none of them has a Spectrum equivalent */
  if( character >= 0xd800 && character <= 0xdfff ) {
    PeekMessageW( &msg, fuse_hWnd, WM_CHAR, WM_CHAR, PM_REMOVE );
    return 0;
  }

  return character;
}

/* Windows doesn't always tell us when a modifier is released. In
   particular, AltGr is sent as Ctrl + Alt, and when Ctrl goes up first the
   Alt release comes as WM_SYSKEYUP. If character based typing still
   believes a modifier is held while Windows says it is up, release it, or
   it would keep Symbol (or Caps) Shift pressed after the next character */
static void
sync_modifiers( void )
{
  static const struct { int vk; input_key key; } modifiers[] = {
    { VK_SHIFT, INPUT_KEY_Shift_L },
    { VK_CONTROL, INPUT_KEY_Control_L },
    { VK_MENU, INPUT_KEY_Alt_L },
  };
  input_event_t fuse_event;
  size_t i;

  if( !settings_current.hungarian_keyboard ) return;

  for( i = 0; i < ARRAY_SIZE( modifiers ); i++ ) {
    if( ( GetKeyState( modifiers[i].vk ) & 0x8000 ) ||
        !hungarian_keyboard_modifier_held( modifiers[i].key ) )
      continue;

    fuse_event.type = INPUT_EVENT_KEYRELEASE;
    fuse_event.types.key.native_key = modifiers[i].key;
    fuse_event.types.key.spectrum_key = modifiers[i].key;
    fuse_event.types.key.character = 0;
    fuse_event.types.key.physical_key = 0;
    input_event( &fuse_event );
  }
}

void
win32keyboard_init( void )
{
  keysyms_map_t *ptr3;

  oem_keysyms_hash = g_hash_table_new( g_int_hash, g_int_equal );

  for( ptr3 = (keysyms_map_t *)oem_keysyms_map; ptr3->ui; ptr3++ )
    g_hash_table_insert( oem_keysyms_hash, &( ptr3->ui ), &( ptr3->fuse ) );
}

void
win32keyboard_end( void )
{
  g_hash_table_destroy( oem_keysyms_hash );
}

void
win32keyboard_keypress( WPARAM wParam, LPARAM lParam )
{
  input_key fuse_keysym;
  input_event_t fuse_event;
  libspectrum_dword character;

  sync_modifiers();

  switch( wParam ) {
  case VK_OEM_1:
  case VK_OEM_2:
  case VK_OEM_3:
  case VK_OEM_4:
  case VK_OEM_5:
  case VK_OEM_6:
  case VK_OEM_7:
  case VK_OEM_8:
  case VK_OEM_102:
  case VK_OEM_COMMA:
  case VK_OEM_MINUS:
  case VK_OEM_PERIOD:
  case VK_OEM_PLUS:
    fuse_keysym = oem_keysyms_remap( wParam );
    break;
  default:
    fuse_keysym = numpad_keysym( wParam );
    if( fuse_keysym != INPUT_KEY_NONE )
      keypad_remember( physical_key( lParam ), fuse_keysym );
    else
      fuse_keysym = keysyms_remap( wParam );
    break;
  }

  character = get_character();

  /* Keys with no mapping are still passed on if they produced a character
     (e.g. accented letters) */
  if( fuse_keysym == INPUT_KEY_NONE && !character ) return;

  fuse_event.type = INPUT_EVENT_KEYPRESS;
  fuse_event.types.key.native_key = fuse_keysym;
  fuse_event.types.key.spectrum_key = fuse_keysym;
  fuse_event.types.key.character = character;
  fuse_event.types.key.physical_key = physical_key( lParam );

  input_event( &fuse_event );
}

void
win32keyboard_keyrelease( WPARAM wParam, LPARAM lParam )
{
  input_key fuse_keysym;
  input_event_t fuse_event;

  sync_modifiers();

  switch( wParam ) {
  case VK_OEM_1:
  case VK_OEM_2:
  case VK_OEM_3:
  case VK_OEM_4:
  case VK_OEM_5:
  case VK_OEM_6:
  case VK_OEM_7:
  case VK_OEM_8:
  case VK_OEM_102:
  case VK_OEM_COMMA:
  case VK_OEM_MINUS:
  case VK_OEM_PERIOD:
  case VK_OEM_PLUS:
    fuse_keysym = oem_keysyms_remap( wParam );
    break;
  default:
    fuse_keysym = keypad_forget( physical_key( lParam ) );
    if( fuse_keysym == INPUT_KEY_NONE ) fuse_keysym = keysyms_remap( wParam );
    break;
  }

  /* With character based typing, keys with no mapping may have produced a
     character on press, so their release is needed too */
  if( fuse_keysym == INPUT_KEY_NONE &&
      !settings_current.hungarian_keyboard ) return;

  fuse_event.type = INPUT_EVENT_KEYRELEASE;
  fuse_event.types.key.native_key = fuse_keysym;
  fuse_event.types.key.spectrum_key = fuse_keysym;
  fuse_event.types.key.character = 0;
  fuse_event.types.key.physical_key = physical_key( lParam );

  input_event( &fuse_event );
}


/* "Use left Alt as Caps Shift with number keys": the left Alt and a number
   key of the main block give Caps Shift + that number (EDIT, cursors,
   DELETE...), which the character based typing can't produce. Alt keeps
   its normal Windows meaning (menus, Alt+F4) for every other key */

/* Number keys currently held as Caps Shift + number */
static WPARAM alt_digits[10];
static int alt_digit_count = 0;

/* Alt was used for a number key since it went down: its release must not
   activate the menu bar */
static int alt_used = 0;

/* Pressing Alt shows the underlined menu letters; once Alt was used for a
   number key, hide them again */
static void
hide_menu_underlines( void )
{
  /* Posted, so that it comes after Windows has handled the Alt release */
  PostMessage( fuse_hWnd, WM_CHANGEUISTATE,
               MAKEWPARAM( UIS_SET, UISF_HIDEACCEL | UISF_HIDEFOCUS ), 0 );
}

static void
send_alt( int press )
{
  input_event_t fuse_event;

  fuse_event.type = press ? INPUT_EVENT_KEYPRESS : INPUT_EVENT_KEYRELEASE;
  fuse_event.types.key.native_key = INPUT_KEY_Alt_L;
  fuse_event.types.key.spectrum_key = INPUT_KEY_Alt_L;
  fuse_event.types.key.character = 0;
  fuse_event.types.key.physical_key = 0;

  /* The left Alt is Caps Shift for this press and its release */
  keyboard_alt_caps_shift = 1;
  input_event( &fuse_event );
  if( !press ) keyboard_alt_caps_shift = 0;
}

/* Is this a number key of the main block? (Also when the digit is on an
   OEM key, like 0 on the Hungarian layout) */
static int
is_number_key( WPARAM wParam )
{
  unsigned int c;

  if( wParam >= '0' && wParam <= '9' ) return 1;

  c = LOWORD( MapVirtualKey( wParam, MAPVK_VK_TO_CHAR ) );
  return wParam >= VK_OEM_1 && c >= '0' && c <= '9';
}

static int
find_alt_digit( WPARAM wParam )
{
  int i;

  for( i = 0; i < alt_digit_count; i++ )
    if( alt_digits[i] == wParam ) return i;

  return -1;
}

/* WM_SYSKEYDOWN. Returns 1 if handled here (keep it from Windows) */
int
win32keyboard_syskeypress( WPARAM wParam, LPARAM lParam )
{
  if( wParam == VK_MENU ) {
    /* A new press of Alt (not autorepeat) */
    if( !( lParam & ( 1 << 30 ) ) ) alt_used = 0;
    return 0;
  }

  if( !settings_current.alt_caps_shift || !is_number_key( wParam ) ||
      !( GetKeyState( VK_LMENU ) & 0x8000 ) ||
      ( GetKeyState( VK_RMENU ) & 0x8000 ) ||
      ( GetKeyState( VK_CONTROL ) & 0x8000 ) )
    return 0;

  alt_used = 1;

  if( find_alt_digit( wParam ) < 0 ) {
    if( alt_digit_count == 0 ) send_alt( 1 );
    if( alt_digit_count < (int)ARRAY_SIZE( alt_digits ) )
      alt_digits[ alt_digit_count++ ] = wParam;
  }

  win32keyboard_keypress( wParam, lParam );
  return 1;
}

/* A number key released (with or without Alt still held) */
static int
alt_digit_release( WPARAM wParam, LPARAM lParam )
{
  int i = find_alt_digit( wParam );

  if( i < 0 ) return 0;

  win32keyboard_keyrelease( wParam, lParam );
  alt_digits[i] = alt_digits[ --alt_digit_count ];
  if( alt_digit_count == 0 ) send_alt( 0 );

  return 1;
}

/* WM_SYSKEYUP. Returns 1 if handled here */
int
win32keyboard_syskeyrelease( WPARAM wParam, LPARAM lParam )
{
  if( alt_digit_release( wParam, lParam ) ) return 1;

  /* Alt itself, after it was used for a number key: Windows doesn't
     activate the menu bar, but make sure the underlines go away too */
  if( wParam == VK_MENU && alt_used ) {
    alt_used = 0;
    hide_menu_underlines();
  }

  return 0;
}

/* WM_KEYUP of a number key after Alt went up first */
int
win32keyboard_alt_digit_keyrelease( WPARAM wParam, LPARAM lParam )
{
  return alt_digit_release( wParam, lParam );
}

/* WM_SYSCHAR: the digit typed with Alt mustn't make Windows beep */
int
win32keyboard_alt_digit_char( WPARAM wParam )
{
  return alt_used && wParam >= '0' && wParam <= '9';
}

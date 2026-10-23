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

/* Left Alt used as Caps Shift: is it held at the moment? */
static int left_alt_held = 0;

static int
is_left_alt( WPARAM wParam, LPARAM lParam )
{
  /* The right Alt (AltGr) has the extended key flag */
  return wParam == VK_MENU && !( lParam & ( 1 << 24 ) );
}

/* If the left Alt went up while another window had the focus (e.g. after
   Alt+Tab), Caps Shift must not stay pressed */
static void
sync_left_alt( void )
{
  input_event_t fuse_event;

  if( !left_alt_held || ( GetKeyState( VK_LMENU ) & 0x8000 ) ) return;

  left_alt_held = 0;
  fuse_event.type = INPUT_EVENT_KEYRELEASE;
  fuse_event.types.key.native_key = INPUT_KEY_Alt_L;
  fuse_event.types.key.spectrum_key = INPUT_KEY_Alt_L;
  fuse_event.types.key.character = 0;
  fuse_event.types.key.physical_key = 0;
  input_event( &fuse_event );
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

  sync_left_alt();
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

  sync_left_alt();
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
    fuse_keysym = keysyms_remap( wParam );
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

/* With "Use left Alt as Caps Shift too", the left Alt and the keys pressed
   together with it come as system keys (WM_SYSKEYDOWN/UP). Pass them to the
   emulated machine, and keep them away from Windows, so that Alt doesn't
   activate the menu bar. Returns 1 if the key was handled here */
int
win32keyboard_syskeypress( WPARAM wParam, LPARAM lParam )
{
  if( is_left_alt( wParam, lParam ) ) {
    if( !settings_current.alt_caps_shift ) return 0;
    if( !left_alt_held ) {
      win32keyboard_keypress( wParam, lParam );
      left_alt_held = 1;
    }
    return 1;
  }

  if( !left_alt_held ) return 0;

  win32keyboard_keypress( wParam, lParam );
  return 1;
}

int
win32keyboard_syskeyrelease( WPARAM wParam, LPARAM lParam )
{
  if( !left_alt_held ) return 0;

  if( is_left_alt( wParam, lParam ) ) {
    win32keyboard_keyrelease( wParam, lParam );
    left_alt_held = 0;
    return 1;
  }

  win32keyboard_keyrelease( wParam, lParam );
  return 1;
}

/* Characters typed with the left Alt held mustn't select menu items */
int
win32keyboard_left_alt_held( void )
{
  return left_alt_held;
}

/* fuse-operations.c: complete engine operations for host callers
   Copyright (c) 1999-2026 Philip Kendall and others
   Copyright (c) 2026 Fredrick Meunier

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

#include "config.h"

#include <string.h>

#include "display.h"
#include "event.h"
#include "fuse.h"
#include "fuse-engine.h"
#include "input.h"
#include "keyboard.h"
#include "machine.h"
#include "peripherals/joystick.h"
#include "peripherals/kempmouse.h"
#include "tape.h"
#include "utils.h"
#include "z80/z80.h"

static void
release_physical_input( void )
{
  int button;
  input_cancel_physical();
  for( button = 0; button < 8; button++ )
    kempmouse_update( 0, 0, button, 0 );
}

fuse_host_operation_result
fuse_host_select_machine( const char *id )
{
  int i, error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !id ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  for( i = 0; i < machine_count; i++ )
    if( !strcmp( id, machine_types[i]->id ) ) break;
  if( i == machine_count ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  fuse_engine_begin_operation();
  /* ID selection avoids the legacy enum/snapshot fallback and abort policy. */
  error = machine_select_id( machine_types[i]->id );
  return fuse_engine_end_machine_operation( error );
}

fuse_host_operation_result
fuse_host_reset( fuse_host_reset_kind kind )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( kind != FUSE_HOST_RESET_SOFT && kind != FUSE_HOST_RESET_HARD )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  fuse_engine_begin_operation();
  error = machine_reset( kind == FUSE_HOST_RESET_HARD );
  return fuse_engine_end_machine_operation( error );
}

fuse_host_operation_result
fuse_host_open_file( const char *filename )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !filename || !*filename ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  fuse_engine_begin_operation();
  error = utils_open_file( filename, tape_can_autoload(), NULL );
  display_refresh_all(); /* Same post-operation refresh as menu_file_open. */
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_nmi( void )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  event_add( 0, z80_nmi_event );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_keyboard_event( input_key key, int pressed )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( ( pressed != 0 && pressed != 1 ) ||
      !( key == INPUT_KEY_Tab || key == INPUT_KEY_Return || key == INPUT_KEY_Escape ||
         ( key >= INPUT_KEY_space && key <= INPUT_KEY_BackSpace ) ||
         key == INPUT_KEY_KP_Enter || ( key >= INPUT_KEY_Up && key <= INPUT_KEY_F12 ) ||
         ( key >= INPUT_KEY_Shift_L && key <= INPUT_KEY_Mode_switch ) ) )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( pressed ) input_keypress_emulation( key );
  else input_keyrelease_emulation( key );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_spectrum_key_event( fuse_host_key key, int pressed )
{
  keyboard_key_name target;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( pressed != 0 && pressed != 1 ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( key == FUSE_HOST_KEY_ENTER ) target = KEYBOARD_Enter;
  else if( key == FUSE_HOST_KEY_CAPS ) target = KEYBOARD_Caps;
  else if( key == FUSE_HOST_KEY_SYMBOL ) target = KEYBOARD_Symbol;
  else if( key == FUSE_HOST_KEY_SPACE ||
           ( key >= FUSE_HOST_KEY_0 && key <= FUSE_HOST_KEY_9 ) ||
           ( key >= FUSE_HOST_KEY_A && key <= FUSE_HOST_KEY_Z ) )
    target = (keyboard_key_name)key;
  else return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( pressed ) keyboard_press( target ); else keyboard_release( target );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_joystick_event( fuse_host_joystick joystick,
  fuse_host_joystick_button button, int pressed )
{
  static const joystick_button buttons[] = {
    JOYSTICK_BUTTON_LEFT, JOYSTICK_BUTTON_RIGHT, JOYSTICK_BUTTON_UP,
    JOYSTICK_BUTTON_DOWN, JOYSTICK_BUTTON_FIRE
  };
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( joystick < FUSE_HOST_JOYSTICK_1 || joystick > FUSE_HOST_JOYSTICK_KEYBOARD ||
      button < FUSE_HOST_JOYSTICK_LEFT || button > FUSE_HOST_JOYSTICK_FIRE ||
      ( pressed != 0 && pressed != 1 ) ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  joystick_press( joystick == FUSE_HOST_JOYSTICK_KEYBOARD ? JOYSTICK_KEYBOARD :
                  joystick == FUSE_HOST_JOYSTICK_1 ? 0 : 1, buttons[button], pressed );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_mouse_motion( int dx, int dy )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  /* Reduce before arithmetic: even INT_MIN/MAX cannot overflow the addition. */
  kempmouse_update( dx % 256, dy % 256, -1, 0 );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_mouse_event( fuse_host_mouse_button button, int pressed )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( ( button != FUSE_HOST_MOUSE_LEFT && button != FUSE_HOST_MOUSE_RIGHT ) ||
      ( pressed != 0 && pressed != 1 ) ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  input_mouse_button_event( button == FUSE_HOST_MOUSE_LEFT ? INPUT_MOUSE_LEFT :
                           INPUT_MOUSE_RIGHT, pressed );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_release_all( void )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  release_physical_input();
  return FUSE_HOST_OPERATION_OK;
}

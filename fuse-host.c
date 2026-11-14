/* fuse-host.c: private host configuration, resources and typed operations
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

#include "event.h"
#include "fuse.h"
#include "fuse-engine.h"
#include "input.h"
#include "keyboard.h"
#include "machine.h"
#include "peripherals/joystick.h"
#include "peripherals/kempmouse.h"
#include "settings.h"
#include "tape.h"
#include "utils.h"
#include "z80/z80.h"

struct fuse_host_config {
  char *machine;
  int sound;
  int joystick_output[3];
  int mouse_swap_buttons;
};

static struct {
  int active;
  fuse_host_resources resources;
} host_preparation;

fuse_host_config *
fuse_host_config_alloc( void )
{
  settings_info defaults = { 0 };
  fuse_host_config *config = libspectrum_new0( fuse_host_config, 1 );
  settings_defaults( &defaults );
  config->machine = utils_safe_strdup( defaults.start_machine );
  config->sound = defaults.sound;
  config->joystick_output[0] = defaults.joystick_1_output;
  config->joystick_output[1] = defaults.joystick_2_output;
  config->joystick_output[2] = defaults.joystick_keyboard_output;
  config->mouse_swap_buttons = defaults.mouse_swap_buttons;
  settings_free( &defaults );
  return config;
}

void
fuse_host_config_free( fuse_host_config *config )
{
  if( !config ) return;
  libspectrum_free( config->machine );
  libspectrum_free( config );
}

int
fuse_host_config_set_machine( fuse_host_config *config, const char *id )
{
  if( !config || !id || !*id ) return 1;
  settings_set_string( &config->machine, id );
  return 0;
}

int
fuse_host_config_set_sound( fuse_host_config *config, int enabled )
{
  if( !config || ( enabled != 0 && enabled != 1 ) ) return 1;
  config->sound = enabled;
  return 0;
}

int
fuse_host_config_set_joystick( fuse_host_config *config,
  fuse_host_joystick slot, fuse_host_joystick_output output )
{
  static const joystick_type_t outputs[] = {
    JOYSTICK_TYPE_NONE, JOYSTICK_TYPE_CURSOR, JOYSTICK_TYPE_KEMPSTON,
    JOYSTICK_TYPE_SINCLAIR_1, JOYSTICK_TYPE_SINCLAIR_2, JOYSTICK_TYPE_TIMEX_1,
    JOYSTICK_TYPE_TIMEX_2, JOYSTICK_TYPE_FULLER
  };
  if( !config || slot < FUSE_HOST_JOYSTICK_1 || slot > FUSE_HOST_JOYSTICK_KEYBOARD ||
      output < FUSE_HOST_JOYSTICK_OUTPUT_NONE || output > FUSE_HOST_JOYSTICK_OUTPUT_FULLER )
    return 1;
  config->joystick_output[slot] = outputs[output];
  return 0;
}

int
fuse_host_config_set_mouse_swap_buttons( fuse_host_config *config, int swap )
{
  if( !config || ( swap != 0 && swap != 1 ) ) return 1;
  config->mouse_swap_buttons = swap;
  return 0;
}

void
fuse_host_apply_config( const fuse_host_config *config )
{
  settings_set_string( &settings_current.start_machine, config->machine );
  settings_current.sound = config->sound;
  settings_current.joystick_1_output = config->joystick_output[0];
  settings_current.joystick_2_output = config->joystick_output[1];
  settings_current.joystick_keyboard_output = config->joystick_output[2];
  settings_current.mouse_swap_buttons = config->mouse_swap_buttons;
}

void
fuse_host_begin_resources( const fuse_host_resources *resources )
{
  host_preparation.active = 1;
  host_preparation.resources = *resources;
}

void
fuse_host_end_resources( void )
{
  memset( &host_preparation, 0, sizeof( host_preparation ) );
}

int
fuse_host_resources_active( void )
{
  return host_preparation.active;
}

int
fuse_host_acquire_rom( const char *name, fuse_host_rom *rom )
{
  memset( rom, 0, sizeof( *rom ) );
  return host_preparation.resources.acquire_rom(
    host_preparation.resources.context, name, rom );
}

fuse_init_result
fuse_host_prepare( const fuse_host_config *config,
                   const fuse_host_resources *resources )
{
  return fuse_engine_host_prepare( config, resources );
}

static void
host_release_physical( void )
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
fuse_host_insert_tape_file( const char *filename )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !filename || !*filename ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  fuse_engine_begin_operation();
  error = tape_open( filename, 0 ); /* Same intent as menu_media_tape_open. */
  /* tape_open owns replacement and selected-UI save/discard/cancel handling.
     Do not retry close on failure: it may represent a cancelled replacement. */
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
  host_release_physical();
  return FUSE_HOST_OPERATION_OK;
}

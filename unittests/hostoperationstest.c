/* Private owner-thread operations. Each invocation owns one fresh process. */
#include "config.h"
#undef NDEBUG
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fuse.h"
#include "fuse-internal.h"
#include "event.h"
#include "keyboard.h"
#include "machine.h"
#include "memory.h"
#include "periph.h"
#include "peripherals/joystick.h"
#include "settings.h"
#include "ui/ui.h"
#include "z80/z80.h"

#define OK FUSE_HOST_OPERATION_OK
static struct {
  const char *directory;
  int fail, acquired, released;
  unsigned char *bytes;
} provider;

static void
release_rom( void *context )
{
  assert( context == &provider );
  free( provider.bytes ); provider.bytes = NULL;
  provider.released++;
}

static int
acquire_rom( void *context, const char *name, fuse_host_rom *rom )
{
  char path[4096];
  long length;
  FILE *file;
  assert( context == &provider );
  assert( provider.acquired == provider.released );
  provider.acquired++;
  rom->context = &provider; rom->release = release_rom;
  assert( fuse_host_reset( FUSE_HOST_RESET_SOFT ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_select_machine( "48" ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_release_all() == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_keyboard_event( INPUT_KEY_a, 1 ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_nmi() == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_BUSY );
  assert( fuse_legacy_run_tstates( 1 ) == FUSE_LEGACY_RUN_BUSY );
  fuse_legacy_shutdown();
  if( provider.fail ) return 1;
  assert( snprintf( path, sizeof(path), "%s/%s", provider.directory, name ) < sizeof(path) );
  file = fopen( path, "rb" );
  assert( file && !fseek( file, 0, SEEK_END ) );
  length = ftell( file );
  assert( length > 0 && !fseek( file, 0, SEEK_SET ) );
  provider.bytes = malloc( length );
  assert( provider.bytes );
  assert( fread( provider.bytes, 1, length, file ) == length );
  assert( !fclose( file ) );
  rom->data = provider.bytes; rom->length = length;
  return 0;
}

static void
capture_input( unsigned char values[KEYBOARD_HALFROWS + 4] )
{
  libspectrum_byte attached;
  memcpy( values, keyboard_return_values, KEYBOARD_HALFROWS );
  values[KEYBOARD_HALFROWS] = joystick_kempston_read( 0, &attached );
  values[KEYBOARD_HALFROWS + 1] = joystick_timex_read( 0, 0 );
  values[KEYBOARD_HALFROWS + 2] = joystick_timex_read( 0, 1 );
  values[KEYBOARD_HALFROWS + 3] = joystick_fuller_read( 0, &attached );
}

static void
check_released( void )
{
  unsigned char values[KEYBOARD_HALFROWS + 4];
  int row;
  capture_input( values );
  for( row = 0; row < KEYBOARD_HALFROWS; row++ ) assert( values[row] == 0xff );
  assert( values[KEYBOARD_HALFROWS] == 0 );
  assert( values[KEYBOARD_HALFROWS + 1] == 0 );
  assert( values[KEYBOARD_HALFROWS + 2] == 0 );
  assert( values[KEYBOARD_HALFROWS + 3] == 0xff );
  assert( ( readport_internal( 0xfadf ) & 3 ) == 3 );
}

static void
hold_buttons( void )
{
  assert( fuse_host_spectrum_key_event( FUSE_HOST_KEY_A, 1 ) == OK );
  assert( fuse_host_joystick_event( FUSE_HOST_JOYSTICK_1, FUSE_HOST_JOYSTICK_FIRE, 1 ) == OK );
  assert( fuse_host_mouse_event( FUSE_HOST_MOUSE_LEFT, 1 ) == OK );
  assert( fuse_host_mouse_event( FUSE_HOST_MOUSE_RIGHT, 1 ) == OK );
}

static void
check_input( void )
{
  int key, slot, output, button, swap;
  libspectrum_byte attached, before[KEYBOARD_HALFROWS], mouse_x, mouse_y;
  libspectrum_dword clock = tstates;
  settings_current.kempston_mouse = 1;
  (void)periph_update(); /* Returns reset-needed, not an error. */
  for( key = FUSE_HOST_KEY_SPACE; key <= FUSE_HOST_KEY_SYMBOL; key++ ) {
    int valid = key == FUSE_HOST_KEY_SPACE || ( key >= '0' && key <= '9' ) ||
      ( key >= 'a' && key <= 'z' ) || key >= FUSE_HOST_KEY_ENTER;
    if( !valid ) continue;
    assert( fuse_host_release_all() == OK );
    keyboard_press( key );
    memcpy( before, keyboard_return_values, sizeof(before) );
    keyboard_release( key );
    assert( fuse_host_spectrum_key_event( key, 1 ) == OK );
    assert( !memcmp( before, keyboard_return_values, sizeof(before) ) );
    assert( fuse_host_spectrum_key_event( key, 0 ) == OK );
    check_released();
  }
  /* The same slot/button call follows each configured mapping, including NONE. */
  for( slot = FUSE_HOST_JOYSTICK_1; slot <= FUSE_HOST_JOYSTICK_KEYBOARD; slot++ )
    for( output = JOYSTICK_TYPE_NONE; output < JOYSTICK_TYPE_COUNT; output++ )
      for( button = FUSE_HOST_JOYSTICK_LEFT; button <= FUSE_HOST_JOYSTICK_FIRE; button++ ) {
        unsigned char expected[KEYBOARD_HALFROWS + 4], actual[KEYBOARD_HALFROWS + 4];
        int *mapping = slot == 0 ? &settings_current.joystick_1_output :
          slot == 1 ? &settings_current.joystick_2_output : &settings_current.joystick_keyboard_output;
        int saved = *mapping;
        *mapping = output;
        joystick_press( slot, button, 1 ); capture_input( expected );
        joystick_press( slot, button, 0 );
        assert( fuse_host_joystick_event( slot, button, 1 ) == OK );
        capture_input( actual );
        assert( !memcmp( expected, actual, sizeof(expected) ) );
        assert( fuse_host_joystick_event( slot, button, 0 ) == OK );
        *mapping = saved;
        check_released();
      }
  /* Normalized keys retain composite translation and configured keyboard-joy. */
  {
    input_key keys[] = { INPUT_KEY_a, INPUT_KEY_Escape, INPUT_KEY_Left, INPUT_KEY_Right,
                        INPUT_KEY_Shift_L, INPUT_KEY_Control_R, INPUT_KEY_F1 };
    unsigned int i;
    int saved = settings_current.joystick_keyboard_output;
    for( output = JOYSTICK_TYPE_NONE; output < JOYSTICK_TYPE_COUNT; output++ ) {
      settings_current.joystick_keyboard_output = output;
      for( i = 0; i < sizeof(keys)/sizeof(keys[0]); i++ ) {
        unsigned char expected[KEYBOARD_HALFROWS + 4], actual[KEYBOARD_HALFROWS + 4];
        input_keypress_emulation( keys[i] ); capture_input( expected );
        input_keyrelease_emulation( keys[i] );
        assert( fuse_host_keyboard_event( keys[i], 1 ) == OK );
        capture_input( actual ); assert( !memcmp( expected, actual, sizeof(expected) ) );
        assert( fuse_host_keyboard_event( keys[i], 0 ) == OK );
        assert( fuse_host_release_all() == OK );
      }
    }
    settings_current.joystick_keyboard_output = saved;
  }
  settings_current.keyboard_arrows_shifted = 1;
  assert( fuse_host_keyboard_event( INPUT_KEY_Left, 1 ) == OK );
  assert( !( keyboard_return_values[0] & 1 ) ); /* Caps Shift. */
  assert( fuse_host_keyboard_event( INPUT_KEY_Left, 0 ) == OK );
  assert( keyboard_return_values[0] & 1 );
  settings_current.keyboard_arrows_shifted = 0;
  settings_current.recreated_spectrum = 1;
  assert( fuse_host_keyboard_event( INPUT_KEY_a, 1 ) == OK );
  assert( !( keyboard_return_values[3] & 1 ) ); /* Recreated a means 1 down. */
  assert( fuse_host_keyboard_event( INPUT_KEY_b, 1 ) == OK );
  assert( keyboard_return_values[3] & 1 ); /* Recreated b means 1 up. */
  settings_current.recreated_spectrum = 0;
  assert( fuse_host_release_all() == OK );
  /* Even UI-triggering values must not invoke native actions. */
  ui_widget_level = 0; ui_mouse_grabbed = 1;
  assert( fuse_host_keyboard_event( INPUT_KEY_Escape, 1 ) == OK );
  assert( ui_mouse_grabbed == 1 && !input_dispatch_pending() );
  assert( keyboard_read( 0xf7 ) != 0xff );
  assert( fuse_host_keyboard_event( INPUT_KEY_Escape, 0 ) == OK );
  assert( fuse_host_keyboard_event( INPUT_KEY_F1, 1 ) == OK );
  ui_widget_level = -1; ui_mouse_grabbed = 0;
  keyboard_synthetic_press( KEYBOARD_SYNTHETIC_PHANTOM_TYPIST, KEYBOARD_a );
  hold_buttons();
  assert( fuse_host_release_all() == OK ); check_released();
  assert( keyboard_read( 0xfd ) != 0xff );
  keyboard_synthetic_release_all( KEYBOARD_SYNTHETIC_PHANTOM_TYPIST );
  /* Clear all interfaces, even if their mapping changed since press. */
  assert( fuse_host_joystick_event( FUSE_HOST_JOYSTICK_1, FUSE_HOST_JOYSTICK_FIRE, 1 ) == OK );
  assert( joystick_kempston_read( 0, &attached ) == 0x10 );
  settings_current.joystick_1_output = JOYSTICK_TYPE_FULLER;
  assert( fuse_host_release_all() == OK ); check_released();
  settings_current.joystick_1_output = JOYSTICK_TYPE_KEMPSTON;
  for( swap = 0; swap < 2; swap++ ) {
    settings_current.mouse_swap_buttons = swap;
    assert( fuse_host_mouse_event( FUSE_HOST_MOUSE_LEFT, 1 ) == OK );
    assert( ( readport_internal( 0xfadf ) & 3 ) == ( swap ? 2 : 1 ) );
    assert( fuse_host_mouse_event( FUSE_HOST_MOUSE_RIGHT, 1 ) == OK );
    assert( ( readport_internal( 0xfadf ) & 3 ) == 0 );
    assert( fuse_host_release_all() == OK );
  }
  mouse_x = readport_internal( 0xfbdf ); mouse_y = readport_internal( 0xffdf );
  assert( fuse_host_mouse_motion( 7, -9 ) == OK );
  assert( readport_internal( 0xfbdf ) == (libspectrum_byte)( mouse_x + 7 ) );
  assert( readport_internal( 0xffdf ) == (libspectrum_byte)( mouse_y + 9 ) );
  mouse_x = readport_internal( 0xfbdf ); mouse_y = readport_internal( 0xffdf );
  assert( fuse_host_mouse_motion( INT_MAX, INT_MIN ) == OK );
  assert( readport_internal( 0xfbdf ) == (libspectrum_byte)( mouse_x + INT_MAX % 256 ) );
  assert( readport_internal( 0xffdf ) == (libspectrum_byte)( mouse_y - INT_MIN % 256 ) );
  assert( fuse_host_release_all() == OK );
  assert( readport_internal( 0xfbdf ) == (libspectrum_byte)( mouse_x + INT_MAX % 256 ) );
  assert( fuse_host_keyboard_event( -1, 1 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_keyboard_event( INPUT_KEY_a, 2 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_spectrum_key_event( -1, 1 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_joystick_event( 99, 0, 1 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_joystick_event( 0, -1, 1 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_mouse_event( 2, 1 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_reset( 2 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  hold_buttons();
  assert( fuse_host_select_machine( "not-a-machine" ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( joystick_kempston_read( 0, &attached ) == 0x10 );
  assert( fuse_host_release_all() == OK );
  assert( tstates == clock );
}

static void
active_callback( libspectrum_dword ts, int type, void *data )
{
  (void)ts; (void)type; (void)data;
  assert( fuse_host_nmi() == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_reset( FUSE_HOST_RESET_SOFT ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_select_machine( "128" ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_keyboard_event( INPUT_KEY_a, 1 ) == FUSE_HOST_OPERATION_BUSY );
}

int
host_operations_test( int argc, char **argv )
{
  fuse_host_config *config;
  fuse_host_resources resources = { &provider, acquire_rom };
  libspectrum_dword clock;
  int callback;
  assert( argc == 3 ); provider.directory = argv[2];
  assert( fuse_host_release_all() == FUSE_HOST_OPERATION_NOT_READY );
  config = fuse_host_config_alloc();
  assert( !fuse_host_config_set_sound( config, 0 ) );
  assert( fuse_host_config_set_joystick( config, 3, 0 ) );
  assert( fuse_host_config_set_joystick( config, 0, 8 ) );
  assert( !fuse_host_config_set_joystick( config, FUSE_HOST_JOYSTICK_1, FUSE_HOST_JOYSTICK_OUTPUT_KEMPSTON ) );
  assert( !fuse_host_config_set_joystick( config, FUSE_HOST_JOYSTICK_2, FUSE_HOST_JOYSTICK_OUTPUT_FULLER ) );
  assert( fuse_host_config_set_mouse_swap_buttons( config, 2 ) );
  assert( !fuse_host_config_set_mouse_swap_buttons( config, 1 ) );
  assert( fuse_host_prepare( config, &resources ) == FUSE_LEGACY_INIT_READY );
  fuse_host_config_free( config );
  assert( settings_current.joystick_1_output == JOYSTICK_TYPE_KEMPSTON );
  assert( settings_current.joystick_2_output == JOYSTICK_TYPE_FULLER );
  assert( settings_current.mouse_swap_buttons == 1 );
  if( strcmp( argv[1], "success" ) ) {
    provider.fail = 1;
    if( !strcmp( argv[1], "reset-failure" ) )
      assert( fuse_host_reset( FUSE_HOST_RESET_HARD ) == FUSE_HOST_OPERATION_FAILED );
    else assert( fuse_host_select_machine( "128" ) == FUSE_HOST_OPERATION_FAILED );
    assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_NOT_READY );
    assert( fuse_legacy_run_tstates( 1 ) == FUSE_LEGACY_RUN_NOT_READY );
    assert( fuse_host_nmi() == FUSE_HOST_OPERATION_NOT_READY );
  } else {
    assert( !fuse_emulation_pause() && !fuse_emulation_pause() );
    check_input(); clock = tstates;
    hold_buttons();
    assert( fuse_host_reset( FUSE_HOST_RESET_SOFT ) == OK );
    assert( tstates == clock && keyboard_return_values[1] != 0xff );
    assert( ( readport_internal( 0xfadf ) & 3 ) == 0 );
    assert( fuse_host_reset( FUSE_HOST_RESET_HARD ) == OK );
    assert( keyboard_return_values[1] != 0xff );
    assert( ( readport_internal( 0xfadf ) & 3 ) == 0 );
    assert( fuse_host_select_machine( "128" ) == OK );
    assert( keyboard_return_values[1] != 0xff );
    assert( ( readport_internal( 0xfadf ) & 3 ) == 0 );
    assert( machine_current->machine == LIBSPECTRUM_MACHINE_128 );
    assert( fuse_emulation_paused == 2 );
    assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_PAUSED );
    assert( fuse_host_release_all() == OK ); check_released();
    assert( fuse_host_select_machine( "48" ) == OK );
    assert( !fuse_emulation_unpause() && !fuse_emulation_unpause() );
    callback = event_register( active_callback, "Host operation reentry" );
    assert( callback >= 0 ); event_add( tstates, callback );
    assert( fuse_legacy_run_tstates( 1 ) == FUSE_LEGACY_RUN_COMPLETED );
    event_reset(); tstates = 0; z80.pc.w = 0x8000; z80.sp.w = 0x9000;
    writebyte_internal( 0x8000, 0 );
    assert( fuse_host_nmi() == OK );
    assert( z80.pc.w == 0x8000 && z80.sp.w == 0x9000 && tstates == 0 );
    event_do_events(); assert( z80.pc.w == 0x66 && z80.sp.w == 0x8ffe );
    fuse_exiting = 1;
    assert( fuse_host_keyboard_event( INPUT_KEY_a, 1 ) == FUSE_HOST_OPERATION_EXITING );
  }
  assert( provider.acquired == provider.released );
  fuse_legacy_shutdown(); fuse_legacy_shutdown();
  assert( fuse_host_reset( FUSE_HOST_RESET_SOFT ) == FUSE_HOST_OPERATION_NOT_READY );
  assert( !fuse_host_resources_active() );
  return 0;
}

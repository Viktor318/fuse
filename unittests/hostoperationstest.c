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
#include "tape.h"
#include "tape_internals.h"
#include "ui/ui.h"
#include "z80/z80.h"

#define OK FUSE_HOST_OPERATION_OK
static struct {
  const char *directory;
  int fail, acquired, released, rejected;
  const char *fail_name;
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
  assert( fuse_host_insert_tape_file( "not-opened.tap" ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_run_frame() == FUSE_RUN_BUSY );
  assert( fuse_run_tstates( 1 ) == FUSE_RUN_BUSY );
  fuse_shutdown();
  if( provider.fail || ( provider.fail_name && !strcmp( name, provider.fail_name ) ) ) {
    provider.rejected++;
    return 1;
  }
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

static compat_file_vtable_t tape_file_io;
static int tape_file_closes;

static off_t
tape_size_failure( compat_fd fd )
{
  (void)fd;
  return -1;
}

static int
tape_file_close( compat_fd fd )
{
  tape_file_closes++;
  return tape_file_io.close( fd );
}

static libspectrum_error
tape_error( libspectrum_error error, const char *format, va_list ap )
{
  (void)format; (void)ap;
  assert( fuse_host_insert_tape_file( "not-opened.tap" ) == FUSE_HOST_OPERATION_BUSY );
  return error;
}

static void
check_tape_workflow( void )
{
  static const unsigned char valid[] = { 2, 0, 0xff, 0xff };
  static const unsigned char bad[] = { 2, 0, 0xff, 0xff, 5, 0, 0xff };
  char filename[] = "host-driver.tap";
  FILE *file;
  compat_file_vtable_t failing;
  libspectrum_error_function_t previous_error;
  libspectrum_tape_block *block;
  libspectrum_dword clock = tstates;
  int i, microphone, saved_auto_load = settings_current.auto_load;
  int rom_requests = provider.acquired, legacy_present;

  assert( fuse_host_insert_tape_file( NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_insert_tape_file( "" ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  file = fopen( filename, "wb" ); assert( file );
  assert( fwrite( valid, 1, sizeof(valid), file ) == sizeof(valid) && !fclose(file) );
  /* Compare the host boundary with the exact Media > Tape > Open mechanics.
     File > Open's preference/ROM eligibility is a different existing intent. */
  for( i = 0; i <= 1; i++ ) {
    settings_current.auto_load = i;
    assert( tape_can_autoload() == i ); /* Provider supplies standard ROMs. */
    assert( !tape_open( filename, 0 ) );
    assert( tape_present() && !tape_is_playing() && tstates == clock );
    assert( !tape_close() );
    assert( fuse_host_insert_tape_file( filename ) == OK );
    assert( tape_present() && !tape_is_playing() && tstates == clock );
    assert( settings_current.auto_load == i && provider.acquired == rom_requests );
    assert( !tape_close() );
  }
  settings_current.auto_load = saved_auto_load;
  file = fopen( "host-bad.tap", "wb" ); assert( file );
  assert( fwrite( bad, 1, sizeof(bad), file ) == sizeof(bad) && !fclose(file) );
  file = fopen( "host-empty.tap", "wb" ); assert( file && !fclose(file) );
  previous_error = libspectrum_error_function;
  libspectrum_error_function = tape_error;
  assert( fuse_host_insert_tape_file( "missing-host.tap" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( !tape_present() && !tape_is_playing() && tstates == clock );
  libspectrum_error_function = previous_error;
  assert( tape_open( "host-bad.tap", 0 ) );
  legacy_present = tape_present();
  assert( !tape_close() );
  libspectrum_error_function = tape_error;
  assert( fuse_host_insert_tape_file( "host-bad.tap" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( tape_present() == legacy_present && !tape_is_playing() && tstates == clock );
  assert( !tape_close() ); /* Explicit fixture cleanup, not API rollback. */
  /* Empty TAP is valid insertion; playability is not an insertion condition. */
  assert( fuse_host_insert_tape_file( "host-empty.tap" ) == OK );
  assert( !tape_present() && !tape_is_playing() && tstates == clock );
  compat_file_get_vtable( &tape_file_io ); failing = tape_file_io;
  failing.get_length = tape_size_failure; failing.close = tape_file_close;
  compat_file_set_vtable( &failing );
  assert( fuse_host_insert_tape_file( filename ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  compat_file_set_vtable( &tape_file_io );
  assert( tape_file_closes == 1 && !tape_present() );
  libspectrum_error_function = previous_error;
  assert( fuse_host_keyboard_event( INPUT_KEY_space, 0 ) == OK );

  assert( !fuse_emulation_pause() );
  assert( fuse_host_insert_tape_file( filename ) == OK );
  filename[0] = 'x'; /* The caller's filename is not retained. */
  assert( !remove( "host-driver.tap" ) ); /* All data is owned by the deck now. */
  assert( tape_present() && !tape_is_playing() && tstates == clock );
  block = libspectrum_tape_current_block( tape ); assert( block );
  assert( libspectrum_tape_block_data( block )[0] == 0xff );
  assert( fuse_run_frame() == FUSE_RUN_PAUSED && tstates == clock );
  assert( fuse_host_insert_tape_file( "missing-host.tap" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  tape_modified = 1;
  assert( fuse_host_insert_tape_file( "missing-host.tap" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( tape_modified && libspectrum_tape_current_block( tape ) == block );
  tape_modified = 0;
  assert( !fuse_emulation_unpause() );
  microphone = tape_microphone;
  for( i = 0; i < 5; i++ )
    assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( !tape_is_playing() && tape_microphone == microphone );
  assert( libspectrum_tape_current_block( tape ) == block );
  /* Playback remains a separate action through the existing tape mechanism. */
  assert( !tape_do_play( 0 ) );
  assert( fuse_run_tstates( 100 ) == FUSE_RUN_COMPLETED );
  microphone = tape_microphone;
  assert( fuse_run_tstates( 2200 ) == FUSE_RUN_COMPLETED );
  assert( tape_is_playing() && tape_microphone != microphone );
  /* A failed read must not close or prompt about an existing playing tape. */
  tape_modified = 1;
  assert( fuse_host_insert_tape_file( "missing-host.tap" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( tape_modified && tape_is_playing() );
  assert( libspectrum_tape_current_block( tape ) == block );
  file = fopen( "host-replacement.tap", "wb" ); assert( file );
  assert( fwrite( valid, 1, sizeof(valid), file ) == sizeof(valid) && !fclose(file) );
  /* The selected null UI chooses DON'T SAVE, as it does for the legacy menu. */
  assert( fuse_host_insert_tape_file( "host-replacement.tap" ) == OK );
  assert( tape_present() && !tape_modified && !tape_is_playing() );
  /* Ordinary clean replacement also uses the same existing path. */
  assert( fuse_host_insert_tape_file( "host-replacement.tap" ) == OK );
  tape_record_start(); assert( tape_recording );
  assert( fuse_run_tstates( 100 ) == FUSE_RUN_COMPLETED );
  assert( fuse_host_insert_tape_file( "host-replacement.tap" ) == OK );
  assert( tape_present() && !tape_recording && !tape_modified && !tape_is_playing() );
  assert( !tape_close() && !remove( "host-replacement.tap" ) );
  assert( !tape_present() && !remove( "host-bad.tap" ) );
  assert( !remove( "host-empty.tap" ) );
}

static void
active_callback( libspectrum_dword ts, int type, void *data )
{
  (void)ts; (void)type; (void)data;
  assert( fuse_host_nmi() == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_reset( FUSE_HOST_RESET_SOFT ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_select_machine( "128" ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_keyboard_event( INPUT_KEY_a, 1 ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_insert_tape_file( "not-opened.tap" ) == FUSE_HOST_OPERATION_BUSY );
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
  assert( fuse_host_insert_tape_file( "not-opened.tap" ) == FUSE_HOST_OPERATION_NOT_READY );
  config = fuse_host_config_alloc();
  assert( !fuse_host_config_set_sound( config, 0 ) );
  assert( fuse_host_config_set_joystick( config, 3, 0 ) );
  assert( fuse_host_config_set_joystick( config, 0, 8 ) );
  assert( !fuse_host_config_set_joystick( config, FUSE_HOST_JOYSTICK_1, FUSE_HOST_JOYSTICK_OUTPUT_KEMPSTON ) );
  assert( !fuse_host_config_set_joystick( config, FUSE_HOST_JOYSTICK_2, FUSE_HOST_JOYSTICK_OUTPUT_FULLER ) );
  assert( fuse_host_config_set_mouse_swap_buttons( config, 2 ) );
  assert( !fuse_host_config_set_mouse_swap_buttons( config, 1 ) );
  assert( fuse_host_prepare( config, &resources ) == FUSE_INIT_READY );
  fuse_host_config_free( config );
  assert( settings_current.joystick_1_output == JOYSTICK_TYPE_KEMPSTON );
  assert( settings_current.joystick_2_output == JOYSTICK_TYPE_FULLER );
  assert( settings_current.mouse_swap_buttons == 1 );
  if( !strcmp( argv[1], "tape-contract" ) ) {
    check_tape_workflow();
  } else if( strcmp( argv[1], "success" ) ) {
    if( !strncmp( argv[1], "peripheral-", 11 ) ) {
      settings_current.beta128 = 1;
      (void)periph_update();
      provider.fail_name = "trdos.rom"; /* Main model ROMs must still load. */
    } else provider.fail = 1;
    if( !strcmp( argv[1], "reset-failure" ) ||
        !strcmp( argv[1], "peripheral-reset-failure" ) )
      assert( fuse_host_reset( FUSE_HOST_RESET_HARD ) == FUSE_HOST_OPERATION_FAILED );
    else assert( fuse_host_select_machine( "128" ) == FUSE_HOST_OPERATION_FAILED );
    assert( provider.rejected == 1 );
    assert( fuse_run_frame() == FUSE_RUN_NOT_READY );
    assert( fuse_run_tstates( 1 ) == FUSE_RUN_NOT_READY );
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
    assert( fuse_run_frame() == FUSE_RUN_PAUSED );
    assert( fuse_host_release_all() == OK ); check_released();
    assert( fuse_host_select_machine( "48" ) == OK );
    assert( !fuse_emulation_unpause() && !fuse_emulation_unpause() );
    callback = event_register( active_callback, "Host operation reentry" );
    assert( callback >= 0 ); event_add( tstates, callback );
    assert( fuse_run_tstates( 1 ) == FUSE_RUN_COMPLETED );
    event_reset(); tstates = 0; z80.pc.w = 0x8000; z80.sp.w = 0x9000;
    writebyte_internal( 0x8000, 0 );
    assert( fuse_host_nmi() == OK );
    assert( z80.pc.w == 0x8000 && z80.sp.w == 0x9000 && tstates == 0 );
    event_do_events(); assert( z80.pc.w == 0x66 && z80.sp.w == 0x8ffe );
    fuse_exiting = 1;
    assert( fuse_host_keyboard_event( INPUT_KEY_a, 1 ) == FUSE_HOST_OPERATION_EXITING );
    assert( fuse_host_insert_tape_file( "not-opened.tap" ) == FUSE_HOST_OPERATION_EXITING );
  }
  assert( provider.acquired == provider.released );
  fuse_shutdown(); fuse_shutdown();
  assert( fuse_host_reset( FUSE_HOST_RESET_SOFT ) == FUSE_HOST_OPERATION_NOT_READY );
  assert( !fuse_host_resources_active() );
  assert( fuse_host_insert_tape_file( "not-opened.tap" ) == FUSE_HOST_OPERATION_NOT_READY );
  return 0;
}

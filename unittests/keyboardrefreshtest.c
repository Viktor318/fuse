/* Real runtime adapter + policy with fake host time and frontend. */
#include "config.h"

/* Test checks must execute even when the emulator is built with NDEBUG. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "debugger/debugger.h"
#include "fuse.h"
#include "keyboard_refresh.h"
#include "spectrum.h"

int fuse_emulation_paused, ui_widget_level = -1, rzx_playback, rzx_recording;
enum debugger_mode_t debugger_mode;
static long long now;
static unsigned long frame;
static int automation, polls, clocks, invalidate, recurse;
static input_refresh_result poll_result = INPUT_REFRESH_SUCCESS;
long long compat_monotonic_time_us( void ) { clocks++; return now; }
libspectrum_dword spectrum_get_frame_count( void ) { return frame; }
int automation_active( void ) { return automation; }
static input_refresh_result poll_input( void *context )
{
  assert( context == &polls ); polls++;
  if( recurse ) keyboard_refresh_before_read( 0xfe );
  if( invalidate ) keyboard_refresh_invalidate();
  return poll_result;
}
int main( void )
{
  keyboard_refresh_before_read( 0xfe );
  assert( !polls && !clocks && !keyboard_refresh_enabled );
  keyboard_refresh_set_frontend( poll_input, &polls );
  keyboard_refresh_before_read( 0xff );
  assert( !polls && !clocks );
  keyboard_refresh_before_read( 0xfe );
  assert( polls == 1 );
  keyboard_refresh_before_read( 0xfe ); assert( polls == 1 );
  now = 4999; keyboard_refresh_before_read( 0xfe ); assert( polls == 1 );
  now = 5000; keyboard_refresh_before_read( 0xfe ); assert( polls == 2 );
  frame++; keyboard_refresh_before_read( 0xfe ); assert( polls == 3 );

  fuse_emulation_paused = 1; frame++;
  keyboard_refresh_before_read( 0xfe ); assert( polls == 3 );
  fuse_emulation_paused = 0;
  keyboard_refresh_before_read( 0xfe ); assert( polls == 4 );
  ui_widget_level = 0; keyboard_refresh_before_read( 0xfe ); assert( polls == 4 );
  ui_widget_level = -1; keyboard_refresh_before_read( 0xfe ); assert( polls == 5 );
  debugger_mode = DEBUGGER_MODE_HALTED;
  keyboard_refresh_before_read( 0xfe ); assert( polls == 5 );
  debugger_mode = DEBUGGER_MODE_ACTIVE;
  keyboard_refresh_before_read( 0xfe ); assert( polls == 5 );
  debugger_mode = DEBUGGER_MODE_INACTIVE;
  keyboard_refresh_before_read( 0xfe ); assert( polls == 6 );
  rzx_playback = 1; keyboard_refresh_before_read( 0xfe ); assert( polls == 6 );
  rzx_playback = 0; keyboard_refresh_before_read( 0xfe ); assert( polls == 7 );
  automation = 1; keyboard_refresh_before_read( 0xfe ); assert( polls == 7 );
  automation = 0; rzx_recording = 1;
  keyboard_refresh_before_read( 0xfe ); assert( polls == 8 );
  /* Lifecycle hook: no excluded port read was needed to invalidate freshness. */
  keyboard_refresh_invalidate();
  keyboard_refresh_before_read( 0xfe ); assert( polls == 9 );
  recurse = 1; keyboard_refresh_invalidate();
  keyboard_refresh_before_read( 0xfe ); assert( polls == 10 ); recurse = 0;
  invalidate = 1;
  keyboard_refresh_invalidate(); keyboard_refresh_before_read( 0xfe );
  assert( polls == 11 );
  invalidate = 0; poll_result = INPUT_REFRESH_FAILED;
  keyboard_refresh_before_read( 0xfe ); assert( polls == 12 );
  poll_result = INPUT_REFRESH_SUCCESS;
  keyboard_refresh_before_read( 0xfe ); assert( polls == 13 );
  /* Invalidated/failed collection must retry, but success restores throttling. */
  keyboard_refresh_before_read( 0xfe ); assert( polls == 13 );
  now += 4999; keyboard_refresh_before_read( 0xfe ); assert( polls == 13 );
  now++; keyboard_refresh_before_read( 0xfe ); assert( polls == 14 );
  keyboard_refresh_set_frontend( NULL, NULL );
  keyboard_refresh_before_read( 0xfe ); assert( polls == 14 );
  puts( "keyboard runtime refresh tests passed" );
  return 0;
}

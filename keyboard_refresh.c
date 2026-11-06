/* keyboard_refresh.c: live-execution adapter for read-time input refresh
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version. */
#include "config.h"
#include "compat.h"
#include "debugger/debugger.h"
#include "fuse.h"
#include "keyboard_refresh.h"
#include "rzx.h"
#include "spectrum.h"
#include "ui/ui.h"
#ifdef ENABLE_AUTOMATION
#include "automation/automation.h"
#endif

int keyboard_refresh_enabled;
static input_refresh_policy policy;
static int initialized, was_live;

static long long
host_clock( void *context GCC_UNUSED )
{
  return compat_monotonic_time_us();
}

void
keyboard_refresh_set_frontend( input_refresh_poll_fn poll, void *context )
{
  if( !initialized ) {
    input_refresh_init( &policy, host_clock, NULL );
    initialized = 1;
  }
  input_refresh_set_frontend( &policy, poll, context );
  keyboard_refresh_enabled = poll != NULL;
  was_live = 0;
}

void
keyboard_refresh_invalidate( void )
{
  if( initialized ) input_refresh_invalidate( &policy );
  was_live = 0;
}

void
keyboard_refresh_before_read( unsigned char porth )
{
  int live;

  if( !keyboard_refresh_enabled || porth == 0xff ) return;
  live = !fuse_emulation_paused && ui_widget_level < 0 && !rzx_playback &&
         debugger_mode == DEBUGGER_MODE_INACTIVE;
#ifdef ENABLE_AUTOMATION
  live = live && !automation_active();
#endif
  if( !live ) {
    if( was_live ) keyboard_refresh_invalidate();
    return;
  }
  if( !was_live ) input_refresh_invalidate( &policy );
  was_live = 1;
  input_refresh_maybe( &policy, spectrum_get_frame_count(), 1, 1 );
}

/* input_refresh.h: shared, optional read-time input refresh policy
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version. */

#ifndef FUSE_INPUT_REFRESH_H
#define FUSE_INPUT_REFRESH_H

#define INPUT_REFRESH_INTERVAL_US 5000

typedef enum input_refresh_result {
  INPUT_REFRESH_SKIPPED,
  INPUT_REFRESH_SUCCESS,
  INPUT_REFRESH_UNSUPPORTED,
  INPUT_REFRESH_FAILED
} input_refresh_result;

/* The clock returns monotonic host microseconds, or a negative value on error.
   A successful frontend poll may contain no events. SKIPPED can indicate an
   ordering barrier; neither it, UNSUPPORTED nor FAILED establishes freshness. */
typedef long long (*input_refresh_clock_fn)( void *context );
typedef input_refresh_result (*input_refresh_poll_fn)( void *context );

typedef struct input_refresh_policy {
  input_refresh_clock_fn clock;
  void *clock_context;
  input_refresh_poll_fn poll;
  void *poll_context;
  int valid, refreshing;
  unsigned long last_frame, generation;
  long long completed_us;
} input_refresh_policy;

/* Single UI/emulation thread only; the guard prevents recursion, not races.
   Initialize once, outside any active refresh. Fields are otherwise internal. */
void input_refresh_init( input_refresh_policy *policy,
                         input_refresh_clock_fn clock, void *clock_context );
/* NULL poll means unsupported. Changing/reinitializing a frontend invalidates
   freshness, including when called recursively from an active poll. */
void input_refresh_set_frontend( input_refresh_policy *policy,
                                 input_refresh_poll_fn poll, void *context );
/* Call after reset, actual resume, or returning from excluded execution modes.
   This does not release the reentrancy guard of an active poll. */
void input_refresh_invalidate( input_refresh_policy *policy );
/* live must exclude playback, automation, pause, widgets and debugger handling.
   rows_selected is false for a keyboard read with high port byte 0xff.
   Only read-time refresh calls belong here: normal frame dispatch must not
   publish freshness to this policy. A success invalidated in flight is SKIPPED. */
input_refresh_result input_refresh_maybe( input_refresh_policy *policy,
                                         unsigned long frame,
                                         int rows_selected, int live );

#endif

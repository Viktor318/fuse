/* input_refresh.c: shared, optional read-time input refresh policy
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version. */

#include "config.h"

#include <string.h>

#include "input_refresh.h"

void
input_refresh_init( input_refresh_policy *policy,
                    input_refresh_clock_fn clock, void *clock_context )
{
  memset( policy, 0, sizeof( *policy ) );
  policy->clock = clock;
  policy->clock_context = clock_context;
}

void
input_refresh_invalidate( input_refresh_policy *policy )
{
  policy->valid = 0;
  policy->last_frame = 0;
  policy->completed_us = 0;
  policy->generation++;
}

void
input_refresh_set_frontend( input_refresh_policy *policy,
                            input_refresh_poll_fn poll, void *context )
{
  policy->poll = poll;
  policy->poll_context = context;
  input_refresh_invalidate( policy );
}

input_refresh_result
input_refresh_maybe( input_refresh_policy *policy, unsigned long frame,
                     int rows_selected, int live )
{
  input_refresh_result result;
  unsigned long generation;
  long long now, completed;

  if( !rows_selected || !live || policy->refreshing ) return INPUT_REFRESH_SKIPPED;
  if( !policy->poll ) return INPUT_REFRESH_UNSUPPORTED;
  if( !policy->clock ) return INPUT_REFRESH_FAILED;

  /* Guard the clock callbacks too, so test/platform adapters cannot recurse. */
  policy->refreshing = 1;
  generation = policy->generation;
  now = policy->clock( policy->clock_context );
  if( generation != policy->generation ) {
    result = INPUT_REFRESH_SKIPPED;
  } else if( now < 0 ) {
    result = INPUT_REFRESH_FAILED;
  } else if( policy->valid && policy->last_frame == frame &&
             now >= policy->completed_us &&
             now - policy->completed_us < INPUT_REFRESH_INTERVAL_US ) {
    result = INPUT_REFRESH_SKIPPED;
  } else {
    result = policy->poll( policy->poll_context );
    if( generation != policy->generation ) {
      result = INPUT_REFRESH_SKIPPED;
    } else if( result == INPUT_REFRESH_SUCCESS ) {
      /* Timestamp completion, never a port read or the start of collection. */
      completed = policy->clock( policy->clock_context );
      if( generation != policy->generation ) {
        result = INPUT_REFRESH_SKIPPED;
      } else if( completed < now ) {
        result = INPUT_REFRESH_FAILED;
      } else {
        policy->last_frame = frame;
        policy->completed_us = completed;
        policy->valid = 1;
      }
    }
  }

  policy->refreshing = 0;
  return result;
}

/* Ordered SDL2 event collection shared by normal and read-time dispatch. */
#ifndef FUSE_SDL2_EVENTS_H
#define FUSE_SDL2_EVENTS_H

#include <SDL.h>
#include "input.h"
#include "input_refresh.h"

typedef input_dispatch_result (*sdl2_event_dispatch_fn)(
  SDL_Event *event, input_dispatch_context context, int focus_cancelled,
  void *data );

typedef struct sdl2_event_collector {
  SDL_Event pending;
  int has_pending, focus_cancelled, focused, normal_depth, reading;
  sdl2_event_dispatch_fn dispatch;
  void (*cancel_focus)( void *data );
  void *data;
} sdl2_event_collector;

void sdl2_events_init( sdl2_event_collector *collector,
                       sdl2_event_dispatch_fn dispatch,
                       void (*cancel_focus)( void *data ), void *data );
void sdl2_events_end( sdl2_event_collector *collector );
input_refresh_result sdl2_events_collect( sdl2_event_collector *collector,
                                         input_dispatch_context context );

#endif

/* sdl2_events.c: ordered SDL2 collection, without nested UI dispatch in reads
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version. */
#include "config.h"
#include <string.h>
#include "sdl2_events.h"

#define READ_EVENT_LIMIT 64

static void
free_payload( SDL_Event *event )
{
  if( event->type == SDL_DROPFILE
#if SDL_VERSION_ATLEAST(2, 0, 5)
      || event->type == SDL_DROPTEXT
#endif
    ) SDL_free( event->drop.file );
#if SDL_VERSION_ATLEAST(2, 0, 22)
  if( event->type == SDL_TEXTEDITING_EXT ) SDL_free( event->editExt.text );
#endif
}

void
sdl2_events_init( sdl2_event_collector *collector,
                  sdl2_event_dispatch_fn dispatch,
                  void (*cancel_focus)( void *data ), void *data )
{
  memset( collector, 0, sizeof( *collector ) );
  collector->dispatch = dispatch;
  collector->cancel_focus = cancel_focus;
  collector->data = data;
  collector->focused = 1;
}

void
sdl2_events_end( sdl2_event_collector *collector )
{
  if( collector->has_pending ) free_payload( &collector->pending );
  collector->has_pending = 0;
}

static void
normal_event( sdl2_event_collector *collector, SDL_Event *event, int cancelled )
{
  if( event->type == SDL_WINDOWEVENT ) {
    if( event->window.event == SDL_WINDOWEVENT_FOCUS_LOST ) collector->focused = 0;
    if( event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED ) collector->focused = 1;
  }
  collector->dispatch( event, INPUT_DISPATCH_NORMAL, cancelled, collector->data );
  free_payload( event );
}

input_refresh_result
sdl2_events_collect( sdl2_event_collector *collector, input_dispatch_context context )
{
  SDL_Event event;
  input_refresh_result result = INPUT_REFRESH_SUCCESS;
  int count = 0, available;

  if( context == INPUT_DISPATCH_READ_TIME ) {
    if( collector->reading || collector->normal_depth || collector->has_pending ||
        !collector->focused || input_dispatch_pending() ) return INPUT_REFRESH_SKIPPED;
    collector->reading = 1;
  } else if( context == INPUT_DISPATCH_NORMAL ) {
    if( collector->reading ) return INPUT_REFRESH_SKIPPED;
    collector->normal_depth++;
    /* Before dequeuing anything: a deferred action can nest normal collection. */
    input_dispatch_deferred();
    if( collector->has_pending ) {
      int cancelled = collector->focus_cancelled;
      event = collector->pending;
      collector->has_pending = 0;
      normal_event( collector, &event, cancelled );
    }
  } else {
    return INPUT_REFRESH_FAILED;
  }

  /* Pump once, then consume the entire queue in order, never extracting keys
     from behind a focus/window/mouse/quit/controller event. Peep reports errors. */
  SDL_PumpEvents();
  while( ( available = SDL_PeepEvents( &event, 1, SDL_GETEVENT,
                                      SDL_FIRSTEVENT, SDL_LASTEVENT ) ) > 0 ) {
    if( context == INPUT_DISPATCH_NORMAL ) {
      normal_event( collector, &event, 0 );
      continue;
    }
    if( event.type == SDL_KEYDOWN || event.type == SDL_KEYUP ) {
      input_dispatch_result dispatched = collector->dispatch(
        &event, context, 0, collector->data );
      if( dispatched == INPUT_DISPATCH_DEFERRED ) {
        result = INPUT_REFRESH_SKIPPED; /* Consumed, only the UI action remains. */
        break;
      }
      if( dispatched == INPUT_DISPATCH_BLOCKED || dispatched == INPUT_DISPATCH_ERROR ) {
        collector->pending = event; collector->has_pending = 1;
        collector->focus_cancelled = 0;
        result = INPUT_REFRESH_SKIPPED; /* Untouched: normal dispatch owns it. */
        break;
      }
    } else {
      collector->focus_cancelled = 0;
      if( event.type == SDL_WINDOWEVENT &&
          event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ) {
        collector->focused = 0;
        collector->cancel_focus( collector->data );
        collector->focus_cancelled = 1;
      }
      /* All other effects, including physical controllers, initially fall back
         to normal dispatch. No raw event or already-applied key is replayed. */
      collector->pending = event; collector->has_pending = 1;
      result = INPUT_REFRESH_SKIPPED;
      break;
    }
    if( ++count == READ_EVENT_LIMIT ) {
      result = INPUT_REFRESH_SKIPPED; /* Bounded work; do not claim a full poll. */
      break;
    }
  }
  if( available < 0 ) result = INPUT_REFRESH_FAILED;
  if( context == INPUT_DISPATCH_READ_TIME ) collector->reading = 0;
  else collector->normal_depth--;
  return result;
}

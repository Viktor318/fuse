/* Runtime keyboard adapter for the shared refresh policy. */
#ifndef FUSE_KEYBOARD_REFRESH_H
#define FUSE_KEYBOARD_REFRESH_H
#include "input_refresh.h"

extern int keyboard_refresh_enabled;
void keyboard_refresh_set_frontend( input_refresh_poll_fn poll, void *context );
void keyboard_refresh_invalidate( void );
void keyboard_refresh_before_read( unsigned char porth );

#endif

/* hungarian_keyboard.h: type with a Hungarian (or any other) host keyboard
   layout, using the character produced by the host
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

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

#ifndef FUSE_HUNGARIAN_KEYBOARD_H
#define FUSE_HUNGARIAN_KEYBOARD_H

#include "libspectrum.h"

#include "input.h"

/* Is character based typing active right now? */
int hungarian_keyboard_active( void );

/* Handle a key press which produced `character' (a Unicode code point, 0 if
   none) on the host. Returns non-zero if the event has been fully handled
   and must not go through the normal (positional) keyboard mapping */
int hungarian_keyboard_keypress( const input_event_key_t *event );

/* Handle a key release. Returns non-zero if the key was handled by us */
int hungarian_keyboard_keyrelease( const input_event_key_t *event );

/* Track host modifier keys (Shift, Ctrl, Alt...). Returns non-zero if the
   event must be swallowed because a timed key sequence is running */
int hungarian_keyboard_modifier( const input_event_key_t *event, int press );

/* Is this host modifier key tracked as held? Lets a UI resynchronise if it
   missed a release event */
int hungarian_keyboard_modifier_held( input_key key );

/* Advance timed key sequences; called once per emulated frame */
void hungarian_keyboard_frame( void );

/* Forget all held keys and pending sequences */
void hungarian_keyboard_reset( void );

/* Exposed for unit testing: map a character to a Spectrum key plus the
   shifts it needs. Returns 0 if the character has no Spectrum equivalent */
int hungarian_keyboard_map_character( libspectrum_dword character,
                                      int *key, int *caps, int *symbol,
                                      int *extended );

#endif				/* #ifndef FUSE_HUNGARIAN_KEYBOARD_H */

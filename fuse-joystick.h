/* fuse-joystick.h: emulated joystick interface identities
   Copyright (c) 2001-2016 Russell Marks, Philip Kendall
   Copyright (c) 2003 Darren Salt
   Copyright (c) 2015 Stuart Brady
   Copyright (c) 2026 Fredrick Meunier

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

#ifndef FUSE_JOYSTICK_TYPES_H
#define FUSE_JOYSTICK_TYPES_H

/* Engine interface choices, shared by configuration and emulation. Keep the
   numeric values/order: stored settings and interface name lists use them. */
typedef enum joystick_type_t {
  JOYSTICK_TYPE_NONE = 0,
  JOYSTICK_TYPE_CURSOR,
  JOYSTICK_TYPE_KEMPSTON,
  JOYSTICK_TYPE_SINCLAIR_1,
  JOYSTICK_TYPE_SINCLAIR_2,
  JOYSTICK_TYPE_TIMEX_1,
  JOYSTICK_TYPE_TIMEX_2,
  JOYSTICK_TYPE_FULLER,
} joystick_type_t;

#define JOYSTICK_TYPE_COUNT 8

#endif

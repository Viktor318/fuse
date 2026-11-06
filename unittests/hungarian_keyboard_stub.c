/* hungarian_keyboard_stub.c: character based typing switched off, for the
   tests which link input.c without the rest of Fuse
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

#include "config.h"

#include "compat.h"
#include "hungarian_keyboard.h"

int
hungarian_keyboard_keypress( const input_event_key_t *event GCC_UNUSED )
{
  return 0;
}

int
hungarian_keyboard_keyrelease( const input_event_key_t *event GCC_UNUSED )
{
  return 0;
}

int
hungarian_keyboard_modifier( const input_event_key_t *event GCC_UNUSED,
                             int press GCC_UNUSED )
{
  return 0;
}

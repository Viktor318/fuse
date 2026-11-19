/* fuse-host.c: host resource callback storage and routing
   Copyright (c) 1999-2026 Philip Kendall and others
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

#include "config.h"

#include <string.h>

#include "fuse-engine.h"

static struct {
  int active;
  fuse_host_resources resources;
} host_preparation;

void
fuse_host_begin_resources( const fuse_host_resources *resources )
{
  host_preparation.active = 1;
  host_preparation.resources = *resources;
}

void
fuse_host_end_resources( void )
{
  memset( &host_preparation, 0, sizeof( host_preparation ) );
}

int
fuse_host_resources_active( void )
{
  return host_preparation.active;
}

int
fuse_host_acquire_rom( const char *name, fuse_host_rom *rom )
{
  memset( rom, 0, sizeof( *rom ) );
  return host_preparation.resources.acquire_rom(
    host_preparation.resources.context, name, rom );
}

/* fuse-disk.h: emulated floppy controller identities
   Copyright (c) 2013 Alex Badea
   Copyright (c) 2015-2017 Gergely Szasz
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

#ifndef FUSE_DISK_CONTROLLER_H
#define FUSE_DISK_CONTROLLER_H

/* Hardware identities shared by operations and drive registration,
   not menu/action numbers. Microdrives and hard-disk interfaces are separate. */
typedef enum fuse_disk_controller {
  FUSE_DISK_CONTROLLER_PLUS3, FUSE_DISK_CONTROLLER_BETA, FUSE_DISK_CONTROLLER_PLUSD,
  FUSE_DISK_CONTROLLER_OPUS, FUSE_DISK_CONTROLLER_DISCIPLE, FUSE_DISK_CONTROLLER_DIDAKTIK
} fuse_disk_controller;

#endif

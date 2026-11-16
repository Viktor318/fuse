/* fuse-disk.h: emulated floppy identities and owned observations
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

/* Allocated owned observation, not a drive handle or decision authorization.
   Release the whole object with fuse_disk_drive_info_free(). Strings are
   independent copies; changing this value does not change emulation. Available
   reports controller availability in the current configuration; enabled reports
   configured drive hardware.
   Loaded is authoritative for media presence; flip/WP reflect current drive
   flags, which can persist while empty. Filename is the retained save target,
   or NULL. No menu IDs, callbacks, FDD pointers or serialized image data. */
typedef struct fuse_disk_drive_info {
  fuse_disk_controller controller;
  int slot;
  char *name;
  int available;
  int enabled;
  int loaded;
  int flipped;
  int write_protected;
  char *filename;
} fuse_disk_drive_info;

/* Does not access engine state; accepts NULL and remains valid after shutdown. */
void fuse_disk_drive_info_free( fuse_disk_drive_info *info );

#endif

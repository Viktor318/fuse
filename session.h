/* session.h: save the emulated machine on exit and restore it on start
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

#ifndef FUSE_SESSION_H
#define FUSE_SESSION_H

/* With the resume_last_session setting on, save the machine (and the tape
   in it, with its position) when Fuse exits ... */
int session_save( void );

/* ... and restore them when it starts. Returns 0 if there was nothing to
   restore */
int session_restore( void );

#endif			/* #ifndef FUSE_SESSION_H */

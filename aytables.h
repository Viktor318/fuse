/* aytables.h: The note frequency tables of the AY trackers
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   The tables of ZXTune (https://zxtune.bitbucket.io/), Copyright (c)
   Vitamin/CAIG <vitamin.caig@gmail.com>, GNU LGPL version 3
   (src/module/players/aym/freq_tables.cpp).

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

#ifndef FUSE_AYTABLES_H
#define FUSE_AYTABLES_H

/* The tone periods of the 96 notes (C-1 to B-8) each tracker uses; a
   shorter table is filled with 0 */
#define AYTABLE_NOTES 96

extern const unsigned short aytable_soundtracker[ AYTABLE_NOTES ];
extern const unsigned short aytable_protracker2[ AYTABLE_NOTES ];
extern const unsigned short aytable_protracker3_3[ AYTABLE_NOTES ];
extern const unsigned short aytable_protracker3_4[ AYTABLE_NOTES ];
extern const unsigned short aytable_protracker3_st[ AYTABLE_NOTES ];
extern const unsigned short aytable_protracker3_3_asm[ AYTABLE_NOTES ];
extern const unsigned short aytable_protracker3_4_asm[ AYTABLE_NOTES ];
extern const unsigned short aytable_protracker3_3_real[ AYTABLE_NOTES ];
extern const unsigned short aytable_protracker3_4_real[ AYTABLE_NOTES ];
extern const unsigned short aytable_asm[ AYTABLE_NOTES ];
extern const unsigned short aytable_soundtracker_pro[ AYTABLE_NOTES ];
extern const unsigned short aytable_natural_scaled[ AYTABLE_NOTES ];
extern const unsigned short aytable_prosoundmaker[ AYTABLE_NOTES ];
extern const unsigned short aytable_sqtracker[ AYTABLE_NOTES ];
extern const unsigned short aytable_fuxoft[ AYTABLE_NOTES ];	/* 84 notes */
extern const unsigned short aytable_fasttracker[ AYTABLE_NOTES ];

#endif			/* #ifndef FUSE_AYTABLES_H */

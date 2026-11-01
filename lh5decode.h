/* lh5decode.h: Decompressing LHA -lh5- data
   Copyright (c) 2026 Viktor (GitHub: Viktor318)
   The method as in lhasa (https://fragglet.github.io/lhasa/), Copyright
   (c) 2011, 2012 Simon Howard (ISC license), and the ar002 archiver of
   Haruhiko Okumura.

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

#ifndef FUSE_LH5DECODE_H
#define FUSE_LH5DECODE_H

#include <stddef.h>

/* Decompress `in' (-lh5- or -lh4- data, as in .lzh/.lha archives, YM and
   VTX music files) into exactly `out_length' bytes. *used (if not NULL):
   how many bytes of `in' were read. 0 on success */
int lh5_decode( const unsigned char *in, size_t in_length,
                unsigned char *out, size_t out_length, size_t *used );

/* The CRC-16 of LHA archives (polynomial A001h, starting from 0) */
unsigned int lh5_crc16( const unsigned char *data, size_t length );

#endif			/* #ifndef FUSE_LH5DECODE_H */

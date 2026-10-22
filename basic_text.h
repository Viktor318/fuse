/* basic_text.h: Load and save BASIC programs as plain text
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

#ifndef FUSE_BASIC_TEXT_H
#define FUSE_BASIC_TEXT_H

#include <stddef.h>

#include <libspectrum.h>

/* How many source line numbers with replaced characters are remembered */
#define BASIC_TEXT_MAX_REPORTED_LINES 8

typedef struct basic_text_info {

  size_t lines;            /* Number of BASIC lines produced */
  size_t replaced;         /* Characters replaced by a similar one (é -> e) */
  size_t unknown;          /* Characters with no equivalent, replaced by ? */

  /* BASIC line numbers where something was replaced */
  int replaced_lines[ BASIC_TEXT_MAX_REPORTED_LINES ];
  size_t replaced_line_count;

} basic_text_info;

/* Convert program text (UTF-8, or Windows-1250 if not valid UTF-8) to
   the tokenised form the Spectrum keeps in memory between PROG and VARS.
   `basic128' enables the 128 BASIC keywords PLAY and SPECTRUM.
   Returns 0 on success; on failure, `error' describes the problem */
int basic_text_tokenize( const char *text, size_t length, int basic128,
                         libspectrum_byte **program, size_t *program_length,
                         basic_text_info *info,
                         char *error, size_t error_length );

/* Convert a tokenised program to text. Returns a newly allocated,
   NUL-terminated string which must be freed with libspectrum_free(),
   or NULL if the program is malformed */
char *basic_text_detokenize( const libspectrum_byte *program, size_t length,
                             int basic128, const char *newline );

/* Menu actions */
int basic_text_load( const char *filename );
int basic_text_save( const char *filename );

/* Called once per emulated frame */
void basic_text_frame( void );

#endif			/* #ifndef FUSE_BASIC_TEXT_H */

/* i18n.h: English / Hungarian user interface texts
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

#ifndef FUSE_I18N_H
#define FUSE_I18N_H

#include <stddef.h>

typedef struct i18n_entry {
  const char *english;
  const char *translation;
} i18n_entry;

/* The text to show for `english' in the current language. Texts without a
   translation are shown in English. On Windows the result is in the
   system's ANSI code page, like the rest of the Windows user interface */
const char *i18n( const char *english );

/* Is the user interface in Hungarian? (setting "language": Automatic
   follows the Windows display language) */
int i18n_hungarian( void );

/* Text read from a file that may still be in the system's legacy ANSI
   code page (written by an older Fuse, or by another program), while
   Fuse itself works in UTF-8: the text converted to UTF-8 in a new
   string (free with libspectrum_free()), or NULL if it needs no change */
char *i18n_legacy_to_utf8( const char *text );

/* Called after the language setting may have changed */
void i18n_posthook( void );

/* Set by the user interface to update texts already shown */
extern void ( *i18n_changed_hook )( void );

/* The translation table, sorted by the English text */
extern const i18n_entry i18n_hu_table[];
extern const size_t i18n_hu_table_size;

#endif			/* #ifndef FUSE_I18N_H */

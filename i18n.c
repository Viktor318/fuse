/* i18n.c: English / Hungarian user interface texts
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

/* Only the Windows user interface is translated for now; elsewhere every
   text stays in English */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <windows.h>
#endif

#include "libspectrum.h"

#include "i18n.h"
#include "settings.h"

void ( *i18n_changed_hook )( void ) = NULL;

int
i18n_hungarian( void )
{
#ifdef UI_WIN32
  const char *language = settings_current.language;

  if( language && !strcmp( language, "Magyar" ) ) return 1;
  if( language && !strcmp( language, "English" ) ) return 0;

  /* Automatic */
  return PRIMARYLANGID( GetUserDefaultUILanguage() ) == LANG_HUNGARIAN;
#else
  return 0;
#endif
}

#ifdef UI_WIN32

/* The translations converted to the ANSI code page, made when first used */
static char **ansi_texts = NULL;

static const char*
ansi_text( size_t index )
{
  const char *utf8 = i18n_hu_table[ index ].translation;
  wchar_t *wide;
  int length;

  if( !ansi_texts )
    ansi_texts = calloc( i18n_hu_table_size, sizeof( *ansi_texts ) );
  if( !ansi_texts ) return utf8;
  if( ansi_texts[ index ] ) return ansi_texts[ index ];

  length = MultiByteToWideChar( CP_UTF8, 0, utf8, -1, NULL, 0 );
  wide = malloc( length * sizeof( *wide ) );
  if( !wide ) return utf8;
  MultiByteToWideChar( CP_UTF8, 0, utf8, -1, wide, length );

  /* Letters missing from the code page get the closest one (o for ő) */
  length = WideCharToMultiByte( CP_ACP, 0, wide, -1, NULL, 0, NULL, NULL );
  ansi_texts[ index ] = malloc( length );
  if( ansi_texts[ index ] )
    WideCharToMultiByte( CP_ACP, 0, wide, -1, ansi_texts[ index ], length,
                         NULL, NULL );
  free( wide );

  return ansi_texts[ index ] ? ansi_texts[ index ] : utf8;
}

static int
compare_entry( const void *key, const void *entry )
{
  return strcmp( key, ( (const i18n_entry*)entry )->english );
}

#endif			/* #ifdef UI_WIN32 */

const char*
i18n( const char *english )
{
#ifdef UI_WIN32
  const i18n_entry *entry;

  if( !english || !i18n_hungarian() ) return english;

  entry = bsearch( english, i18n_hu_table, i18n_hu_table_size,
                   sizeof( *i18n_hu_table ), compare_entry );
  if( !entry ) return english;

  return ansi_text( entry - i18n_hu_table );
#else
  return english;
#endif
}

char*
i18n_legacy_to_utf8( const char *text )
{
#ifdef WIN32
  UINT legacy = 0;
  wchar_t *wide;
  char *utf8;
  int length;

  /* Only when the process code page is UTF-8 (see fuse.manifest); with
     an ANSI code page the text is already in the one in use */
  if( !text || GetACP() != CP_UTF8 ) return NULL;

  /* Valid UTF-8 (this includes plain ASCII) needs no change */
  if( MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
                           NULL, 0 ) )
    return NULL;

  if( !GetLocaleInfoW( LOCALE_SYSTEM_DEFAULT,
                       LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                       (LPWSTR)&legacy, sizeof( legacy ) / sizeof( WCHAR ) )
      || !legacy || legacy == CP_UTF8 )
    legacy = 1252;

  length = MultiByteToWideChar( legacy, 0, text, -1, NULL, 0 );
  if( !length ) return NULL;
  wide = malloc( length * sizeof( *wide ) );
  if( !wide ) return NULL;
  MultiByteToWideChar( legacy, 0, text, -1, wide, length );

  length = WideCharToMultiByte( CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL );
  utf8 = length ? libspectrum_new( char, length ) : NULL;
  if( utf8 )
    WideCharToMultiByte( CP_UTF8, 0, wide, -1, utf8, length, NULL, NULL );
  free( wide );

  return utf8;
#else
  return NULL;
#endif
}

void
i18n_posthook( void )
{
  if( i18n_changed_hook ) i18n_changed_hook();
}

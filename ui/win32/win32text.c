/* win32text.c: Texts to and from Windows as Unicode
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

#include <stdlib.h>
#include <string.h>

#include "win32text.h"

/* Length in bytes of a list of texts each ending in \0, with the \0 that
   ends the list */
static size_t
list_length( const char *list )
{
  const char *p = list;

  while( *p ) p += strlen( p ) + 1;
  return p - list + 1;
}

static size_t
list_length_wide( const WCHAR *list )
{
  const WCHAR *p = list;

  while( *p ) p += lstrlenW( p ) + 1;
  return p - list + 1;
}

/* `length' bytes (or -1: up to the \0) as Unicode */
static WCHAR*
wide_n( const char *text, int length )
{
  WCHAR *wide;
  int count;

  if( !text ) return NULL;

  count = MultiByteToWideChar( CP_ACP, 0, text, length, NULL, 0 );
  if( count <= 0 ) return NULL;
  wide = malloc( count * sizeof( *wide ) );
  if( !wide ) return NULL;
  MultiByteToWideChar( CP_ACP, 0, text, length, wide, count );

  return wide;
}

WCHAR*
win32text_wide( const char *text )
{
  return wide_n( text, -1 );
}

char*
win32text_narrow( const WCHAR *text )
{
  char *narrow;
  int count;

  if( !text ) return NULL;

  count = WideCharToMultiByte( CP_ACP, 0, text, -1, NULL, 0, NULL, NULL );
  if( count <= 0 ) return NULL;
  narrow = malloc( count );
  if( !narrow ) return NULL;
  WideCharToMultiByte( CP_ACP, 0, text, -1, narrow, count, NULL, NULL );

  return narrow;
}

BOOL
win32text_file_dialog( OPENFILENAMEA *dialog, int save )
{
  OPENFILENAMEW wide;
  WCHAR *file;
  BOOL result = FALSE;
  int count;

  if( !dialog->lpstrFile || !dialog->nMaxFile ) return FALSE;

  file = calloc( dialog->nMaxFile, sizeof( *file ) );
  if( !file ) return FALSE;
  if( dialog->lpstrFile[0] )
    MultiByteToWideChar( CP_ACP, 0, dialog->lpstrFile, -1, file,
                         dialog->nMaxFile - 1 );

  memset( &wide, 0, sizeof( wide ) );
  wide.lStructSize = sizeof( wide );
  wide.hwndOwner = dialog->hwndOwner;
  wide.hInstance = dialog->hInstance;
  wide.lpstrFilter = dialog->lpstrFilter ?
    wide_n( dialog->lpstrFilter, list_length( dialog->lpstrFilter ) ) : NULL;
  wide.nFilterIndex = dialog->nFilterIndex;
  wide.lpstrFile = file;
  wide.nMaxFile = dialog->nMaxFile;
  wide.lpstrInitialDir = win32text_wide( dialog->lpstrInitialDir );
  wide.lpstrTitle = win32text_wide( dialog->lpstrTitle );
  wide.Flags = dialog->Flags;
  wide.lpstrDefExt = win32text_wide( dialog->lpstrDefExt );

  if( save ? GetSaveFileNameW( &wide ) : GetOpenFileNameW( &wide ) ) {
    /* Several names (folder\0name\0name\0\0), or one */
    int length = ( dialog->Flags & OFN_ALLOWMULTISELECT ) ?
                 (int)list_length_wide( file ) : -1;

    count = WideCharToMultiByte( CP_ACP, 0, file, length, dialog->lpstrFile,
                                 dialog->nMaxFile, NULL, NULL );
    if( count > 0 ) {
      result = TRUE;
      dialog->nFilterIndex = wide.nFilterIndex;
    }
  } else {
    dialog->nFilterIndex = wide.nFilterIndex;
  }

  free( (void*)wide.lpstrFilter );
  free( (void*)wide.lpstrInitialDir );
  free( (void*)wide.lpstrTitle );
  free( (void*)wide.lpstrDefExt );
  free( file );

  return result;
}

int
win32text_browse_folder( HWND owner, const char *title, UINT flags,
                         char *path, size_t length )
{
  BROWSEINFOW browse;
  LPITEMIDLIST folder;
  WCHAR chosen[ MAX_PATH ];
  WCHAR *wide_title = win32text_wide( title );
  int found = 0;

  memset( &browse, 0, sizeof( browse ) );
  browse.hwndOwner = owner;
  browse.lpszTitle = wide_title;
  browse.ulFlags = flags;

  folder = SHBrowseForFolderW( &browse );
  if( folder ) {
    if( SHGetPathFromIDListW( folder, chosen ) &&
        WideCharToMultiByte( CP_ACP, 0, chosen, -1, path, (int)length,
                             NULL, NULL ) > 0 )
      found = 1;
    CoTaskMemFree( folder );
  }
  free( wide_title );

  return found;
}

UINT
win32text_drag_query_file( HDROP drop, UINT index, char *buffer,
                           UINT length )
{
  WCHAR *name;
  UINT count;
  int result;

  count = DragQueryFileW( drop, index, NULL, 0 );
  if( !count ) return 0;
  name = malloc( ( count + 1 ) * sizeof( *name ) );
  if( !name ) return 0;
  DragQueryFileW( drop, index, name, count + 1 );

  result = WideCharToMultiByte( CP_ACP, 0, name, -1, buffer, length, NULL,
                                NULL );
  free( name );

  return result > 0 ? (UINT)( result - 1 ) : 0;
}

int
win32text_listview_insert_column( HWND list, int index,
                                  const LVCOLUMNA *column )
{
  LVCOLUMNW wide;
  int result;

  memcpy( &wide, column, sizeof( wide ) );
  wide.pszText = ( column->mask & LVCF_TEXT ) ?
                 win32text_wide( column->pszText ) : NULL;

  result = (int)SendMessageW( list, LVM_INSERTCOLUMNW, index,
                              (LPARAM)&wide );
  free( wide.pszText );

  return result;
}

LRESULT
win32text_listview_item( HWND list, UINT message, WPARAM wparam,
                         const LVITEMA *item )
{
  LVITEMW wide;
  LRESULT result;
  UINT wide_message;

  switch( message ) {
  case LVM_INSERTITEMA: wide_message = LVM_INSERTITEMW; break;
  case LVM_SETITEMA: wide_message = LVM_SETITEMW; break;
  case LVM_SETITEMTEXTA: wide_message = LVM_SETITEMTEXTW; break;
  default: return SendMessageA( list, message, wparam, (LPARAM)item );
  }

  memcpy( &wide, item, sizeof( wide ) );
  wide.pszText = NULL;
  if( ( message == LVM_SETITEMTEXTA || ( item->mask & LVIF_TEXT ) ) &&
      item->pszText && item->pszText != LPSTR_TEXTCALLBACKA )
    wide.pszText = win32text_wide( item->pszText );
  else if( item->pszText == LPSTR_TEXTCALLBACKA )
    wide.pszText = LPSTR_TEXTCALLBACKW;

  result = SendMessageW( list, wide_message, wparam, (LPARAM)&wide );
  if( wide.pszText != LPSTR_TEXTCALLBACKW ) free( wide.pszText );

  return result;
}

static const char*
special( WCHAR letter, char *buffer, size_t size )
{
  WCHAR wide[2] = { letter, 0 };

  if( !buffer[0] &&
      WideCharToMultiByte( CP_ACP, 0, wide, -1, buffer, (int)size, NULL,
                           NULL ) <= 0 )
    buffer[0] = '\0';
  return buffer[0] ? buffer : "-";
}

const char*
win32text_dash( void )
{
  static char dash[8];
  return special( 0x2013, dash, sizeof( dash ) );
}

const char*
win32text_guillemet( void )
{
  static char guillemet[8];
  return special( 0x00bb, guillemet, sizeof( guillemet ) );
}

LRESULT
win32text_statusbar_text( HWND status, WPARAM part, const char *text )
{
  WCHAR *wide;
  LRESULT result;

  /* Owner drawn parts carry data, not text */
  if( part & SBT_OWNERDRAW )
    return SendMessageA( status, SB_SETTEXTA, part, (LPARAM)text );

  wide = win32text_wide( text );
  result = SendMessageW( status, SB_SETTEXTW, part,
                         (LPARAM)( wide ? wide : L"" ) );
  free( wide );

  return result;
}

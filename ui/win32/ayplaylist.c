/* ayplaylist.c: Win32 playlist window of the AY player
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

#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include "ayplayer.h"
#include "../../ayplayer.h"
#include "../../ayplaylist.h"
#include "i18n.h"
#include "win32internals.h"
#include "win32text.h"
#include "winstate.h"

HWND fuse_hAYListWnd = NULL;

/* Columns of the list */
enum { COLUMN_NUMBER, COLUMN_TITLE, COLUMN_AUTHOR, COLUMN_LENGTH };

/* The buttons keep their size and their distance from the right edge;
   the list and the line under it follow the size of the window */
#define BUTTONS 11
static const int button_ids[ BUTTONS ] = {
  IDC_AYL_ADD, IDC_AYL_ADD_FOLDER, IDC_AYL_REMOVE, IDC_AYL_CLEAR, IDC_AYL_UP,
  IDC_AYL_DOWN, IDC_AYL_LOAD, IDC_AYL_SAVE, IDC_AYL_MEASURE, IDCLOSE, 0
};
static int button_top[ BUTTONS ], buttons_width;
static int edge_margin, buttons_gap, list_bottom_gap, status_bottom_gap;
static POINT list_corner;
static int number_width, author_width, length_width;
static SIZE minimum_size, default_size;
static HFONT bold_font = NULL;

static void update_status( void );

static HWND
item( int id )
{
  return GetDlgItem( fuse_hAYListWnd, id );
}

/* ------------------------------------------------------------------------
   The list
   ------------------------------------------------------------------------ */

static void
format_length( char *text, size_t size, int index )
{
  long frames;

  if( !ayplaylist_length( index, &frames ) ) {
    _snprintf( text, size, "%s", "\x96" );
  } else {
    if( !frames ) frames = 5 * 60 * 50;	/* the default length */
    _snprintf( text, size, "%ld:%02ld", frames / 50 / 60,
               frames / 50 % 60 );
  }
  text[ size - 1 ] = '\0';
}

static void
set_cell( HWND list, int row, int column, const char *text )
{
  LVITEMA cell;
  /* Titles and authors from the music files are often in an old 8-bit
     code page */
  char *utf8 = i18n_legacy_to_utf8( text );

  memset( &cell, 0, sizeof( cell ) );
  cell.mask = LVIF_TEXT;
  cell.iItem = row;
  cell.iSubItem = column;
  cell.pszText = utf8 ? utf8 : (char *)text;
  win32text_listview_item( list, column ? LVM_SETITEMTEXTA : LVM_INSERTITEMA,
                           column ? row : 0, &cell );
  libspectrum_free( utf8 );
}

void
win32_ayplaylist_update_status( void )
{
  if( fuse_hAYListWnd ) update_status();
}

static void
update_status( void )
{
  char text[ 256 ];
  int i, count = ayplaylist_count(), measured = 0;
  long frames;

  for( i = 0; i < count; i++ )
    if( ayplaylist_length( i, &frames ) ) measured++;

  _snprintf( text, sizeof( text ),
             i18n( "%d songs, %d measured - double click: play" ), count,
             measured );
  text[ sizeof( text ) - 1 ] = '\0';
  SetDlgItemTextA( fuse_hAYListWnd, IDC_AYL_STATUS, text );

  /* While measuring the button stops it */
  if( win32_ayplayer_busy() ) {
    SetDlgItemTextA( fuse_hAYListWnd, IDC_AYL_MEASURE,
                     i18n( "&Stop measuring" ) );
    EnableWindow( item( IDC_AYL_MEASURE ),
                  !win32_ayplayer_measure_stopping() );
  } else {
    SetDlgItemTextA( fuse_hAYListWnd, IDC_AYL_MEASURE,
                     i18n( "&Measure lengths" ) );
    EnableWindow( item( IDC_AYL_MEASURE ), count > measured );
  }
}

/* Fill the list again (keeping the selection and where it was scrolled) */
void
win32_ayplaylist_update( void )
{
  HWND list;
  char text[ 64 ];
  int i, count, top, *selected;

  if( !fuse_hAYListWnd ) return;
  list = item( IDC_AYL_LIST );
  count = ayplaylist_count();

  top = SendMessage( list, LVM_GETTOPINDEX, 0, 0 );
  selected = calloc( count + 1, sizeof( int ) );
  for( i = 0; i < count && i < ListView_GetItemCount( list ); i++ )
    selected[i] = ListView_GetItemState( list, i, LVIS_SELECTED ) != 0;

  SendMessage( list, WM_SETREDRAW, FALSE, 0 );
  ListView_DeleteAllItems( list );
  for( i = 0; i < count; i++ ) {
    const ayplaylist_entry *e = ayplaylist_get( i );

    _snprintf( text, sizeof( text ), "%s%d",
               i == ayplaylist_current() && ayplayer_current_song() >= 0 ?
                 "\xbb " : "", i + 1 );
    text[ sizeof( text ) - 1 ] = '\0';
    set_cell( list, i, COLUMN_NUMBER, text );
    set_cell( list, i, COLUMN_TITLE, e->title );
    set_cell( list, i, COLUMN_AUTHOR, e->author );
    format_length( text, sizeof( text ), i );
    set_cell( list, i, COLUMN_LENGTH, text );
    if( selected[i] )
      ListView_SetItemState( list, i, LVIS_SELECTED, LVIS_SELECTED );
  }
  free( selected );

  if( count && top > 0 ) {
    if( top >= count ) top = count - 1;
    ListView_EnsureVisible( list, count - 1, FALSE );
    ListView_EnsureVisible( list, top, FALSE );
  }
  /* A new song being played: show it */
  {
    static int shown_current = -1;
    int current = ayplaylist_current();
    if( current != shown_current && current >= 0 && current < count )
      ListView_EnsureVisible( list, current, FALSE );
    shown_current = current;
  }

  SendMessage( list, WM_SETREDRAW, TRUE, 0 );
  InvalidateRect( list, NULL, TRUE );

  update_status();
}

int
win32_ayplaylist_selected( void )
{
  if( !fuse_hAYListWnd ) return -1;
  return ListView_GetNextItem( item( IDC_AYL_LIST ), -1, LVNI_SELECTED );
}

static void
select_only( int index )
{
  HWND list = item( IDC_AYL_LIST );
  int i, count = ListView_GetItemCount( list );

  for( i = 0; i < count; i++ )
    ListView_SetItemState( list, i, i == index ? LVIS_SELECTED | LVIS_FOCUSED
                                               : 0,
                           LVIS_SELECTED | LVIS_FOCUSED );
  if( index >= 0 ) ListView_EnsureVisible( list, index, FALSE );
}

/* ------------------------------------------------------------------------
   Adding and removing
   ------------------------------------------------------------------------ */

static void
add_files( void )
{
  /* Room for many names: the folder, then the file names */
  static char buffer[ 65536 ];
  OPENFILENAMEA ofn;
  char path[ MAX_PATH * 2 ];
  const char *name;

  buffer[0] = '\0';
  memset( &ofn, 0, sizeof( ofn ) );
  ofn.lStructSize = sizeof( ofn );
  ofn.hwndOwner = fuse_hAYListWnd;
  {
    static char filter[ 256 ];
    const char *music = i18n( "AY Music Files (*.ay;*.pt3)" );
    const char *all = i18n( "All Files (*.*)" );
    const char *music_types = "*.ay;*.AY;*.pt3;*.PT3", *all_types = "*.*";
    size_t used = 0;

    /* name, types, name, types, each ending in \0, then one more \0 */
    memcpy( filter + used, music, strlen( music ) + 1 );
    used += strlen( music ) + 1;
    memcpy( filter + used, music_types, strlen( music_types ) + 1 );
    used += strlen( music_types ) + 1;
    memcpy( filter + used, all, strlen( all ) + 1 );
    used += strlen( all ) + 1;
    memcpy( filter + used, all_types, strlen( all_types ) + 1 );
    used += strlen( all_types ) + 1;
    filter[ used ] = '\0';
    ofn.lpstrFilter = filter;
  }
  ofn.lpstrFile = buffer;
  ofn.nMaxFile = sizeof( buffer );
  ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST |
              OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
  ofn.lpstrTitle = i18n( "Fuse - Add AY Files" );

  if( !win32text_file_dialog( &ofn, 0 ) ) return;

  /* One file: its full name; more: the folder, then the names */
  name = buffer + strlen( buffer ) + 1;
  if( !*name ) {
    ayplaylist_add_file( buffer );
  } else {
    for( ; *name; name += strlen( name ) + 1 ) {
      _snprintf( path, sizeof( path ), "%s\\%s", buffer, name );
      path[ sizeof( path ) - 1 ] = '\0';
      ayplaylist_add_file( path );
    }
  }
  win32_ayplaylist_update();
}

static void
add_folder( void )
{
  /* UTF-8 can take up to 3 bytes for a letter of a MAX_PATH long name */
  char path[ MAX_PATH * 3 ];

  if( !win32text_browse_folder( fuse_hAYListWnd,
                                i18n( "The .ay files of this folder are added:" ),
                                BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE,
                                path, sizeof( path ) ) )
    return;
  ayplaylist_add_folder( path );
  win32_ayplaylist_update();
}

static void
remove_selected( void )
{
  HWND list = item( IDC_AYL_LIST );
  int i;

  /* From the end, so the indexes of the others do not change */
  for( i = ListView_GetItemCount( list ) - 1; i >= 0; i-- )
    if( ListView_GetItemState( list, i, LVIS_SELECTED ) )
      ayplaylist_remove( i );
  win32_ayplaylist_update();
}

static void
move_selected( int direction )
{
  int index = win32_ayplaylist_selected();

  if( index < 0 ) return;
  index = ayplaylist_move( index, direction );
  win32_ayplaylist_update();
  select_only( index );
  win32_ayplayer_update();
}

static int
list_file_dialog( char *path, size_t size, int save )
{
  OPENFILENAMEA ofn;

  path[0] = '\0';
  memset( &ofn, 0, sizeof( ofn ) );
  ofn.lStructSize = sizeof( ofn );
  ofn.hwndOwner = fuse_hAYListWnd;
  ofn.lpstrFilter = "Playlists (*.m3u)\0*.m3u;*.M3U\0All Files (*.*)\0*.*\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = size;
  ofn.lpstrDefExt = "m3u";
  ofn.Flags = OFN_HIDEREADONLY | OFN_PATHMUSTEXIST |
              ( save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST );
  ofn.lpstrTitle = i18n( save ? "Fuse - Save Playlist" :
                                "Fuse - Load Playlist" );

  return win32text_file_dialog( &ofn, save );
}

static void
dropped( HDROP drop )
{
  char path[ MAX_PATH * 3 ];
  UINT i, count = DragQueryFileW( drop, 0xffffffff, NULL, 0 );

  for( i = 0; i < count; i++ ) {
    size_t length;
    DWORD attributes;

    if( !win32text_drag_query_file( drop, i, path, sizeof( path ) ) )
      continue;
    attributes = GetFileAttributesA( path );
    length = strlen( path );

    if( attributes != INVALID_FILE_ATTRIBUTES &&
        ( attributes & FILE_ATTRIBUTE_DIRECTORY ) )
      ayplaylist_add_folder( path );
    else if( length > 4 && !_stricmp( path + length - 4, ".m3u" ) )
      ayplaylist_load_m3u( path, 1 );
    else if( ayplayer_is_ay_file( path ) )
      ayplaylist_add_file( path );
  }
  DragFinish( drop );
  win32_ayplaylist_update();
}

/* ------------------------------------------------------------------------
   The window
   ------------------------------------------------------------------------ */

static void
remember_place( void )
{
  RECT rect;

  if( !fuse_hAYListWnd || IsIconic( fuse_hAYListWnd ) ||
      !GetWindowRect( fuse_hAYListWnd, &rect ) )
    return;
  winstate_set( "playlist", "x", rect.left );
  winstate_set( "playlist", "y", rect.top );
  winstate_set( "playlist", "size_x", rect.right - rect.left );
  winstate_set( "playlist", "size_y", rect.bottom - rect.top );

  /* The widths of the columns set by the user (the title takes what is
     left) */
  {
    HWND list = item( IDC_AYL_LIST );
    winstate_set( "playlist", "column_number",
                  ListView_GetColumnWidth( list, COLUMN_NUMBER ) );
    winstate_set( "playlist", "column_author",
                  ListView_GetColumnWidth( list, COLUMN_AUTHOR ) );
    winstate_set( "playlist", "column_length",
                  ListView_GetColumnWidth( list, COLUMN_LENGTH ) );
  }
}

static void
restore_place( void )
{
  long x, y, width, height;
  RECT title;

  /* Earlier versions kept sizes that grew at every start: forget them */
  winstate_remove( "playlist", "width" );
  winstate_remove( "playlist", "height" );
  winstate_remove( "playlist", "cx" );
  winstate_remove( "playlist", "cy" );
  winstate_remove( "playlist", "w" );
  winstate_remove( "playlist", "h" );

  /* The widths of the columns of last time */
  {
    HWND list = item( IDC_AYL_LIST );
    long width;
    if( winstate_get( "playlist", "column_number", &width ) && width > 0 )
      ListView_SetColumnWidth( list, COLUMN_NUMBER, width );
    if( winstate_get( "playlist", "column_author", &width ) && width > 0 )
      ListView_SetColumnWidth( list, COLUMN_AUTHOR, width );
    if( winstate_get( "playlist", "column_length", &width ) && width > 0 )
      ListView_SetColumnWidth( list, COLUMN_LENGTH, width );
  }

  if( !winstate_get( "playlist", "size_x", &width ) ||
      !winstate_get( "playlist", "size_y", &height ) ) {
    width = default_size.cx;
    height = default_size.cy;
  }
  if( width < minimum_size.cx ) width = minimum_size.cx;
  if( height < minimum_size.cy ) height = minimum_size.cy;

  if( !winstate_get( "playlist", "x", &x ) ||
      !winstate_get( "playlist", "y", &y ) ) {
    /* First time: beside the player */
    RECT player;
    if( fuse_hAYWnd && GetWindowRect( fuse_hAYWnd, &player ) )
      SetWindowPos( fuse_hAYListWnd, NULL, player.right + 4, player.top,
                    width, height, SWP_NOZORDER | SWP_NOACTIVATE );
    else
      SetWindowPos( fuse_hAYListWnd, NULL, 0, 0, width, height,
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
    return;
  }

  /* Only if the title bar is on a screen (otherwise just the size) */
  title.left = x; title.top = y; title.right = x + width;
  title.bottom = y + GetSystemMetrics( SM_CYCAPTION );
  if( !MonitorFromRect( &title, MONITOR_DEFAULTTONULL ) ) {
    SetWindowPos( fuse_hAYListWnd, NULL, 0, 0, width, height,
                  SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
    return;
  }

  SetWindowPos( fuse_hAYListWnd, NULL, x, y, width, height,
                SWP_NOZORDER | SWP_NOACTIVATE );
}

static void
layout( void )
{
  RECT client;
  HWND list = item( IDC_AYL_LIST ), status = item( IDC_AYL_STATUS );
  RECT rect;
  int i, list_width, title_width, buttons_x;

  GetClientRect( fuse_hAYListWnd, &client );

  /* The buttons at the right edge, all as wide as the widest */
  buttons_x = client.right - edge_margin - buttons_width;
  for( i = 0; button_ids[i]; i++ ) {
    HWND button = item( button_ids[i] );
    GetWindowRect( button, &rect );
    SetWindowPos( button, NULL, buttons_x, button_top[i], buttons_width,
                  rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE );
  }

  /* The list (and the line under it) up to the buttons */
  list_width = buttons_x - buttons_gap - list_corner.x;
  if( list_width < 50 ) list_width = 50;
  SetWindowPos( list, NULL, list_corner.x, list_corner.y, list_width,
                client.bottom - list_bottom_gap - list_corner.y,
                SWP_NOZORDER | SWP_NOACTIVATE );

  GetWindowRect( status, &rect );
  SetWindowPos( status, NULL, list_corner.x,
                client.bottom - status_bottom_gap, list_width,
                rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE );

  /* The title takes the room left by the other columns */
  title_width = list_width - ListView_GetColumnWidth( list, COLUMN_NUMBER ) -
                ListView_GetColumnWidth( list, COLUMN_AUTHOR ) -
                ListView_GetColumnWidth( list, COLUMN_LENGTH ) -
                GetSystemMetrics( SM_CXVSCROLL ) - 4;
  if( title_width < 60 ) title_width = 60;
  ListView_SetColumnWidth( list, COLUMN_TITLE, title_width );

  InvalidateRect( fuse_hAYListWnd, NULL, TRUE );
}

static void
add_column( HWND list, int index, const char *title, int width, int format )
{
  LVCOLUMNA column;

  memset( &column, 0, sizeof( column ) );
  column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
  column.fmt = format;
  column.cx = width;
  column.pszText = (char *)i18n( title );
  win32text_listview_insert_column( list, index, &column );
}

/* After the texts were translated (which may make the buttons and the
   window wider): the sizes to keep, the smallest and the first size, then
   the place and size of last time */
static void
setup_geometry( void )
{
  HWND dialog = fuse_hAYListWnd, list = item( IDC_AYL_LIST );
  RECT client, rect, window, units = { 7, 6, 250, 150 };
  int i, frame_width, client_width;

  /* Dialog units: margin 7, gap 6, list 250 wide by default */
  MapDialogRect( dialog, &units );
  edge_margin = units.left;
  buttons_gap = units.top;

  GetClientRect( dialog, &client );
  buttons_width = 0;
  for( i = 0; button_ids[i]; i++ ) {
    POINT corner;
    GetWindowRect( item( button_ids[i] ), &rect );
    corner.x = rect.left; corner.y = rect.top;
    ScreenToClient( dialog, &corner );
    button_top[i] = corner.y;
    if( rect.right - rect.left > buttons_width )
      buttons_width = rect.right - rect.left;
  }

  GetWindowRect( list, &rect );
  {
    POINT top_left = { rect.left, rect.top };
    POINT bottom_right = { rect.right, rect.bottom };
    ScreenToClient( dialog, &top_left );
    ScreenToClient( dialog, &bottom_right );
    list_corner = top_left;
    list_bottom_gap = client.bottom - bottom_right.y;
  }
  GetWindowRect( item( IDC_AYL_STATUS ), &rect );
  {
    POINT top_left = { rect.left, rect.top };
    ScreenToClient( dialog, &top_left );
    status_bottom_gap = client.bottom - top_left.y;
  }

  /* The first size: the list 250 dialog units wide; at least 150 pixels */
  GetWindowRect( dialog, &window );
  frame_width = window.right - window.left - client.right;
  client_width = list_corner.x + units.right + buttons_gap + buttons_width +
                 edge_margin;
  default_size.cx = client_width + frame_width;
  default_size.cy = window.bottom - window.top;
  minimum_size.cx = default_size.cx - units.right + 150;
  minimum_size.cy = default_size.cy;

  restore_place();
  layout();
}

static void
init_dialog( HWND dialog )
{
  HWND list;
  RECT units = { 0, 0, 4, 8 };

  fuse_hAYListWnd = dialog;
  list = item( IDC_AYL_LIST );

  /* Column widths in dialog units, as the rest of the window */
  MapDialogRect( dialog, &units );
  number_width = 8 * units.right;
  author_width = 20 * units.right;
  length_width = 9 * units.right;

  ListView_SetExtendedListViewStyle( list, LVS_EX_FULLROWSELECT );
  add_column( list, COLUMN_NUMBER, "#", number_width, LVCFMT_LEFT );
  add_column( list, COLUMN_TITLE, "Title", 100, LVCFMT_LEFT );
  add_column( list, COLUMN_AUTHOR, "Author", author_width, LVCFMT_LEFT );
  add_column( list, COLUMN_LENGTH, "Length", length_width, LVCFMT_RIGHT );

  {
    LOGFONT font;
    HFONT normal = (HFONT)SendMessage( dialog, WM_GETFONT, 0, 0 );
    if( normal && GetObject( normal, sizeof( font ), &font ) ) {
      font.lfWeight = FW_BOLD;
      bold_font = CreateFontIndirect( &font );
    }
  }

  DragAcceptFiles( dialog, TRUE );
}

/* The song being played in bold */
static LRESULT
custom_draw( NMLVCUSTOMDRAW *draw )
{
  switch( draw->nmcd.dwDrawStage ) {
  case CDDS_PREPAINT:
    return CDRF_NOTIFYITEMDRAW;
  case CDDS_ITEMPREPAINT:
    if( (int)draw->nmcd.dwItemSpec == ayplaylist_current() &&
        ayplayer_current_song() >= 0 && bold_font ) {
      SelectObject( draw->nmcd.hdc, bold_font );
      return CDRF_NEWFONT;
    }
    return CDRF_DODEFAULT;
  }
  return CDRF_DODEFAULT;
}

static INT_PTR CALLBACK
playlist_proc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
  char path[ MAX_PATH ];

  switch( msg ) {

  case WM_INITDIALOG:
    init_dialog( hWnd );
    return TRUE;

  case WM_SIZE:
    if( wParam != SIZE_MINIMIZED && fuse_hAYListWnd && minimum_size.cx )
      layout();
    return 0;

  case WM_GETMINMAXINFO:
    if( minimum_size.cx ) {
      MINMAXINFO *info = (MINMAXINFO *)lParam;
      info->ptMinTrackSize.x = minimum_size.cx;
      info->ptMinTrackSize.y = minimum_size.cy;
      return 0;
    }
    break;

  case WM_ENTERSIZEMOVE:
  case WM_ENTERMENULOOP:
    win32ui_modal_enter();
    return 0;

  case WM_EXITSIZEMOVE:
  case WM_EXITMENULOOP:
    win32ui_modal_exit();
    remember_place();
    return 0;

  case WM_DROPFILES:
    dropped( (HDROP)wParam );
    return 0;

  case WM_NOTIFY:
    {
      NMHDR *header = (NMHDR *)lParam;
      if( header->idFrom != IDC_AYL_LIST ) break;
      if( header->code == NM_DBLCLK ) {
        int index = ( (NMITEMACTIVATE *)lParam )->iItem;
        if( index >= 0 ) win32_ayplayer_play_entry( index );
        return TRUE;
      }
      if( header->code == NM_CUSTOMDRAW ) {
        SetWindowLongPtr( hWnd, DWLP_MSGRESULT,
                          custom_draw( (NMLVCUSTOMDRAW *)lParam ) );
        return TRUE;
      }
      if( header->code == LVN_KEYDOWN &&
          ( (NMLVKEYDOWN *)lParam )->wVKey == VK_DELETE ) {
        remove_selected();
        return TRUE;
      }
      if( header->code == NM_RETURN ) {
        int index = win32_ayplaylist_selected();
        if( index >= 0 ) win32_ayplayer_play_entry( index );
        return TRUE;
      }
    }
    break;

  case WM_COMMAND:
    switch( LOWORD( wParam ) ) {
    case IDC_AYL_ADD: add_files(); return TRUE;
    case IDC_AYL_ADD_FOLDER: add_folder(); return TRUE;
    case IDC_AYL_REMOVE: remove_selected(); return TRUE;
    case IDC_AYL_CLEAR:
      ayplaylist_clear();
      win32_ayplaylist_update();
      win32_ayplayer_update();
      return TRUE;
    case IDC_AYL_UP: move_selected( -1 ); return TRUE;
    case IDC_AYL_DOWN: move_selected( 1 ); return TRUE;
    case IDC_AYL_LOAD:
      if( list_file_dialog( path, sizeof( path ), 0 ) ) {
        ayplaylist_load_m3u( path, 0 );
        win32_ayplaylist_update();
        win32_ayplayer_update();
      }
      return TRUE;
    case IDC_AYL_SAVE:
      if( list_file_dialog( path, sizeof( path ), 1 ) )
        ayplaylist_save_m3u( path );
      return TRUE;
    case IDC_AYL_MEASURE:
      if( win32_ayplayer_busy() ) win32_ayplayer_measure_stop();
      else win32_ayplayer_measure_all();
      return TRUE;
    case IDCLOSE:
    case IDCANCEL:
      remember_place();
      ShowWindow( hWnd, SW_HIDE );
      return TRUE;
    }
    break;

  case WM_CLOSE:
    remember_place();
    ShowWindow( hWnd, SW_HIDE );
    return TRUE;
  }

  return FALSE;
}

static int
create( void )
{
  if( fuse_hAYListWnd ) return 0;

  fuse_hAYListWnd = CreateDialog( fuse_hInstance,
                                  MAKEINTRESOURCE( IDD_AYPLAYLIST ),
                                  fuse_hAYWnd ? fuse_hAYWnd : fuse_hWnd,
                                  (DLGPROC)playlist_proc );
  if( !fuse_hAYListWnd ) {
    win32_verror( 1 );
    return 1;
  }
  setup_geometry();
  win32_ayplaylist_update();
  return 0;
}

void
win32_ayplaylist_show( void )
{
  if( create() ) return;
  ShowWindow( fuse_hAYListWnd, SW_SHOWNOACTIVATE );
  win32_ayplaylist_update();
}

void
win32_ayplaylist_toggle( void )
{
  if( fuse_hAYListWnd && IsWindowVisible( fuse_hAYListWnd ) ) {
    remember_place();
    ShowWindow( fuse_hAYListWnd, SW_HIDE );
  } else {
    win32_ayplaylist_show();
  }
}

void
win32_ayplaylist_close( void )
{
  if( !fuse_hAYListWnd ) return;
  remember_place();
  DestroyWindow( fuse_hAYListWnd );
  fuse_hAYListWnd = NULL;
  if( bold_font ) {
    DeleteObject( bold_font );
    bold_font = NULL;
  }
}

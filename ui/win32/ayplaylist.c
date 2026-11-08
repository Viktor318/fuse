/* ayplaylist.c: Win32 playlist windows of the AY and TurboSound players
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
#include <stdlib.h>
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
HWND fuse_hTSListWnd = NULL;

/* Columns of the list */
enum { COLUMN_NUMBER, COLUMN_TITLE, COLUMN_AUTHOR, COLUMN_CHANNELS,
       COLUMN_LENGTH };

/* The buttons keep their size and their distance from the right edge;
   the list and the line under it follow the size of the window */
#define BUTTONS 12
static const int button_ids[ BUTTONS ] = {
  IDC_AYL_ADD, IDC_AYL_ADD_FOLDER, IDC_AYL_REMOVE, IDC_AYL_CLEAR, IDC_AYL_UP,
  IDC_AYL_DOWN, IDC_AYL_LOAD, IDC_AYL_SAVE, IDC_AYL_MEASURE, IDC_AYL_REMEASURE,
  IDCLOSE, 0
};

/* A playlist window: that of the AY player or of the TurboSound player
   (the same window, with the list of its player) */
typedef struct list_window {
  int turbosound;
  int dialog;
  const char *key;		/* of its place in fuse-windows.ini */
  HWND *window;

  int button_top[ BUTTONS ], buttons_width;
  int edge_margin, buttons_gap, list_bottom_gap, status_bottom_gap;
  POINT list_corner;
  int number_width, author_width, channels_width, length_width;

  /* Shown by the user: kept while the player is minimized (and the list
     with it), to be remembered when Fuse exits */
  int list_open;
  SIZE minimum_size, default_size;
  HFONT bold_font;
  /* The song being played, last shown */
  int shown_current;
} list_window;

static list_window lists[ 2 ] = {
  { 0, IDD_AYPLAYLIST, "playlist", &fuse_hAYListWnd },
  { 1, IDD_TSPLAYLIST, "tsplaylist", &fuse_hTSListWnd },
};

static void update_status( list_window *l );

static list_window*
list_window_of( int turbosound )
{
  return &lists[ turbosound ? 1 : 0 ];
}

static HWND
wnd( const list_window *l )
{
  return *l->window;
}

static ayplaylist*
list_of( const list_window *l )
{
  return l->turbosound ? ayplaylist_ts() : ayplaylist_ay();
}

static HWND
item( const list_window *l, int id )
{
  return GetDlgItem( wnd( l ), id );
}

/* ------------------------------------------------------------------------
   The list
   ------------------------------------------------------------------------ */

static void
format_length( list_window *l, char *text, size_t size, int index )
{
  long frames;

  if( !ayplaylist_length( list_of( l ), index, &frames ) ) {
    _snprintf( text, size, "%s", win32text_dash() );
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
win32_ayplaylist_update_status( int turbosound )
{
  list_window *l = list_window_of( turbosound );

  if( wnd( l ) ) update_status( l );
}

static void
update_status( list_window *l )
{
  char text[ 256 ];
  int i, count = ayplaylist_count( list_of( l ) ), measured = 0;
  long frames;

  for( i = 0; i < count; i++ )
    if( ayplaylist_length( list_of( l ), i, &frames ) ) measured++;

  _snprintf( text, sizeof( text ),
             i18n( "%d songs, %d measured - double click: play" ), count,
             measured );
  text[ sizeof( text ) - 1 ] = '\0';
  SetDlgItemTextA( wnd( l ), IDC_AYL_STATUS, text );

  /* While measuring the button stops it */
  if( win32_ayplayer_busy( l->turbosound ) ) {
    SetDlgItemTextA( wnd( l ), IDC_AYL_MEASURE,
                     i18n( "&Stop measuring" ) );
    EnableWindow( item( l, IDC_AYL_MEASURE ),
                  !win32_ayplayer_measure_stopping( l->turbosound ) );
  } else {
    SetDlgItemTextA( wnd( l ), IDC_AYL_MEASURE,
                     i18n( "&Measure lengths" ) );
    EnableWindow( item( l, IDC_AYL_MEASURE ), count > measured );
  }
}

/* Fill the list again (keeping the selection and where it was scrolled) */
void
win32_ayplaylist_update( int turbosound )
{
  list_window *l = list_window_of( turbosound );
  ayplaylist *songs = list_of( l );
  HWND list;
  char text[ 64 ];
  int i, count, top, *selected, playing;

  if( !wnd( l ) ) return;
  list = item( l, IDC_AYL_LIST );
  count = ayplaylist_count( songs );
  playing = ayplaylist_playing( songs );

  top = SendMessage( list, LVM_GETTOPINDEX, 0, 0 );
  selected = calloc( count + 1, sizeof( int ) );
  for( i = 0; i < count && i < ListView_GetItemCount( list ); i++ )
    selected[i] = ListView_GetItemState( list, i, LVIS_SELECTED ) != 0;

  SendMessage( list, WM_SETREDRAW, FALSE, 0 );
  ListView_DeleteAllItems( list );
  for( i = 0; i < count; i++ ) {
    const ayplaylist_entry *e = ayplaylist_get( songs, i );
    int now = playing && i == ayplaylist_current( songs );

    _snprintf( text, sizeof( text ), "%s%s%d",
               now ? win32text_guillemet() : "", now ? " " : "", i + 1 );
    text[ sizeof( text ) - 1 ] = '\0';
    set_cell( list, i, COLUMN_NUMBER, text );
    set_cell( list, i, COLUMN_TITLE, e->title );
    set_cell( list, i, COLUMN_AUTHOR, e->author );
    _snprintf( text, sizeof( text ), "%d",
               ayplaylist_channels( songs, i ) );
    text[ sizeof( text ) - 1 ] = '\0';
    set_cell( list, i, COLUMN_CHANNELS, text );
    format_length( l, text, sizeof( text ), i );
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
    int current = ayplaylist_current( songs );
    if( current != l->shown_current && current >= 0 && current < count )
      ListView_EnsureVisible( list, current, FALSE );
    l->shown_current = current;
  }

  SendMessage( list, WM_SETREDRAW, TRUE, 0 );
  InvalidateRect( list, NULL, TRUE );

  update_status( l );
}

int
win32_ayplaylist_selected( int turbosound )
{
  list_window *l = list_window_of( turbosound );

  if( !wnd( l ) ) return -1;
  return ListView_GetNextItem( item( l, IDC_AYL_LIST ), -1, LVNI_SELECTED );
}

static void
select_only( list_window *l, int index )
{
  HWND list = item( l, IDC_AYL_LIST );
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
add_files( list_window *l )
{
  /* Room for many names: the folder, then the file names */
  static char buffer[ 65536 ];
  OPENFILENAMEA ofn;
  char path[ MAX_PATH * 2 ];
  const char *name;

  buffer[0] = '\0';
  memset( &ofn, 0, sizeof( ofn ) );
  ofn.lStructSize = sizeof( ofn );
  ofn.hwndOwner = wnd( l );
  {
    static char filter[ 512 ];
    /* TurboSound: the formats that can hold music of more chips */
    const char *music = l->turbosound ?
      i18n( "TurboSound Music Files (*.ts;*.pt3;*.vt2;*.txt)" ) :
      i18n( "AY Music Files (*.ay;*.pt3;*.stc;*.ym;...)" );
    const char *all = i18n( "All Files (*.*)" );
    const char *music_types = l->turbosound ? "*.ts;*.pt3;*.vt2;*.txt" :
                                              AY_MUSIC_TYPES ";*.txt";
    const char *all_types = "*.*";
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
  ofn.lpstrTitle = l->turbosound ? i18n( "Fuse - Add TurboSound Files" ) :
                                   i18n( "Fuse - Add AY Files" );

  if( !win32text_file_dialog( &ofn, 0 ) ) return;

  /* One file: its full name; more: the folder, then the names */
  name = buffer + strlen( buffer ) + 1;
  if( !*name ) {
    ayplaylist_add_file( list_of( l ), buffer );
  } else {
    for( ; *name; name += strlen( name ) + 1 ) {
      _snprintf( path, sizeof( path ), "%s\\%s", buffer, name );
      path[ sizeof( path ) - 1 ] = '\0';
      ayplaylist_add_file( list_of( l ), path );
    }
  }
  win32_ayplaylist_update( l->turbosound );
}

static void
add_folder( list_window *l )
{
  /* UTF-8 can take up to 3 bytes for a letter of a MAX_PATH long name */
  char path[ MAX_PATH * 3 ];

  if( !win32text_browse_folder( wnd( l ), l->turbosound ?
          i18n( "The TurboSound files of this folder are added:" ) :
          i18n( "The .ay files of this folder are added:" ),
        BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE, path, sizeof( path ) ) )
    return;
  ayplaylist_add_folder( list_of( l ), path );
  win32_ayplaylist_update( l->turbosound );
}

/* Measure again the songs chosen in the list, or, with none chosen, the
   song of the player; their lengths found before are replaced */
static void
remeasure_chosen( list_window *l )
{
  HWND list = item( l, IDC_AYL_LIST );
  int i, count = ListView_GetItemCount( list ), chosen = 0;
  int *indexes = malloc( ( count + 1 ) * sizeof( *indexes ) );

  if( !indexes ) return;
  for( i = 0; i < count; i++ )
    if( ListView_GetItemState( list, i, LVIS_SELECTED ) )
      indexes[ chosen++ ] = i;
  if( !chosen && ayplaylist_current( list_of( l ) ) >= 0 )
    indexes[ chosen++ ] = ayplaylist_current( list_of( l ) );

  if( chosen ) win32_ayplayer_remeasure( l->turbosound, indexes, chosen );
  free( indexes );
}

static void
remove_selected( list_window *l )
{
  HWND list = item( l, IDC_AYL_LIST );
  int i;

  /* From the end, so the indexes of the others do not change */
  for( i = ListView_GetItemCount( list ) - 1; i >= 0; i-- )
    if( ListView_GetItemState( list, i, LVIS_SELECTED ) )
      ayplaylist_remove( list_of( l ), i );
  win32_ayplaylist_update( l->turbosound );
}

static void
move_selected( list_window *l, int direction )
{
  int index = win32_ayplaylist_selected( l->turbosound );

  if( index < 0 ) return;
  index = ayplaylist_move( list_of( l ), index, direction );
  win32_ayplaylist_update( l->turbosound );
  select_only( l, index );
  win32_ayplayer_update();
}

static int
list_file_dialog( list_window *l, char *path, size_t size, int save )
{
  OPENFILENAMEA ofn;

  path[0] = '\0';
  memset( &ofn, 0, sizeof( ofn ) );
  ofn.lStructSize = sizeof( ofn );
  ofn.hwndOwner = wnd( l );
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
dropped( list_window *l, HDROP drop )
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
      ayplaylist_add_folder( list_of( l ), path );
    else if( length > 4 && !_stricmp( path + length - 4, ".m3u" ) )
      ayplaylist_load_m3u( list_of( l ), path, 1 );
    else if( ayplayer_is_ay_file( path ) )
      ayplaylist_add_file( list_of( l ), path );
  }
  DragFinish( drop );
  win32_ayplaylist_update( l->turbosound );
}

/* ------------------------------------------------------------------------
   The window
   ------------------------------------------------------------------------ */

static void
remember_place( list_window *l )
{
  RECT rect;

  if( !wnd( l ) || IsIconic( wnd( l ) ) || !GetWindowRect( wnd( l ), &rect ) )
    return;
  winstate_set( l->key, "x", rect.left );
  winstate_set( l->key, "y", rect.top );
  winstate_set( l->key, "size_x", rect.right - rect.left );
  winstate_set( l->key, "size_y", rect.bottom - rect.top );

  /* The widths of the columns set by the user (the title takes what is
     left) */
  {
    HWND list = item( l, IDC_AYL_LIST );
    winstate_set( l->key, "column_number",
                  ListView_GetColumnWidth( list, COLUMN_NUMBER ) );
    winstate_set( l->key, "column_author",
                  ListView_GetColumnWidth( list, COLUMN_AUTHOR ) );
    winstate_set( l->key, "column_channels",
                  ListView_GetColumnWidth( list, COLUMN_CHANNELS ) );
    winstate_set( l->key, "column_length",
                  ListView_GetColumnWidth( list, COLUMN_LENGTH ) );
  }
}

static void
restore_place( list_window *l )
{
  long x, y, width, height;
  RECT title;

  /* Earlier versions kept sizes that grew at every start: forget them */
  winstate_remove( l->key, "width" );
  winstate_remove( l->key, "height" );
  winstate_remove( l->key, "cx" );
  winstate_remove( l->key, "cy" );
  winstate_remove( l->key, "w" );
  winstate_remove( l->key, "h" );

  /* The widths of the columns of last time */
  {
    HWND list = item( l, IDC_AYL_LIST );
    long width;
    if( winstate_get( l->key, "column_number", &width ) && width > 0 )
      ListView_SetColumnWidth( list, COLUMN_NUMBER, width );
    if( winstate_get( l->key, "column_author", &width ) && width > 0 )
      ListView_SetColumnWidth( list, COLUMN_AUTHOR, width );
    if( winstate_get( l->key, "column_channels", &width ) && width > 0 )
      ListView_SetColumnWidth( list, COLUMN_CHANNELS, width );
    if( winstate_get( l->key, "column_length", &width ) && width > 0 )
      ListView_SetColumnWidth( list, COLUMN_LENGTH, width );
  }

  if( !winstate_get( l->key, "size_x", &width ) ||
      !winstate_get( l->key, "size_y", &height ) ) {
    width = l->default_size.cx;
    height = l->default_size.cy;
  }
  if( width < l->minimum_size.cx ) width = l->minimum_size.cx;
  if( height < l->minimum_size.cy ) height = l->minimum_size.cy;

  if( !winstate_get( l->key, "x", &x ) || !winstate_get( l->key, "y", &y ) ) {
    /* First time: beside the player */
    RECT player;
    HWND player_window = win32_ayplayer_window( l->turbosound );
    if( player_window && GetWindowRect( player_window, &player ) )
      SetWindowPos( wnd( l ), NULL, player.right + 4, player.top,
                    width, height, SWP_NOZORDER | SWP_NOACTIVATE );
    else
      SetWindowPos( wnd( l ), NULL, 0, 0, width, height,
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
    return;
  }

  /* Only if the title bar is on a screen (otherwise just the size) */
  title.left = x; title.top = y; title.right = x + width;
  title.bottom = y + GetSystemMetrics( SM_CYCAPTION );
  if( !MonitorFromRect( &title, MONITOR_DEFAULTTONULL ) ) {
    SetWindowPos( wnd( l ), NULL, 0, 0, width, height,
                  SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
    return;
  }

  SetWindowPos( wnd( l ), NULL, x, y, width, height,
                SWP_NOZORDER | SWP_NOACTIVATE );
}

static void
layout( list_window *l )
{
  RECT client;
  HWND list = item( l, IDC_AYL_LIST ), status = item( l, IDC_AYL_STATUS );
  RECT rect;
  int i, list_width, title_width, buttons_x;

  GetClientRect( wnd( l ), &client );

  /* The buttons at the right edge, all as wide as the widest */
  buttons_x = client.right - l->edge_margin - l->buttons_width;
  for( i = 0; button_ids[i]; i++ ) {
    HWND button = item( l, button_ids[i] );
    GetWindowRect( button, &rect );
    SetWindowPos( button, NULL, buttons_x, l->button_top[i],
                  l->buttons_width, rect.bottom - rect.top,
                  SWP_NOZORDER | SWP_NOACTIVATE );
  }

  /* The list (and the line under it) up to the buttons */
  list_width = buttons_x - l->buttons_gap - l->list_corner.x;
  if( list_width < 50 ) list_width = 50;
  SetWindowPos( list, NULL, l->list_corner.x, l->list_corner.y, list_width,
                client.bottom - l->list_bottom_gap - l->list_corner.y,
                SWP_NOZORDER | SWP_NOACTIVATE );

  GetWindowRect( status, &rect );
  SetWindowPos( status, NULL, l->list_corner.x,
                client.bottom - l->status_bottom_gap, list_width,
                rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE );

  /* The title takes the room left by the other columns */
  title_width = list_width - ListView_GetColumnWidth( list, COLUMN_NUMBER ) -
                ListView_GetColumnWidth( list, COLUMN_AUTHOR ) -
                ListView_GetColumnWidth( list, COLUMN_CHANNELS ) -
                ListView_GetColumnWidth( list, COLUMN_LENGTH ) -
                GetSystemMetrics( SM_CXVSCROLL ) - 4;
  if( title_width < 60 ) title_width = 60;
  ListView_SetColumnWidth( list, COLUMN_TITLE, title_width );

  InvalidateRect( wnd( l ), NULL, TRUE );
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
setup_geometry( list_window *l )
{
  HWND dialog = wnd( l ), list = item( l, IDC_AYL_LIST );
  RECT client, rect, window, units = { 7, 6, 250, 150 };
  int i, frame_width, client_width;

  /* Dialog units: margin 7, gap 6, list 250 wide by default */
  MapDialogRect( dialog, &units );
  l->edge_margin = units.left;
  l->buttons_gap = units.top;

  GetClientRect( dialog, &client );
  l->buttons_width = 0;
  for( i = 0; button_ids[i]; i++ ) {
    POINT corner;
    GetWindowRect( item( l, button_ids[i] ), &rect );
    corner.x = rect.left; corner.y = rect.top;
    ScreenToClient( dialog, &corner );
    l->button_top[i] = corner.y;
    if( rect.right - rect.left > l->buttons_width )
      l->buttons_width = rect.right - rect.left;
  }

  GetWindowRect( list, &rect );
  {
    POINT top_left = { rect.left, rect.top };
    POINT bottom_right = { rect.right, rect.bottom };
    ScreenToClient( dialog, &top_left );
    ScreenToClient( dialog, &bottom_right );
    l->list_corner = top_left;
    l->list_bottom_gap = client.bottom - bottom_right.y;
  }
  GetWindowRect( item( l, IDC_AYL_STATUS ), &rect );
  {
    POINT top_left = { rect.left, rect.top };
    ScreenToClient( dialog, &top_left );
    l->status_bottom_gap = client.bottom - top_left.y;
  }

  /* The first size: the list 250 dialog units wide; at least 150 pixels */
  GetWindowRect( dialog, &window );
  frame_width = window.right - window.left - client.right;
  client_width = l->list_corner.x + units.right + l->buttons_gap +
                 l->buttons_width + l->edge_margin;
  l->default_size.cx = client_width + frame_width;
  l->default_size.cy = window.bottom - window.top;
  l->minimum_size.cx = l->default_size.cx - units.right + 150;
  l->minimum_size.cy = l->default_size.cy;

  restore_place( l );
  layout( l );
}

static void
init_dialog( list_window *l, HWND dialog )
{
  HWND list;
  RECT units = { 0, 0, 4, 8 };

  *l->window = dialog;
  list = item( l, IDC_AYL_LIST );
  l->shown_current = -1;

  /* Column widths in dialog units, as the rest of the window */
  MapDialogRect( dialog, &units );
  l->number_width = 8 * units.right;
  l->author_width = 20 * units.right;
  l->channels_width = 7 * units.right;
  l->length_width = 9 * units.right;

  ListView_SetExtendedListViewStyle( list, LVS_EX_FULLROWSELECT );
  add_column( list, COLUMN_NUMBER, "#", l->number_width, LVCFMT_LEFT );
  add_column( list, COLUMN_TITLE, "Title", 100, LVCFMT_LEFT );
  add_column( list, COLUMN_AUTHOR, "Author", l->author_width, LVCFMT_LEFT );
  /* The channels of the song: 3 for each AY chip */
  add_column( list, COLUMN_CHANNELS, "Ch.", l->channels_width,
              LVCFMT_RIGHT );
  add_column( list, COLUMN_LENGTH, "Length", l->length_width,
              LVCFMT_RIGHT );

  {
    LOGFONT font;
    HFONT normal = (HFONT)SendMessage( dialog, WM_GETFONT, 0, 0 );
    if( normal && GetObject( normal, sizeof( font ), &font ) ) {
      font.lfWeight = FW_BOLD;
      l->bold_font = CreateFontIndirect( &font );
    }
  }

  DragAcceptFiles( dialog, TRUE );
}

/* The song being played in bold */
static LRESULT
custom_draw( list_window *l, NMLVCUSTOMDRAW *draw )
{
  switch( draw->nmcd.dwDrawStage ) {
  case CDDS_PREPAINT:
    return CDRF_NOTIFYITEMDRAW;
  case CDDS_ITEMPREPAINT:
    if( (int)draw->nmcd.dwItemSpec == ayplaylist_current( list_of( l ) ) &&
        ayplaylist_playing( list_of( l ) ) && l->bold_font ) {
      SelectObject( draw->nmcd.hdc, l->bold_font );
      return CDRF_NEWFONT;
    }
    return CDRF_DODEFAULT;
  }
  return CDRF_DODEFAULT;
}

static list_window*
list_window_of_hwnd( HWND window )
{
  int i;

  for( i = 0; i < 2; i++ )
    if( window && wnd( &lists[i] ) == window ) return &lists[i];
  return NULL;
}

static void
hide( list_window *l )
{
  remember_place( l );
  ShowWindow( wnd( l ), SW_HIDE );
  l->list_open = 0;
}

static INT_PTR CALLBACK
playlist_proc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
  char path[ MAX_PATH ];
  list_window *l;

  if( msg == WM_INITDIALOG ) {
    init_dialog( (list_window *)lParam, hWnd );
    return TRUE;
  }

  l = list_window_of_hwnd( hWnd );
  if( !l ) return FALSE;

  switch( msg ) {

  case WM_SIZE:
    if( wParam != SIZE_MINIMIZED && l->minimum_size.cx ) layout( l );
    return 0;

  case WM_GETMINMAXINFO:
    if( l->minimum_size.cx ) {
      MINMAXINFO *info = (MINMAXINFO *)lParam;
      info->ptMinTrackSize.x = l->minimum_size.cx;
      info->ptMinTrackSize.y = l->minimum_size.cy;
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
    remember_place( l );
    return 0;

  case WM_DROPFILES:
    dropped( l, (HDROP)wParam );
    return 0;

  case WM_NOTIFY:
    {
      NMHDR *header = (NMHDR *)lParam;
      if( header->idFrom != IDC_AYL_LIST ) break;
      if( header->code == NM_DBLCLK ) {
        int index = ( (NMITEMACTIVATE *)lParam )->iItem;
        if( index >= 0 ) win32_ayplayer_play_entry( l->turbosound, index );
        return TRUE;
      }
      if( header->code == NM_CUSTOMDRAW ) {
        SetWindowLongPtr( hWnd, DWLP_MSGRESULT,
                          custom_draw( l, (NMLVCUSTOMDRAW *)lParam ) );
        return TRUE;
      }
      if( header->code == LVN_KEYDOWN &&
          ( (NMLVKEYDOWN *)lParam )->wVKey == VK_DELETE ) {
        remove_selected( l );
        return TRUE;
      }
      if( header->code == NM_RETURN ) {
        int index = win32_ayplaylist_selected( l->turbosound );
        if( index >= 0 ) win32_ayplayer_play_entry( l->turbosound, index );
        return TRUE;
      }
    }
    break;

  case WM_COMMAND:
    switch( LOWORD( wParam ) ) {
    case IDC_AYL_ADD: add_files( l ); return TRUE;
    case IDC_AYL_ADD_FOLDER: add_folder( l ); return TRUE;
    case IDC_AYL_REMOVE: remove_selected( l ); return TRUE;
    case IDC_AYL_CLEAR:
      ayplaylist_clear( list_of( l ) );
      win32_ayplaylist_update( l->turbosound );
      win32_ayplayer_update();
      return TRUE;
    case IDC_AYL_UP: move_selected( l, -1 ); return TRUE;
    case IDC_AYL_DOWN: move_selected( l, 1 ); return TRUE;
    case IDC_AYL_LOAD:
      if( list_file_dialog( l, path, sizeof( path ), 0 ) ) {
        ayplaylist_load_m3u( list_of( l ), path, 0 );
        win32_ayplaylist_update( l->turbosound );
        win32_ayplayer_update();
      }
      return TRUE;
    case IDC_AYL_SAVE:
      if( list_file_dialog( l, path, sizeof( path ), 1 ) )
        ayplaylist_save_m3u( list_of( l ), path );
      return TRUE;
    case IDC_AYL_REMEASURE:
      remeasure_chosen( l );
      return TRUE;
    case IDC_AYL_MEASURE:
      if( win32_ayplayer_busy( l->turbosound ) )
        win32_ayplayer_measure_stop( l->turbosound );
      else
        win32_ayplayer_measure_all( l->turbosound );
      return TRUE;
    case IDCLOSE:
    case IDCANCEL:
      hide( l );
      return TRUE;
    }
    break;

  case WM_CLOSE:
    hide( l );
    return TRUE;
  }

  return FALSE;
}

static int
create( list_window *l )
{
  HWND player = win32_ayplayer_window( l->turbosound );

  if( wnd( l ) ) return 0;

  if( !CreateDialogParam( fuse_hInstance, MAKEINTRESOURCE( l->dialog ),
                          player ? player : fuse_hWnd,
                          (DLGPROC)playlist_proc, (LPARAM)l ) ) {
    *l->window = NULL;
    win32_verror( 1 );
    return 1;
  }
  setup_geometry( l );
  win32_ayplaylist_update( l->turbosound );
  return 0;
}

void
win32_ayplaylist_show( int turbosound )
{
  list_window *l = list_window_of( turbosound );

  if( create( l ) ) return;
  if( IsIconic( wnd( l ) ) )
    ShowWindow( wnd( l ), SW_RESTORE );
  else
    ShowWindow( wnd( l ), SW_SHOWNOACTIVATE );
  l->list_open = 1;
  win32_ayplaylist_update( turbosound );
}

/* The List button: a minimized list comes back to the front, a shown one
   is closed, a closed one is opened */
void
win32_ayplaylist_toggle( int turbosound )
{
  list_window *l = list_window_of( turbosound );

  if( wnd( l ) && IsIconic( wnd( l ) ) ) {
    ShowWindow( wnd( l ), SW_RESTORE );
    SetForegroundWindow( wnd( l ) );
    l->list_open = 1;
  } else if( wnd( l ) && IsWindowVisible( wnd( l ) ) ) {
    hide( l );
  } else {
    win32_ayplaylist_show( turbosound );
  }
}

int
win32_ayplaylist_is_open( int turbosound )
{
  list_window *l = list_window_of( turbosound );

  return wnd( l ) && l->list_open;
}

/* The song where the player stood when Fuse exited: chosen, not played */
void
win32_ayplaylist_choose( int turbosound, int index )
{
  list_window *l = list_window_of( turbosound );
  ayplaylist *list = list_of( l );

  if( index < 0 || index >= ayplaylist_count( list ) ) return;
  ayplaylist_set_current( list, index );
  if( wnd( l ) ) {
    win32_ayplaylist_update( turbosound );
    select_only( l, index );
  }
}

void
win32_ayplaylist_close( int turbosound )
{
  list_window *l = list_window_of( turbosound );

  if( !wnd( l ) ) return;
  l->list_open = 0;
  remember_place( l );
  DestroyWindow( wnd( l ) );
  *l->window = NULL;
  if( l->bold_font ) {
    DeleteObject( l->bold_font );
    l->bold_font = NULL;
  }
}

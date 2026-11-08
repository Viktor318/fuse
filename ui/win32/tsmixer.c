/* tsmixer.c: Win32 TurboSound mixer window
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

/* The mixer of the TurboSound player (tsmix.c): the master volume, the
   stereo mode, and a group for each AY chip of the song, with a column for
   each channel: its panorama, its letter, whether it is heard, its level
   meter beside its volume, and the volume in percent. The window shows as
   many chips as the song has (two or three), getting narrower or wider on
   the right. The places of the controls are set here, in dialog units */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <commctrl.h> /* windows.h must be included prior to commctrl.h */

#include "ayplayer.h"
#include "../../ayplay.h"
#include "../../ayplayer.h"
#include "../../aymix.h"
#include "../../ayplaylist.h"
#include "../../tsmix.h"
#include "fuse.h"
#include "i18n.h"
#include "settings.h"
#include "sound.h"
#include "win32internals.h"
#include "win32text.h"
#include "winstate.h"

HWND fuse_hTSMixWnd = NULL;

#define CHIPS TSMIX_CHIPS
#define CHANNELS TSMIX_CHANNELS
#define COLUMNS ( CHIPS * CHANNELS )

#define TSMIXER_TIMER 1
#define METER_STEPS 15

/* The stereo modes in the order of the combo box (as in the AY mixer) */
static const char * const stereo_names[] =
  { "ABC", "ACB", "BAC", "BCA", "CBA", "CAB", "Mono", "Custom" };
static const int stereo_modes[] = {
  SOUND_STEREO_AY_ABC, SOUND_STEREO_AY_ACB, SOUND_STEREO_AY_BAC,
  SOUND_STEREO_AY_BCA, SOUND_STEREO_AY_CBA, SOUND_STEREO_AY_CAB,
  SOUND_STEREO_AY_NONE, SOUND_STEREO_AY_CUSTOM
};
#define STEREO_COUNT 8

/* The layout, in dialog units */
#define MARGIN 7
#define TOP_ROW 7
#define GROUP_TOP 26
#define GROUP_HEIGHT 156
#define MASTER_WIDTH 52
#define CHIP_LEFT 63			/* the first chip's group */
#define CHIP_STEP 126			/* from one chip's group to the next */
#define CHIP_WIDTH 122
#define COLUMN_LEFT 6			/* the first column in a group */
#define COLUMN_STEP 38
#define COLUMN_WIDTH 36
#define SLIDER_TOP 70			/* the volume sliders */
#define SLIDER_HEIGHT 96
#define METER_TOP 72
#define METER_HEIGHT 92
#define PERCENT_TOP 168
#define CLIENT_HEIGHT 202

/* The chips shown (2 or 3) */
static int chips_shown = 0;
/* The window is being filled in: its notifications are not changes */
static int filling = 0;
/* The shown level of each channel, in 1/4 steps */
static int meter_shown[ CHIPS ][ CHANNELS ];
static DWORD last_meters = 0;

static void close_window( void );

static HWND
item( int id )
{
  return GetDlgItem( fuse_hTSMixWnd, id );
}

static int
column_id( int first, int chip, int channel )
{
  return first + chip * CHANNELS + channel;
}

/* A control at a place given in dialog units */
static void
place( HWND control, int x, int y, int width, int height )
{
  RECT rect;

  rect.left = x; rect.top = y; rect.right = x + width;
  rect.bottom = y + height;
  MapDialogRect( fuse_hTSMixWnd, &rect );
  SetWindowPos( control, NULL, rect.left, rect.top, rect.right - rect.left,
                rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE );
}

static int
client_width( int chips )
{
  return CHIP_LEFT + chips * CHIP_STEP - ( CHIP_STEP - CHIP_WIDTH ) + MARGIN;
}

/* ------------------------------------------------------------------------
   Making and placing the controls
   ------------------------------------------------------------------------ */

static void
make_control( const char *class_name, DWORD style, int id )
{
  HWND control = CreateWindowExA( 0, class_name, "",
                                  WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
                                  fuse_hTSMixWnd, (HMENU)(INT_PTR)id,
                                  fuse_hInstance, NULL );
  SendMessage( control, WM_SETFONT,
               SendMessage( fuse_hTSMixWnd, WM_GETFONT, 0, 0 ), FALSE );
}

/* The controls of the channels, in the order of the Tab key */
static void
make_columns( void )
{
  int chip, channel;

  for( chip = 0; chip < CHIPS; chip++ )
    for( channel = 0; channel < CHANNELS; channel++ ) {
      HWND pan, volume;

      make_control( TRACKBAR_CLASSA, TBS_HORZ | TBS_BOTTOM | WS_TABSTOP,
                    column_id( IDC_TSM_PAN1, chip, channel ) );
      make_control( "Static", SS_CENTER | SS_NOPREFIX,
                    column_id( IDC_TSM_LETTER1, chip, channel ) );
      make_control( "Button", BS_AUTOCHECKBOX | WS_TABSTOP,
                    column_id( IDC_TSM_ON1, chip, channel ) );
      make_control( "Static", SS_OWNERDRAW,
                    column_id( IDC_TSM_METER1, chip, channel ) );
      make_control( TRACKBAR_CLASSA, TBS_VERT | TBS_AUTOTICKS | WS_TABSTOP,
                    column_id( IDC_TSM_VOLUME1, chip, channel ) );
      make_control( "Static", SS_CENTER | SS_NOPREFIX,
                    column_id( IDC_TSM_VOLUME_TEXT1, chip, channel ) );

      /* -100..100 as 0..200, with a mark in the middle */
      pan = item( column_id( IDC_TSM_PAN1, chip, channel ) );
      SendMessage( pan, TBM_SETRANGE, FALSE, MAKELPARAM( 0, 200 ) );
      SendMessage( pan, TBM_SETPAGESIZE, 0, 20 );
      SendMessage( pan, TBM_CLEARTICS, FALSE, 0 );
      SendMessage( pan, TBM_SETTIC, 0, 100 );

      /* 100% at the top */
      volume = item( column_id( IDC_TSM_VOLUME1, chip, channel ) );
      SendMessage( volume, TBM_SETRANGE, FALSE, MAKELPARAM( 0, 100 ) );
      SendMessage( volume, TBM_SETPAGESIZE, 0, 10 );
      SendMessage( volume, TBM_SETTICFREQ, 10, 0 );
    }
}

/* Everything in its place: the top row stays where it is whatever the
   number of chips; the stereo mode is above the second chip's group */
static void
layout( void )
{
  HWND label = item( IDC_TSM_STEREO_LABEL );
  RECT units = { 0, 0, 4, 8 };
  int chip, channel, label_width, width = client_width( chips_shown );

  place( item( IDC_TSM_LOAD ), MARGIN, TOP_ROW, 50, 14 );
  place( item( IDC_TSM_SAVE ), MARGIN + 54, TOP_ROW, 50, 14 );
  place( item( IDC_TSM_RESET ), MARGIN + 108, TOP_ROW, 50, 14 );

  /* The label as wide as its (translated) text */
  {
    WCHAR text[ 128 ];
    HDC dc = GetDC( label );
    HFONT old = SelectObject( dc, (HFONT)SendMessage( label, WM_GETFONT, 0,
                                                       0 ) );
    RECT size = { 0, 0, 0, 0 };
    GetWindowTextW( label, text, sizeof( text ) / sizeof( text[0] ) );
    DrawTextW( dc, text, -1, &size, DT_CALCRECT | DT_SINGLELINE );
    SelectObject( dc, old );
    ReleaseDC( label, dc );
    MapDialogRect( fuse_hTSMixWnd, &units );
    label_width = ( size.right * 4 + units.right - 1 ) / units.right + 2;
  }
  place( label, CHIP_LEFT + CHIP_STEP, TOP_ROW + 3, label_width, 8 );
  place( item( IDC_TSM_STEREO ), CHIP_LEFT + CHIP_STEP + label_width + 3,
         TOP_ROW + 1, 60, 110 );

  place( item( IDC_TSM_MASTER_GROUP ), MARGIN, GROUP_TOP, MASTER_WIDTH,
         GROUP_HEIGHT );
  place( item( IDC_TSM_MASTER ), MARGIN + ( MASTER_WIDTH - 18 ) / 2,
         SLIDER_TOP, 18, SLIDER_HEIGHT );
  place( item( IDC_TSM_MASTER_TEXT ), MARGIN + 2, PERCENT_TOP,
         MASTER_WIDTH - 4, 8 );

  for( chip = 0; chip < CHIPS; chip++ ) {
    int group = CHIP_LEFT + chip * CHIP_STEP;
    int shown = chip < chips_shown;

    place( item( IDC_TSM_GROUP1 + chip ), group, GROUP_TOP, CHIP_WIDTH,
           GROUP_HEIGHT );
    ShowWindow( item( IDC_TSM_GROUP1 + chip ), shown ? SW_SHOW : SW_HIDE );

    for( channel = 0; channel < CHANNELS; channel++ ) {
      int x = group + COLUMN_LEFT + channel * COLUMN_STEP;
      static const int firsts[] = {
        IDC_TSM_PAN1, IDC_TSM_LETTER1, IDC_TSM_ON1, IDC_TSM_METER1,
        IDC_TSM_VOLUME1, IDC_TSM_VOLUME_TEXT1
      };
      size_t i;

      place( item( column_id( IDC_TSM_PAN1, chip, channel ) ), x, 37,
             COLUMN_WIDTH, 12 );
      place( item( column_id( IDC_TSM_LETTER1, chip, channel ) ), x, 50,
             COLUMN_WIDTH, 8 );
      /* The switch above the meter */
      place( item( column_id( IDC_TSM_ON1, chip, channel ) ), x + 5, 60, 10,
             10 );
      place( item( column_id( IDC_TSM_METER1, chip, channel ) ), x + 5,
             METER_TOP, 9, METER_HEIGHT );
      place( item( column_id( IDC_TSM_VOLUME1, chip, channel ) ), x + 16,
             SLIDER_TOP, 18, SLIDER_HEIGHT );
      place( item( column_id( IDC_TSM_VOLUME_TEXT1, chip, channel ) ), x,
             PERCENT_TOP, COLUMN_WIDTH, 8 );

      for( i = 0; i < sizeof( firsts ) / sizeof( firsts[0] ); i++ )
        ShowWindow( item( column_id( firsts[i], chip, channel ) ),
                    shown ? SW_SHOW : SW_HIDE );
    }
  }

  place( item( IDC_TSM_LINE ), MARGIN, GROUP_TOP + GROUP_HEIGHT + 5,
         width - 2 * MARGIN, 1 );
  place( item( IDC_TSM_STATUS ), MARGIN, GROUP_TOP + GROUP_HEIGHT + 9,
         width - 2 * MARGIN, 8 );

  /* The window: wider or narrower on the right, the top left corner
     stays */
  {
    RECT rect = { 0, 0, 0, CLIENT_HEIGHT };
    rect.right = width;
    MapDialogRect( fuse_hTSMixWnd, &rect );
    AdjustWindowRectEx( &rect, GetWindowLong( fuse_hTSMixWnd, GWL_STYLE ),
                        FALSE, GetWindowLong( fuse_hTSMixWnd, GWL_EXSTYLE ) );
    SetWindowPos( fuse_hTSMixWnd, NULL, 0, 0, rect.right - rect.left,
                  rect.bottom - rect.top,
                  SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
  }
  InvalidateRect( fuse_hTSMixWnd, NULL, TRUE );
}

/* ------------------------------------------------------------------------
   Showing the settings
   ------------------------------------------------------------------------ */

static void
set_text( int id, const char *text )
{
  /* Texts from the music files may be in an old 8-bit code page */
  char *utf8 = i18n_legacy_to_utf8( text );

  SetDlgItemTextA( fuse_hTSMixWnd, id, utf8 ? utf8 : text );
  libspectrum_free( utf8 );
}

static void
set_percent( int id, int value )
{
  char text[ 16 ];

  _snprintf( text, sizeof( text ), "%d%%", value );
  text[ sizeof( text ) - 1 ] = '\0';
  set_text( id, text );
}

/* A vertical slider: 100% at the top */
static void
set_volume_slider( HWND slider, int volume )
{
  if( SendMessage( slider, TBM_GETPOS, 0, 0 ) != 100 - volume )
    SendMessage( slider, TBM_SETPOS, TRUE, 100 - volume );
}

/* The song of the TurboSound player: its file name and chips */
static const char *
song_file( int *chips )
{
  ayplaylist *list = ayplaylist_ts();
  const ayplaylist_entry *entry =
    ayplaylist_get( list, ayplaylist_current( list ) );
  const char *name, *p;

  if( ayplaylist_owns_player( list ) ) {
    *chips = ayplayer_chips();
    return ayplayer_filename();
  }
  if( !entry ) return NULL;
  *chips = entry->chips;
  name = entry->path;
  for( p = entry->path; *p; p++ )
    if( *p == '/' || *p == '\\' ) name = p + 1;
  return name;
}

static void
update_title( void )
{
  char title[ 512 ];
  int chips;
  const char *file = song_file( &chips );

  if( file && *file )
    _snprintf( title, sizeof( title ), "%s - %s",
               i18n( "Fuse - TurboSound Mixer" ), file );
  else
    _snprintf( title, sizeof( title ), "%s",
               i18n( "Fuse - TurboSound Mixer" ) );
  title[ sizeof( title ) - 1 ] = '\0';
  {
    char *utf8 = i18n_legacy_to_utf8( title );
    SetWindowTextA( fuse_hTSMixWnd, utf8 ? utf8 : title );
    libspectrum_free( utf8 );
  }
}

/* Everything from the settings */
void
win32_tsmixer_update( void )
{
  int stereo, chip, channel, i;
  char text[ 384 ];
  const char *name;

  if( !fuse_hTSMixWnd ) return;
  filling = 1;

  stereo = tsmix_stereo();
  for( i = 0; i < STEREO_COUNT; i++ )
    if( stereo_modes[i] == stereo )
      SendDlgItemMessage( fuse_hTSMixWnd, IDC_TSM_STEREO, CB_SETCURSEL, i, 0 );

  set_volume_slider( item( IDC_TSM_MASTER ), tsmix_master() );
  set_percent( IDC_TSM_MASTER_TEXT, tsmix_master() );

  for( chip = 0; chip < CHIPS; chip++ )
    for( channel = 0; channel < CHANNELS; channel++ ) {
      HWND pan = item( column_id( IDC_TSM_PAN1, chip, channel ) );
      int volume = tsmix_volume( chip, channel );
      int position = tsmix_pan( chip, channel ) + 100;

      _snprintf( text, sizeof( text ), "%c", 'A' + channel );
      text[ sizeof( text ) - 1 ] = '\0';
      set_text( column_id( IDC_TSM_LETTER1, chip, channel ), text );

      /* Mono: no panorama */
      EnableWindow( pan, stereo != SOUND_STEREO_AY_NONE );
      if( SendMessage( pan, TBM_GETPOS, 0, 0 ) != position )
        SendMessage( pan, TBM_SETPOS, TRUE, position );

      CheckDlgButton( fuse_hTSMixWnd, column_id( IDC_TSM_ON1, chip, channel ),
                      tsmix_on( chip, channel ) ? BST_CHECKED :
                                                  BST_UNCHECKED );
      set_volume_slider( item( column_id( IDC_TSM_VOLUME1, chip, channel ) ),
                         volume );
      set_percent( column_id( IDC_TSM_VOLUME_TEXT1, chip, channel ), volume );
      InvalidateRect( item( column_id( IDC_TSM_METER1, chip, channel ) ),
                      NULL, FALSE );
    }

  /* The .mix file in use, or the settings of the user */
  name = tsmix_loaded_name();
  _snprintf( text, sizeof( text ), i18n( "Settings: %s" ),
             *name ? name : i18n( "Default" ) );
  text[ sizeof( text ) - 1 ] = '\0';
  set_text( IDC_TSM_STATUS, text );

  update_title();
  filling = 0;
}

/* The song of the TurboSound player changed: as many chips as it has */
void
win32_tsmixer_song_changed( void )
{
  int chips = chips_shown;

  if( !fuse_hTSMixWnd ) return;
  song_file( &chips );
  if( chips < 2 ) chips = 2;
  if( chips > CHIPS ) chips = CHIPS;
  if( chips != chips_shown ) {
    chips_shown = chips;
    layout();
  }
  win32_tsmixer_update();
}

/* ------------------------------------------------------------------------
   Level meters
   ------------------------------------------------------------------------ */

/* Fast up, slowly down; nothing while the song is not the TurboSound
   player's */
static void
update_meters( void )
{
  int playing = ayplaylist_playing( ayplaylist_ts() ) && !ayplayer_paused();
  int chip, channel;

  for( chip = 0; chip < chips_shown; chip++ )
    for( channel = 0; channel < CHANNELS; channel++ ) {
      int level = playing ? ayplay_chip_peak( chip, channel ) : 0;
      int target, old = meter_shown[ chip ][ channel ], shown;

      if( !tsmix_on( chip, channel ) ) level = 0;
      target = ayplay_level_heard( level, tsmix_volume( chip, channel ) ) * 4;
      if( target >= old ) shown = target;
      else shown = old - 3 > target ? old - 3 : target;
      meter_shown[ chip ][ channel ] = shown;

      if( ( shown + 3 ) / 4 != ( old + 3 ) / 4 )
        InvalidateRect( item( column_id( IDC_TSM_METER1, chip, channel ) ),
                        NULL, FALSE );
    }
}

/* Called by the AY player every frame (and by the window's own timer):
   the meters every 40 ms */
void
win32_tsmixer_tick( void )
{
  DWORD now = GetTickCount();

  if( !fuse_hTSMixWnd || IsIconic( fuse_hTSMixWnd ) ) return;
  if( now - last_meters < 40 ) return;
  last_meters = now;
  /* Paused: the meters stay where the music stopped */
  if( ayplayer_paused() && ayplaylist_playing( ayplaylist_ts() ) ) return;
  update_meters();
}

/* Upwards: green, then yellow and red at the top; dark where the level
   does not reach, and all dark for a channel switched off */
static void
draw_meter( const DRAWITEMSTRUCT *draw )
{
  int index = draw->CtlID - IDC_TSM_METER1;
  int chip = index / CHANNELS, channel = index % CHANNELS;
  int level = ( meter_shown[ chip ][ channel ] + 3 ) / 4;
  RECT area = draw->rcItem, segment;
  int width = area.right - area.left, height = area.bottom - area.top;
  int gap = 1, border = 2, i, inner;
  HDC dc;
  HBITMAP bitmap, old_bitmap;
  HBRUSH brush;

  dc = CreateCompatibleDC( draw->hDC );
  bitmap = CreateCompatibleBitmap( draw->hDC, width, height );
  old_bitmap = SelectObject( dc, bitmap );

  segment.left = 0; segment.top = 0;
  segment.right = width; segment.bottom = height;
  brush = CreateSolidBrush( RGB( 17, 22, 26 ) );
  FillRect( dc, &segment, brush );
  DeleteObject( brush );

  inner = height - 2 * border - ( METER_STEPS - 1 ) * gap;
  for( i = 0; i < METER_STEPS; i++ ) {
    COLORREF colour;

    segment.left = border;
    segment.right = width - border;
    segment.bottom = height - border - i * inner / METER_STEPS - i * gap;
    segment.top = height - border - ( i + 1 ) * inner / METER_STEPS - i * gap;

    if( i >= level ) colour = RGB( 40, 46, 52 );
    else if( i < 10 ) colour = RGB( 45, 184, 77 );
    else if( i < 13 ) colour = RGB( 224, 192, 32 );
    else colour = RGB( 224, 64, 48 );

    brush = CreateSolidBrush( colour );
    FillRect( dc, &segment, brush );
    DeleteObject( brush );
  }

  BitBlt( draw->hDC, area.left, area.top, width, height, dc, 0, 0, SRCCOPY );
  SelectObject( dc, old_bitmap );
  DeleteObject( bitmap );
  DeleteDC( dc );
}

/* ------------------------------------------------------------------------
   Changes
   ------------------------------------------------------------------------ */

static void
scroll( HWND slider )
{
  int chip, channel, position = SendMessage( slider, TBM_GETPOS, 0, 0 );

  if( slider == item( IDC_TSM_MASTER ) ) {
    tsmix_set_master( 100 - position );
    return;
  }
  for( chip = 0; chip < CHIPS; chip++ )
    for( channel = 0; channel < CHANNELS; channel++ ) {
      if( slider == item( column_id( IDC_TSM_VOLUME1, chip, channel ) ) ) {
        tsmix_set_volume( chip, channel, 100 - position );
        return;
      }
      if( slider == item( column_id( IDC_TSM_PAN1, chip, channel ) ) ) {
        int pan = position - 100;
        /* The middle is easy to find */
        if( pan > -3 && pan < 3 ) pan = 0;
        tsmix_set_pan( chip, channel, pan );
        return;
      }
    }
}

/* "Mixer settings (*.mix)\0*.mix\0\0" */
static void
mix_filter( char *filter, size_t size )
{
  const char *type = i18n( "Mixer settings (*.mix)" );
  size_t length = strlen( type );

  if( length > size - 8 ) length = size - 8;
  memcpy( filter, type, length );
  memcpy( filter + length, "\0*.mix\0\0", 8 );
}

static const char *
song_path( void )
{
  ayplaylist *list = ayplaylist_ts();
  const ayplaylist_entry *entry =
    ayplaylist_get( list, ayplaylist_current( list ) );

  if( ayplaylist_owns_player( list ) ) return ayplayer_path();
  return entry ? entry->path : NULL;
}

static void
load_or_save( int save )
{
  OPENFILENAMEA dialog;
  char filename[ 1100 ], filter[ 256 ];

  aymix_default_path( song_path(), filename, sizeof( filename ) );
  if( !save ) {
    /* The folder of the song, no name */
    char *p, *slash = NULL;
    for( p = filename; *p; p++ )
      if( *p == '/' || *p == '\\' ) slash = p;
    if( slash ) {
      FILE *f = fopen( filename, "r" );
      if( f ) fclose( f );
      else *( slash + 1 ) = '\0';
    }
  }
  mix_filter( filter, sizeof( filter ) );

  memset( &dialog, 0, sizeof( dialog ) );
  dialog.lStructSize = sizeof( dialog );
  dialog.hwndOwner = fuse_hTSMixWnd;
  dialog.lpstrFilter = filter;
  dialog.lpstrFile = filename;
  dialog.nMaxFile = sizeof( filename );
  dialog.lpstrTitle = save ? i18n( "Save TurboSound mixer settings" ) :
                             i18n( "Load TurboSound mixer settings" );
  dialog.lpstrDefExt = "mix";
  dialog.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR |
                 ( save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST );

  if( !win32text_file_dialog( &dialog, save ) ) return;
  if( save ) tsmix_save( filename );
  else tsmix_load( filename );
}

static void
command( WORD id, WORD code )
{
  int index;

  if( filling ) return;

  switch( id ) {

  case IDC_TSM_STEREO:
    if( code == CBN_SELCHANGE ) {
      int i = SendDlgItemMessage( fuse_hTSMixWnd, IDC_TSM_STEREO,
                                  CB_GETCURSEL, 0, 0 );
      if( i >= 0 && i < STEREO_COUNT ) tsmix_set_stereo( stereo_names[i] );
    }
    return;

  case IDC_TSM_RESET:
    if( code == BN_CLICKED ) tsmix_reset();
    return;

  case IDC_TSM_LOAD:
  case IDC_TSM_SAVE:
    if( code == BN_CLICKED ) load_or_save( id == IDC_TSM_SAVE );
    return;

  case IDCLOSE:
  case IDCANCEL:
    close_window();
    return;
  }

  index = id - IDC_TSM_ON1;
  if( index >= 0 && index < COLUMNS && code == BN_CLICKED ) {
    tsmix_set_on( index / CHANNELS, index % CHANNELS,
                  IsDlgButtonChecked( fuse_hTSMixWnd, id ) == BST_CHECKED );
    InvalidateRect( item( IDC_TSM_METER1 + index ), NULL, FALSE );
  }
}

/* ------------------------------------------------------------------------
   The window
   ------------------------------------------------------------------------ */

static void
remember_position( void )
{
  RECT rect;

  if( !fuse_hTSMixWnd || IsIconic( fuse_hTSMixWnd ) ||
      !GetWindowRect( fuse_hTSMixWnd, &rect ) )
    return;
  winstate_set( "tsmixer", "x", rect.left );
  winstate_set( "tsmixer", "y", rect.top );
}

/* The place of last time; the first time under the TurboSound playlist or
   beside the player, on the screen */
static void
restore_position( void )
{
  MONITORINFO monitor;
  POINT title;
  RECT anchor, window;
  long x, y;
  int dx = 0, dy = 0;

  if( winstate_get( "tsmixer", "x", &x ) &&
      winstate_get( "tsmixer", "y", &y ) ) {
    /* Only if the title bar is on a screen (one may have been removed) */
    title.x = x + 40;
    title.y = y + 8;
    if( MonitorFromPoint( title, MONITOR_DEFAULTTONULL ) ) {
      SetWindowPos( fuse_hTSMixWnd, NULL, x, y, 0, 0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
      return;
    }
  }

  if( fuse_hTSListWnd && IsWindowVisible( fuse_hTSListWnd ) &&
      GetWindowRect( fuse_hTSListWnd, &anchor ) )
    SetWindowPos( fuse_hTSMixWnd, NULL, anchor.left, anchor.bottom + 4, 0, 0,
                  SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
  else if( fuse_hTSWnd && GetWindowRect( fuse_hTSWnd, &anchor ) )
    SetWindowPos( fuse_hTSMixWnd, NULL, anchor.right + 4, anchor.top, 0, 0,
                  SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
  else
    return;

  /* Not over the edge of the screen; as wide as with three chips */
  monitor.cbSize = sizeof( monitor );
  if( !GetWindowRect( fuse_hTSMixWnd, &window ) ||
      !GetMonitorInfo( MonitorFromRect( &anchor, MONITOR_DEFAULTTONEAREST ),
                       &monitor ) )
    return;
  {
    RECT units = { 0, 0, CHIP_STEP, 0 };
    MapDialogRect( fuse_hTSMixWnd, &units );
    if( chips_shown < CHIPS )
      window.right += ( CHIPS - chips_shown ) * units.right;
  }
  if( window.right > monitor.rcWork.right )
    dx = monitor.rcWork.right - window.right;
  if( window.bottom > monitor.rcWork.bottom )
    dy = monitor.rcWork.bottom - window.bottom;
  if( window.left + dx < monitor.rcWork.left )
    dx = monitor.rcWork.left - window.left;
  if( window.top + dy < monitor.rcWork.top )
    dy = monitor.rcWork.top - window.top;
  if( dx || dy )
    SetWindowPos( fuse_hTSMixWnd, NULL, window.left + dx, window.top + dy,
                  0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
}

static void
close_window( void )
{
  if( !fuse_hTSMixWnd ) return;
  remember_position();
  KillTimer( fuse_hTSMixWnd, TSMIXER_TIMER );
  DestroyWindow( fuse_hTSMixWnd );
  fuse_hTSMixWnd = NULL;
}

void
win32_tsmixer_close( void )
{
  close_window();
}

static INT_PTR CALLBACK
tsmixer_proc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
  int i;

  switch( msg ) {

  case WM_INITDIALOG:
    fuse_hTSMixWnd = hWnd;
    for( i = 0; i < STEREO_COUNT; i++ )
      SendDlgItemMessageA( hWnd, IDC_TSM_STEREO, CB_ADDSTRING, 0,
                           (LPARAM)i18n( stereo_names[ i ] ) );
    SendDlgItemMessage( hWnd, IDC_TSM_MASTER, TBM_SETRANGE, FALSE,
                        MAKELPARAM( 0, 100 ) );
    SendDlgItemMessage( hWnd, IDC_TSM_MASTER, TBM_SETPAGESIZE, 0, 10 );
    SendDlgItemMessage( hWnd, IDC_TSM_MASTER, TBM_SETTICFREQ, 10, 0 );
    make_columns();
    memset( meter_shown, 0, sizeof( meter_shown ) );
    SetTimer( hWnd, TSMIXER_TIMER, 40, NULL );
    return TRUE;

  case WM_TIMER:
    if( wParam == TSMIXER_TIMER ) {
      win32_tsmixer_tick();
      return 0;
    }
    break;

  case WM_HSCROLL:
  case WM_VSCROLL:
    if( !filling && lParam ) scroll( (HWND)lParam );
    return 0;

  case WM_DRAWITEM:
    if( wParam >= IDC_TSM_METER1 && wParam < IDC_TSM_METER1 + COLUMNS ) {
      draw_meter( (const DRAWITEMSTRUCT *)lParam );
      return TRUE;
    }
    break;

  case WM_COMMAND:
    command( LOWORD( wParam ), HIWORD( wParam ) );
    return 0;

  /* Moving the window: the music goes on */
  case WM_ENTERSIZEMOVE:
    win32ui_modal_enter();
    return 0;

  case WM_EXITSIZEMOVE:
    win32ui_modal_exit();
    remember_position();
    return 0;

  case WM_CLOSE:
    close_window();
    return 0;
  }

  return FALSE;
}

/* Open the mixer (or bring it back), owned by `owner': the TurboSound
   player, so it is minimized and closed with it, or the main window */
void
win32_tsmixer_show_for( HWND owner )
{
  tsmix_set_changed_hook( win32_tsmixer_update );

  if( !fuse_hTSMixWnd ) {
    if( !CreateDialog( fuse_hInstance, MAKEINTRESOURCE( IDD_TSMIXER ),
                       owner, (DLGPROC)tsmixer_proc ) ) {
      fuse_hTSMixWnd = NULL;
      win32_verror( 1 );
      return;
    }
    /* After the texts were translated: as many chips as the song has */
    {
      int chips = 2;
      song_file( &chips );
      chips_shown = chips < 2 ? 2 : chips > CHIPS ? CHIPS : chips;
    }
    layout();
    restore_position();
  } else if( win32_tsmixer_owner() != owner ) {
    SetWindowLongPtr( fuse_hTSMixWnd, GWLP_HWNDPARENT, (LONG_PTR)owner );
  }

  win32_tsmixer_update();
  ShowWindow( fuse_hTSMixWnd, IsIconic( fuse_hTSMixWnd ) ? SW_RESTORE :
                                                           SW_SHOW );
  SetForegroundWindow( fuse_hTSMixWnd );
}

/* Options > TurboSound mixer: with the TurboSound player if it is open,
   else with the main window (an open mixer stays with its owner) */
void
win32_tsmixer_show( void )
{
  win32_tsmixer_show_for( fuse_hTSMixWnd ? win32_tsmixer_owner() :
                          fuse_hTSWnd ? fuse_hTSWnd : fuse_hWnd );
}

/* The Mixer button of the TurboSound player: a minimized mixer comes back
   to the front, an open one is closed, a closed one is opened */
void
win32_tsmixer_toggle( HWND player )
{
  if( fuse_hTSMixWnd && !IsIconic( fuse_hTSMixWnd ) &&
      win32_tsmixer_owner() == player )
    win32_tsmixer_close();
  else
    win32_tsmixer_show_for( player );
}

/* The TurboSound player was opened: from now on the mixer goes with it */
void
win32_tsmixer_adopt( HWND player )
{
  if( fuse_hTSMixWnd )
    SetWindowLongPtr( fuse_hTSMixWnd, GWLP_HWNDPARENT, (LONG_PTR)player );
}

HWND
win32_tsmixer_owner( void )
{
  return fuse_hTSMixWnd ? GetWindow( fuse_hTSMixWnd, GW_OWNER ) : NULL;
}

int
win32_tsmixer_is_open( void )
{
  return fuse_hTSMixWnd != NULL;
}

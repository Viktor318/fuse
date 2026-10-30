/* aymixer.c: Win32 AY mixer window
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

/* The stereo mode, the places of the channels in the custom mode, and the
   volume and panorama of each AY channel (games hear them too). The rows
   show the channels from left to right, as the level meters of the AY
   player do */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <commctrl.h> /* windows.h must be included prior to commctrl.h */

#include "ayplayer.h"
#include "../../ayplayer.h"
#include "../../aymix.h"
#include "fuse.h"
#include "i18n.h"
#include "settings.h"
#include "sound.h"
#include "sound/ay_engine.h"
#include "win32internals.h"
#include "winstate.h"

HWND fuse_hAYMixWnd = NULL;

static void close_window( void );

/* The stereo modes in the order of the combo box (as in the AY player) */
static const char * const stereo_names[] =
  { "ABC", "ACB", "BAC", "BCA", "CBA", "CAB", "Mono", "Custom" };
static const int stereo_modes[] = {
  SOUND_STEREO_AY_ABC, SOUND_STEREO_AY_ACB, SOUND_STEREO_AY_BAC,
  SOUND_STEREO_AY_BCA, SOUND_STEREO_AY_CBA, SOUND_STEREO_AY_CAB,
  SOUND_STEREO_AY_NONE, SOUND_STEREO_AY_CUSTOM
};
#define STEREO_COUNT 8

/* The channel shown in each row */
static int order[ 3 ] = { 0, 1, 2 };
/* The window is being filled in: its notifications are not changes */
static int filling = 0;

static void
set_text( int id, const char *text )
{
  SetDlgItemTextA( fuse_hAYMixWnd, id, text );
}

/* "L 100%", "C", "R 40%" (in Hungarian B, K, J) */
static void
pan_text( int pan, char *text, size_t length )
{
  if( pan < 0 )
    _snprintf( text, length, i18n( "L %d%%" ), -pan );
  else if( pan > 0 )
    _snprintf( text, length, i18n( "R %d%%" ), pan );
  else
    _snprintf( text, length, "%s", i18n( "C" ) );
  text[ length - 1 ] = '\0';
}

static int
channel_volume( int channel )
{
  int volume = channel == 0 ? settings_current.volume_ay_a :
               channel == 1 ? settings_current.volume_ay_b :
                              settings_current.volume_ay_c;
  return volume < 0 ? 0 : volume > 100 ? 100 : volume;
}

static void
update_title( void )
{
  char title[ 384 ];
  const char *name = aymix_loaded_name();

  if( *name )
    _snprintf( title, sizeof( title ), "%s - %s", i18n( "Fuse - AY Mixer" ),
               name );
  else
    _snprintf( title, sizeof( title ), "%s", i18n( "Fuse - AY Mixer" ) );
  title[ sizeof( title ) - 1 ] = '\0';
  SetWindowTextA( fuse_hAYMixWnd, title );
}

/* Everything from the settings */
void
win32_aymixer_update( void )
{
  int stereo = sound_ay_stereo_setting();
  int position[ 3 ], row, i;
  char text[ 32 ];

  if( !fuse_hAYMixWnd ) return;
  filling = 1;

  for( i = 0; i < STEREO_COUNT; i++ )
    if( stereo_modes[i] == stereo )
      SendDlgItemMessage( fuse_hAYMixWnd, IDC_AYM_STEREO, CB_SETCURSEL, i, 0 );

  ay_engine_stereo_order( stereo, order );
  sound_ay_channel_positions( stereo, position );

  for( row = 0; row < 3; row++ ) {
    int channel = order[ row ];
    int pan = sound_ay_channel_pan( stereo, channel );
    HWND place = GetDlgItem( fuse_hAYMixWnd, IDC_AYM_PLACE1 + row );
    HWND pan_slider = GetDlgItem( fuse_hAYMixWnd, IDC_AYM_PAN1 + row );

    _snprintf( text, sizeof( text ), "%c", 'A' + channel );
    text[ sizeof( text ) - 1 ] = '\0';
    set_text( IDC_AYM_CHANNEL1 + row, text );

    /* The places: set in the custom mode, shown in the others */
    SendMessage( place, CB_SETCURSEL,
                 stereo == SOUND_STEREO_AY_NONE ? SOUND_AY_CENTRE :
                 position[ channel ], 0 );
    EnableWindow( place, stereo == SOUND_STEREO_AY_CUSTOM );

    if( SendDlgItemMessage( fuse_hAYMixWnd, IDC_AYM_VOLUME1 + row,
                            TBM_GETPOS, 0, 0 ) != channel_volume( channel ) )
      SendDlgItemMessage( fuse_hAYMixWnd, IDC_AYM_VOLUME1 + row, TBM_SETPOS,
                          TRUE, channel_volume( channel ) );
    _snprintf( text, sizeof( text ), "%d%%", channel_volume( channel ) );
    text[ sizeof( text ) - 1 ] = '\0';
    set_text( IDC_AYM_VOLUME_TEXT1 + row, text );

    /* Mono: no panorama */
    EnableWindow( pan_slider, stereo != SOUND_STEREO_AY_NONE );
    if( SendMessage( pan_slider, TBM_GETPOS, 0, 0 ) != pan + 100 )
      SendMessage( pan_slider, TBM_SETPOS, TRUE, pan + 100 );
    if( stereo == SOUND_STEREO_AY_NONE ) text[0] = '\0';
    else pan_text( pan, text, sizeof( text ) );
    set_text( IDC_AYM_PAN_TEXT1 + row, text );
  }

  update_title();
  filling = 0;
}

static void
save( void )
{
  OPENFILENAMEA dialog;
  char filename[ 1100 ], filter[ 256 ];
  const char *type = i18n( "AY mixer settings (*.mix)" );
  size_t length;

  aymix_default_path( ayplayer_is_open() ? ayplayer_path() : NULL, filename,
                      sizeof( filename ) );

  /* "type\0*.mix\0\0" */
  length = strlen( type );
  if( length > sizeof( filter ) - 8 ) length = sizeof( filter ) - 8;
  memcpy( filter, type, length );
  memcpy( filter + length, "\0*.mix\0\0", 8 );

  memset( &dialog, 0, sizeof( dialog ) );
  dialog.lStructSize = sizeof( dialog );
  dialog.hwndOwner = fuse_hAYMixWnd;
  dialog.lpstrFilter = filter;
  dialog.lpstrFile = filename;
  dialog.nMaxFile = sizeof( filename );
  dialog.lpstrTitle = i18n( "Save AY mixer settings" );
  dialog.lpstrDefExt = "mix";
  dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
                 OFN_NOCHANGEDIR;

  if( GetSaveFileNameA( &dialog ) ) aymix_save( filename );
}

static void
remember_position( void )
{
  RECT rect;

  if( !fuse_hAYMixWnd || IsIconic( fuse_hAYMixWnd ) ||
      !GetWindowRect( fuse_hAYMixWnd, &rect ) )
    return;
  winstate_set( "aymixer", "x", rect.left );
  winstate_set( "aymixer", "y", rect.top );
}

static void
restore_position( void )
{
  POINT title;
  RECT player;
  long x, y;

  if( winstate_get( "aymixer", "x", &x ) &&
      winstate_get( "aymixer", "y", &y ) ) {
    /* Only if the title bar is on a screen (one may have been removed) */
    title.x = x + 40;
    title.y = y + 8;
    if( MonitorFromPoint( title, MONITOR_DEFAULTTONULL ) ) {
      SetWindowPos( fuse_hAYMixWnd, NULL, x, y, 0, 0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
      return;
    }
  }

  /* The first time: under the playlist window, or beside the AY player */
  if( fuse_hAYListWnd && IsWindowVisible( fuse_hAYListWnd ) &&
      GetWindowRect( fuse_hAYListWnd, &player ) )
    SetWindowPos( fuse_hAYMixWnd, NULL, player.left, player.bottom + 4, 0, 0,
                  SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
  else if( fuse_hAYWnd && GetWindowRect( fuse_hAYWnd, &player ) )
    SetWindowPos( fuse_hAYMixWnd, NULL, player.right + 4, player.top, 0, 0,
                  SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
}

void
win32_aymixer_close( void )
{
  close_window();
}

static void
close_window( void )
{
  if( !fuse_hAYMixWnd ) return;
  remember_position();
  DestroyWindow( fuse_hAYMixWnd );
  fuse_hAYMixWnd = NULL;
}

static void
scroll( HWND slider, WORD code )
{
  int row, position = SendMessage( slider, TBM_GETPOS, 0, 0 );

  (void)code;
  for( row = 0; row < 3; row++ ) {
    if( slider == GetDlgItem( fuse_hAYMixWnd, IDC_AYM_VOLUME1 + row ) ) {
      aymix_set_volume( order[ row ], position );
      return;
    }
    if( slider == GetDlgItem( fuse_hAYMixWnd, IDC_AYM_PAN1 + row ) ) {
      int pan = position - 100;
      /* The middle is easy to find */
      if( pan > -3 && pan < 3 ) pan = 0;
      aymix_set_pan( order[ row ], pan );
      return;
    }
  }
}

static void
command( WORD id, WORD code )
{
  int row;

  if( filling ) return;

  switch( id ) {

  case IDC_AYM_STEREO:
    if( code == CBN_SELCHANGE ) {
      int i = SendDlgItemMessage( fuse_hAYMixWnd, IDC_AYM_STEREO, CB_GETCURSEL,
                                  0, 0 );
      if( i >= 0 && i < STEREO_COUNT ) aymix_set_stereo( stereo_names[i] );
    }
    return;

  case IDC_AYM_RESET:
    if( code == BN_CLICKED ) aymix_reset();
    return;

  case IDC_AYM_SAVE:
    if( code == BN_CLICKED ) save();
    return;

  case IDCLOSE:
  case IDCANCEL:
    close_window();
    return;
  }

  for( row = 0; row < 3; row++ )
    if( id == IDC_AYM_PLACE1 + row && code == CBN_SELCHANGE ) {
      int place = SendDlgItemMessage( fuse_hAYMixWnd, id, CB_GETCURSEL, 0, 0 );
      aymix_set_place( order[ row ], place );
      return;
    }
}

static INT_PTR CALLBACK
aymixer_proc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
  int i, row;

  switch( msg ) {

  case WM_INITDIALOG:
    fuse_hAYMixWnd = hWnd;
    for( i = 0; i < STEREO_COUNT; i++ )
      SendDlgItemMessageA( hWnd, IDC_AYM_STEREO, CB_ADDSTRING, 0,
                           (LPARAM)i18n( stereo_names[ i ] ) );
    for( row = 0; row < 3; row++ ) {
      HWND pan = GetDlgItem( hWnd, IDC_AYM_PAN1 + row );

      SendDlgItemMessageA( hWnd, IDC_AYM_PLACE1 + row, CB_ADDSTRING, 0,
                           (LPARAM)i18n( "Left" ) );
      SendDlgItemMessageA( hWnd, IDC_AYM_PLACE1 + row, CB_ADDSTRING, 0,
                           (LPARAM)i18n( "Centre" ) );
      SendDlgItemMessageA( hWnd, IDC_AYM_PLACE1 + row, CB_ADDSTRING, 0,
                           (LPARAM)i18n( "Right" ) );

      SendDlgItemMessage( hWnd, IDC_AYM_VOLUME1 + row, TBM_SETRANGE, FALSE,
                          MAKELPARAM( 0, 100 ) );
      SendDlgItemMessage( hWnd, IDC_AYM_VOLUME1 + row, TBM_SETPAGESIZE, 0,
                          10 );
      /* -100..100 as 0..200, with a mark in the middle */
      SendMessage( pan, TBM_SETRANGE, FALSE, MAKELPARAM( 0, 200 ) );
      SendMessage( pan, TBM_SETPAGESIZE, 0, 20 );
      SendMessage( pan, TBM_CLEARTICS, FALSE, 0 );
      SendMessage( pan, TBM_SETTIC, 0, 100 );
    }
    return TRUE;

  case WM_HSCROLL:
    if( !filling ) scroll( (HWND)lParam, LOWORD( wParam ) );
    return 0;

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

void
win32_aymixer_show( void )
{
  aymix_set_changed_hook( win32_aymix_changed );

  if( !fuse_hAYMixWnd ) {
    if( !CreateDialog( fuse_hInstance, MAKEINTRESOURCE( IDD_AYMIXER ),
                       fuse_hWnd, (DLGPROC)aymixer_proc ) ) {
      win32_verror( 1 );
      return;
    }
    restore_position();
  }

  win32_aymixer_update();
  ShowWindow( fuse_hAYMixWnd, SW_SHOW );
  SetForegroundWindow( fuse_hAYMixWnd );
}

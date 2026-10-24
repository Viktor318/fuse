/* ayplayer.c: Win32 AY player window
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
#include <windows.h>
#include <commctrl.h> /* windows.h must be included prior to commctrl.h */

#include "ayplayer.h"
#include "../../ayplayer.h"
#include "fuse.h"
#include "i18n.h"
#include "menu.h"
#include "win32internals.h"

HWND fuse_hAYWnd = NULL;

#define AYPLAYER_TIMER 1
#define AYPLAYER_TIMER_MS 200

/* Pausing works as Machine > Pause (the emulation really stops) */
static int
player_paused( void )
{
  return win32ui_machine_paused();
}

static void
set_text( int id, const char *text )
{
  SetDlgItemTextA( fuse_hAYWnd, id, text );
}

static void
format_time( char *text, size_t length, const char *label, long frames )
{
  long seconds = frames / 50;
  _snprintf( text, length, i18n( label ), (int)( seconds / 60 ),
             (int)( seconds % 60 ) );
  text[ length - 1 ] = '\0';
}

/* Pause button: Pause / Resume / Play */
static void
update_pause_button( void )
{
  const char *label;

  if( ayplayer_current_song() < 0 ) label = "&Play";
  else if( player_paused() ) label = "&Resume";
  else label = "&Pause";

  set_text( IDC_AY_PAUSE, i18n( label ) );
}

static void
update_time( void )
{
  char text[ 128 ];
  long played = ayplayer_frames_played();
  long length = ayplayer_song_frames();

  if( ayplayer_current_song() < 0 ) played = 0;

  format_time( text, sizeof( text ), "Elapsed: %d:%02d", played );
  set_text( IDC_AY_ELAPSED, text );

  if( length ) {
    format_time( text, sizeof( text ), "Length: %d:%02d", length );
    SendDlgItemMessage( fuse_hAYWnd, IDC_AY_PROGRESS, PBM_SETRANGE32, 0,
                        length );
    SendDlgItemMessage( fuse_hAYWnd, IDC_AY_PROGRESS, PBM_SETPOS,
                        played < length ? played : length, 0 );
  } else {
    _snprintf( text, sizeof( text ), "%s", i18n( "Length: unknown" ) );
    text[ sizeof( text ) - 1 ] = '\0';
    SendDlgItemMessage( fuse_hAYWnd, IDC_AY_PROGRESS, PBM_SETRANGE32, 0, 1 );
    SendDlgItemMessage( fuse_hAYWnd, IDC_AY_PROGRESS, PBM_SETPOS, 0, 0 );
  }
  set_text( IDC_AY_LENGTH, text );
}

/* Everything shown, after a new file or song */
static void
update_all( void )
{
  int i, count = ayplayer_song_count();
  int current = ayplayer_current_song();
  char text[ 512 ], name[ 256 ];

  set_text( IDC_AY_FILE, ayplayer_filename() );
  set_text( IDC_AY_AUTHOR, ayplayer_author() );
  set_text( IDC_AY_MISC, ayplayer_misc() );

  SendDlgItemMessage( fuse_hAYWnd, IDC_AY_SONG, CB_RESETCONTENT, 0, 0 );
  for( i = 0; i < count; i++ ) {
    ayplayer_song_name( i, name, sizeof( name ) );
    _snprintf( text, sizeof( text ), "%d / %d - %s", i + 1, count, name );
    text[ sizeof( text ) - 1 ] = '\0';
    SendDlgItemMessageA( fuse_hAYWnd, IDC_AY_SONG, CB_ADDSTRING, 0,
                         (LPARAM)text );
  }
  if( current >= 0 )
    SendDlgItemMessage( fuse_hAYWnd, IDC_AY_SONG, CB_SETCURSEL, current, 0 );

  update_pause_button();
  update_time();
}

static void
resume_if_paused( void )
{
  if( player_paused() ) menu_machine_pause( 0 );
}

static void
play( int song )
{
  resume_if_paused();
  ayplayer_play( song );
  update_all();
}

static void
close_window( void )
{
  if( !fuse_hAYWnd ) return;

  KillTimer( fuse_hAYWnd, AYPLAYER_TIMER );
  ayplayer_set_frame_hook( NULL );
  resume_if_paused();
  DestroyWindow( fuse_hAYWnd );
  fuse_hAYWnd = NULL;

  ayplayer_close();
}

static void
command( WORD id, WORD code )
{
  int current = ayplayer_current_song();
  int count = ayplayer_song_count();

  switch( id ) {

  case IDC_AY_SONG:
    if( code == CBN_SELCHANGE ) {
      int song = SendDlgItemMessage( fuse_hAYWnd, IDC_AY_SONG, CB_GETCURSEL,
                                     0, 0 );
      if( song >= 0 ) play( song );
    }
    break;

  case IDC_AY_PREV:
    if( current > 0 ) play( current - 1 );
    else if( current < 0 ) play( 0 );
    break;

  case IDC_AY_NEXT:
    if( current < 0 ) play( 0 );
    else if( current + 1 < count ) play( current + 1 );
    break;

  case IDC_AY_PAUSE:
    if( current < 0 ) {
      int song = SendDlgItemMessage( fuse_hAYWnd, IDC_AY_SONG, CB_GETCURSEL,
                                     0, 0 );
      play( song >= 0 ? song : 0 );
    } else {
      /* Pause or resume. When pausing, this returns only after resuming,
         so show the new label first */
      if( !player_paused() ) set_text( IDC_AY_PAUSE, i18n( "&Resume" ) );
      menu_machine_pause( 0 );
    }
    update_pause_button();
    break;

  case IDC_AY_STOP:
    resume_if_paused();
    ayplayer_stop();
    update_pause_button();
    update_time();
    break;

  case IDCLOSE:
  case IDCANCEL:
    close_window();
    break;
  }
}

/* Called by the emulation every frame: update the window every 10 frames
   and go to the next song at the end. Does not depend on WM_TIMER, which
   Windows does not always send while the emulation keeps the message
   queue busy */
#define WM_AYPLAYER_SONG_END ( WM_APP + 1 )

static int song_end_posted = 0;

static void
frame_update( void )
{
  static int counter = 0;
  int current;

  if( !fuse_hAYWnd || ++counter < 10 ) return;
  counter = 0;

  current = ayplayer_current_song();
  if( current >= 0 && ayplayer_song_ended() && !song_end_posted &&
      IsDlgButtonChecked( fuse_hAYWnd, IDC_AY_AUTO ) == BST_CHECKED ) {
    /* Not here, in the middle of a frame: the window does it */
    song_end_posted = 1;
    PostMessage( fuse_hAYWnd, WM_AYPLAYER_SONG_END, 0, 0 );
  }
  update_time();
}

/* End of the song: next one, or stop after the last one */
static void
song_end( void )
{
  int current = ayplayer_current_song();

  song_end_posted = 0;
  if( current < 0 || !ayplayer_song_ended() ) return;

  if( current + 1 < ayplayer_song_count() ) {
    play( current + 1 );
  } else {
    ayplayer_stop();
    update_pause_button();
    update_time();
  }
}

static INT_PTR CALLBACK
ayplayer_proc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
  switch( msg ) {

  case WM_INITDIALOG:
    CheckDlgButton( hWnd, IDC_AY_AUTO, BST_CHECKED );
    SetTimer( hWnd, AYPLAYER_TIMER, AYPLAYER_TIMER_MS, NULL );
    return TRUE;

  case WM_TIMER:
    /* Only for the paused state; while playing the frames update it */
    if( wParam == AYPLAYER_TIMER ) {
      update_pause_button();
      return 0;
    }
    break;

  case WM_AYPLAYER_SONG_END:
    song_end();
    return 0;

  case WM_COMMAND:
    command( LOWORD( wParam ), HIWORD( wParam ) );
    return 0;

  case WM_CLOSE:
    close_window();
    return 0;
  }

  return FALSE;
}

/* The file was opened: show the window (or update it) */
void
win32_ayplayer_show( void )
{
  if( !fuse_hAYWnd ) {
    fuse_hAYWnd = CreateDialog( fuse_hInstance, MAKEINTRESOURCE( IDD_AYPLAYER ),
                                fuse_hWnd, (DLGPROC)ayplayer_proc );
    if( !fuse_hAYWnd ) {
      win32_verror( 1 );
      return;
    }
  }

  ayplayer_set_frame_hook( frame_update );
  resume_if_paused();
  update_all();
  ShowWindow( fuse_hAYWnd, SW_SHOW );
  SetForegroundWindow( fuse_hAYWnd );
}

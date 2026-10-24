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
#include "win32internals.h"

HWND fuse_hAYWnd = NULL;

#define AYPLAYER_TIMER 1
#define AYPLAYER_TIMER_MS 200

/* The player paused the emulation (Pause button) */
static int paused_by_player = 0;

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
  else if( paused_by_player ) label = "&Resume";
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
  if( paused_by_player ) {
    paused_by_player = 0;
    fuse_emulation_unpause();
  }
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
    } else if( paused_by_player ) {
      resume_if_paused();
    } else {
      paused_by_player = 1;
      fuse_emulation_pause();
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

static INT_PTR CALLBACK
ayplayer_proc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
  switch( msg ) {

  case WM_INITDIALOG:
    CheckDlgButton( hWnd, IDC_AY_AUTO, BST_CHECKED );
    SetTimer( hWnd, AYPLAYER_TIMER, AYPLAYER_TIMER_MS, NULL );
    return TRUE;

  case WM_TIMER:
    if( wParam == AYPLAYER_TIMER ) {
      int current = ayplayer_current_song();

      /* End of the song: next one, or stop after the last one */
      if( current >= 0 && ayplayer_song_ended() &&
          IsDlgButtonChecked( hWnd, IDC_AY_AUTO ) == BST_CHECKED ) {
        if( current + 1 < ayplayer_song_count() ) {
          play( current + 1 );
        } else {
          ayplayer_stop();
          update_pause_button();
        }
      }
      update_time();
      return 0;
    }
    break;

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

  resume_if_paused();
  update_all();
  ShowWindow( fuse_hAYWnd, SW_SHOW );
  SetForegroundWindow( fuse_hAYWnd );
}

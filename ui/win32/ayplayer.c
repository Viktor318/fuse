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
#include <string.h>
#include <windows.h>
#include <commctrl.h> /* windows.h must be included prior to commctrl.h */

#include "ayplayer.h"
#include "../../ayplayer.h"
#include "fuse.h"
#include "i18n.h"
#include "machine.h"
#include "menu.h"
#include "options.h"
#include "settings.h"
#include "sound.h"
#include "sound/ay_engine.h"
#include "win32internals.h"

HWND fuse_hAYWnd = NULL;

#define AYPLAYER_TIMER 1
#define AYPLAYER_TIMER_MS 200

/* The position slider is being moved by the user: do not move it */
static int seek_dragging = 0;
/* Length shown on the position slider */
static long slider_length = -1;

/* Level meters: the shown level of each channel, in 1/4 steps */
#define METER_STEPS 15
static int meter_shown[ 3 ];
/* The channel shown in each row */
static int meter_order[ 3 ] = { 0, 1, 2 };

/* The stereo modes in the order of the combo box (and of the options) */
static const char * const stereo_names[] = { "None", "ACB", "ABC", "BAC" };
#define STEREO_COUNT 4

/* The window shown while the length of a song is measured */
static HWND scan_window = NULL;
static void scan_window_close( void );

static int shown_volume = -1;
static int shown_stereo = -1;

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
  HWND slider = GetDlgItem( fuse_hAYWnd, IDC_AY_PROGRESS );
  long played = ayplayer_frames_played();
  long length = ayplayer_song_frames();
  int current = ayplayer_current_song();

  if( current < 0 ) played = 0;
  if( ayplayer_seeking() ) played = ayplayer_seek_target();
  if( seek_dragging ) played = SendMessage( slider, TBM_GETPOS, 0, 0 );
  if( length && played > length ) played = length;

  format_time( text, sizeof( text ), "Elapsed: %d:%02d", played );
  set_text( IDC_AY_ELAPSED, text );

  if( length != slider_length ) {
    slider_length = length;
    SendMessage( slider, TBM_SETRANGEMIN, FALSE, 0 );
    SendMessage( slider, TBM_SETRANGEMAX, TRUE, length ? length : 1 );
    SendMessage( slider, TBM_SETPAGESIZE, 0, 50 * 10 );
    SendMessage( slider, TBM_SETLINESIZE, 0, 50 * 2 );
  }
  EnableWindow( slider, length && current >= 0 );
  if( !seek_dragging ) SendMessage( slider, TBM_SETPOS, TRUE, played );

  if( length && ayplayer_length_is_measured() ) {
    format_time( text, sizeof( text ), "Length: %d:%02d (measured)", length );
  } else if( length && ayplayer_length_is_default() ) {
    format_time( text, sizeof( text ), "Length: %d:%02d (default)", length );
  } else if( length ) {
    format_time( text, sizeof( text ), "Length: %d:%02d", length );
  } else {
    _snprintf( text, sizeof( text ), "%s", i18n( "Length: unknown" ) );
    text[ sizeof( text ) - 1 ] = '\0';
  }
  set_text( IDC_AY_LENGTH, text );
}

/* ------------------------------------------------------------------------
   Level meters
   ------------------------------------------------------------------------ */

static void
update_level_labels( void )
{
  static const char * const sides[] = { "left", "middle", "right" };
  int stereo = option_enumerate_sound_stereo_ay();
  char text[ 64 ];
  int row;

  ay_engine_stereo_order( stereo, meter_order );
  for( row = 0; row < 3; row++ ) {
    if( stereo == SOUND_STEREO_AY_NONE )
      _snprintf( text, sizeof( text ), "%c", 'A' + meter_order[ row ] );
    else
      _snprintf( text, sizeof( text ), "%c  %s", 'A' + meter_order[ row ],
                 i18n( sides[ row ] ) );
    text[ sizeof( text ) - 1 ] = '\0';
    set_text( IDC_AY_LEVEL_LABEL1 + row, text );
    /* Ticked: heard */
    CheckDlgButton( fuse_hAYWnd, IDC_AY_LEVEL_LABEL1 + row,
                    ay_engine_channel_muted( meter_order[ row ] ) ?
                    BST_UNCHECKED : BST_CHECKED );
    InvalidateRect( GetDlgItem( fuse_hAYWnd, IDC_AY_LEVEL1 + row ), NULL,
                    FALSE );
  }
}

/* The level of a channel. Without sound output the sound generator does
   not run: then the volume registers (envelope: full) */
static int
channel_level( int channel )
{
  int volume, mixer;

  if( sound_enabled ) return ay_engine_channel_peak( channel );

  volume = machine_current->ay.registers[ 8 + channel ];
  mixer = machine_current->ay.registers[ 7 ];
  if( ( ( mixer >> channel ) & 9 ) == 9 ) return 0;
  return volume & 16 ? 15 : volume & 15;
}

/* Fast up, slowly down */
static void
update_meters( int clear )
{
  int channel, row;

  for( channel = 0; channel < 3; channel++ ) {
    int target = clear ? 0 : channel_level( channel ) * 4;
    int old = meter_shown[ channel ], shown;

    if( target >= old ) shown = target;
    else shown = old - 3 > target ? old - 3 : target;
    meter_shown[ channel ] = shown;

    if( ( shown + 3 ) / 4 != ( old + 3 ) / 4 ) {
      for( row = 0; row < 3; row++ )
        if( meter_order[ row ] == channel )
          InvalidateRect( GetDlgItem( fuse_hAYWnd, IDC_AY_LEVEL1 + row ),
                          NULL, FALSE );
    }
  }
}

static void
draw_meter( const DRAWITEMSTRUCT *item )
{
  int row = item->CtlID - IDC_AY_LEVEL1;
  int level = ( meter_shown[ meter_order[ row ] ] + 3 ) / 4;
  int muted = ay_engine_channel_muted( meter_order[ row ] );
  RECT area = item->rcItem, segment;
  int width = area.right - area.left, height = area.bottom - area.top;
  int gap = 2, border = 2, i;
  HDC dc;
  HBITMAP bitmap, old_bitmap;
  HBRUSH brush;

  dc = CreateCompatibleDC( item->hDC );
  bitmap = CreateCompatibleBitmap( item->hDC, width, height );
  old_bitmap = SelectObject( dc, bitmap );

  segment.left = 0; segment.top = 0;
  segment.right = width; segment.bottom = height;
  brush = CreateSolidBrush( RGB( 17, 22, 26 ) );
  FillRect( dc, &segment, brush );
  DeleteObject( brush );

  for( i = 0; i < METER_STEPS; i++ ) {
    COLORREF colour;
    int inner = width - 2 * border - ( METER_STEPS - 1 ) * gap;

    segment.left = border + i * inner / METER_STEPS + i * gap;
    segment.right = border + ( i + 1 ) * inner / METER_STEPS + i * gap;
    segment.top = border;
    segment.bottom = height - border;

    if( i >= level ) colour = RGB( 40, 46, 52 );
    else if( muted ) colour = RGB( 110, 116, 122 );
    else if( i < 10 ) colour = RGB( 45, 184, 77 );
    else if( i < 13 ) colour = RGB( 224, 192, 32 );
    else colour = RGB( 224, 64, 48 );

    brush = CreateSolidBrush( colour );
    FillRect( dc, &segment, brush );
    DeleteObject( brush );
  }

  BitBlt( item->hDC, area.left, area.top, width, height, dc, 0, 0, SRCCOPY );
  SelectObject( dc, old_bitmap );
  DeleteObject( bitmap );
  DeleteDC( dc );
}

/* ------------------------------------------------------------------------
   Volume and stereo
   ------------------------------------------------------------------------ */

static void
show_volume( void )
{
  char text[ 16 ];
  int volume = settings_current.volume_ay;

  if( volume < 0 ) volume = 0;
  if( volume > 100 ) volume = 100;
  shown_volume = settings_current.volume_ay;

  SendDlgItemMessage( fuse_hAYWnd, IDC_AY_VOLUME, TBM_SETPOS, TRUE, volume );
  _snprintf( text, sizeof( text ), "%d%%", volume );
  text[ sizeof( text ) - 1 ] = '\0';
  set_text( IDC_AY_VOLUME_TEXT, text );
}

static void
volume_changed( void )
{
  char text[ 16 ];
  int volume = SendDlgItemMessage( fuse_hAYWnd, IDC_AY_VOLUME, TBM_GETPOS,
                                   0, 0 );

  if( volume == settings_current.volume_ay ) return;
  settings_current.volume_ay = volume;
  shown_volume = volume;
  sound_ay_volume_changed();

  _snprintf( text, sizeof( text ), "%d%%", volume );
  text[ sizeof( text ) - 1 ] = '\0';
  set_text( IDC_AY_VOLUME_TEXT, text );
}

static void
show_stereo( void )
{
  shown_stereo = option_enumerate_sound_stereo_ay();
  SendDlgItemMessage( fuse_hAYWnd, IDC_AY_STEREO, CB_SETCURSEL, shown_stereo,
                      0 );
  update_level_labels();
}

static void
stereo_changed( void )
{
  int stereo = SendDlgItemMessage( fuse_hAYWnd, IDC_AY_STEREO, CB_GETCURSEL,
                                   0, 0 );

  if( stereo < 0 || stereo >= STEREO_COUNT ) return;
  settings_set_string( &settings_current.stereo_ay, stereo_names[ stereo ] );
  sound_ay_stereo_changed();
  shown_stereo = stereo;
  update_level_labels();
}

/* Changed in the options meanwhile? */
static void
sync_sound_settings( void )
{
  if( settings_current.volume_ay != shown_volume ) show_volume();
  if( option_enumerate_sound_stereo_ay() != shown_stereo ) show_stereo();
}

/* ------------------------------------------------------------------------
   Seeking with the position slider
   ------------------------------------------------------------------------ */

/* A click beside the thumb: go to where the click was, not a page away */
static void
slider_to_mouse( HWND slider )
{
  POINT point;
  RECT channel;
  long maximum = SendMessage( slider, TBM_GETRANGEMAX, 0, 0 );
  long position;

  GetCursorPos( &point );
  ScreenToClient( slider, &point );
  SendMessage( slider, TBM_GETCHANNELRECT, 0, (LPARAM)&channel );
  if( channel.right <= channel.left ) return;

  position = (long)( (double)( point.x - channel.left ) * maximum /
                     ( channel.right - channel.left ) + 0.5 );
  if( position < 0 ) position = 0;
  if( position > maximum ) position = maximum;
  SendMessage( slider, TBM_SETPOS, TRUE, position );
}

static void
slider_scroll( HWND slider, WORD code )
{
  switch( code ) {
  case TB_PAGEUP:
  case TB_PAGEDOWN:
    slider_to_mouse( slider );
    seek_dragging = 1;
    break;

  case TB_THUMBTRACK:
  case TB_THUMBPOSITION:
  case TB_LINEUP:
  case TB_LINEDOWN:
  case TB_TOP:
  case TB_BOTTOM:
    seek_dragging = 1;
    break;

  case TB_ENDTRACK:
    if( seek_dragging ) {
      seek_dragging = 0;
      ayplayer_seek( SendMessage( slider, TBM_GETPOS, 0, 0 ) );
    }
    break;
  }
  update_time();
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
  sync_sound_settings();
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
  scan_window_close();
  /* Games must not stay without a channel */
  ay_engine_set_channel_muted( 0, 0 );
  ay_engine_set_channel_muted( 1, 0 );
  ay_engine_set_channel_muted( 2, 0 );
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

  case IDC_AY_LEVEL_LABEL1:
  case IDC_AY_LEVEL_LABEL2:
  case IDC_AY_LEVEL_LABEL3:
    if( code == BN_CLICKED ) {
      int row = id - IDC_AY_LEVEL_LABEL1;
      ay_engine_set_channel_muted( meter_order[ row ],
        IsDlgButtonChecked( fuse_hAYWnd, id ) != BST_CHECKED );
      InvalidateRect( GetDlgItem( fuse_hAYWnd, IDC_AY_LEVEL1 + row ), NULL,
                      FALSE );
    }
    break;

  case IDC_AY_AUTO:
    ayplayer_set_fade_enabled(
      IsDlgButtonChecked( fuse_hAYWnd, IDC_AY_AUTO ) == BST_CHECKED );
    break;

  case IDC_AY_STEREO:
    if( code == CBN_SELCHANGE ) stereo_changed();
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

/* ------------------------------------------------------------------------
   Measuring the length of a song
   ------------------------------------------------------------------------ */

#define WM_AYPLAYER_SCAN_DONE ( WM_APP + 2 )

static INT_PTR CALLBACK
scan_proc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
  switch( msg ) {
  case WM_INITDIALOG:
    SendDlgItemMessage( hWnd, IDC_AYSCAN_PROGRESS, PBM_SETRANGE32, 0, 100 );
    return TRUE;

  case WM_COMMAND:
    if( LOWORD( wParam ) == IDCANCEL ) {
      ayplayer_scan_cancel();
      return TRUE;
    }
    break;

  case WM_CLOSE:
    ayplayer_scan_cancel();
    return TRUE;
  }
  return FALSE;
}

static void
scan_window_close( void )
{
  if( !scan_window ) return;
  DestroyWindow( scan_window );
  scan_window = NULL;
  EnableWindow( fuse_hAYWnd, TRUE );
}

/* Called every frame while measuring, and once when it is over */
static void
scan_update( void )
{
  static int counter = 0;

  if( ayplayer_scanning() ) {
    if( !scan_window ) {
      char text[ 512 ], name[ 256 ];
      int current = ayplayer_current_song();

      scan_window = CreateDialog( fuse_hInstance, MAKEINTRESOURCE( IDD_AYSCAN ),
                                  fuse_hAYWnd, (DLGPROC)scan_proc );
      if( !scan_window ) return;
      ayplayer_song_name( current, name, sizeof( name ) );
      _snprintf( text, sizeof( text ), "%d / %d - %s", current + 1,
                 ayplayer_song_count(), name );
      text[ sizeof( text ) - 1 ] = '\0';
      SetDlgItemTextA( scan_window, IDC_AYSCAN_SONG, text );
      EnableWindow( fuse_hAYWnd, FALSE );
      ShowWindow( scan_window, SW_SHOW );
      counter = 0;
    }
    if( ++counter >= 25 ) {
      counter = 0;
      SendDlgItemMessage( scan_window, IDC_AYSCAN_PROGRESS, PBM_SETPOS,
                          ayplayer_scan_progress(), 0 );
    }
    return;
  }

  /* Over: play the song from the start, with its length */
  if( ayplayer_scan_take_finished() ) {
    SendDlgItemMessage( scan_window, IDC_AYSCAN_PROGRESS, PBM_SETPOS, 100, 0 );
    PostMessage( fuse_hAYWnd, WM_AYPLAYER_SCAN_DONE, 0, 0 );
  }
}

static void
frame_update( void )
{
  static int counter = 0, meter_counter = 0;
  int current;

  if( !fuse_hAYWnd ) return;

  if( ayplayer_scanning() || scan_window ) {
    scan_update();
    return;
  }

  if( ++meter_counter >= 2 ) {
    meter_counter = 0;
    update_meters( 0 );
  }

  if( ++counter < 10 ) return;
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
    {
      int i;

      fuse_hAYWnd = hWnd;
      CheckDlgButton( hWnd, IDC_AY_AUTO, BST_CHECKED );
      ayplayer_set_fade_enabled( 1 );
      SendDlgItemMessage( hWnd, IDC_AY_VOLUME, TBM_SETRANGE, FALSE,
                          MAKELPARAM( 0, 100 ) );
      SendDlgItemMessage( hWnd, IDC_AY_VOLUME, TBM_SETPAGESIZE, 0, 10 );
      for( i = 0; i < STEREO_COUNT; i++ )
        SendDlgItemMessageA( hWnd, IDC_AY_STEREO, CB_ADDSTRING, 0,
                             (LPARAM)i18n( stereo_names[ i ] ) );
      slider_length = -1;
      seek_dragging = 0;
      memset( meter_shown, 0, sizeof( meter_shown ) );
      show_volume();
      show_stereo();
      SetTimer( hWnd, AYPLAYER_TIMER, AYPLAYER_TIMER_MS, NULL );
    }
    return TRUE;

  case WM_TIMER:
    /* Only for the paused state; while playing the frames update it */
    if( wParam == AYPLAYER_TIMER ) {
      update_pause_button();
      sync_sound_settings();
      if( player_paused() || ayplayer_current_song() < 0 )
        update_meters( 1 );
      return 0;
    }
    break;

  case WM_HSCROLL:
    if( (HWND)lParam == GetDlgItem( hWnd, IDC_AY_PROGRESS ) ) {
      slider_scroll( (HWND)lParam, LOWORD( wParam ) );
      return 0;
    }
    if( (HWND)lParam == GetDlgItem( hWnd, IDC_AY_VOLUME ) ) {
      volume_changed();
      return 0;
    }
    break;

  case WM_DRAWITEM:
    if( wParam >= IDC_AY_LEVEL1 && wParam <= IDC_AY_LEVEL3 ) {
      draw_meter( (const DRAWITEMSTRUCT *)lParam );
      return TRUE;
    }
    break;

  /* Moving the window: the music goes on */
  case WM_ENTERSIZEMOVE:
  case WM_ENTERMENULOOP:
    win32ui_modal_enter();
    return 0;

  case WM_EXITSIZEMOVE:
  case WM_EXITMENULOOP:
    win32ui_modal_exit();
    return 0;

  case WM_AYPLAYER_SONG_END:
    song_end();
    return 0;

  case WM_AYPLAYER_SCAN_DONE:
    scan_window_close();
    if( ayplayer_current_song() >= 0 ) {
      ayplayer_play( ayplayer_current_song() );
      update_all();
    }
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

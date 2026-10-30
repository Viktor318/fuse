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
#include "../../ayplay.h"
#include "../../ayplayer.h"
#include "../../aymix.h"
#include "../../ayplaylist.h"
#include "fuse.h"
#include "i18n.h"
#include "machine.h"
#include "menu.h"
#include "options.h"
#include "settings.h"
#include "sound.h"
#include "sound/ay_engine.h"
#include "win32internals.h"
#include "winstate.h"

HWND fuse_hAYWnd = NULL;

#define AYPLAYER_TIMER 1
#define AYPLAYER_TIMER_MS 40

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
static const char * const stereo_names[] =
  { "ABC", "ACB", "BAC", "BCA", "CBA", "CAB", "Mono", "Custom" };
static const int stereo_modes[] = {
  SOUND_STEREO_AY_ABC, SOUND_STEREO_AY_ACB, SOUND_STEREO_AY_BAC,
  SOUND_STEREO_AY_BCA, SOUND_STEREO_AY_CBA, SOUND_STEREO_AY_CAB,
  SOUND_STEREO_AY_NONE, SOUND_STEREO_AY_CUSTOM
};
#define STEREO_COUNT 8

/* The length of a song is being measured (the status line shows it) */
static HWND scan_window = NULL;
static void scan_window_close( void );
static void end_options_changed( void );
static void remember_position( void );

static HFONT song_font = NULL;

static int shown_volume = -1;
static int shown_stereo = -1;
/* The places of the channels shown in the level meter labels */
static int shown_positions[3] = { -1, -1, -1 };
/* Volumes of the channels A, B and C the meters were drawn with */
static int shown_channel_volume[3] = { -1, -1, -1 };

/* Pausing stops the music only: the emulated machine goes on */
static int
player_paused( void )
{
  return ayplayer_paused();
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

  /* Nothing chosen: nothing shown */
  if( ayplaylist_current() < 0 && current < 0 ) {
    set_text( IDC_AY_ELAPSED, "" );
    set_text( IDC_AY_LENGTH, "" );
  }
}

/* ------------------------------------------------------------------------
   Level meters
   ------------------------------------------------------------------------ */

static void
update_level_labels( void )
{
  static const char * const sides[] = { "left", "middle", "right" };
  int stereo = sound_ay_stereo_setting();
  int position[ 3 ];
  char text[ 64 ];
  int row;

  ay_engine_stereo_order( stereo, meter_order );
  sound_ay_channel_positions( stereo, position );
  ayplay_update_mix();
  memcpy( shown_positions, position, sizeof( position ) );
  for( row = 0; row < 3; row++ ) {
    if( stereo == SOUND_STEREO_AY_NONE )
      _snprintf( text, sizeof( text ), "%c", 'A' + meter_order[ row ] );
    else
      _snprintf( text, sizeof( text ), "%c  %s", 'A' + meter_order[ row ],
                 i18n( sides[ position[ meter_order[ row ] ] ] ) );
    text[ sizeof( text ) - 1 ] = '\0';
    set_text( IDC_AY_LEVEL_LABEL1 + row, text );
    /* Ticked: heard */
    CheckDlgButton( fuse_hAYWnd, IDC_AY_LEVEL_LABEL1 + row,
                    ayplay_muted( meter_order[ row ] ) ?
                    BST_UNCHECKED : BST_CHECKED );
    InvalidateRect( GetDlgItem( fuse_hAYWnd, IDC_AY_LEVEL1 + row ), NULL,
                    FALSE );
  }
}

/* The level of a channel of the music, after its volume */
static int
channel_level( int channel )
{
  if( ayplayer_current_song() < 0 ) return 0;
  return ay_engine_channel_level_heard( channel,
                                        ayplay_channel_peak( channel ) );
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
  int muted = ayplay_muted( meter_order[ row ] );
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
  win32_options_sound_sync();

  _snprintf( text, sizeof( text ), "%d%%", volume );
  text[ sizeof( text ) - 1 ] = '\0';
  set_text( IDC_AY_VOLUME_TEXT, text );
}

static void
show_stereo( void )
{
  int i;

  shown_stereo = sound_ay_stereo_setting();
  for( i = 0; i < STEREO_COUNT; i++ )
    if( stereo_modes[i] == shown_stereo )
      SendDlgItemMessage( fuse_hAYWnd, IDC_AY_STEREO, CB_SETCURSEL, i, 0 );
  update_level_labels();
}

static void
stereo_changed( void )
{
  int stereo = SendDlgItemMessage( fuse_hAYWnd, IDC_AY_STEREO, CB_GETCURSEL,
                                   0, 0 );

  if( stereo < 0 || stereo >= STEREO_COUNT ) return;
  /* The AY mixer shows it too */
  aymix_set_stereo( stereo_names[ stereo ] );
}

/* Changed in the options or in the AY mixer meanwhile? */
static void
sync_sound_settings( void )
{
  int volume[3], channel;

  if( !fuse_hAYWnd ) return;
  if( settings_current.volume_ay != shown_volume ) show_volume();
  /* The meters show the levels after the volumes of the channels */
  volume[0] = settings_current.volume_ay_a;
  volume[1] = settings_current.volume_ay_b;
  volume[2] = settings_current.volume_ay_c;
  if( memcmp( volume, shown_channel_volume, sizeof( volume ) ) ) {
    memcpy( shown_channel_volume, volume, sizeof( volume ) );
    for( channel = 0; channel < 3; channel++ )
      InvalidateRect( GetDlgItem( fuse_hAYWnd, IDC_AY_LEVEL1 + channel ),
                      NULL, FALSE );
  }
  if( sound_ay_stereo_setting() != shown_stereo ) {
    show_stereo();
  } else {
    /* Custom: the places of the channels may have changed */
    int position[3];
    sound_ay_channel_positions( shown_stereo, position );
    if( memcmp( position, shown_positions, sizeof( position ) ) )
      update_level_labels();
  }
}

/* The AY mixer settings changed (in any window, or a .mix file was
   loaded): every window shows them */
void
win32_aymix_changed( void )
{
  sync_sound_settings();
  win32_aymixer_update();
  win32_options_sound_sync();
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
  int index = ayplaylist_current(), count = ayplaylist_count();
  char text[ 512 ], name[ 256 ];

  /* Nothing chosen: nothing shown */
  if( index < 0 && ayplayer_current_song() < 0 ) {
    set_text( IDC_AY_FILE, "" );
    set_text( IDC_AY_AUTHOR, "" );
    set_text( IDC_AY_MISC, "" );
  } else {
    set_text( IDC_AY_FILE, ayplayer_filename() );
    set_text( IDC_AY_AUTHOR, ayplayer_author() );
    set_text( IDC_AY_MISC, ayplayer_misc() );
  }

  /* The song being played: its place in the list and title */
  text[0] = '\0';
  if( index >= 0 ) {
    _snprintf( text, sizeof( text ), "%d / %d \x96 %s", index + 1, count,
               ayplaylist_get( index )->title );
  } else if( ayplayer_current_song() >= 0 ) {
    ayplayer_song_name( ayplayer_current_song(), name, sizeof( name ) );
    _snprintf( text, sizeof( text ), "%s", name );
  }
  text[ sizeof( text ) - 1 ] = '\0';
  set_text( IDC_AY_SONG, text );

  update_pause_button();
  update_time();
  sync_sound_settings();
  win32_ayplaylist_update();
}

/* Mixed into the sound of the emulator, the music needs the emulation:
   Machine > Pause is over */
static void
resume_if_paused( void )
{
  if( !ayplay_output_independent() && win32ui_machine_paused() )
    menu_machine_pause( 0 );
}

/* Measuring the lengths of the whole list (one song after the other, in
   the background; the music goes on meanwhile) */
static int measuring = 0;
/* Stop asked for: the song being measured is finished, then no more */
static int measure_stopping = 0;
static int measure_next_index = 0;

static void
play( int index )
{
  /* Chosen by the user: measuring (if any) is over */
  measuring = 0;
  measure_stopping = 0;
  resume_if_paused();
  ayplaylist_play( index );
  update_all();
}

void
win32_ayplayer_update( void )
{
  if( fuse_hAYWnd ) update_all();
}

void
win32_ayplayer_play_entry( int index )
{
  if( fuse_hAYWnd ) play( index );
}

/* The next song of the list whose length is not known yet; when there is
   none, measuring is over */
static void
measure_next( void )
{
  long frames;

  while( measuring && !measure_stopping &&
         measure_next_index < ayplaylist_count() ) {
    int index = measure_next_index++;
    const ayplaylist_entry *entry = ayplaylist_get( index );

    if( ayplaylist_length( index, &frames ) ) continue;
    if( !ayplayer_measure( entry->path, entry->song ) ) {
      win32_ayplaylist_update();
      return;
    }
  }

  measuring = 0;
  measure_stopping = 0;
  win32_ayplaylist_update();
}

void
win32_ayplayer_measure_all( void )
{
  if( !fuse_hAYWnd || measuring ) return;

  measuring = 1;
  measure_stopping = 0;
  measure_next_index = 0;
  /* The song playing is being measured: that first */
  if( !ayplayer_measuring() ) measure_next();
}

/* Stop measuring the lengths of the list: the song being measured is
   finished first, so its length is not lost */
void
win32_ayplayer_measure_stop( void )
{
  if( !measuring || measure_stopping ) return;
  measure_stopping = 1;
  win32_ayplaylist_update_status();
  /* Between two songs: over at once */
  if( !ayplayer_measuring() ) measure_next();
}

int
win32_ayplayer_measure_stopping( void )
{
  return measuring && measure_stopping;
}

int
win32_ayplayer_busy( void )
{
  return measuring;
}

static void
close_window( void )
{
  if( !fuse_hAYWnd ) return;

  KillTimer( fuse_hAYWnd, AYPLAYER_TIMER );
  remember_position();
  scan_window_close();
  ayplay_set_muted( 0, 0 );
  ayplay_set_muted( 1, 0 );
  ayplay_set_muted( 2, 0 );
  ayplayer_set_frame_hook( NULL );
  measuring = 0;
  win32_ayplaylist_close();
  DestroyWindow( fuse_hAYWnd );
  fuse_hAYWnd = NULL;
  if( song_font ) {
    DeleteObject( song_font );
    song_font = NULL;
  }

  ayplayer_close();
}

static void
command( WORD id, WORD code )
{
  int current = ayplaylist_current();
  int count = ayplaylist_count();
  int playing = ayplayer_current_song() >= 0;

  switch( id ) {

  case IDC_AY_PREV:
    if( current > 0 ) play( current - 1 );
    else if( current < 0 && count ) play( 0 );
    break;

  case IDC_AY_NEXT:
    if( current < 0 && count ) play( 0 );
    else if( current >= 0 && current + 1 < count ) play( current + 1 );
    break;

  case IDC_AY_LIST:
    win32_ayplaylist_toggle();
    break;

  case IDC_AY_PAUSE:
    if( !playing ) {
      /* Play: the song chosen in the list, or the last one, or the first */
      int index = win32_ayplaylist_selected();
      if( index < 0 ) index = current;
      if( index < 0 ) index = 0;
      if( index < count ) play( index );
    } else {
      ayplayer_set_paused( !player_paused() );
      update_meters( 0 );
    }
    update_pause_button();
    break;

  case IDC_AY_LEVEL_LABEL1:
  case IDC_AY_LEVEL_LABEL2:
  case IDC_AY_LEVEL_LABEL3:
    if( code == BN_CLICKED ) {
      int row = id - IDC_AY_LEVEL_LABEL1;
      ayplay_set_muted( meter_order[ row ],
        IsDlgButtonChecked( fuse_hAYWnd, id ) != BST_CHECKED );
      InvalidateRect( GetDlgItem( fuse_hAYWnd, IDC_AY_LEVEL1 + row ), NULL,
                      FALSE );
    }
    break;

  case IDC_AY_MODE_SINGLE:
  case IDC_AY_MODE_LIST:
  case IDC_AY_REPEAT:
    if( code == BN_CLICKED ) end_options_changed();
    break;

  case IDC_AY_STEREO:
    if( code == CBN_SELCHANGE ) stereo_changed();
    break;

  case IDC_AY_MIXER:
    /* Opens the mixer, or closes it if it is open */
    if( code == BN_CLICKED ) {
      if( fuse_hAYMixWnd ) win32_aymixer_close();
      else win32_aymixer_show();
    }
    break;

  case IDC_AY_MIX_AUTO:
    if( code == BN_CLICKED ) {
      settings_current.ayplayer_mix_auto =
        IsDlgButtonChecked( fuse_hAYWnd, IDC_AY_MIX_AUTO ) == BST_CHECKED;
      /* At once for the song playing too */
      if( ayplayer_current_song() >= 0 )
        aymix_song_started( ayplayer_path(), ayplayer_current_song(),
                            ayplayer_song_count(),
                            settings_current.ayplayer_mix_auto );
      else if( !settings_current.ayplayer_mix_auto )
        aymix_restore_user();
    }
    break;

  case IDC_AY_STOP:
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

/* Called by the emulation every frame and by a timer (the music goes on
   while the emulation does not run): the level meters every 40 ms, the rest
   every 200 ms, and at the end of a song the next one. The timer alone is
   not enough: Windows does not always send WM_TIMER while the emulation
   keeps the message queue busy */
#define WM_AYPLAYER_SONG_END ( WM_APP + 1 )

static int song_end_posted = 0;

/* While the length of the song playing is measured: a line at the bottom
   of the window */
static void
scan_window_close( void )
{
  if( !scan_window ) return;
  scan_window = NULL;
  if( fuse_hAYWnd ) set_text( IDC_AY_STATUS, "" );
}

static void
scan_update( void )
{
  if( ayplayer_measuring_current() ) {
    if( !scan_window ) {
      scan_window = fuse_hAYWnd;
      set_text( IDC_AY_STATUS, i18n( "Reading the length of the song..." ) );
    }
  } else {
    scan_window_close();
  }
}

static void
tick( void )
{
  static DWORD last_meters = 0, last_time = 0;
  DWORD now = GetTickCount();
  int current;

  if( !fuse_hAYWnd ) return;

  /* A length found: shown, and the next song of the list measured */
  if( ayplayer_poll() ) {
    if( measuring ) measure_next();
    win32_ayplaylist_update();
    update_time();
  }

  if( now - last_meters >= 40 ) {
    last_meters = now;
    /* Paused: the meters stay where the music stopped, like a still
       picture, and go on when it goes on */
    if( !player_paused() ) update_meters( ayplayer_current_song() < 0 );
  }

  if( now - last_time < 200 ) return;
  last_time = now;

  scan_update();
  current = ayplayer_current_song();
  if( current >= 0 && ayplayer_song_ended() && !song_end_posted ) {
    /* Not here, maybe in the middle of a frame: the window does it */
    song_end_posted = 1;
    PostMessage( fuse_hAYWnd, WM_AYPLAYER_SONG_END, 0, 0 );
  }
  update_time();
}

static void
frame_update( void )
{
  tick();
}

/* Play one song or the whole list, repeat it or not: kept in the settings */
static void
end_options_changed( void )
{
  int list_mode =
    IsDlgButtonChecked( fuse_hAYWnd, IDC_AY_MODE_LIST ) == BST_CHECKED;
  int repeat =
    IsDlgButtonChecked( fuse_hAYWnd, IDC_AY_REPEAT ) == BST_CHECKED;

  settings_current.ayplayer_auto_next = list_mode;
  settings_current.ayplayer_repeat = repeat;
  ayplayer_set_repeat_song( repeat && !list_mode );
}

/* End of the song:
     1 song:      stop; repeat: the same song again (songs that go round
                  do not get here: they go on playing)
     whole list:  the next song; after the last one stop, or with repeat
                  the first one again */
static void
song_end( void )
{
  int current = ayplaylist_current();
  int count = ayplaylist_count();
  int list_mode =
    IsDlgButtonChecked( fuse_hAYWnd, IDC_AY_MODE_LIST ) == BST_CHECKED;
  int repeat =
    IsDlgButtonChecked( fuse_hAYWnd, IDC_AY_REPEAT ) == BST_CHECKED;

  song_end_posted = 0;
  if( ayplayer_current_song() < 0 || !ayplayer_song_ended() ) return;

  if( list_mode && current >= 0 && current + 1 < count ) {
    play( current + 1 );
  } else if( list_mode && repeat && count ) {
    play( 0 );
  } else if( !list_mode && repeat && current >= 0 ) {
    play( current );
  } else {
    ayplayer_stop();
    update_pause_button();
    update_time();
    win32_ayplaylist_update();
  }
}

static INT_PTR CALLBACK
ayplayer_proc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
  switch( msg ) {

  case WM_INITDIALOG:
    {
      int i;
      long value;

      fuse_hAYWnd = hWnd;

      /* The previous version kept these in fuse-windows.ini */
      if( winstate_get( "ayplayer", "auto_next", &value ) ) {
        settings_current.ayplayer_auto_next = value;
        winstate_remove( "ayplayer", "auto_next" );
      }
      if( winstate_get( "ayplayer", "repeat", &value ) ) {
        settings_current.ayplayer_repeat = value;
        winstate_remove( "ayplayer", "repeat" );
      }

      CheckRadioButton( hWnd, IDC_AY_MODE_SINGLE, IDC_AY_MODE_LIST,
                        settings_current.ayplayer_auto_next ?
                        IDC_AY_MODE_LIST : IDC_AY_MODE_SINGLE );

      /* The song being played in bold */
      {
        LOGFONT font;
        HFONT normal = (HFONT)SendMessage( hWnd, WM_GETFONT, 0, 0 );
        if( normal && GetObject( normal, sizeof( font ), &font ) ) {
          font.lfWeight = FW_BOLD;
          song_font = CreateFontIndirect( &font );
          SendDlgItemMessage( hWnd, IDC_AY_SONG, WM_SETFONT,
                              (WPARAM)song_font, FALSE );
        }
      }
      CheckDlgButton( hWnd, IDC_AY_REPEAT, settings_current.ayplayer_repeat ?
                      BST_CHECKED : BST_UNCHECKED );
      end_options_changed();
      SendDlgItemMessage( hWnd, IDC_AY_VOLUME, TBM_SETRANGE, FALSE,
                          MAKELPARAM( 0, 100 ) );
      SendDlgItemMessage( hWnd, IDC_AY_VOLUME, TBM_SETPAGESIZE, 0, 10 );
      CheckDlgButton( hWnd, IDC_AY_MIX_AUTO,
                      settings_current.ayplayer_mix_auto ?
                      BST_CHECKED : BST_UNCHECKED );
      aymix_set_changed_hook( win32_aymix_changed );
      /* The meters show the channels after their volumes even without
         sound */
      sound_ay_volume_changed();
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
    if( wParam == AYPLAYER_TIMER ) {
      static int last_busy = -1;
      int busy = win32_ayplayer_busy();
      if( busy != last_busy ) {
        last_busy = busy;
        win32_ayplaylist_update_status();
      }

      update_pause_button();
      sync_sound_settings();
      tick();
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
    remember_position();
    return 0;

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

/* "Play: (o) 1 song ( ) Whole list [x] Repeat": one after the other, as
   wide as their texts are (in either language); the repeat box under the
   Next button */
static void
layout_play_row( void )
{
  static const int ids[] = { IDC_AY_MODE_LABEL, IDC_AY_MODE_SINGLE,
                             IDC_AY_MODE_LIST, IDC_AY_REPEAT };
  HDC dc;
  HFONT font, old_font;
  RECT rect;
  POINT corner;
  int i, x, box = GetSystemMetrics( SM_CXMENUCHECK );

  dc = GetDC( fuse_hAYWnd );
  font = (HFONT)SendMessage( fuse_hAYWnd, WM_GETFONT, 0, 0 );
  old_font = SelectObject( dc, font );

  GetWindowRect( GetDlgItem( fuse_hAYWnd, ids[0] ), &rect );
  corner.x = rect.left; corner.y = rect.top;
  ScreenToClient( fuse_hAYWnd, &corner );
  x = corner.x;

  for( i = 0; i < 4; i++ ) {
    HWND control = GetDlgItem( fuse_hAYWnd, ids[i] );
    char text[ 128 ], plain[ 128 ];
    SIZE size;
    int j, k, width;

    GetWindowTextA( control, text, sizeof( text ) );
    for( j = k = 0; text[j] && k < (int)sizeof( plain ) - 1; j++ )
      if( text[j] != '&' ) plain[ k++ ] = text[j];
    plain[k] = '\0';
    GetTextExtentPoint32A( dc, plain, k, &size );

    /* A radio button or tick box: the box and the room after it too */
    width = size.cx + ( i ? box + 6 : 0 );

    /* The repeat box starts where the Next button does, if there is room */
    if( i == 3 ) {
      RECT next;
      POINT next_corner;
      GetWindowRect( GetDlgItem( fuse_hAYWnd, IDC_AY_NEXT ), &next );
      next_corner.x = next.left; next_corner.y = next.top;
      ScreenToClient( fuse_hAYWnd, &next_corner );
      if( next_corner.x > x ) x = next_corner.x;
    }

    GetWindowRect( control, &rect );
    corner.x = rect.left; corner.y = rect.top;
    ScreenToClient( fuse_hAYWnd, &corner );
    SetWindowPos( control, NULL, x, corner.y, width,
                  rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE );

    /* The repeat box a little apart from the two choices */
    x += width + ( i == 2 ? 24 : i ? 12 : 8 );
  }

  SelectObject( dc, old_font );
  ReleaseDC( fuse_hAYWnd, dc );
}

/* The place of the window is kept in fuse-windows.ini */

static void
remember_position( void )
{
  RECT rect;

  if( !fuse_hAYWnd || IsIconic( fuse_hAYWnd ) ||
      !GetWindowRect( fuse_hAYWnd, &rect ) )
    return;
  winstate_set( "ayplayer", "x", rect.left );
  winstate_set( "ayplayer", "y", rect.top );
}

static void
restore_position( void )
{
  POINT title;
  long x, y;

  if( !winstate_get( "ayplayer", "x", &x ) ||
      !winstate_get( "ayplayer", "y", &y ) )
    return;

  /* Only if the title bar is on a screen (one may have been removed) */
  title.x = x + 40;
  title.y = y + 8;
  if( !MonitorFromPoint( title, MONITOR_DEFAULTTONULL ) ) return;

  SetWindowPos( fuse_hAYWnd, NULL, x, y, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
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
    /* After the texts were translated */
    layout_play_row();
    restore_position();
  }

  ayplayer_set_frame_hook( frame_update );
  win32ui_ayplayer_timer_start();
  resume_if_paused();
  win32_ayplaylist_show();
  update_all();
  ShowWindow( fuse_hAYWnd, SW_SHOW );
  SetForegroundWindow( fuse_hAYWnd );
}

/* ayplayer.c: Win32 AY player and TurboSound player windows
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

/* Two player windows share this code, each with a playlist of its own
   (ayplaylist.c): the AY player, and the TurboSound player for music of
   two or three AY chips. The TurboSound player's window is simpler: it has
   a Format line but no level meters and no sound settings, so the code
   leaves out what its window does not have. The player (ayplayer.c)
   plays a song of one of the lists at a time; the other window shows
   its own list's song as stopped meanwhile */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
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
#include "win32text.h"
#include "winstate.h"

HWND fuse_hAYWnd = NULL;
HWND fuse_hTSWnd = NULL;

#define AYPLAYER_TIMER 1
#define AYPLAYER_TIMER_MS 40

/* A player window */
typedef struct player {
  int turbosound;		/* 0: the AY player, 1: the TurboSound one */
  int dialog;
  const char *key;		/* of its place in fuse-windows.ini */
  HWND *window;
  /* Play one song or the whole list, repeat or not (settings) */
  int *auto_next, *repeat;

  /* The position slider is being moved by the user: do not move it */
  int seek_dragging;
  /* Length shown on the position slider */
  long slider_length;
  HFONT song_font;
  /* The length of the song is being measured (the status line shows it) */
  int scan_shown;
  int song_end_posted;
  DWORD last_meters, last_time;
  int last_busy;
  /* Whether the song of the player was this window's, last shown */
  int shown_owns;
} player;

static player players[ 2 ] = {
  { 0, IDD_AYPLAYER, "ayplayer", &fuse_hAYWnd,
    &settings_current.ayplayer_auto_next, &settings_current.ayplayer_repeat,
    0, -1, NULL, 0, 0, 0, 0, -1, 0 },
  { 1, IDD_TSPLAYER, "tsplayer", &fuse_hTSWnd,
    &settings_current.tsplayer_auto_next, &settings_current.tsplayer_repeat,
    0, -1, NULL, 0, 0, 0, 0, -1, 0 },
};

/* The AY player: the only one with level meters and sound settings */
#define AY ( &players[ 0 ] )

static HWND
wnd( const player *p )
{
  return *p->window;
}

static player*
other( const player *p )
{
  return &players[ !p->turbosound ];
}

static ayplaylist*
list_of( const player *p )
{
  return p->turbosound ? ayplaylist_ts() : ayplaylist_ay();
}

/* The player has the file of a song of this window's list */
static int
owns( const player *p )
{
  return ayplaylist_owns_player( list_of( p ) );
}

/* The song played for this window; -1: stopped (also while the other
   window's song plays) */
static int
current_song( const player *p )
{
  return owns( p ) ? ayplayer_current_song() : -1;
}

static player*
player_of( HWND window )
{
  int i;

  for( i = 0; i < 2; i++ )
    if( window && wnd( &players[i] ) == window ) return &players[i];
  return NULL;
}

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

static void scan_window_close( player *p );
static void end_options_changed( player *p );
static void remember_position( player *p );
static void update_all( player *p );

static int shown_volume = -1;
static int shown_stereo = -1;
/* The places of the channels shown in the level meter labels */
static int shown_positions[3] = { -1, -1, -1 };
/* Volumes of the channels A, B and C the meters were drawn with */
static int shown_channel_volume[3] = { -1, -1, -1 };

/* Pausing stops the music only: the emulated machine goes on */
static int
player_paused( const player *p )
{
  return owns( p ) && ayplayer_paused();
}

static void
set_text( const player *p, int id, const char *text )
{
  /* Texts from the music files are often in an old 8-bit code page */
  char *utf8 = i18n_legacy_to_utf8( text );

  SetDlgItemTextA( wnd( p ), id, utf8 ? utf8 : text );
  libspectrum_free( utf8 );
}

/* The comment of the file, and the stereo mode stored in it (VTX). A
   file not loaded (the song of the list is not the player's): what the
   list knows of it */
static void
set_misc_text( const player *p, const ayplaylist_entry *entry )
{
  const char *stereo = entry ? "" : ayplayer_file_stereo();
  const char *source = entry ? ( p->turbosound ? entry->comment :
                                                 entry->misc ) :
                               ( p->turbosound ? ayplayer_comment() :
                                                 ayplayer_misc() );
  char *misc = i18n_legacy_to_utf8( source );
  const char *comment = misc ? misc : source;
  char text[ 512 ], mode[ 64 ];

  if( !*stereo ) {
    SetDlgItemTextA( wnd( p ), IDC_AY_MISC, comment );
    libspectrum_free( misc );
    return;
  }
  _snprintf( mode, sizeof( mode ), i18n( "Stereo: %s" ), stereo );
  mode[ sizeof( mode ) - 1 ] = '\0';
  if( *comment )
    _snprintf( text, sizeof( text ), "%s %s %s", comment, win32text_dash(),
               mode );
  else
    _snprintf( text, sizeof( text ), "%s", mode );
  text[ sizeof( text ) - 1 ] = '\0';
  SetDlgItemTextA( wnd( p ), IDC_AY_MISC, text );
  libspectrum_free( misc );
}

/* The Format line of the TurboSound player: "TurboSound - PT3 + PT3
   (2 AY, 6 channels)" */
static void
set_format_text( const player *p, const char *format, int chips )
{
  char text[ 256 ];

  if( !GetDlgItem( wnd( p ), IDC_AY_FORMAT ) ) return;
  if( !*format ) {
    set_text( p, IDC_AY_FORMAT, "" );
    return;
  }
  _snprintf( text, sizeof( text ), i18n( "TurboSound %s %s (%d AY, %d channels)" ),
             win32text_dash(), format, chips, chips * 3 );
  text[ sizeof( text ) - 1 ] = '\0';
  SetDlgItemTextA( wnd( p ), IDC_AY_FORMAT, text );
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
update_pause_button( const player *p )
{
  const char *label;

  if( current_song( p ) < 0 ) label = "&Play";
  else if( player_paused( p ) ) label = "&Resume";
  else label = "&Pause";

  set_text( p, IDC_AY_PAUSE, i18n( label ) );
}

static void
update_time( player *p )
{
  char text[ 128 ];
  HWND slider = GetDlgItem( wnd( p ), IDC_AY_PROGRESS );
  int current = current_song( p ), measured = 0, is_default = 0;
  long played = current >= 0 ? ayplayer_frames_played() : 0;
  long length = 0;
  int index = ayplaylist_current( list_of( p ) );

  if( owns( p ) ) {
    length = ayplayer_song_frames();
    measured = ayplayer_length_is_measured();
    is_default = ayplayer_length_is_default();
  } else if( index >= 0 && ayplaylist_length( list_of( p ), index, &length ) ) {
    /* The song of the list, not loaded: its length measured earlier */
    measured = length > 0;
    is_default = !length;
    if( !length ) length = 5 * 60 * 50;
  }

  if( p->seek_dragging ) played = SendMessage( slider, TBM_GETPOS, 0, 0 );
  if( length && played > length ) played = length;

  format_time( text, sizeof( text ), "Elapsed: %d:%02d", played );
  set_text( p, IDC_AY_ELAPSED, text );

  if( length != p->slider_length ) {
    p->slider_length = length;
    SendMessage( slider, TBM_SETRANGEMIN, FALSE, 0 );
    SendMessage( slider, TBM_SETRANGEMAX, TRUE, length ? length : 1 );
    SendMessage( slider, TBM_SETPAGESIZE, 0, 50 * 10 );
    SendMessage( slider, TBM_SETLINESIZE, 0, 50 * 2 );
  }
  EnableWindow( slider, length && current >= 0 );
  if( !p->seek_dragging ) SendMessage( slider, TBM_SETPOS, TRUE, played );

  if( length && measured ) {
    format_time( text, sizeof( text ), "Length: %d:%02d (measured)", length );
  } else if( length && is_default ) {
    format_time( text, sizeof( text ), "Length: %d:%02d (default)", length );
  } else if( length ) {
    format_time( text, sizeof( text ), "Length: %d:%02d", length );
  } else {
    _snprintf( text, sizeof( text ), "%s", i18n( "Length: unknown" ) );
    text[ sizeof( text ) - 1 ] = '\0';
  }
  set_text( p, IDC_AY_LENGTH, text );

  /* Nothing chosen: nothing shown */
  if( index < 0 && current < 0 ) {
    set_text( p, IDC_AY_ELAPSED, "" );
    set_text( p, IDC_AY_LENGTH, "" );
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
    set_text( AY, IDC_AY_LEVEL_LABEL1 + row, text );
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
  if( current_song( AY ) < 0 ) return 0;
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
  set_text( AY, IDC_AY_VOLUME_TEXT, text );
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
  set_text( AY, IDC_AY_VOLUME_TEXT, text );
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
slider_scroll( player *p, HWND slider, WORD code )
{
  switch( code ) {
  case TB_PAGEUP:
  case TB_PAGEDOWN:
    slider_to_mouse( slider );
    p->seek_dragging = 1;
    break;

  case TB_THUMBTRACK:
  case TB_THUMBPOSITION:
  case TB_LINEUP:
  case TB_LINEDOWN:
  case TB_TOP:
  case TB_BOTTOM:
    p->seek_dragging = 1;
    break;

  case TB_ENDTRACK:
    if( p->seek_dragging ) {
      p->seek_dragging = 0;
      if( owns( p ) )
        ayplayer_seek( SendMessage( slider, TBM_GETPOS, 0, 0 ) );
    }
    break;
  }
  update_time( p );
}

/* Everything shown, after a new file or song */
static void
update_all( player *p )
{
  ayplaylist *list = list_of( p );
  int index, count;
  const ayplaylist_entry *entry;
  char text[ 512 ], name[ 256 ];

  if( !wnd( p ) ) return;
  index = ayplaylist_current( list );
  count = ayplaylist_count( list );
  entry = ayplaylist_get( list, index );
  p->shown_owns = owns( p );

  /* Nothing chosen: nothing shown */
  if( index < 0 && current_song( p ) < 0 ) {
    set_text( p, IDC_AY_FILE, "" );
    set_text( p, IDC_AY_AUTHOR, "" );
    set_text( p, IDC_AY_MISC, "" );
    set_format_text( p, "", 0 );
  } else if( owns( p ) &&
             ( !entry || !strcmp( entry->path, ayplayer_path() ) ) ) {
    set_text( p, IDC_AY_FILE, ayplayer_filename() );
    set_text( p, IDC_AY_AUTHOR, ayplayer_author() );
    set_misc_text( p, NULL );
    set_format_text( p, ayplayer_format(), ayplayer_chips() );
  } else if( entry ) {
    /* Not loaded: what the list knows of it */
    const char *file = entry->path, *c;
    for( c = entry->path; *c; c++ )
      if( *c == '/' || *c == '\\' ) file = c + 1;
    set_text( p, IDC_AY_FILE, file );
    set_text( p, IDC_AY_AUTHOR, entry->author );
    set_misc_text( p, entry );
    set_format_text( p, entry->format, entry->chips );
  }

  /* The song being played: its place in the list and title (which may be
     in an old code page) */
  text[0] = '\0';
  if( entry ) {
    char *utf8 = i18n_legacy_to_utf8( entry->title );
    _snprintf( text, sizeof( text ), "%d / %d %s %s", index + 1, count,
               win32text_dash(), utf8 ? utf8 : entry->title );
    libspectrum_free( utf8 );
  } else if( current_song( p ) >= 0 ) {
    ayplayer_song_name( current_song( p ), name, sizeof( name ) );
    _snprintf( text, sizeof( text ), "%s", name );
  }
  text[ sizeof( text ) - 1 ] = '\0';
  set_text( p, IDC_AY_SONG, text );

  CheckDlgButton( wnd( p ), IDC_AY_MIX_AUTO,
                  settings_current.ayplayer_mix_auto ?
                  BST_CHECKED : BST_UNCHECKED );
  update_pause_button( p );
  update_time( p );
  if( p == AY ) sync_sound_settings();
  win32_ayplaylist_update( p->turbosound );
}

/* Mixed into the sound of the emulator, the music needs the emulation:
   Machine > Pause is over */
static void
resume_if_paused( void )
{
  if( !ayplay_output_independent() && win32ui_machine_paused() )
    menu_machine_pause( 0 );
}

/* Measuring the lengths of a whole list (one song after the other, in the
   background; the music goes on meanwhile). One list at a time, as the
   player measures one song at a time: that of `measure_player' */
static player *measure_player = NULL;
static int measuring = 0;
/* Stop asked for: the song being measured is finished, then no more */
static int measure_stopping = 0;
static int measure_next_index = 0;
/* Measuring again chosen songs: their places in the list (NULL: the songs
   of the list not measured yet) */
static int *remeasure_indexes = NULL;
static int remeasure_count = 0;

static void
remeasure_free( void )
{
  free( remeasure_indexes );
  remeasure_indexes = NULL;
  remeasure_count = 0;
}

/* Measuring the list is over (or given up) */
static void
measure_end( void )
{
  player *p = measure_player;

  measuring = 0;
  measure_stopping = 0;
  measure_player = NULL;
  remeasure_free();
  if( p ) win32_ayplaylist_update( p->turbosound );
}

/* Play one song or the whole list, repeat it or not: the song played
   repeats by the choice of its window */
static void
apply_end_options( const player *p )
{
  ayplayer_set_repeat_song( *p->repeat && !*p->auto_next );
}

static void
play( player *p, int index )
{
  /* Chosen by the user: measuring (if any) is over */
  measure_end();
  resume_if_paused();
  apply_end_options( p );
  ayplaylist_play( list_of( p ), index );
  update_all( p );
  /* The song of the other window stopped */
  update_all( other( p ) );
}

void
win32_ayplayer_update( void )
{
  update_all( &players[0] );
  update_all( &players[1] );
}

void
win32_ayplayer_play_entry( int turbosound, int index )
{
  player *p = &players[ turbosound ? 1 : 0 ];

  if( wnd( p ) ) play( p, index );
}

HWND
win32_ayplayer_window( int turbosound )
{
  return wnd( &players[ turbosound ? 1 : 0 ] );
}

/* The next song of the list whose length is not known yet; when there is
   none, measuring is over */
static void
measure_next( void )
{
  player *p = measure_player;
  ayplaylist *list;
  long frames;

  if( !p ) {
    measure_end();
    return;
  }
  list = list_of( p );

  /* Chosen songs: each of them, measured or not */
  while( measuring && !measure_stopping && remeasure_indexes &&
         measure_next_index < remeasure_count ) {
    int index = remeasure_indexes[ measure_next_index++ ];
    const ayplaylist_entry *entry = ayplaylist_get( list, index );

    if( !entry ) continue;
    if( !ayplayer_remeasure( entry->path, entry->song ) ) {
      win32_ayplaylist_update( p->turbosound );
      return;
    }
  }

  while( measuring && !measure_stopping && !remeasure_indexes &&
         measure_next_index < ayplaylist_count( list ) ) {
    int index = measure_next_index++;
    const ayplaylist_entry *entry = ayplaylist_get( list, index );

    if( ayplaylist_length( list, index, &frames ) ) continue;
    if( !ayplayer_measure( entry->path, entry->song ) ) {
      win32_ayplaylist_update( p->turbosound );
      return;
    }
  }

  measure_end();
}

/* The other window's list was being measured: given up for this one
   (the song playing, if it is being measured, is finished) */
static void
measure_take_over( player *p )
{
  if( !measuring || measure_player == p ) return;
  measure_end();
  if( !ayplayer_measuring_current() ) ayplayer_measure_cancel();
}

void
win32_ayplayer_measure_all( int turbosound )
{
  player *p = &players[ turbosound ? 1 : 0 ];

  if( !wnd( p ) || ( measuring && measure_player == p ) ) return;

  measure_take_over( p );
  measuring = 1;
  measure_player = p;
  measure_stopping = 0;
  measure_next_index = 0;
  remeasure_free();
  /* The song playing is being measured: that first */
  if( !ayplayer_measuring() ) measure_next();
}

/* Measure again the songs at `indexes' of the list, measured or not; any
   measuring going on is given up for it */
void
win32_ayplayer_remeasure( int turbosound, const int *indexes, int count )
{
  player *p = &players[ turbosound ? 1 : 0 ];

  if( !wnd( p ) || count <= 0 ) return;

  measure_end();
  remeasure_indexes = malloc( count * sizeof( *remeasure_indexes ) );
  if( !remeasure_indexes ) return;
  memcpy( remeasure_indexes, indexes, count * sizeof( *remeasure_indexes ) );
  remeasure_count = count;

  ayplayer_measure_cancel();
  measuring = 1;
  measure_player = p;
  measure_stopping = 0;
  measure_next_index = 0;
  measure_next();
}

/* Stop measuring the lengths of the list: the song being measured is
   finished first, so its length is not lost */
void
win32_ayplayer_measure_stop( int turbosound )
{
  if( !win32_ayplayer_busy( turbosound ) || measure_stopping ) return;
  measure_stopping = 1;
  win32_ayplaylist_update_status( turbosound );
  /* Between two songs: over at once */
  if( !ayplayer_measuring() ) measure_next();
}

int
win32_ayplayer_measure_stopping( int turbosound )
{
  return win32_ayplayer_busy( turbosound ) && measure_stopping;
}

int
win32_ayplayer_busy( int turbosound )
{
  return measuring && measure_player == &players[ turbosound ? 1 : 0 ];
}

static void tick_all( void );

static void
close_window( player *p )
{
  if( !wnd( p ) ) return;

  KillTimer( wnd( p ), AYPLAYER_TIMER );
  remember_position( p );
  scan_window_close( p );
  /* The channels switched off in the AY player are heard again */
  if( p == AY ) {
    ayplay_set_muted( 0, 0 );
    ayplay_set_muted( 1, 0 );
    ayplay_set_muted( 2, 0 );
  }
  if( measure_player == p ) {
    measure_end();
    if( !ayplayer_measuring_current() ) ayplayer_measure_cancel();
  }
  /* What was open with it, for the next time */
  win32_ayplayer_remember_companions( p->turbosound );
  win32_ayplaylist_close( p->turbosound );
  /* The mixer goes with the player that opened it, like the list */
  if( win32_aymixer_is_open() && win32_aymixer_owner() == wnd( p ) )
    win32_aymixer_close();
  DestroyWindow( wnd( p ) );
  *p->window = NULL;
  if( p->song_font ) {
    DeleteObject( p->song_font );
    p->song_font = NULL;
  }

  /* Its song stops; that of the other window plays on */
  ayplaylist_close_player( list_of( p ) );
  if( !wnd( other( p ) ) ) {
    ayplayer_set_frame_hook( NULL );
  } else if( measuring && !ayplayer_measuring() ) {
    /* The other list was being measured: stopping the song gave up its
       song being measured; on with the next one */
    measure_next();
  }
}

static void
command( player *p, WORD id, WORD code )
{
  ayplaylist *list = list_of( p );
  int current = ayplaylist_current( list );
  int count = ayplaylist_count( list );
  int playing = current_song( p ) >= 0;

  switch( id ) {

  case IDC_AY_PREV:
    if( current > 0 ) play( p, current - 1 );
    else if( current < 0 && count ) play( p, 0 );
    break;

  case IDC_AY_NEXT:
    if( current < 0 && count ) play( p, 0 );
    else if( current >= 0 && current + 1 < count ) play( p, current + 1 );
    break;

  case IDC_AY_LIST:
    win32_ayplaylist_toggle( p->turbosound );
    break;

  case IDC_AY_PAUSE:
    if( !playing ) {
      /* Play: the song chosen in the list, or the last one, or the first */
      int index = win32_ayplaylist_selected( p->turbosound );
      if( index < 0 ) index = current;
      if( index < 0 ) index = 0;
      if( index < count ) play( p, index );
    } else {
      ayplayer_set_paused( !player_paused( p ) );
      if( p == AY ) update_meters( 0 );
    }
    update_pause_button( p );
    break;

  case IDC_AY_LEVEL_LABEL1:
  case IDC_AY_LEVEL_LABEL2:
  case IDC_AY_LEVEL_LABEL3:
    if( code == BN_CLICKED ) {
      int row = id - IDC_AY_LEVEL_LABEL1;
      ayplay_set_muted( meter_order[ row ],
        IsDlgButtonChecked( wnd( p ), id ) != BST_CHECKED );
      InvalidateRect( GetDlgItem( wnd( p ), IDC_AY_LEVEL1 + row ), NULL,
                      FALSE );
    }
    break;

  case IDC_AY_MODE_SINGLE:
  case IDC_AY_MODE_LIST:
  case IDC_AY_REPEAT:
    if( code == BN_CLICKED ) end_options_changed( p );
    break;

  case IDC_AY_STEREO:
    if( code == CBN_SELCHANGE ) stereo_changed();
    break;

  case IDC_AY_MIXER:
    /* Opens the mixer, brings it back if minimized, or closes it */
    if( code == BN_CLICKED ) win32_aymixer_toggle( wnd( p ) );
    break;

  case IDC_AY_MIX_AUTO:
    if( code == BN_CLICKED ) {
      settings_current.ayplayer_mix_auto =
        IsDlgButtonChecked( wnd( p ), IDC_AY_MIX_AUTO ) == BST_CHECKED;
      /* At once for the song playing too */
      if( ayplayer_current_song() >= 0 )
        aymix_song_started( ayplayer_path(), ayplayer_current_song(),
                            ayplayer_song_count(),
                            settings_current.ayplayer_mix_auto,
                            ayplayer_file_stereo() );
      else if( !settings_current.ayplayer_mix_auto )
        aymix_restore_user();
      /* The other window shows the same setting */
      if( wnd( other( p ) ) )
        CheckDlgButton( wnd( other( p ) ), IDC_AY_MIX_AUTO,
                        settings_current.ayplayer_mix_auto ?
                        BST_CHECKED : BST_UNCHECKED );
    }
    break;

  case IDC_AY_STOP:
    if( owns( p ) ) ayplayer_stop();
    update_pause_button( p );
    update_time( p );
    break;

  case IDCLOSE:
  case IDCANCEL:
    close_window( p );
    break;
  }
}

/* Called by the emulation every frame and by a timer (the music goes on
   while the emulation does not run): the level meters every 40 ms, the rest
   every 200 ms, and at the end of a song the next one. The timer alone is
   not enough: Windows does not always send WM_TIMER while the emulation
   keeps the message queue busy */
#define WM_AYPLAYER_SONG_END ( WM_APP + 1 )

/* While the length of the song playing is measured: a line at the bottom
   of the window */
static void
scan_window_close( player *p )
{
  if( !p->scan_shown ) return;
  p->scan_shown = 0;
  if( wnd( p ) ) set_text( p, IDC_AY_STATUS, "" );
}

static void
scan_update( player *p )
{
  if( owns( p ) && ayplayer_measuring_current() ) {
    if( !p->scan_shown ) {
      p->scan_shown = 1;
      set_text( p, IDC_AY_STATUS, i18n( "Reading the length of the song..." ) );
    }
  } else {
    scan_window_close( p );
  }
}

static void
tick( player *p )
{
  DWORD now = GetTickCount();
  int current;

  if( !wnd( p ) ) return;

  if( p == AY && now - p->last_meters >= 40 ) {
    p->last_meters = now;
    /* Paused: the meters stay where the music stopped, like a still
       picture, and go on when it goes on */
    if( !player_paused( p ) ) update_meters( current_song( p ) < 0 );
  }

  if( now - p->last_time < 200 ) return;
  p->last_time = now;

  /* The other window's song started (also from File > Open): this one's
     is shown stopped */
  if( owns( p ) != p->shown_owns ) update_all( p );

  scan_update( p );
  current = current_song( p );
  if( current >= 0 && ayplayer_song_ended() && !p->song_end_posted ) {
    /* Not here, maybe in the middle of a frame: the window does it */
    p->song_end_posted = 1;
    PostMessage( wnd( p ), WM_AYPLAYER_SONG_END, 0, 0 );
  }
  update_time( p );
}

static void
tick_all( void )
{
  int i;

  /* A length found: shown, and the next song of the list measured */
  if( ayplayer_poll() ) {
    if( measuring ) measure_next();
    for( i = 0; i < 2; i++ ) {
      if( !wnd( &players[i] ) ) continue;
      win32_ayplaylist_update( i );
      update_time( &players[i] );
    }
  }

  for( i = 0; i < 2; i++ ) tick( &players[i] );
}

/* Play one song or the whole list, repeat it or not: kept in the settings */
static void
end_options_changed( player *p )
{
  *p->auto_next =
    IsDlgButtonChecked( wnd( p ), IDC_AY_MODE_LIST ) == BST_CHECKED;
  *p->repeat =
    IsDlgButtonChecked( wnd( p ), IDC_AY_REPEAT ) == BST_CHECKED;
  if( owns( p ) ) apply_end_options( p );
}

/* End of the song:
     1 song:      stop; repeat: the same song again (songs that go round
                  do not get here: they go on playing)
     whole list:  the next song; after the last one stop, or with repeat
                  the first one again */
static void
song_end( player *p )
{
  ayplaylist *list = list_of( p );
  int current = ayplaylist_current( list );
  int count = ayplaylist_count( list );
  int list_mode =
    IsDlgButtonChecked( wnd( p ), IDC_AY_MODE_LIST ) == BST_CHECKED;
  int repeat =
    IsDlgButtonChecked( wnd( p ), IDC_AY_REPEAT ) == BST_CHECKED;

  p->song_end_posted = 0;
  if( current_song( p ) < 0 || !ayplayer_song_ended() ) return;

  if( list_mode && current >= 0 && current + 1 < count ) {
    play( p, current + 1 );
  } else if( list_mode && repeat && count ) {
    play( p, 0 );
  } else if( !list_mode && repeat && current >= 0 ) {
    play( p, current );
  } else {
    ayplayer_stop();
    update_pause_button( p );
    update_time( p );
    win32_ayplaylist_update( p->turbosound );
  }
}

static void
init_dialog( player *p, HWND hWnd )
{
  int i;
  long value;

  *p->window = hWnd;

  /* The previous version kept these in fuse-windows.ini */
  if( winstate_get( p->key, "auto_next", &value ) ) {
    *p->auto_next = value;
    winstate_remove( p->key, "auto_next" );
  }
  if( winstate_get( p->key, "repeat", &value ) ) {
    *p->repeat = value;
    winstate_remove( p->key, "repeat" );
  }

  CheckRadioButton( hWnd, IDC_AY_MODE_SINGLE, IDC_AY_MODE_LIST,
                    *p->auto_next ? IDC_AY_MODE_LIST : IDC_AY_MODE_SINGLE );

  /* The song being played in bold */
  {
    LOGFONT font;
    HFONT normal = (HFONT)SendMessage( hWnd, WM_GETFONT, 0, 0 );
    if( normal && GetObject( normal, sizeof( font ), &font ) ) {
      font.lfWeight = FW_BOLD;
      p->song_font = CreateFontIndirect( &font );
      SendDlgItemMessage( hWnd, IDC_AY_SONG, WM_SETFONT,
                          (WPARAM)p->song_font, FALSE );
    }
  }
  CheckDlgButton( hWnd, IDC_AY_REPEAT, *p->repeat ?
                  BST_CHECKED : BST_UNCHECKED );
  end_options_changed( p );
  CheckDlgButton( hWnd, IDC_AY_MIX_AUTO,
                  settings_current.ayplayer_mix_auto ?
                  BST_CHECKED : BST_UNCHECKED );
  aymix_set_changed_hook( win32_aymix_changed );
  p->slider_length = -1;
  p->seek_dragging = 0;
  p->scan_shown = 0;
  p->song_end_posted = 0;
  p->last_busy = -1;

  /* The level meters and the sound settings: the AY player only */
  if( p == AY ) {
    SendDlgItemMessage( hWnd, IDC_AY_VOLUME, TBM_SETRANGE, FALSE,
                        MAKELPARAM( 0, 100 ) );
    SendDlgItemMessage( hWnd, IDC_AY_VOLUME, TBM_SETPAGESIZE, 0, 10 );
    /* The meters show the channels after their volumes even without
       sound */
    sound_ay_volume_changed();
    for( i = 0; i < STEREO_COUNT; i++ )
      SendDlgItemMessageA( hWnd, IDC_AY_STEREO, CB_ADDSTRING, 0,
                           (LPARAM)i18n( stereo_names[ i ] ) );
    memset( meter_shown, 0, sizeof( meter_shown ) );
    show_volume();
    show_stereo();
  }
  SetTimer( hWnd, AYPLAYER_TIMER, AYPLAYER_TIMER_MS, NULL );
}

static INT_PTR CALLBACK
ayplayer_proc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
  player *p;

  if( msg == WM_INITDIALOG ) {
    init_dialog( (player *)lParam, hWnd );
    return TRUE;
  }

  p = player_of( hWnd );
  if( !p ) return FALSE;

  switch( msg ) {

  case WM_TIMER:
    if( wParam == AYPLAYER_TIMER ) {
      int busy = win32_ayplayer_busy( p->turbosound );
      if( busy != p->last_busy ) {
        p->last_busy = busy;
        win32_ayplaylist_update_status( p->turbosound );
      }

      update_pause_button( p );
      if( p == AY ) sync_sound_settings();
      tick_all();
      return 0;
    }
    break;

  case WM_HSCROLL:
    if( (HWND)lParam == GetDlgItem( hWnd, IDC_AY_PROGRESS ) ) {
      slider_scroll( p, (HWND)lParam, LOWORD( wParam ) );
      return 0;
    }
    if( p == AY && (HWND)lParam == GetDlgItem( hWnd, IDC_AY_VOLUME ) ) {
      volume_changed();
      return 0;
    }
    break;

  case WM_DRAWITEM:
    if( p == AY && wParam >= IDC_AY_LEVEL1 && wParam <= IDC_AY_LEVEL3 ) {
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
    remember_position( p );
    return 0;

  case WM_AYPLAYER_SONG_END:
    song_end( p );
    return 0;

  case WM_COMMAND:
    command( p, LOWORD( wParam ), HIWORD( wParam ) );
    return 0;

  case WM_CLOSE:
    close_window( p );
    return 0;
  }

  return FALSE;
}

/* "Play: (o) 1 song ( ) Whole list [x] Repeat": one after the other, as
   wide as their texts are (in either language); the repeat box under the
   Next button */
static void
layout_play_row( player *p )
{
  static const int ids[] = { IDC_AY_MODE_LABEL, IDC_AY_MODE_SINGLE,
                             IDC_AY_MODE_LIST, IDC_AY_REPEAT };
  HWND window = wnd( p );
  HDC dc;
  HFONT font, old_font;
  RECT rect;
  POINT corner;
  int i, x, box = GetSystemMetrics( SM_CXMENUCHECK );

  dc = GetDC( window );
  font = (HFONT)SendMessage( window, WM_GETFONT, 0, 0 );
  old_font = SelectObject( dc, font );

  GetWindowRect( GetDlgItem( window, ids[0] ), &rect );
  corner.x = rect.left; corner.y = rect.top;
  ScreenToClient( window, &corner );
  x = corner.x;

  for( i = 0; i < 4; i++ ) {
    HWND control = GetDlgItem( window, ids[i] );
    WCHAR text[ 128 ], plain[ 128 ];
    SIZE size;
    int j, k, width;

    /* As Unicode: with the UTF-8 code page the ANSI functions may cut
       texts with accented letters */
    GetWindowTextW( control, text, sizeof( text ) / sizeof( text[0] ) );
    for( j = k = 0; text[j] && k < (int)( sizeof( plain ) /
                                          sizeof( plain[0] ) ) - 1; j++ )
      if( text[j] != L'&' ) plain[ k++ ] = text[j];
    plain[k] = L'\0';
    GetTextExtentPoint32W( dc, plain, k, &size );

    /* A radio button or tick box: the box and the room after it too */
    width = size.cx + ( i ? box + 6 : 0 );

    /* The repeat box starts where the Next button does, if there is room */
    if( i == 3 ) {
      RECT next;
      POINT next_corner;
      GetWindowRect( GetDlgItem( window, IDC_AY_NEXT ), &next );
      next_corner.x = next.left; next_corner.y = next.top;
      ScreenToClient( window, &next_corner );
      if( next_corner.x > x ) x = next_corner.x;
    }

    GetWindowRect( control, &rect );
    corner.x = rect.left; corner.y = rect.top;
    ScreenToClient( window, &corner );
    SetWindowPos( control, NULL, x, corner.y, width,
                  rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE );

    /* The repeat box a little apart from the two choices */
    x += width + ( i == 2 ? 24 : i ? 12 : 8 );
  }

  SelectObject( dc, old_font );
  ReleaseDC( window, dc );
}

/* The place of the window is kept in fuse-windows.ini */

static void
remember_position( player *p )
{
  RECT rect;

  if( !wnd( p ) || IsIconic( wnd( p ) ) ||
      !GetWindowRect( wnd( p ), &rect ) )
    return;
  winstate_set( p->key, "x", rect.left );
  winstate_set( p->key, "y", rect.top );
}

static void
restore_position( player *p )
{
  POINT title;
  long x, y;

  if( !winstate_get( p->key, "x", &x ) ||
      !winstate_get( p->key, "y", &y ) )
    return;

  /* Only if the title bar is on a screen (one may have been removed) */
  title.x = x + 40;
  title.y = y + 8;
  if( !MonitorFromPoint( title, MONITOR_DEFAULTTONULL ) ) return;

  SetWindowPos( wnd( p ), NULL, x, y, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
}

/* Whether the playlist and the mixer were open with the player, as it
   was closed; they open with it the next time. Before the first time:
   the list does, the mixer does not */
void
win32_ayplayer_remember_companions( int turbosound )
{
  player *p = &players[ turbosound ? 1 : 0 ];

  if( !wnd( p ) ) return;
  winstate_set( p->key, "with_list",
                win32_ayplaylist_is_open( p->turbosound ) );
  winstate_set( p->key, "with_mixer", win32_aymixer_is_open() &&
                                      win32_aymixer_owner() == wnd( p ) );
}

static int
remembered( const player *p, const char *key, int otherwise )
{
  long value;

  return winstate_get( p->key, key, &value ) ? value != 0 : otherwise;
}

int
win32_ayplayer_remembered_mixer( int turbosound )
{
  return remembered( &players[ turbosound ? 1 : 0 ], "with_mixer", 0 );
}

/* `with_list', `with_mixer': 1 open, 0 not, -1 as they were the last time
   the player was closed (only when the player opens; if it is already
   open, they are left as they are) */
static void
open_window( player *p, int with_list, int with_mixer, int activate )
{
  if( !wnd( p ) ) {
    if( !CreateDialogParam( fuse_hInstance, MAKEINTRESOURCE( p->dialog ),
                            fuse_hWnd, (DLGPROC)ayplayer_proc,
                            (LPARAM)p ) ) {
      *p->window = NULL;
      win32_verror( 1 );
      return;
    }
    /* After the texts were translated */
    layout_play_row( p );
    restore_position( p );
    /* A mixer already open (not with the other player) goes with the
       player from now on */
    win32_aymixer_adopt( wnd( p ) );

    if( with_list < 0 ) with_list = remembered( p, "with_list", 1 );
    if( with_mixer < 0 ) with_mixer = remembered( p, "with_mixer", 0 );
  } else {
    if( with_list < 0 ) with_list = 0;
    if( with_mixer < 0 ) with_mixer = 0;
  }

  ayplayer_set_frame_hook( tick_all );
  win32ui_ayplayer_timer_start();
  resume_if_paused();
  if( with_list ) win32_ayplaylist_show( p->turbosound );
  /* Owned by the player, as it exists by now */
  if( with_mixer ) win32_aymixer_show_for( wnd( p ) );
  update_all( p );
  if( IsIconic( wnd( p ) ) ) ShowWindow( wnd( p ), SW_RESTORE );
  ShowWindow( wnd( p ), activate ? SW_SHOW : SW_SHOWNOACTIVATE );
  if( activate ) SetForegroundWindow( wnd( p ) );
}

/* The file was opened, or the player from the menu: show the window (or
   update it), with the list and the mixer if they were open with it */
void
win32_ayplayer_show( void )
{
  open_window( &players[0], -1, -1, 1 );
}

void
win32_tsplayer_show( void )
{
  open_window( &players[1], -1, -1, 1 );
}

void
win32_ayplayer_show_for( int turbosound )
{
  open_window( &players[ turbosound ? 1 : 0 ], -1, -1, 1 );
}

/* Opened again as Fuse starts: with the list if it was open, standing on
   the song it stood on, without playing (the mixer is opened on its own,
   as it may have been open without the player) */
void
win32_ayplayer_reopen( int turbosound, int with_list, int song )
{
  player *p = &players[ turbosound ? 1 : 0 ];

  open_window( p, with_list, 0, 0 );
  win32_ayplaylist_choose( p->turbosound, song );
  update_all( p );
}

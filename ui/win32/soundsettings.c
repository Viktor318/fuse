/* soundsettings.c: Win32 Sound settings window
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

/* Options > Sound: whether there is sound, the AY stereo mode and the
   speaker type at the top; below them a column for the master volume and
   for each source (AY, beeper, uSpeech, SpecDrum, Covox), each with a
   fader, a level meter beside it, the volume in percent and a Mute box.
   Every change is applied at once (there is no Cancel); the window is
   modeless, the emulation goes on while it is open. The columns of the
   peripherals which are not on are greyed out.

   The faders are drawn here (the SOUND_FADER_CLASS window class), with
   their level meters: like those of the Sound settings of EightyOne */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h> /* windows.h must be included prior to commctrl.h */

#include "../../aymix.h"
#include "fuse.h"
#include "i18n.h"
#include "menu.h"
#include "options.h"
#include "periph.h"
#include "settings.h"
#include "sound.h"
#include "soundsettings.h"
#include "win32internals.h"
#include "win32text.h"
#include "winstate.h"

HWND fuse_hSoundWnd = NULL;

#define COLUMNS SOUND_SOURCES
#define SOUND_TIMER 1
#define TICK_MS 40

/* The level meters: 24 segments for -40 dB ... 0 dB (full scale), green,
   then yellow and red at the top; falling 1 dB a tick */
#define METER_SEGMENTS 24
#define METER_GREEN 16
#define METER_YELLOW 21
#define METER_RANGE_DB 40.0
#define METER_FALL ( 1.0 / METER_RANGE_DB )

/* The sound output Fuse was built with */
#if defined HAVE_WASAPI_AUDIO
#define OUTPUT_NAME "WASAPI"
#elif defined DIRECTSOUND_VERSION
#define OUTPUT_NAME "DirectSound"
#else
#define OUTPUT_NAME "waveOut"
#endif

typedef struct column_t {
  int source;			/* SOUND_SOURCE_... */
  int *volume, *mute;		/* the settings */
  const int *default_volume;
  int value;			/* the volume shown */
  int enabled;			/* not greyed out */
  double level;			/* of the meter, 0 ... 1 */
  int lit;			/* segments of the meter lit */
  int wheel_rest;		/* wheel movement less than a step */
} column_t;

static column_t columns[ COLUMNS ] = {
  { SOUND_SOURCE_MASTER, &settings_current.volume_master,
    &settings_current.mute_master, &settings_default.volume_master },
  { SOUND_SOURCE_AY, &settings_current.volume_ay,
    &settings_current.mute_ay, &settings_default.volume_ay },
  { SOUND_SOURCE_BEEPER, &settings_current.volume_beeper,
    &settings_current.mute_beeper, &settings_default.volume_beeper },
  { SOUND_SOURCE_USPEECH, &settings_current.volume_uspeech,
    &settings_current.mute_uspeech, &settings_default.volume_uspeech },
  { SOUND_SOURCE_SPECDRUM, &settings_current.volume_specdrum,
    &settings_current.mute_specdrum, &settings_default.volume_specdrum },
  { SOUND_SOURCE_COVOX, &settings_current.volume_covox,
    &settings_current.mute_covox, &settings_default.volume_covox },
};

/* The stereo modes in the order of the combo box (as in the AY mixer) */
static const char * const stereo_names[] =
  { "ABC", "ACB", "BAC", "BCA", "CBA", "CAB", "Mono", "Custom" };
static const int stereo_modes[] = {
  SOUND_STEREO_AY_ABC, SOUND_STEREO_AY_ACB, SOUND_STEREO_AY_BAC,
  SOUND_STEREO_AY_BCA, SOUND_STEREO_AY_CBA, SOUND_STEREO_AY_CAB,
  SOUND_STEREO_AY_NONE, SOUND_STEREO_AY_CUSTOM
};
#define STEREO_COUNT 8

/* The speaker types, as in options.dat */
static const char * const speaker_names[] =
  { "Automatic", "TV speaker", "Beeper", "Unfiltered" };
#define SPEAKER_COUNT 4

/* The window is being filled in: its notifications are not changes */
static int filling = 0;
/* The size of the faders' parts: pixels for one pixel at 100% */
static double fader_scale = 1.0;
/* The fader being dragged with the mouse, and where it was taken */
static column_t *dragging = NULL;
static int drag_offset = 0;
/* The percentage shown above the thumb while dragging */
static HWND bubble = NULL;
static WCHAR bubble_text[ 16 ];
static DWORD last_tick = 0;
static char output_shown[ 256 ];

static void close_window( void );
static void sync_settings( void );

static HWND
item( int id )
{
  return GetDlgItem( fuse_hSoundWnd, id );
}

static column_t *
fader_column( HWND fader )
{
  int index = GetDlgCtrlID( fader ) - IDC_SND_FADER1;

  return index >= 0 && index < COLUMNS ? &columns[ index ] : NULL;
}

static int
column_index( const column_t *c )
{
  return c - columns;
}

static int
clamp_percent( int value )
{
  return value < 0 ? 0 : value > 100 ? 100 : value;
}

/* ------------------------------------------------------------------------
   The faders
   ------------------------------------------------------------------------ */

/* The parts of a fader, in pixels: the scale (master volume only), the
   track with the thumb, the level meter; the top and the bottom of the
   track (100% and 0%) */
typedef struct fader_metrics_t {
  int scale_w, knob_w, knob_h, track_w, meter_x, meter_w, track_x;
  int top, bottom;
} fader_metrics_t;

static int
px( double size )
{
  int pixels = (int)( size * fader_scale + 0.5 );
  return pixels < 1 ? 1 : pixels;
}

/* The width of a fader: with the scale for the master volume */
static int
fader_width( int scale )
{
  return ( scale ? px( 26 ) : 0 ) + px( 22 ) + px( 3 ) + px( 12 );
}

static void
fader_metrics( HWND fader, const column_t *c, fader_metrics_t *m )
{
  RECT client;

  GetClientRect( fader, &client );
  m->scale_w = c->source == SOUND_SOURCE_MASTER ? px( 26 ) : 0;
  m->knob_w = px( 22 );
  m->knob_h = px( 12 ) | 1;		/* odd: the line in the middle */
  m->track_w = px( 6 );
  m->meter_w = px( 12 );
  m->track_x = m->scale_w + m->knob_w / 2;
  m->meter_x = m->scale_w + m->knob_w + px( 3 );
  m->top = m->knob_h / 2 + 1;
  m->bottom = client.bottom - 2 - m->knob_h / 2;
  if( m->bottom <= m->top ) m->bottom = m->top + 1;
}

static int
fader_y( const fader_metrics_t *m, int value )
{
  int range = m->bottom - m->top;

  return m->top + ( ( 100 - value ) * range + 50 ) / 100;
}

static int
fader_value_at( const fader_metrics_t *m, int y )
{
  int range = m->bottom - m->top;

  if( y <= m->top ) return 100;
  if( y >= m->bottom ) return 0;
  return 100 - ( ( y - m->top ) * 100 + range / 2 ) / range;
}

static void
fill( HDC dc, int left, int top, int right, int bottom, COLORREF colour )
{
  RECT rect;
  HBRUSH brush = CreateSolidBrush( colour );

  rect.left = left; rect.top = top; rect.right = right; rect.bottom = bottom;
  FillRect( dc, &rect, brush );
  DeleteObject( brush );
}

static void
fader_paint( HWND fader, HDC target )
{
  static const COLORREF on[3] =
    { RGB( 43, 209, 43 ), RGB( 227, 208, 42 ), RGB( 227, 58, 42 ) };
  static const COLORREF off[3] =
    { RGB( 29, 58, 29 ), RGB( 61, 58, 20 ), RGB( 61, 26, 26 ) };
  column_t *c = fader_column( fader );
  int enabled = IsWindowEnabled( fader );
  int focused = GetFocus() == fader;
  fader_metrics_t m;
  RECT client;
  HDC dc;
  HBITMAP bitmap, old_bitmap;
  int width, height, ky, i, left, pad, gap, inner_top, inner_bottom;

  if( !c ) return;

  GetClientRect( fader, &client );
  width = client.right;
  height = client.bottom;
  fader_metrics( fader, c, &m );
  ky = fader_y( &m, c->value );

  dc = CreateCompatibleDC( target );
  bitmap = CreateCompatibleBitmap( target, width, height );
  old_bitmap = SelectObject( dc, bitmap );

  fill( dc, 0, 0, width, height, GetSysColor( COLOR_BTNFACE ) );

  /* The scale of the master volume: 100, 75, 50, 25, 0 */
  if( m.scale_w ) {
    HFONT font = (HFONT)SendMessage( fuse_hSoundWnd, WM_GETFONT, 0, 0 );
    HFONT old_font = font ? SelectObject( dc, font ) : NULL;

    SetBkMode( dc, TRANSPARENT );
    SetTextColor( dc, enabled ? RGB( 85, 85, 85 ) :
                                GetSysColor( COLOR_GRAYTEXT ) );
    for( i = 0; i <= 100; i += 25 ) {
      char text[ 8 ];
      RECT rect;
      int y = fader_y( &m, i );

      _snprintf( text, sizeof( text ), "%d", i );
      text[ sizeof( text ) - 1 ] = '\0';
      rect.left = 0; rect.right = m.scale_w - px( 5 );
      rect.top = y - px( 10 ); rect.bottom = y + px( 10 );
      DrawTextA( dc, text, -1, &rect,
                 DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX );
    }
    if( old_font ) SelectObject( dc, old_font );
  }

  /* The track, blue from the thumb down */
  left = m.track_x - m.track_w / 2;
  fill( dc, left, m.top, left + m.track_w, m.bottom + 1,
        RGB( 200, 200, 200 ) );
  fill( dc, left, ky, left + m.track_w, m.bottom + 1,
        enabled ? RGB( 0, 103, 192 ) : RGB( 169, 169, 169 ) );

  /* The level meter: dark where the level does not reach */
  fill( dc, m.meter_x, m.top, m.meter_x + m.meter_w, m.bottom + 1,
        RGB( 34, 34, 34 ) );
  pad = px( 2 );
  gap = px( 2 );
  inner_top = m.top + pad;
  inner_bottom = m.bottom + 1 - pad;
  for( i = 0; i < METER_SEGMENTS; i++ ) {
    int range = inner_bottom - inner_top;
    int bottom = inner_bottom - i * range / METER_SEGMENTS;
    int top = inner_bottom - ( i + 1 ) * range / METER_SEGMENTS + gap;
    int zone = i < METER_GREEN ? 0 : i < METER_YELLOW ? 1 : 2;
    int lit = enabled && i < c->lit;

    if( top >= bottom ) top = bottom - 1;
    fill( dc, m.meter_x + pad, top, m.meter_x + m.meter_w - pad, bottom,
          lit ? on[ zone ] : off[ zone ] );
  }

  /* The thumb, framed in blue when it has the keyboard focus */
  {
    int kl = m.track_x - m.knob_w / 2, kt = ky - m.knob_h / 2;
    int kr = kl + m.knob_w, kb = kt + m.knob_h;
    COLORREF frame = !enabled ? RGB( 170, 170, 170 ) :
                     focused ? RGB( 0, 103, 192 ) : RGB( 118, 118, 118 );
    int border = focused ? px( 2 ) : 1;

    fill( dc, kl, kt, kr, kb, frame );
    fill( dc, kl + border, kt + border, kr - border, kb - border,
          enabled ? RGB( 245, 245, 245 ) : RGB( 232, 232, 232 ) );
    fill( dc, kl + px( 5 ), ky, kr - px( 5 ), ky + 1,
          enabled ? RGB( 85, 85, 85 ) : RGB( 160, 160, 160 ) );
  }

  BitBlt( target, 0, 0, width, height, dc, 0, 0, SRCCOPY );
  SelectObject( dc, old_bitmap );
  DeleteObject( bitmap );
  DeleteDC( dc );
}

/* The percentage in a small window above the thumb, while dragging */
static void
bubble_show( HWND fader, const column_t *c )
{
  TOOLINFOW info;
  fader_metrics_t m;
  POINT point;
  LRESULT size;

  if( !bubble ) return;

  _snwprintf( bubble_text, sizeof( bubble_text ) / sizeof( bubble_text[0] ),
              L"%d%%", c->value );
  bubble_text[ sizeof( bubble_text ) / sizeof( bubble_text[0] ) - 1 ] = 0;

  memset( &info, 0, sizeof( info ) );
  info.cbSize = sizeof( info );
  info.hwnd = fuse_hSoundWnd;
  info.uId = 1;
  info.lpszText = bubble_text;
  SendMessageW( bubble, TTM_UPDATETIPTEXTW, 0, (LPARAM)&info );

  fader_metrics( fader, c, &m );
  point.x = m.track_x;
  point.y = fader_y( &m, c->value ) - m.knob_h / 2 - px( 3 );
  ClientToScreen( fader, &point );
  size = SendMessageW( bubble, TTM_GETBUBBLESIZE, 0, (LPARAM)&info );
  SendMessageW( bubble, TTM_TRACKPOSITION, 0,
                MAKELPARAM( point.x - LOWORD( size ) / 2,
                            point.y - HIWORD( size ) ) );
  SendMessageW( bubble, TTM_TRACKACTIVATE, TRUE, (LPARAM)&info );
}

static void
bubble_hide( void )
{
  TOOLINFOW info;

  if( !bubble ) return;
  memset( &info, 0, sizeof( info ) );
  info.cbSize = sizeof( info );
  info.hwnd = fuse_hSoundWnd;
  info.uId = 1;
  SendMessageW( bubble, TTM_TRACKACTIVATE, FALSE, (LPARAM)&info );
}

static void
bubble_create( void )
{
  TOOLINFOW info;

  bubble = CreateWindowExW( WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                            CW_USEDEFAULT, fuse_hSoundWnd, NULL,
                            fuse_hInstance, NULL );
  if( !bubble ) return;

  memset( &info, 0, sizeof( info ) );
  info.cbSize = sizeof( info );
  info.uFlags = TTF_TRACK | TTF_ABSOLUTE;
  info.hwnd = fuse_hSoundWnd;
  info.uId = 1;
  bubble_text[0] = 0;
  info.lpszText = bubble_text;
  SendMessageW( bubble, TTM_ADDTOOLW, 0, (LPARAM)&info );
}

/* ------------------------------------------------------------------------
   Changes
   ------------------------------------------------------------------------ */

static void
show_percent( const column_t *c )
{
  char text[ 16 ];

  _snprintf( text, sizeof( text ), "%d%%", c->value );
  text[ sizeof( text ) - 1 ] = '\0';
  SetDlgItemTextA( fuse_hSoundWnd, IDC_SND_PERCENT1 + column_index( c ),
                   text );
}

/* The AY volume is heard by the AY player too, and shown there */
static void
volume_applied( const column_t *c )
{
  if( c->source != SOUND_SOURCE_AY ) return;
  sound_ay_volume_changed();
  win32_aymix_changed();
}

/* A fader moved: the volume is heard at once (the sound code follows the
   other volumes by itself) */
static void
column_set( column_t *c, int value )
{
  HWND fader = item( IDC_SND_FADER1 + column_index( c ) );

  value = clamp_percent( value );
  if( value != c->value ) {
    c->value = value;
    *c->volume = value;
    show_percent( c );
    InvalidateRect( fader, NULL, FALSE );
    volume_applied( c );
  }
  if( dragging == c ) bubble_show( fader, c );
}

static void
fader_wheel( column_t *c, int delta, int big )
{
  int steps;

  c->wheel_rest += delta;
  steps = c->wheel_rest / WHEEL_DELTA;
  c->wheel_rest -= steps * WHEEL_DELTA;
  if( steps ) column_set( c, c->value + steps * ( big ? 10 : 1 ) );
}

static LRESULT CALLBACK
fader_proc( HWND fader, UINT msg, WPARAM wParam, LPARAM lParam )
{
  column_t *c = fader_column( fader );
  fader_metrics_t m;

  if( !c || !fuse_hSoundWnd )
    return DefWindowProcW( fader, msg, wParam, lParam );

  switch( msg ) {

  case WM_PAINT:
    {
      PAINTSTRUCT paint;
      HDC dc = BeginPaint( fader, &paint );
      fader_paint( fader, dc );
      EndPaint( fader, &paint );
    }
    return 0;

  /* WM_PAINT covers everything: no erasing first, so no flicker */
  case WM_ERASEBKGND:
    return 1;

  case WM_LBUTTONDOWN:
    {
      int y = GET_Y_LPARAM( lParam ), ky;

      SetFocus( fader );
      fader_metrics( fader, c, &m );
      ky = fader_y( &m, c->value );
      /* On the thumb: dragged from where it was taken; elsewhere: it
         jumps there, and is dragged from there */
      if( y >= ky - m.knob_h / 2 - 1 && y <= ky + m.knob_h / 2 + 1 ) {
        drag_offset = y - ky;
      } else {
        drag_offset = 0;
        column_set( c, fader_value_at( &m, y ) );
      }
      dragging = c;
      SetCapture( fader );
      bubble_show( fader, c );
    }
    return 0;

  case WM_MOUSEMOVE:
    if( dragging == c && GetCapture() == fader ) {
      fader_metrics( fader, c, &m );
      column_set( c, fader_value_at( &m, GET_Y_LPARAM( lParam ) -
                                         drag_offset ) );
    }
    return 0;

  case WM_LBUTTONUP:
    if( GetCapture() == fader ) ReleaseCapture();
    return 0;

  case WM_CAPTURECHANGED:
    if( dragging == c ) {
      dragging = NULL;
      bubble_hide();
    }
    return 0;

  /* Back to the default value of the setting */
  case WM_LBUTTONDBLCLK:
    SetFocus( fader );
    column_set( c, *c->default_volume );
    return 0;

  case WM_MOUSEWHEEL:
    fader_wheel( c, GET_WHEEL_DELTA_WPARAM( wParam ),
                 ( GET_KEYSTATE_WPARAM( wParam ) & MK_SHIFT ) != 0 );
    return 0;

  case WM_KEYDOWN:
    {
      int step = GetKeyState( VK_SHIFT ) < 0 ? 10 : 1;

      switch( wParam ) {
      case VK_UP: case VK_RIGHT: column_set( c, c->value + step ); return 0;
      case VK_DOWN: case VK_LEFT: column_set( c, c->value - step ); return 0;
      case VK_PRIOR: column_set( c, c->value + 10 ); return 0;
      case VK_NEXT: column_set( c, c->value - 10 ); return 0;
      case VK_HOME: column_set( c, 100 ); return 0;
      case VK_END: column_set( c, 0 ); return 0;
      }
    }
    break;

  /* The arrow keys are for the fader, not for going to the next control */
  case WM_GETDLGCODE:
    return DLGC_WANTARROWS;

  case WM_SETFOCUS:
  case WM_KILLFOCUS:
  case WM_ENABLE:
    InvalidateRect( fader, NULL, FALSE );
    if( msg == WM_KILLFOCUS && dragging == c && GetCapture() == fader )
      ReleaseCapture();
    return 0;
  }

  return DefWindowProcW( fader, msg, wParam, lParam );
}

static void
register_fader_class( void )
{
  static int registered = 0;
  WNDCLASSW wc;

  if( registered ) return;

  memset( &wc, 0, sizeof( wc ) );
  wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = fader_proc;
  wc.hInstance = fuse_hInstance;
  wc.hCursor = LoadCursor( NULL, IDC_ARROW );
  wc.lpszClassName = L"" SOUND_FADER_CLASS;
  if( RegisterClassW( &wc ) ) registered = 1;
}

/* The mouse wheel moves the fader under the mouse, whichever control has
   the keyboard focus; called with every message of the main loop */
int
win32_soundsettings_wheel( const MSG *msg )
{
  POINT point;
  HWND under;
  column_t *c;

  if( !fuse_hSoundWnd || msg->message != WM_MOUSEWHEEL ) return 0;

  point.x = GET_X_LPARAM( msg->lParam );
  point.y = GET_Y_LPARAM( msg->lParam );
  under = WindowFromPoint( point );
  if( !under || GetParent( under ) != fuse_hSoundWnd ) return 0;
  c = fader_column( under );
  if( !c || !IsWindowEnabled( under ) ) return 0;

  fader_wheel( c, GET_WHEEL_DELTA_WPARAM( msg->wParam ),
               ( GET_KEYSTATE_WPARAM( msg->wParam ) & MK_SHIFT ) != 0 );
  return 1;
}

/* ------------------------------------------------------------------------
   Showing the settings
   ------------------------------------------------------------------------ */

/* Is the source of a column there (the peripherals of the machine)? */
static int
column_available( const column_t *c )
{
  switch( c->source ) {
  case SOUND_SOURCE_USPEECH:
    return periph_is_active( PERIPH_TYPE_USPEECH );
  case SOUND_SOURCE_SPECDRUM:
    return periph_is_active( PERIPH_TYPE_SPECDRUM );
  case SOUND_SOURCE_COVOX:
    return periph_is_active( PERIPH_TYPE_COVOX_FB ) ||
           periph_is_active( PERIPH_TYPE_COVOX_DD );
  default:
    return 1;
  }
}

static void
show_output( void )
{
  char text[ 256 ], first[ 128 ];

  if( settings_current.sound ) {
    _snprintf( first, sizeof( first ), i18n( "Output: %s, %d Hz" ),
               OUTPUT_NAME, settings_current.sound_freq );
    first[ sizeof( first ) - 1 ] = '\0';
    _snprintf( text, sizeof( text ), "%s  %s  %s", first,
               win32text_middot(), i18n( "changes are heard at once" ) );
  } else {
    _snprintf( first, sizeof( first ), i18n( "Output: %s" ), OUTPUT_NAME );
    first[ sizeof( first ) - 1 ] = '\0';
    _snprintf( text, sizeof( text ), "%s  %s  %s", first,
               win32text_middot(), i18n( "the sound is off" ) );
  }
  text[ sizeof( text ) - 1 ] = '\0';

  if( strcmp( text, output_shown ) ) {
    strcpy( output_shown, text );
    SetDlgItemTextA( fuse_hSoundWnd, IDC_SND_OUTPUT, text );
  }
}

static void
check( int id, int on )
{
  int checked = IsDlgButtonChecked( fuse_hSoundWnd, id ) == BST_CHECKED;

  if( checked != !!on )
    CheckDlgButton( fuse_hSoundWnd, id, on ? BST_CHECKED : BST_UNCHECKED );
}

static void
select_item( int id, int index )
{
  HWND combo = item( id );

  if( SendMessage( combo, CB_GETDROPPEDSTATE, 0, 0 ) ) return;
  if( SendMessage( combo, CB_GETCURSEL, 0, 0 ) != index )
    SendMessage( combo, CB_SETCURSEL, index, 0 );
}

/* Everything as the settings are now (another window, a menu or a .mix
   file may have changed them); the columns of the peripherals which are
   not on greyed out */
static void
sync_settings( void )
{
  int stereo = sound_ay_stereo_setting(), i;

  if( !fuse_hSoundWnd ) return;
  filling = 1;

  check( IDC_SND_ENABLED, settings_current.sound );
  check( IDC_SND_LOADING, settings_current.sound_load );

  for( i = 0; i < STEREO_COUNT; i++ )
    if( stereo_modes[i] == stereo ) select_item( IDC_SND_STEREO, i );
  i = option_enumerate_sound_speaker_type();
  if( i >= 0 && i < SPEAKER_COUNT ) select_item( IDC_SND_SPEAKER, i );

  for( i = 0; i < COLUMNS; i++ ) {
    column_t *c = &columns[i];
    int value = clamp_percent( *c->volume );
    int enabled = column_available( c );

    if( value != c->value && dragging != c ) {
      c->value = value;
      show_percent( c );
      InvalidateRect( item( IDC_SND_FADER1 + i ), NULL, FALSE );
    }
    check( IDC_SND_MUTE1 + i, *c->mute );

    if( enabled != c->enabled ) {
      c->enabled = enabled;
      if( !enabled && dragging == c ) ReleaseCapture();
      EnableWindow( item( IDC_SND_GROUP1 + i ), enabled );
      EnableWindow( item( IDC_SND_FADER1 + i ), enabled );
      EnableWindow( item( IDC_SND_PERCENT1 + i ), enabled );
      EnableWindow( item( IDC_SND_MUTE1 + i ), enabled );
    }
  }

  show_output();
  filling = 0;
}

/* The AY player or the AY mixer changed the AY volume or the stereo mode */
void
win32_options_sound_sync( void )
{
  sync_settings();
}

/* The level meters, every tick: the highest level since the last one, in
   dB, falling slowly; dark without sound and for a greyed out column */
static void
update_meters( void )
{
  int i;

  for( i = 0; i < COLUMNS; i++ ) {
    column_t *c = &columns[i];
    int peak = sound_meter_take( c->source ), lit;
    double level = 0;

    if( settings_current.sound && c->enabled ) {
      if( peak > 0 ) {
        level = 1.0 + 20.0 * log10( peak / 32767.0 ) / METER_RANGE_DB;
        if( level < 0 ) level = 0;
        if( level > 1 ) level = 1;
      }
      if( level < c->level - METER_FALL ) level = c->level - METER_FALL;
      if( level < 0 ) level = 0;
    }

    c->level = level;
    lit = (int)( level * METER_SEGMENTS + 0.5 );
    if( lit != c->lit ) {
      c->lit = lit;
      InvalidateRect( item( IDC_SND_FADER1 + i ), NULL, FALSE );
    }
  }
}

/* Called every frame by the main loop, and by the window's timer (the
   emulation may be paused): the meters and the settings every 40 ms */
void
win32_soundsettings_tick( void )
{
  DWORD now = GetTickCount();

  if( !fuse_hSoundWnd || IsIconic( fuse_hSoundWnd ) ) return;
  if( now - last_tick < TICK_MS ) return;
  last_tick = now;

  sync_settings();
  update_meters();
}

/* ------------------------------------------------------------------------
   The controls
   ------------------------------------------------------------------------ */

/* Sound on or off: the output is closed or opened again, as after the
   Options dialogs */
static void
set_sound_enabled( int on )
{
  if( !!settings_current.sound == !!on ) return;
  fuse_emulation_pause();
  settings_current.sound = on ? 1 : 0;
  fuse_emulation_unpause();
  show_output();
}

/* Every volume to its default, nothing muted, the stereo mode and the
   speaker type to their defaults; the two check boxes stay */
static void
reset( void )
{
  int i;

  for( i = 0; i < COLUMNS; i++ ) {
    *columns[i].volume = *columns[i].default_volume;
    *columns[i].mute = 0;
  }
  settings_set_string( &settings_current.speaker_type,
                       settings_default.speaker_type );
  /* The AY mixer and the AY player show it too */
  aymix_set_stereo( settings_default.stereo_ay ? settings_default.stereo_ay :
                                                 "Mono" );
  sound_ay_volume_changed();
  win32_aymix_changed();
  sync_settings();
}

static void
command( WORD id, WORD code )
{
  int index;

  if( filling ) return;

  switch( id ) {

  case IDC_SND_ENABLED:
    if( code == BN_CLICKED )
      set_sound_enabled( IsDlgButtonChecked( fuse_hSoundWnd, id ) ==
                         BST_CHECKED );
    return;

  case IDC_SND_LOADING:
    if( code == BN_CLICKED )
      settings_current.sound_load =
        IsDlgButtonChecked( fuse_hSoundWnd, id ) == BST_CHECKED;
    return;

  case IDC_SND_STEREO:
    if( code == CBN_SELCHANGE ) {
      int i = SendDlgItemMessage( fuse_hSoundWnd, id, CB_GETCURSEL, 0, 0 );
      /* The AY mixer and the AY player show it too */
      if( i >= 0 && i < STEREO_COUNT ) aymix_set_stereo( stereo_names[i] );
    }
    return;

  case IDC_SND_SPEAKER:
    if( code == CBN_SELCHANGE ) {
      int i = SendDlgItemMessage( fuse_hSoundWnd, id, CB_GETCURSEL, 0, 0 );
      if( i >= 0 && i < SPEAKER_COUNT )
        settings_set_string( &settings_current.speaker_type,
                             speaker_names[i] );
    }
    return;

  case IDC_SND_RESET:
    if( code == BN_CLICKED ) reset();
    return;

  case IDC_SND_MIXER:
    if( code == BN_CLICKED ) win32_aymixer_show();
    return;

  case IDOK:
  case IDCANCEL:
    close_window();
    return;
  }

  index = id - IDC_SND_MUTE1;
  if( index >= 0 && index < COLUMNS && code == BN_CLICKED ) {
    *columns[ index ].mute =
      IsDlgButtonChecked( fuse_hSoundWnd, id ) == BST_CHECKED;
    volume_applied( &columns[ index ] );
  }
}

/* ------------------------------------------------------------------------
   The layout: everything as wide as its (translated) text needs
   ------------------------------------------------------------------------ */

static int
dlu_x( int units )
{
  RECT rect = { 0, 0, 0, 0 };

  rect.right = units;
  MapDialogRect( fuse_hSoundWnd, &rect );
  return rect.right;
}

static int
dlu_y( int units )
{
  RECT rect = { 0, 0, 0, 0 };

  rect.bottom = units;
  MapDialogRect( fuse_hSoundWnd, &rect );
  return rect.bottom;
}

static void
place( int id, int x, int y, int width, int height )
{
  SetWindowPos( item( id ), NULL, x, y, width, height,
                SWP_NOZORDER | SWP_NOACTIVATE );
}

static int
text_width( int id )
{
  HWND control = item( id );
  WCHAR text[ 256 ];
  HDC dc = GetDC( control );
  HFONT old = SelectObject( dc, (HFONT)SendMessage( control, WM_GETFONT, 0,
                                                     0 ) );
  RECT size = { 0, 0, 0, 0 };

  GetWindowTextW( control, text, sizeof( text ) / sizeof( text[0] ) );
  DrawTextW( dc, text, -1, &size, DT_CALCRECT | DT_SINGLELINE );
  SelectObject( dc, old );
  ReleaseDC( control, dc );
  return size.right;
}

/* A drop-down list as wide as its longest item */
static int
combo_width( int id, int minimum )
{
  HWND combo = item( id );
  HDC dc = GetDC( combo );
  HFONT old = SelectObject( dc, (HFONT)SendMessage( combo, WM_GETFONT, 0,
                                                     0 ) );
  LRESULT count = SendMessageW( combo, CB_GETCOUNT, 0, 0 ), i;
  int widest = 0;

  for( i = 0; i < count; i++ ) {
    WCHAR text[ 128 ];
    RECT size = { 0, 0, 0, 0 };

    if( SendMessageW( combo, CB_GETLBTEXTLEN, i, 0 ) >=
        (LRESULT)( sizeof( text ) / sizeof( text[0] ) ) )
      continue;
    SendMessageW( combo, CB_GETLBTEXT, i, (LPARAM)text );
    DrawTextW( dc, text, -1, &size, DT_CALCRECT | DT_SINGLELINE );
    if( size.right > widest ) widest = size.right;
  }
  SelectObject( dc, old );
  ReleaseDC( combo, dc );

  widest += GetSystemMetrics( SM_CXVSCROLL ) + dlu_x( 8 );
  return widest > minimum ? widest : minimum;
}

static int
max_int( int a, int b )
{
  return a > b ? a : b;
}

static void
layout( void )
{
  int margin = dlu_x( 7 ), check_extra, i;
  int frame_w, frame_gap = dlu_x( 6 ), inset = dlu_x( 7 );
  int x, y, w, top_row, group_w, client_w, client_h;
  int stereo_label, stereo_w, speaker_label, speaker_w, reset_w;
  int group_top, frame_top, frame_h, fader_top, fader_h, percent_y;
  int mute_y, mixer_y, hint_y, group_h, ok_y, ok_w;
  int row_h = dlu_y( 14 );

  {
    RECT units = { 0, 0, 100, 0 };
    MapDialogRect( fuse_hSoundWnd, &units );
    /* Ms Shell Dlg 2 at 8 points: 1.5 pixels a dialog unit at 96 dpi */
    fader_scale = units.right / 150.0;
  }
  check_extra = GetSystemMetrics( SM_CXMENUCHECK ) + dlu_x( 4 );

  /* The width of the columns: the widest of their parts */
  frame_w = dlu_x( 66 );
  for( i = 0; i < COLUMNS; i++ ) {
    frame_w = max_int( frame_w, text_width( IDC_SND_GROUP1 + i ) +
                                dlu_x( 16 ) );
    frame_w = max_int( frame_w, text_width( IDC_SND_MUTE1 + i ) +
                                check_extra + dlu_x( 8 ) );
  }
  frame_w = max_int( frame_w, text_width( IDC_SND_MIXER ) + dlu_x( 20 ) );
  frame_w = max_int( frame_w, fader_width( 1 ) + dlu_x( 8 ) );
  group_w = 2 * inset + COLUMNS * frame_w + ( COLUMNS - 1 ) * frame_gap;
  group_w = max_int( group_w, text_width( IDC_SND_HINT ) + 2 * inset );

  /* The top rows */
  stereo_label = text_width( IDC_SND_STEREO_LABEL );
  stereo_w = combo_width( IDC_SND_STEREO, dlu_x( 56 ) );
  speaker_label = text_width( IDC_SND_SPEAKER_LABEL );
  speaker_w = combo_width( IDC_SND_SPEAKER, dlu_x( 64 ) );
  reset_w = max_int( dlu_x( 56 ), text_width( IDC_SND_RESET ) +
                                  dlu_x( 12 ) );
  top_row = stereo_label + dlu_x( 4 ) + stereo_w + dlu_x( 14 ) +
            speaker_label + dlu_x( 4 ) + speaker_w + dlu_x( 14 ) + reset_w;
  group_w = max_int( group_w, top_row );
  client_w = group_w + 2 * margin;

  y = dlu_y( 7 );
  w = text_width( IDC_SND_ENABLED ) + check_extra;
  place( IDC_SND_ENABLED, margin, y, w, dlu_y( 10 ) );
  place( IDC_SND_LOADING, margin + w + dlu_x( 16 ), y,
         text_width( IDC_SND_LOADING ) + check_extra, dlu_y( 10 ) );

  y = dlu_y( 21 );
  x = margin;
  place( IDC_SND_STEREO_LABEL, x, y + dlu_y( 3 ), stereo_label, dlu_y( 8 ) );
  x += stereo_label + dlu_x( 4 );
  place( IDC_SND_STEREO, x, y, stereo_w, dlu_y( 110 ) );
  x += stereo_w + dlu_x( 14 );
  place( IDC_SND_SPEAKER_LABEL, x, y + dlu_y( 3 ), speaker_label,
         dlu_y( 8 ) );
  x += speaker_label + dlu_x( 4 );
  place( IDC_SND_SPEAKER, x, y, speaker_w, dlu_y( 80 ) );
  place( IDC_SND_RESET, client_w - margin - reset_w, y - 1, reset_w, row_h );

  /* The mixing group, with a framed column for each source */
  group_top = dlu_y( 41 );
  frame_top = group_top + dlu_y( 12 );
  fader_top = frame_top + dlu_y( 13 );
  fader_h = dlu_y( 122 );
  percent_y = fader_top + fader_h + dlu_y( 3 );
  mute_y = percent_y + dlu_y( 11 );
  mixer_y = mute_y + dlu_y( 14 );
  frame_h = mixer_y + row_h + dlu_y( 6 ) - frame_top;
  hint_y = frame_top + frame_h + dlu_y( 5 );
  group_h = hint_y + dlu_y( 8 ) + dlu_y( 6 ) - group_top;

  place( IDC_SND_MIXING, margin, group_top, group_w, group_h );
  for( i = 0; i < COLUMNS; i++ ) {
    int left = margin + inset + i * ( frame_w + frame_gap );
    int fader_w = fader_width( columns[i].source == SOUND_SOURCE_MASTER );
    int mute_w = text_width( IDC_SND_MUTE1 + i ) + check_extra;

    place( IDC_SND_GROUP1 + i, left, frame_top, frame_w, frame_h );
    place( IDC_SND_FADER1 + i, left + ( frame_w - fader_w ) / 2, fader_top,
           fader_w, fader_h );
    place( IDC_SND_PERCENT1 + i, left + 2, percent_y, frame_w - 4,
           dlu_y( 9 ) );
    place( IDC_SND_MUTE1 + i, left + ( frame_w - mute_w ) / 2, mute_y,
           mute_w, dlu_y( 10 ) );
    if( columns[i].source == SOUND_SOURCE_AY )
      place( IDC_SND_MIXER, left + dlu_x( 8 ), mixer_y,
             frame_w - 2 * dlu_x( 8 ), row_h );
  }
  place( IDC_SND_HINT, margin + inset, hint_y, group_w - 2 * inset,
         dlu_y( 9 ) );

  /* The bottom line: the output, and OK */
  ok_w = max_int( dlu_x( 50 ), text_width( IDOK ) + dlu_x( 12 ) );
  ok_y = group_top + group_h + dlu_y( 7 );
  place( IDOK, client_w - margin - ok_w, ok_y, ok_w, row_h );
  place( IDC_SND_OUTPUT, margin, ok_y + dlu_y( 3 ),
         client_w - 2 * margin - ok_w - dlu_x( 8 ), dlu_y( 9 ) );
  client_h = ok_y + row_h + dlu_y( 7 );

  /* The window around it */
  {
    RECT rect = { 0, 0, 0, 0 };
    rect.right = client_w;
    rect.bottom = client_h;
    AdjustWindowRectEx( &rect, GetWindowLong( fuse_hSoundWnd, GWL_STYLE ),
                        FALSE, GetWindowLong( fuse_hSoundWnd, GWL_EXSTYLE ) );
    SetWindowPos( fuse_hSoundWnd, NULL, 0, 0, rect.right - rect.left,
                  rect.bottom - rect.top,
                  SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
  }
  InvalidateRect( fuse_hSoundWnd, NULL, TRUE );
}

/* ------------------------------------------------------------------------
   The window
   ------------------------------------------------------------------------ */

static void
remember_position( void )
{
  RECT rect;

  if( !fuse_hSoundWnd || IsIconic( fuse_hSoundWnd ) ||
      !GetWindowRect( fuse_hSoundWnd, &rect ) )
    return;
  winstate_set( "sound", "x", rect.left );
  winstate_set( "sound", "y", rect.top );
}

/* The place of last time; the first time over the middle of the main
   window, on the screen */
static void
restore_position( void )
{
  MONITORINFO monitor;
  POINT title;
  RECT main_window, window;
  long x, y;
  int dx = 0, dy = 0;

  if( winstate_get( "sound", "x", &x ) && winstate_get( "sound", "y", &y ) ) {
    /* Only if the title bar is on a screen (one may have been removed) */
    title.x = x + 40;
    title.y = y + 8;
    if( MonitorFromPoint( title, MONITOR_DEFAULTTONULL ) ) {
      SetWindowPos( fuse_hSoundWnd, NULL, x, y, 0, 0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
      return;
    }
  }

  if( !GetWindowRect( fuse_hWnd, &main_window ) ||
      !GetWindowRect( fuse_hSoundWnd, &window ) )
    return;
  x = ( main_window.left + main_window.right ) / 2 -
      ( window.right - window.left ) / 2;
  y = ( main_window.top + main_window.bottom ) / 2 -
      ( window.bottom - window.top ) / 2;
  OffsetRect( &window, x - window.left, y - window.top );

  monitor.cbSize = sizeof( monitor );
  if( GetMonitorInfo( MonitorFromRect( &main_window,
                                       MONITOR_DEFAULTTONEAREST ),
                      &monitor ) ) {
    if( window.right > monitor.rcWork.right )
      dx = monitor.rcWork.right - window.right;
    if( window.bottom > monitor.rcWork.bottom )
      dy = monitor.rcWork.bottom - window.bottom;
    if( window.left + dx < monitor.rcWork.left )
      dx = monitor.rcWork.left - window.left;
    if( window.top + dy < monitor.rcWork.top )
      dy = monitor.rcWork.top - window.top;
  }
  SetWindowPos( fuse_hSoundWnd, NULL, window.left + dx, window.top + dy, 0,
                0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
}

static void
close_window( void )
{
  if( !fuse_hSoundWnd ) return;
  if( dragging ) ReleaseCapture();
  remember_position();
  KillTimer( fuse_hSoundWnd, SOUND_TIMER );
  DestroyWindow( fuse_hSoundWnd );
  fuse_hSoundWnd = NULL;
  bubble = NULL;			/* went with the window */
  dragging = NULL;
}

static INT_PTR CALLBACK
sound_proc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
  int i;

  switch( msg ) {

  case WM_INITDIALOG:
    fuse_hSoundWnd = hWnd;
    SendMessage( hWnd, WM_SETICON, ICON_SMALL,
                 (LPARAM)LoadIcon( fuse_hInstance, "win32_icon" ) );
    for( i = 0; i < STEREO_COUNT; i++ )
      SendDlgItemMessageA( hWnd, IDC_SND_STEREO, CB_ADDSTRING, 0,
                           (LPARAM)i18n( stereo_names[i] ) );
    for( i = 0; i < SPEAKER_COUNT; i++ )
      SendDlgItemMessageA( hWnd, IDC_SND_SPEAKER, CB_ADDSTRING, 0,
                           (LPARAM)i18n( speaker_names[i] ) );
    for( i = 0; i < COLUMNS; i++ ) {
      /* The micro sign of uSpeech as Unicode, so that it shows whatever
         the code page */
      if( columns[i].source == SOUND_SOURCE_USPEECH )
        SetDlgItemTextW( hWnd, IDC_SND_GROUP1 + i, L"\x00b5Speech" );
      columns[i].value = -1;
      columns[i].enabled = -1;
      columns[i].level = 0;
      columns[i].lit = 0;
      columns[i].wheel_rest = 0;
      sound_meter_take( columns[i].source );
    }
    output_shown[0] = '\0';
    bubble_create();
    SetTimer( hWnd, SOUND_TIMER, TICK_MS, NULL );
    return TRUE;

  case WM_TIMER:
    if( wParam == SOUND_TIMER ) {
      win32_soundsettings_tick();
      return 0;
    }
    break;

  case WM_COMMAND:
    command( LOWORD( wParam ), HIWORD( wParam ) );
    return 0;

  /* Moving the window: the sound goes on */
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

  (void)lParam;
  return FALSE;
}

/* Open the window, or bring it to the front */
void
win32_soundsettings_show( void )
{
  /* Changes of the stereo mode are shown by every window */
  aymix_set_changed_hook( win32_aymix_changed );

  if( !fuse_hSoundWnd ) {
    register_fader_class();
    if( !CreateDialog( fuse_hInstance, MAKEINTRESOURCE( IDD_SOUNDSETTINGS ),
                       fuse_hWnd, (DLGPROC)sound_proc ) ) {
      fuse_hSoundWnd = NULL;
      win32_verror( 1 );
      return;
    }
    /* After the texts were translated */
    layout();
    restore_position();
  }

  sync_settings();
  ShowWindow( fuse_hSoundWnd, IsIconic( fuse_hSoundWnd ) ? SW_RESTORE :
                                                           SW_SHOW );
  SetForegroundWindow( fuse_hSoundWnd );
}

/* Options > Sound */
void
menu_options_sound( int action GCC_UNUSED )
{
  win32_soundsettings_show();
}

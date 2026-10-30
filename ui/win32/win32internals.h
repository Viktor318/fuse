/* win32internals.h: stuff internal to the Win32 UI
   Copyright (c) 2004-2008 Marek Januszewski

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

   Author contact information:

   E-mail: philip-fuse@shadowmagic.org.uk

*/

#ifndef FUSE_WIN32INTERNALS_H
#define FUSE_WIN32INTERNALS_H

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

/* FIXME: this should be included as part of windows.h, but is not
          because WIN32_LEAN_AND_MEAN is defined along the way somewhere */
#include <mmsystem.h>

#include "libspectrum.h"

#define ID_STATUSBAR 900

/* Reduce listview flickering. Defined from WINVER >= 6.00 */
#ifndef LVS_EX_DOUBLEBUFFER
#define LVS_EX_DOUBLEBUFFER 0x00010000
#endif

/* window handler */
extern HWND fuse_hWnd;

/* application instance */
extern HINSTANCE fuse_hInstance;

/* status bar handle */
extern HWND fuse_hStatusWindow;

/* pokefinder window handle */
extern HWND fuse_hPFWnd;

/* debugger window handle */
extern HWND fuse_hDBGWnd;

/* about window handle */
extern HWND fuse_hABOWnd;

/* AY player window (ayplayer.c) */
extern HWND fuse_hAYWnd;
extern HWND fuse_hAYListWnd;
/* AY mixer window (aymixer.c) */
extern HWND fuse_hAYMixWnd;
void win32_aymixer_show( void );
void win32_aymixer_close( void );
void win32_aymixer_update( void );
/* The AY mixer settings changed: every window shows them */
void win32_aymix_changed( void );

/* The AY player window and its playlist window */
void win32_ayplayer_update( void );
/* The AY player changed an AY sound setting: show it in Options > Sound
   if that is open */
void win32_options_sound_sync( void );
void win32_ayplayer_play_entry( int index );
void win32_ayplayer_measure_all( void );
int win32_ayplayer_busy( void );
void win32_ayplayer_measure_stop( void );
int win32_ayplayer_measure_stopping( void );
void win32_ayplaylist_show( void );
void win32_ayplaylist_toggle( void );
void win32_ayplaylist_close( void );
void win32_ayplaylist_update( void );
void win32_ayplaylist_update_status( void );
int win32_ayplaylist_selected( void );
void win32_ayplayer_show( void );
int win32ui_machine_paused( void );

/* A Windows modal loop (menu, moving or resizing a window) starts / ends */
void win32ui_modal_enter( void );
void win32ui_modal_exit( void );
/* The AY player started: keep the music going during such loops */
void win32ui_ayplayer_timer_start( void );

/*
 * Display routines (win32display.c)
 */

/* The biggest size screen (in units of DISPLAY_ASPECT_WIDTH x
   DISPLAY_SCREEN_HEIGHT ie a Timex screen is size 2) we will be
   creating via the scalers */
#define MAX_SCALE 4

/* The colour palette in use */
extern libspectrum_dword win32display_colours[16];

int win32display_init( void );
int win32display_end( void );
int win32display_scaled_height( void );
int win32display_scaled_width( void );


/* Below variables and functions are shared
   between win32display.c and win32ui.c */

void win32display_area(int x, int y, int width, int height);
int win32display_drawing_area_resize( int width, int height, int force_scaler );

void win32display_fullscreen_begin( void );
void win32display_fullscreen_resize( int width, int height );
void win32display_fullscreen_end( void );
void win32display_fullscreen_restore( void );

void blit( void );

/*
 * Keyboard routines (win32keyboard.c)
 */

void win32keyboard_init( void );
void win32keyboard_end( void );
void win32keyboard_keypress( WPARAM wParam, LPARAM lParam );
void win32keyboard_keyrelease( WPARAM wParam, LPARAM lParam );
int win32keyboard_syskeypress( WPARAM wParam, LPARAM lParam );
int win32keyboard_syskeyrelease( WPARAM wParam, LPARAM lParam );
int win32keyboard_alt_digit_keyrelease( WPARAM wParam, LPARAM lParam );
int win32keyboard_alt_digit_char( WPARAM wParam );
void win32keyboard_alt_not_alone( void );
int win32keyboard_menu_underline( void );
void win32ui_menubar_redraw( void );
void win32ui_menubar_texts_changed( void );

/* Draw or (with DT_CALCRECT) measure a user interface text as Unicode */
int win32ui_draw_text( HDC dc, const char *text, RECT *rect, UINT format );
void win32ui_menubar_from_keyboard( void );

/*
 * Mouse routines (win32mouse.c)
 */

void win32mouse_position( LPARAM lParam );
void win32mouse_button( int button, int down );

/*
 * General user interface routines (win32ui.c)
 */

void win32ui_fuse_resize( int width, int height );

int win32ui_is_full_screen( void );
void win32ui_fullscreen_apply( void );

int win32ui_confirm( const char *string );

void win32ui_infobox( const char *message );

void win32i18n_init( void );
void win32i18n_end( void );

/* The English text of a menu item (with its & and shortcut), whatever the
   language of the menu; NULL if it is not known */
const char *win32i18n_menu_english( HMENU menu, int position );

typedef enum win32ui_picture_format {
  PICTURE_SCR,
  PICTURE_PNG,
  PICTURE_JPG,
} win32ui_picture_format;

int win32ui_picture( const char *filename, win32ui_picture_format format );

int win32ui_get_monospaced_font( HFONT *font );
void win32ui_set_font( HWND hDlg, int nIDDlgItem, HFONT font );

int handle_menu( DWORD cmd, HWND okno );

void win32_verror( int is_error );

void win32ui_process_messages( int process_queue_once );

#define WM_USER_EXIT_PROCESS_MESSAGES WM_USER

/*
 * Statusbar routines (statusbar.c)
 */

void win32statusbar_create( HWND hWnd );
int win32statusbar_set_visibility( int visible );
void win32statusbar_redraw( HWND hWnd, LPARAM lParam );
void win32statusbar_resize( HWND hWnd, WPARAM wParam, LPARAM lParam );
void win32statusbar_update_machine( const char *name );

/*
 * Dialog box reset
 */

void win32ui_pokefinder_clear( void );

#endif                          /* #ifndef FUSE_WIN32INTERNALS_H */

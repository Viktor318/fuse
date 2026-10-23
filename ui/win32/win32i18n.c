/* win32i18n.c: show the Windows user interface in the selected language
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

/* The menus and dialogs are defined in English in the resource files.

   Menus: their English texts are remembered when Fuse starts, and set
   again translated whenever the language changes.

   Dialogs: a hook sees every dialog of Fuse as it is created, and
   translates its title, labels and buttons. Translated texts are often
   longer, so labels, check boxes and the dialog are widened to fit, moving
   the fields next to the labels to the right. */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "i18n.h"
#include "options_internals.h"
#include "win32internals.h"

/* ------------------------------------------------------------------------
   Menus
   ------------------------------------------------------------------------ */

typedef struct menu_text_t {
  HMENU menu;
  UINT position;
  char *english;
} menu_text_t;

static menu_text_t *menu_texts = NULL;
static size_t menu_text_count = 0, menu_text_allocated = 0;

static void
remember_menu( HMENU menu )
{
  int count = GetMenuItemCount( menu ), i;

  for( i = 0; i < count; i++ ) {
    MENUITEMINFOA info;
    char text[ 256 ];

    memset( &info, 0, sizeof( info ) );
    info.cbSize = sizeof( info );
    info.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_SUBMENU;
    info.dwTypeData = text;
    info.cch = sizeof( text );
    if( !GetMenuItemInfoA( menu, i, TRUE, &info ) ) continue;

    if( !( info.fType & ( MFT_SEPARATOR | MFT_BITMAP | MFT_OWNERDRAW ) ) &&
        info.cch ) {
      if( menu_text_count == menu_text_allocated ) {
        menu_text_t *bigger;
        menu_text_allocated = menu_text_allocated ? menu_text_allocated * 2 : 256;
        bigger = realloc( menu_texts,
                          menu_text_allocated * sizeof( *menu_texts ) );
        if( !bigger ) return;
        menu_texts = bigger;
      }
      menu_texts[ menu_text_count ].menu = menu;
      menu_texts[ menu_text_count ].position = i;
      menu_texts[ menu_text_count ].english = _strdup( text );
      if( menu_texts[ menu_text_count ].english ) menu_text_count++;
    }

    if( info.hSubMenu ) remember_menu( info.hSubMenu );
  }
}

/* Set the menu texts in the current language */
static void
translate_menu( void )
{
  size_t i;

  for( i = 0; i < menu_text_count; i++ ) {
    char text[ 512 ];
    const char *english = menu_texts[i].english;
    const char *tab = strchr( english, '\t' );
    MENUITEMINFOA info;

    /* The shortcut after the tab (F3, Insert, ...) stays as it is */
    if( tab ) {
      char label[ 256 ];
      size_t length = tab - english;
      if( length >= sizeof( label ) ) length = sizeof( label ) - 1;
      memcpy( label, english, length ); label[ length ] = '\0';
      _snprintf( text, sizeof( text ), "%s%s", i18n( label ), tab );
    } else {
      _snprintf( text, sizeof( text ), "%s", i18n( english ) );
    }
    text[ sizeof( text ) - 1 ] = '\0';

    memset( &info, 0, sizeof( info ) );
    info.cbSize = sizeof( info );
    info.fMask = MIIM_STRING;
    info.dwTypeData = text;
    SetMenuItemInfoA( menu_texts[i].menu, menu_texts[i].position, TRUE,
                      &info );
  }

  DrawMenuBar( fuse_hWnd );
}

/* ------------------------------------------------------------------------
   Dialogs
   ------------------------------------------------------------------------ */

#define MAX_CONTROLS 256

typedef struct control_t {
  HWND window;
  RECT rect;			/* in dialog client coordinates */
  int kind;
  int needed;			/* width the text needs, 0 if it fits */
} control_t;

enum { KIND_OTHER, KIND_LABEL, KIND_CHECK, KIND_BUTTON, KIND_GROUP,
       KIND_FIELD };

typedef struct dialog_t {
  HWND dialog;
  control_t controls[ MAX_CONTROLS ];
  int count;
  int translated;
} dialog_t;

static int
control_kind( HWND window )
{
  char class_name[ 32 ];
  LONG style = GetWindowLongA( window, GWL_STYLE );

  GetClassNameA( window, class_name, sizeof( class_name ) );

  if( !_stricmp( class_name, "Static" ) ) {
    int type = style & SS_TYPEMASK;
    if( type == SS_LEFT || type == SS_CENTER || type == SS_RIGHT ||
        type == SS_LEFTNOWORDWRAP || type == SS_SIMPLE )
      return KIND_LABEL;
    return KIND_OTHER;
  }

  if( !_stricmp( class_name, "Button" ) ) {
    switch( style & BS_TYPEMASK ) {
    case BS_CHECKBOX: case BS_AUTOCHECKBOX: case BS_3STATE:
    case BS_AUTO3STATE: case BS_RADIOBUTTON: case BS_AUTORADIOBUTTON:
      return KIND_CHECK;
    case BS_GROUPBOX:
      return KIND_GROUP;
    default:
      return KIND_BUTTON;
    }
  }

  if( !_stricmp( class_name, "Edit" ) || !_stricmp( class_name, "ComboBox" ) )
    return KIND_FIELD;

  return KIND_OTHER;
}

/* Width of a control's text in pixels, with its font */
static int
text_width( HWND window, const char *text )
{
  HDC dc = GetDC( window );
  HFONT font = (HFONT)SendMessageA( window, WM_GETFONT, 0, 0 );
  HFONT old = font ? SelectObject( dc, font ) : NULL;
  RECT rect = { 0, 0, 0, 0 };

  DrawTextA( dc, text, -1, &rect, DT_CALCRECT | DT_SINGLELINE );
  if( old ) SelectObject( dc, old );
  ReleaseDC( window, dc );

  return rect.right - rect.left;
}

static BOOL CALLBACK
collect_control( HWND window, LPARAM data )
{
  dialog_t *d = (dialog_t*)data;
  control_t *c;
  char text[ 512 ];
  const char *translated;

  /* Only the dialog's own controls, not those of controls inside it */
  if( GetParent( window ) != d->dialog || d->count == MAX_CONTROLS )
    return TRUE;

  c = &d->controls[ d->count++ ];
  c->window = window;
  c->kind = control_kind( window );
  c->needed = 0;
  GetWindowRect( window, &c->rect );
  MapWindowPoints( NULL, d->dialog, (POINT*)&c->rect, 2 );

  if( c->kind == KIND_FIELD ) {
    /* Drop-down lists: wide enough for their longest (translated) item */
    char class_name[ 32 ];
    GetClassNameA( window, class_name, sizeof( class_name ) );
    if( !_stricmp( class_name, "ComboBox" ) ) {
      LRESULT items = SendMessageA( window, CB_GETCOUNT, 0, 0 ), i;
      int widest = 0;
      for( i = 0; i < items; i++ ) {
        if( SendMessageA( window, CB_GETLBTEXTLEN, i, 0 ) >= (LRESULT)sizeof( text ) )
          continue;
        SendMessageA( window, CB_GETLBTEXT, i, (LPARAM)text );
        if( text_width( window, text ) > widest ) widest = text_width( window, text );
      }
      widest += GetSystemMetrics( SM_CXVSCROLL ) + 10;
      if( widest > c->rect.right - c->rect.left ) {
        c->needed = widest;
        if( i18n_hungarian() ) d->translated = 1;
      }
    }
    return TRUE;
  }

  if( c->kind == KIND_OTHER ) return TRUE;

  GetWindowTextA( window, text, sizeof( text ) );
  if( !text[0] ) return TRUE;

  translated = i18n( text );
  if( translated == text || !strcmp( translated, text ) ) return TRUE;

  SetWindowTextA( window, translated );
  d->translated = 1;

  /* Room the new text needs. Multi-line labels (taller than one line)
     wrap, so they are left alone */
  {
    int width = text_width( window, translated ), extra = 0;
    int height = c->rect.bottom - c->rect.top;

    if( c->kind == KIND_LABEL && height > 2 * GetSystemMetrics( SM_CYMENU ) )
      return TRUE;
    if( c->kind == KIND_CHECK ) extra = GetSystemMetrics( SM_CXMENUCHECK ) + 6;
    if( c->kind == KIND_BUTTON ) extra = 12;
    if( c->kind == KIND_GROUP ) extra = 16;

    if( width + extra > c->rect.right - c->rect.left )
      c->needed = width + extra;
  }

  return TRUE;
}

static void
move_control( control_t *c, int dx, int widen )
{
  c->rect.left += dx;
  c->rect.right += dx + widen;
  SetWindowPos( c->window, NULL, c->rect.left, c->rect.top,
                c->rect.right - c->rect.left, c->rect.bottom - c->rect.top,
                SWP_NOZORDER | SWP_NOACTIVATE );
}

/* Is there a field to the right of this label, on the same row? */
static int
field_beside( const dialog_t *d, const control_t *label )
{
  int i;

  for( i = 0; i < d->count; i++ ) {
    const control_t *c = &d->controls[i];
    if( c->kind != KIND_FIELD ) continue;
    if( c->rect.left >= label->rect.right - 2 &&
        c->rect.top < label->rect.bottom && c->rect.bottom > label->rect.top )
      return 1;
  }
  return 0;
}

static void
fit_dialog( dialog_t *d )
{
  RECT client;
  int i, shift = 0, column = 0, right = 0, margin, grow;

  GetClientRect( d->dialog, &client );

  /* Labels with a field beside them: move every field column right */
  for( i = 0; i < d->count; i++ ) {
    control_t *c = &d->controls[i];
    if( c->kind == KIND_LABEL && c->needed && field_beside( d, c ) ) {
      int more = c->needed - ( c->rect.right - c->rect.left );
      if( more > shift ) { shift = more; column = c->rect.right; }
    }
  }
  if( shift ) {
    for( i = 0; i < d->count; i++ ) {
      control_t *c = &d->controls[i];
      if( c->kind == KIND_LABEL && field_beside( d, c ) &&
          c->rect.right <= column + 2 )
        move_control( c, 0, shift );
      else if( c->rect.left >= column - 2 && c->kind != KIND_BUTTON &&
               c->kind != KIND_GROUP )
        move_control( c, shift, 0 );
    }
  }

  /* Everything else just gets wider */
  for( i = 0; i < d->count; i++ ) {
    control_t *c = &d->controls[i];
    int width = c->rect.right - c->rect.left;
    if( c->needed > width && !( c->kind == KIND_LABEL && field_beside( d, c ) ) ) {
      if( c->kind == KIND_BUTTON ) {
        /* Grow buttons around their centre */
        int more = c->needed - width;
        move_control( c, -more / 2, more );
      } else {
        move_control( c, 0, c->needed - width );
      }
    }
  }

  /* Widen the dialog to keep its right margin */
  margin = client.right;
  for( i = 0; i < d->count; i++ ) {
    const control_t *c = &d->controls[i];
    if( c->kind != KIND_BUTTON && client.right - c->rect.right < margin )
      margin = client.right - c->rect.right;
  }
  if( margin < 0 ) margin = 0;
  for( i = 0; i < d->count; i++ )
    if( d->controls[i].rect.right > right ) right = d->controls[i].rect.right;

  grow = right + margin - client.right;
  if( grow > 0 ) {
    RECT window;

    /* Group boxes reaching the old right edge, and the buttons at the
       bottom, follow the new width */
    for( i = 0; i < d->count; i++ ) {
      control_t *c = &d->controls[i];
      if( c->kind == KIND_GROUP && client.right - c->rect.right <= margin + 2 )
        move_control( c, 0, grow );
      else if( c->kind == KIND_BUTTON ) move_control( c, grow / 2, 0 );
    }

    GetWindowRect( d->dialog, &window );
    SetWindowPos( d->dialog, NULL, window.left - grow / 2, window.top,
                  window.right - window.left + grow,
                  window.bottom - window.top,
                  SWP_NOZORDER | SWP_NOACTIVATE );
  }
}

/* A dialog's own menu (the debugger has one), just created in English */
static void
translate_dialog_menu( HMENU menu )
{
  int count = GetMenuItemCount( menu ), i;

  for( i = 0; i < count; i++ ) {
    MENUITEMINFOA info;
    char text[ 256 ];

    memset( &info, 0, sizeof( info ) );
    info.cbSize = sizeof( info );
    info.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_SUBMENU;
    info.dwTypeData = text;
    info.cch = sizeof( text );
    if( !GetMenuItemInfoA( menu, i, TRUE, &info ) ) continue;

    if( !( info.fType & ( MFT_SEPARATOR | MFT_BITMAP | MFT_OWNERDRAW ) ) &&
        info.cch && strcmp( i18n( text ), text ) ) {
      MENUITEMINFOA set;
      memset( &set, 0, sizeof( set ) );
      set.cbSize = sizeof( set );
      set.fMask = MIIM_STRING;
      set.dwTypeData = (char*)i18n( text );
      SetMenuItemInfoA( menu, i, TRUE, &set );
    }
    if( info.hSubMenu ) translate_dialog_menu( info.hSubMenu );
  }
}

static void
translate_dialog( HWND window )
{
  char title[ 256 ];
  dialog_t *d;

  if( !i18n_hungarian() ) return;

  /* Only Fuse's own dialogs */
  {
    DWORD process;
    GetWindowThreadProcessId( window, &process );
    if( process != GetCurrentProcessId() ) return;
  }

  /* Not the main window's menu, which is translated on its own */
  if( window != fuse_hWnd && GetMenu( window ) ) {
    translate_dialog_menu( GetMenu( window ) );
    DrawMenuBar( window );
  }

  d = calloc( 1, sizeof( *d ) );
  if( !d ) return;
  d->dialog = window;

  EnumChildWindows( window, collect_control, (LPARAM)d );

  /* Message boxes and file selectors are already in the Windows language,
     and none of their texts are in the table */
  if( d->translated ) {
    GetWindowTextA( window, title, sizeof( title ) );
    if( title[0] && strcmp( i18n( title ), title ) )
      SetWindowTextA( window, i18n( title ) );
    fit_dialog( d );
  } else {
    GetWindowTextA( window, title, sizeof( title ) );
    if( title[0] && strcmp( i18n( title ), title ) )
      SetWindowTextA( window, i18n( title ) );
  }

  free( d );
}

/* Options -> General: the language list starts where the OK button does,
   close to its short label */
/* Put the language list exactly above the emulation speed box (same left
   edge and width), whatever the translation did to the label widths */
static void
align_language_list( HWND dialog )
{
  HWND list = GetDlgItem( dialog, IDC_OPT_GENERAL_LANGUAGE );
  HWND label = GetDlgItem( dialog, IDC_OPT_GENERAL_LABEL_LANGUAGE );
  HWND below = GetDlgItem( dialog, IDC_OPT_GENERAL_EMULATION_SPEED );
  RECT list_rect, label_rect, below_rect;

  if( !list || !label || !below ) return;

  GetWindowRect( list, &list_rect );
  GetWindowRect( label, &label_rect );
  GetWindowRect( below, &below_rect );
  MapWindowPoints( NULL, dialog, (POINT*)&list_rect, 2 );
  MapWindowPoints( NULL, dialog, (POINT*)&label_rect, 2 );
  MapWindowPoints( NULL, dialog, (POINT*)&below_rect, 2 );

  if( below_rect.left <= label_rect.left ) return;

  SetWindowPos( label, NULL, 0, 0, below_rect.left - label_rect.left - 4,
                label_rect.bottom - label_rect.top,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
  SetWindowPos( list, NULL, below_rect.left, list_rect.top,
                below_rect.right - below_rect.left,
                list_rect.bottom - list_rect.top,
                SWP_NOZORDER | SWP_NOACTIVATE );
}

static HHOOK dialog_hook = NULL;

static LRESULT CALLBACK
dialog_hook_proc( int code, WPARAM wParam, LPARAM lParam )
{
  if( code == HC_ACTION ) {
    const CWPRETSTRUCT *message = (const CWPRETSTRUCT*)lParam;
    if( message->message == WM_INITDIALOG ) {
      translate_dialog( message->hwnd );
      align_language_list( message->hwnd );
    }
  }

  return CallNextHookEx( dialog_hook, code, wParam, lParam );
}

/* ------------------------------------------------------------------------ */

static void
language_changed( void )
{
  translate_menu();
}

void
win32i18n_init( void )
{
  remember_menu( GetMenu( fuse_hWnd ) );
  translate_menu();

  dialog_hook = SetWindowsHookExA( WH_CALLWNDPROCRET, dialog_hook_proc, NULL,
                                   GetCurrentThreadId() );

  i18n_changed_hook = language_changed;
}

void
win32i18n_end( void )
{
  size_t i;

  if( dialog_hook ) UnhookWindowsHookEx( dialog_hook );
  dialog_hook = NULL;
  i18n_changed_hook = NULL;

  for( i = 0; i < menu_text_count; i++ ) free( menu_texts[i].english );
  free( menu_texts );
  menu_texts = NULL;
  menu_text_count = menu_text_allocated = 0;
}

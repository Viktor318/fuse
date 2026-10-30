/* infobox.c: silent information message box
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

/* Like MessageBox( ..., MB_ICONINFORMATION ), but without the system sound
   Windows plays for it, as the About box */

#include "config.h"

#include <windows.h>

#include "win32internals.h"

#include "infobox.h"

/* Make the text control (and the dialog) tall enough for the message */
static void
fit_text( HWND dialog, const char *message )
{
  HWND text = GetDlgItem( dialog, IDC_INFOBOX_TEXT );
  HWND button = GetDlgItem( dialog, IDOK );
  RECT text_rect, needed, button_rect, dialog_rect;
  HDC dc;
  HFONT font, old_font;
  int grow;

  GetWindowRect( text, &text_rect );
  MapWindowPoints( NULL, dialog, (POINT*)&text_rect, 2 );

  needed = text_rect;
  dc = GetDC( text );
  font = (HFONT)SendMessage( text, WM_GETFONT, 0, 0 );
  old_font = SelectObject( dc, font );
  win32ui_draw_text( dc, message, &needed, DT_CALCRECT | DT_WORDBREAK );
  SelectObject( dc, old_font );
  ReleaseDC( text, dc );

  grow = ( needed.bottom - needed.top ) - ( text_rect.bottom - text_rect.top );
  if( grow <= 0 ) return;

  SetWindowPos( text, NULL, 0, 0, text_rect.right - text_rect.left,
                needed.bottom - needed.top, SWP_NOMOVE | SWP_NOZORDER );

  GetWindowRect( button, &button_rect );
  MapWindowPoints( NULL, dialog, (POINT*)&button_rect, 2 );
  SetWindowPos( button, NULL, button_rect.left, button_rect.top + grow, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER );

  GetWindowRect( dialog, &dialog_rect );
  SetWindowPos( dialog, NULL, dialog_rect.left, dialog_rect.top - grow / 2,
                dialog_rect.right - dialog_rect.left,
                dialog_rect.bottom - dialog_rect.top + grow, SWP_NOZORDER );
}

static INT_PTR CALLBACK
infobox_proc( HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam )
{
  switch( msg ) {

  case WM_INITDIALOG:
    SendDlgItemMessage( dialog, IDC_INFOBOX_ICON, STM_SETICON,
                        (WPARAM)LoadIcon( NULL, IDI_INFORMATION ), 0 );
    SetDlgItemText( dialog, IDC_INFOBOX_TEXT, (const char*)lParam );
    fit_text( dialog, (const char*)lParam );
    return TRUE;

  case WM_COMMAND:
    if( LOWORD( wParam ) == IDOK || LOWORD( wParam ) == IDCANCEL ) {
      EndDialog( dialog, 0 );
      return TRUE;
    }
    break;

  case WM_CLOSE:
    EndDialog( dialog, 0 );
    return TRUE;

  }

  return FALSE;
}

void
win32ui_infobox( const char *message )
{
  DialogBoxParam( fuse_hInstance, MAKEINTRESOURCE( IDD_INFOBOX ), fuse_hWnd,
                  infobox_proc, (LPARAM)message );
}

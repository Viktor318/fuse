/* win32text.h: Texts to and from Windows as Unicode
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

#ifndef FUSE_WIN32TEXT_H
#define FUSE_WIN32TEXT_H

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

/* Fuse keeps its texts in the process code page: UTF-8 on Windows 10 1903
   and later (see fuse.manifest), the ANSI code page before. User32 and
   kernel32 take them as they are, but the common controls, the common
   dialogs and the shell convert ANSI texts with the system code page, so
   these functions pass them as Unicode instead. */

/* A text as Unicode, or back; free the result with free(). NULL in, NULL
   out */
WCHAR *win32text_wide( const char *text );
char *win32text_narrow( const WCHAR *text );

/* GetOpenFileNameA() / GetSaveFileNameA() through the Unicode dialog. The
   filter, the file name buffer (with what it holds, and the list of names
   with OFN_ALLOWMULTISELECT | OFN_EXPLORER), the title, folder and
   default extension are converted; nFilterIndex is returned too */
BOOL win32text_file_dialog( OPENFILENAMEA *dialog, int save );

/* SHBrowseForFolderA() + SHGetPathFromIDListA(): the chosen folder in
   `path' (MAX_PATH bytes); non-zero if one was chosen */
int win32text_browse_folder( HWND owner, const char *title, UINT flags,
                             char *path, size_t length );

/* DragQueryFileA(): the name of a dropped file; 0 if there is none */
UINT win32text_drag_query_file( HDROP drop, UINT index, char *buffer,
                                UINT length );

/* LVM_INSERTCOLUMNA */
int win32text_listview_insert_column( HWND list, int index,
                                      const LVCOLUMNA *column );

/* LVM_INSERTITEMA, LVM_SETITEMA or LVM_SETITEMTEXTA (with the index in
   `wparam') */
LRESULT win32text_listview_item( HWND list, UINT message, WPARAM wparam,
                                 const LVITEMA *item );

/* A dash (en dash) and a right guillemet in the process code page: in
   UTF-8 they take more than one byte */
const char *win32text_dash( void );
const char *win32text_guillemet( void );

/* SB_SETTEXTA */
LRESULT win32text_statusbar_text( HWND status, WPARAM part,
                                  const char *text );

#endif			/* #ifndef FUSE_WIN32TEXT_H */

/* picture.c: Win32 routines to draw the keyboard picture
   Copyright (c) 2002-2019 Philip Kendall, Marek Januszewski, Stuart Brady,
                           Sergio Baldoví

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

#include "config.h"

#include <stdlib.h>
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#ifdef USE_LIBPNG
#include <png.h>
#include <zlib.h>
#endif

#include "compat.h"
#include "display.h"
#include "picture.h"
#include "utils.h"
#include "win32internals.h"
#include "winstate.h"

/* An RGB image of the keyboard picture */
/* the memory will be allocated by Windows ( height * width * 4 bytes ) */
static libspectrum_byte *picture;
static const int picture_pitch = DISPLAY_ASPECT_WIDTH * 4;

static HWND hDialogPicture = NULL;
static HBITMAP picture_BMP = NULL;

static HWND create_dialog( int width, int height );

static void draw_screen( libspectrum_byte *screen, int border );

static LRESULT WINAPI picture_wnd_proc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

static int show_system_picture( const char *filename );
static HBITMAP load_picture( const char *filename, int *width, int *height );
static void draw_bitmap( HBITMAP bitmap, int width, int height );
static void restore_geometry( HWND dialog );

#ifdef PNG_SIMPLIFIED_READ_SUPPORTED

static void draw_png( libspectrum_byte *buffer, size_t size );
static int read_png_file( const char *filename, png_bytep *buffer,
                          size_t *size, int *width, int *height );

#endif			/* #ifdef PNG_SIMPLIFIED_READ_SUPPORTED */

int
win32ui_picture( const char *filename, win32ui_picture_format format )
{
  /* Dialog already open? Show it again, without taking the focus */
  if( hDialogPicture != NULL ) {
    if( IsIconic( hDialogPicture ) )
      ShowWindow( hDialogPicture, SW_SHOWNOACTIVATE );
    SetWindowPos( hDialogPicture, HWND_TOP, 0, 0, 0, 0,
                  SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE );
    return 0;
  }

  switch( format ) {

  case PICTURE_PNG:
  {

#ifdef PNG_SIMPLIFIED_READ_SUPPORTED
    int width, height;
    png_bytep buffer;
    size_t size;

    if( read_png_file( filename, &buffer, &size, &width, &height ) )
      return 1;

    hDialogPicture = create_dialog( width, height );
    if( hDialogPicture == NULL )
      return 1;

    draw_png( buffer, size );

    free( buffer );
    break;
#else
    /* Without libpng, let Windows decode it */
    if( show_system_picture( filename ) ) return 1;
    break;
#endif			/* #ifdef PNG_SIMPLIFIED_READ_SUPPORTED */
  }

  case PICTURE_JPG:
    if( show_system_picture( filename ) ) return 1;
    break;

  case PICTURE_SCR:
  {
    utils_file screen;

    if( utils_read_screen( filename, &screen ) )
      return 1;

    hDialogPicture = create_dialog( DISPLAY_ASPECT_WIDTH,
                                    DISPLAY_SCREEN_HEIGHT );
    if( hDialogPicture == NULL )
      return 1;

    draw_screen( screen.buffer, 0 );

    utils_close_file( &screen );
    break;
  }

  default:
    /* Unsupported format */
    return 1;

  }

  /* Show it without taking the focus, so typing keeps going to the
     emulated machine */
  ShowWindow( hDialogPicture, SW_SHOWNOACTIVATE );

  return 0;
}

static void
resize_dialog( HWND dialog )
{
  RECT wr_dialog, cr_dialog, wr_button, wr_frame, rc;
  HWND button_hWnd, frame_hWnd;

  /* Dialog dimensions */
  GetWindowRect( dialog, &wr_dialog );
  GetClientRect( dialog, &cr_dialog );
  OffsetRect( &wr_dialog, -wr_dialog.left, -wr_dialog.top );
  OffsetRect( &cr_dialog, -cr_dialog.left, -cr_dialog.top );

  /* Move Close button */
  SetRectEmpty( &rc );
  rc.top = 7; /*  bottom spacing (DLUs) */
  MapDialogRect( dialog, &rc );

  button_hWnd = GetDlgItem( dialog, IDCLOSE );
  GetWindowRect( button_hWnd, &wr_button );
  wr_button.left = ( cr_dialog.right -
                     ( wr_button.right - wr_button.left ) ) / 2;
  wr_button.top = cr_dialog.bottom - ( wr_button.bottom - wr_button.top ) -
                  rc.top;

  SetWindowPos( button_hWnd, NULL, wr_button.left, wr_button.top, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );

  /* Give any unused space to picture frame */
  frame_hWnd = GetDlgItem( dialog, IDC_PICTURE_FRAME );
  SetRectEmpty( &rc );
  rc.bottom = 7 + 7; /* spacing above and below button (DLUs) */
  MapDialogRect( dialog, &rc );
  GetWindowRect( button_hWnd, &wr_button );
  rc.bottom += wr_button.bottom - wr_button.top; /* button height */
  wr_frame.bottom = cr_dialog.bottom - rc.bottom;
  wr_frame.right = cr_dialog.right;

  SetWindowPos( frame_hWnd, NULL, 0, 0, wr_frame.right, wr_frame.bottom,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS );
}

/* ------------------------------------------------------------------------
   The size steps: 100% is half of the picture's own size, 200% its own
   size, so it is only ever made smaller and stays sharp
   ------------------------------------------------------------------------ */

#define STEPS 5			/* 100%, 125%, 150%, 175%, 200% */
#define LARGEST_STEP ( STEPS - 1 )

/* The picture's own size */
static int picture_width, picture_height;
/* The step shown */
static int step = LARGEST_STEP;

/* The size of the picture at a step: ( 4 + step ) / 8 of its own size */
static void
content_size( int at, int *width, int *height )
{
  *height = picture_height * ( 4 + at ) / 8;
  *width = picture_width * ( 4 + at ) / 8;
}

/* The size of the window's client area besides the picture: the Close
   button with the room around it, under the picture */
static void
button_area( HWND dialog, int *width, int *height )
{
  RECT button, rc;

  GetWindowRect( GetDlgItem( dialog, IDCLOSE ), &button );
  SetRectEmpty( &rc );
  rc.bottom = 7 + 7; /* top spacing + bottom spacing (DLUs) */
  rc.right  = 7 + 7; /* left spacing + right spacing (DLUs) */
  MapDialogRect( dialog, &rc );
  *width = rc.right + button.right - button.left;
  *height = rc.bottom + button.bottom - button.top;
}

/* The size of the whole window at a step */
static void
window_size( HWND dialog, int at, int *width, int *height )
{
  RECT rect;
  int content_width, content_height, button_width, button_height;

  content_size( at, &content_width, &content_height );
  button_area( dialog, &button_width, &button_height );

  rect.left = rect.top = 0;
  rect.right = content_width > button_width ? content_width : button_width;
  rect.bottom = content_height + button_height;
  AdjustWindowRectEx( &rect, GetWindowLong( dialog, GWL_STYLE ), FALSE,
                      GetWindowLong( dialog, GWL_EXSTYLE ) );
  *width = rect.right - rect.left;
  *height = rect.bottom - rect.top;
}

/* The work area of the monitor the window (or the place) is on */
static void
work_area( HWND dialog, const RECT *place, RECT *area )
{
  MONITORINFO info;
  HMONITOR monitor = place ? MonitorFromRect( place, MONITOR_DEFAULTTONEAREST )
                           : MonitorFromWindow( dialog,
                                                MONITOR_DEFAULTTONEAREST );

  info.cbSize = sizeof( info );
  if( monitor && GetMonitorInfo( monitor, &info ) )
    *area = info.rcWork;
  else
    SystemParametersInfo( SPI_GETWORKAREA, 0, area, 0 );
}

/* Does the window fit the work area at this step? */
static int
step_fits( HWND dialog, int at, const RECT *place )
{
  RECT area;
  int width, height;

  work_area( dialog, place, &area );
  window_size( dialog, at, &width, &height );
  return width <= area.right - area.left && height <= area.bottom - area.top;
}

/* The largest step that fits, not larger than `at' (100% if none) */
static int
fitting_step( HWND dialog, int at, const RECT *place )
{
  for( ; at > 0; at-- )
    if( step_fits( dialog, at, place ) ) break;
  return at;
}

/* Show a step: the size changes, the top left corner stays where it is
   (unless the window would go over the edge of the work area) */
static void
set_step( HWND dialog, int at )
{
  RECT window, area;
  int width, height, x, y;

  step = at;
  window_size( dialog, at, &width, &height );
  GetWindowRect( dialog, &window );
  work_area( dialog, &window, &area );

  x = window.left;
  y = window.top;
  if( x + width > area.right ) x = area.right - width;
  if( y + height > area.bottom ) y = area.bottom - height;
  if( x < area.left ) x = area.left;
  if( y < area.top ) y = area.top;

  SetWindowPos( dialog, NULL, x, y, width, height,
                SWP_NOZORDER | SWP_NOACTIVATE );
  InvalidateRect( GetDlgItem( dialog, IDC_PICTURE_FRAME ), NULL, FALSE );
}

/* Double click: the next step that fits, after 200% 100% again */
static void
next_step( HWND dialog )
{
  int at = step, i;

  for( i = 0; i < STEPS; i++ ) {
    at = ( at + 1 ) % STEPS;
    if( !at || step_fits( dialog, at, NULL ) ) break;
  }
  set_step( dialog, at );
}

static void
inflate_dialog( HWND dialog, int width, int height )
{
  picture_width = width;
  picture_height = height;

  /* The first time: 200% if it fits, else the largest step that does */
  set_step( dialog, fitting_step( dialog, LARGEST_STEP, NULL ) );
}

static void
move_dialog( HWND dialog, HWND parent )
{
  RECT rc_workarea, rc_parent, wr_dialog, rc;
  int placed = 0;

  SystemParametersInfo( SPI_GETWORKAREA, 0, &rc_workarea, 0 );
  GetWindowRect( parent, &rc_parent );
  GetWindowRect( dialog, &wr_dialog );

  /* Place centered below Fuse's main window */
  if( !placed ) {
    CopyRect( &rc, &wr_dialog );
    OffsetRect( &rc, -rc.left, -rc.top );
    OffsetRect( &rc, -rc.right/2, 0 );
    OffsetRect( &rc, ( rc_parent.right - rc_parent.left ) / 2, 0 );
    OffsetRect( &rc, rc_parent.left, rc_parent.bottom );
    if( rc.left >= rc_workarea.left && rc.right <= rc_workarea.right &&
        rc.bottom <= rc_workarea.bottom )
      placed = 1;
  }

  /* Place right to Fuse's main window */
  if( !placed ) {
    CopyRect( &rc, &wr_dialog );
    OffsetRect( &rc, -rc.left, -rc.top );
    OffsetRect( &rc, rc_parent.right, rc_parent.top );
    if( rc.right <= rc_workarea.right && rc.bottom <= rc_workarea.bottom )
      placed = 1;
  }

  /* Place left to Fuse's main window */
  if( !placed ) {
    CopyRect( &rc, &wr_dialog );
    OffsetRect( &rc, -rc.left, -rc.top );
    OffsetRect( &rc, rc_parent.left - rc.right, rc_parent.top );
    if( rc.left >= rc_workarea.left && rc.bottom <= rc_workarea.bottom )
      placed = 1;
  }

  /* Move the dialog or leave at initial position */
  if( placed ) {
    SetWindowPos( dialog, NULL, rc.left, rc.top, 0, 0,
                  SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
  }
}

int
win32ui_picture_is_open( void )
{
  return hDialogPicture != NULL;
}

static HWND
create_dialog( int width, int height )
{
  HWND dialog;

  /* Owned by Fuse's main window, so it always stays above it (and is
     minimized and restored with it), but never activated, so the keyboard
     focus stays with the main window and typing goes to the emulated
     machine even while the picture is being looked at, moved or closed */
  dialog = CreateDialog( fuse_hInstance, MAKEINTRESOURCE( IDD_PICTURE ),
                         fuse_hWnd, (DLGPROC)picture_wnd_proc );

  if( dialog != NULL )
    SetWindowLongPtr( dialog, GWL_EXSTYLE,
                      GetWindowLongPtr( dialog, GWL_EXSTYLE ) |
                      WS_EX_NOACTIVATE );

  if( dialog == NULL )
    return NULL;

  inflate_dialog( dialog, width, height );
  move_dialog( dialog, fuse_hWnd );
  restore_geometry( dialog );

  /* Create the picture buffer */
  BITMAPINFO picture_BMI;
  memset( &picture_BMI, 0, sizeof( picture_BMI ) );
  picture_BMI.bmiHeader.biSize = sizeof( picture_BMI.bmiHeader );
  picture_BMI.bmiHeader.biWidth = width;
  /* negative to avoid "shep-mode": */
  picture_BMI.bmiHeader.biHeight = -height;
  picture_BMI.bmiHeader.biPlanes = 1;
  picture_BMI.bmiHeader.biBitCount = 32;
  picture_BMI.bmiHeader.biCompression = BI_RGB;
  picture_BMI.bmiHeader.biSizeImage = 0;
  picture_BMI.bmiHeader.biXPelsPerMeter = 0;
  picture_BMI.bmiHeader.biYPelsPerMeter = 0;
  picture_BMI.bmiHeader.biClrUsed = 0;
  picture_BMI.bmiHeader.biClrImportant = 0;
  picture_BMI.bmiColors[0].rgbRed = 0;
  picture_BMI.bmiColors[0].rgbGreen = 0;
  picture_BMI.bmiColors[0].rgbBlue = 0;
  picture_BMI.bmiColors[0].rgbReserved = 0;

  HDC dc = GetDC( dialog );
  picture_BMP = CreateDIBSection( dc, &picture_BMI, DIB_RGB_COLORS,
                                  (void **)&picture, NULL, 0 );
  ReleaseDC( dialog, dc );

  return dialog;
}

/* The window's place and size step are kept in fuse-windows.ini */
static void
save_geometry( HWND dialog )
{
  RECT rect;

  if( IsIconic( dialog ) || !GetWindowRect( dialog, &rect ) ) return;
  winstate_set( "keyboard", "x", rect.left );
  winstate_set( "keyboard", "y", rect.top );
  winstate_set( "keyboard", "step", step );
  /* Earlier versions kept the size */
  winstate_remove( "keyboard", "width" );
  winstate_remove( "keyboard", "height" );
}

/* The step nearest to a window width saved by an earlier version */
static int
step_for_width( HWND dialog, long width )
{
  int at, best = LARGEST_STEP, best_difference = -1;

  for( at = 0; at < STEPS; at++ ) {
    int step_width, step_height, difference;
    window_size( dialog, at, &step_width, &step_height );
    difference = abs( step_width - (int)width );
    if( best_difference < 0 || difference < best_difference ) {
      best = at;
      best_difference = difference;
    }
  }
  return best;
}

/* Put the window where it was last time, if its title bar would be on a
   monitor (which may have been disconnected since), at the step it had
   (or the largest one that fits there now) */
static void
restore_geometry( HWND dialog )
{
  RECT rect, title;
  long x, y, saved_step, width;

  if( !winstate_get( "keyboard", "x", &x ) ||
      !winstate_get( "keyboard", "y", &y ) )
    return;

  if( winstate_get( "keyboard", "step", &saved_step ) &&
      saved_step >= 0 && saved_step < STEPS ) {
    step = saved_step;
  } else if( winstate_get( "keyboard", "width", &width ) && width > 0 ) {
    step = step_for_width( dialog, width );
  }

  GetWindowRect( dialog, &rect );
  OffsetRect( &rect, x - rect.left, y - rect.top );
  title = rect;
  title.bottom = rect.top + GetSystemMetrics( SM_CYCAPTION );
  if( !MonitorFromRect( &title, MONITOR_DEFAULTTONULL ) ) {
    set_step( dialog, fitting_step( dialog, step, NULL ) );
    return;
  }

  SetWindowPos( dialog, NULL, x, y, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
  set_step( dialog, fitting_step( dialog, step, &rect ) );
}

static void
destroy_dialog( void )
{
  HWND dialog = hDialogPicture;

  save_geometry( dialog );
  hDialogPicture = NULL;
  DestroyWindow( dialog );

  DeleteObject( picture_BMP );
  picture_BMP = NULL;
}

/* Dragging the frame: where the window was and the mouse pointer was when
   it started */
static RECT drag_start_window;
static POINT drag_start_pointer;

static void
start_resizing( HWND dialog )
{
  GetWindowRect( dialog, &drag_start_window );
  GetCursorPos( &drag_start_pointer );
}

/* Dragging the frame: the nearest step that fits; the side being dragged
   moves, the opposite one stays. The size wanted is taken from the
   pointer, not from the rectangle proposed (which may be built on the one
   snapped to last time, so it would never get to the next step) */
static void
resizing_dialog( HWND dialog, WPARAM edge, LPARAM lParam )
{
  RECT *rc = (RECT *)lParam;
  POINT pointer;
  int at, best = step, best_difference = -1, width, height;
  int wanted_width, wanted_height, dx, dy;
  int by_width = edge == WMSZ_LEFT || edge == WMSZ_RIGHT;
  int by_height = edge == WMSZ_TOP || edge == WMSZ_BOTTOM;
  int left = edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT ||
             edge == WMSZ_BOTTOMLEFT;
  int top = edge == WMSZ_TOP || edge == WMSZ_TOPLEFT ||
            edge == WMSZ_TOPRIGHT;

  GetCursorPos( &pointer );
  dx = pointer.x - drag_start_pointer.x;
  dy = pointer.y - drag_start_pointer.y;
  wanted_width = drag_start_window.right - drag_start_window.left +
                 ( left ? -dx : dx );
  wanted_height = drag_start_window.bottom - drag_start_window.top +
                  ( top ? -dy : dy );

  for( at = 0; at < STEPS; at++ ) {
    int difference;

    if( at && !step_fits( dialog, at, rc ) ) continue;
    window_size( dialog, at, &width, &height );
    /* A corner: by both */
    difference = by_width ? abs( width - wanted_width ) :
                 by_height ? abs( height - wanted_height ) :
                 abs( width - wanted_width ) + abs( height - wanted_height );
    if( best_difference < 0 || difference < best_difference ) {
      best = at;
      best_difference = difference;
    }
  }

  step = best;
  window_size( dialog, step, &width, &height );

  /* From where the window was: the opposite sides stay */
  *rc = drag_start_window;
  if( left ) rc->left = rc->right - width;
  else rc->right = rc->left + width;
  if( top ) rc->top = rc->bottom - height;
  else rc->bottom = rc->top + height;
}

static int
draw_frame( LPDRAWITEMSTRUCT drawitem )
{
  float scale_x, scale_y, scale_factor;
  int offset_x, offset_y;
  int scaled_width, scaled_height;
  int allocation_width, allocation_height;
  BITMAP bitmap_info;
  RECT rc;

  if( !( drawitem->itemAction & ODA_DRAWENTIRE ) )
    return FALSE;

  HDC pic_dc = CreateCompatibleDC( drawitem->hDC );
  HBITMAP old_bmp = SelectObject( pic_dc, picture_BMP );
  GetObject( picture_BMP, sizeof( BITMAP ), &bitmap_info );

  allocation_width = drawitem->rcItem.right - drawitem->rcItem.left;
  allocation_height = drawitem->rcItem.bottom - drawitem->rcItem.top;

  scale_x = (float)allocation_width / bitmap_info.bmWidth;
  scale_y = (float)allocation_height / bitmap_info.bmHeight;
  scale_factor = scale_x < scale_y ? scale_x : scale_y;

  scaled_width = bitmap_info.bmWidth * scale_factor;
  scaled_height = bitmap_info.bmHeight * scale_factor;
  offset_x = ( allocation_width - scaled_width ) / 2;
  offset_y = ( allocation_height - scaled_height ) / 2;

  /* Good quality when the picture is shown smaller than its own size */
  if( scale_factor < 1.0f ) {
    SetStretchBltMode( drawitem->hDC, HALFTONE );
    SetBrushOrgEx( drawitem->hDC, 0, 0, NULL );
  } else {
    SetStretchBltMode( drawitem->hDC, COLORONCOLOR );
  }

  StretchBlt( drawitem->hDC, offset_x, offset_y,
              scaled_width,
              scaled_height,
              pic_dc, 0, 0, bitmap_info.bmWidth, bitmap_info.bmHeight,
              SRCCOPY );

  SelectObject( pic_dc, old_bmp );
  DeleteDC( pic_dc );

  /* Erase background (hopefully) without flickering */
  if( offset_y > 0 ) {
    rc.left = 0;
    rc.top = 0;
    rc.right = allocation_width;
    rc.bottom = offset_y;
    FillRect( drawitem->hDC, &rc, (HBRUSH) GetStockObject( BLACK_BRUSH ) );

    rc.left = 0;
    rc.top = offset_y + scaled_height;
    rc.right = allocation_width;
    rc.bottom = allocation_height;
    FillRect( drawitem->hDC, &rc, (HBRUSH) GetStockObject( BLACK_BRUSH ) );
  }

  if( offset_x > 0 ) {
    rc.left = 0;
    rc.top = 0;
    rc.right = offset_x;
    rc.bottom = allocation_height;
    FillRect( drawitem->hDC, &rc, (HBRUSH) GetStockObject( BLACK_BRUSH ) );

    rc.left = offset_x + scaled_width;
    rc.top = 0;
    rc.right = allocation_width;
    rc.bottom = allocation_height;
    FillRect( drawitem->hDC, &rc, (HBRUSH) GetStockObject( BLACK_BRUSH ) );
  }

  return TRUE;
}

static LRESULT WINAPI
picture_wnd_proc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
  switch( msg ) {
    case WM_SIZE:
      resize_dialog( hWnd );
      return 0;

    case WM_ENTERSIZEMOVE:
      start_resizing( hWnd );
      break;

    case WM_SIZING:
      resizing_dialog( hWnd, wParam, lParam );
      return TRUE;

    case WM_DRAWITEM:
      if( wParam == IDC_PICTURE_FRAME )
        return draw_frame( (LPDRAWITEMSTRUCT) lParam );
      break;

    case WM_COMMAND:
      if( LOWORD( wParam ) == IDCLOSE ) {
        destroy_dialog();
        return 0;
      }
      /* A double click on the picture: the next size step */
      if( LOWORD( wParam ) == IDC_PICTURE_FRAME &&
          HIWORD( wParam ) == STN_DBLCLK ) {
        next_step( hWnd );
        return 0;
      }
      break;

    case WM_CLOSE:
      destroy_dialog();
      return 0;

    case WM_DESTROY:
      /* Destroyed with the main window, as Fuse exits */
      if( hWnd == hDialogPicture ) {
        save_geometry( hWnd );
        hDialogPicture = NULL;
      }
      break;

    case WM_MOUSEACTIVATE:
      /* Clicking the picture (or its Close button) doesn't take the focus
         from the main window */
      SetWindowLongPtr( hWnd, DWLP_MSGRESULT, MA_NOACTIVATE );
      return TRUE;
  }

  return FALSE;
}

static void
draw_screen( libspectrum_byte *screen, int border )
{
  int i, x, y, ink, paper;
  libspectrum_byte attr, data;

  GdiFlush();

  for( y=0; y < DISPLAY_BORDER_HEIGHT; y++ ) {
    for( x=0; x < DISPLAY_ASPECT_WIDTH; x++ ) {
      *(libspectrum_dword*)( picture + y * picture_pitch + 4 * x ) =
        win32display_colours[border];
      *(libspectrum_dword*)(
          picture +
          ( y + DISPLAY_BORDER_HEIGHT + DISPLAY_HEIGHT ) * picture_pitch +
          4 * x
        ) = win32display_colours[ border ];
    }
  }

  for( y=0; y<DISPLAY_HEIGHT; y++ ) {

    for( x=0; x < DISPLAY_BORDER_ASPECT_WIDTH; x++ ) {
      *(libspectrum_dword*)
        (picture + ( y + DISPLAY_BORDER_HEIGHT) * picture_pitch + 4 * x) =
        win32display_colours[ border ];
      *(libspectrum_dword*)(
          picture +
          ( y + DISPLAY_BORDER_HEIGHT ) * picture_pitch +
          4 * ( x+DISPLAY_ASPECT_WIDTH-DISPLAY_BORDER_ASPECT_WIDTH )
        ) = win32display_colours[ border ];
    }

    for( x=0; x < DISPLAY_WIDTH_COLS; x++ ) {

      attr = screen[ display_attr_start[y] + x ];

      ink = ( attr & 0x07 ) + ( ( attr & 0x40 ) >> 3 );
      paper = ( attr & ( 0x0f << 3 ) ) >> 3;

      data = screen[ display_line_start[y]+x ];

      for( i=0; i<8; i++ ) {
        libspectrum_dword pix =
          win32display_colours[ ( data & 0x80 ) ? ink : paper ];

        /* rearrange pixel components */
        pix = ( pix & 0x0000ff00 ) |
              ( ( pix & 0x000000ff ) << 16 ) |
              ( ( pix & 0x00ff0000 ) >> 16 );

        *(libspectrum_dword*)(
            picture +
            ( y + DISPLAY_BORDER_HEIGHT ) * picture_pitch +
            4 * ( 8 * x + DISPLAY_BORDER_ASPECT_WIDTH + i )
          ) = pix;
        data <<= 1;
      }
    }

  }
}

/* Load a picture in any format Windows can decode itself with GDI+ (JPEG,
   PNG, GIF, BMP) from the lib directory. Returns a bitmap the caller must
   delete, or NULL on error */
static HBITMAP
load_picture( const char *filename, int *width, int *height )
{
  utils_file file;
  HGLOBAL memory;
  void *data;
  IStream *stream = NULL;
  GdiplusStartupInput startup_input;
  ULONG_PTR token;
  GpBitmap *image = NULL;
  HBITMAP bitmap = NULL;
  UINT image_width, image_height;

  if( utils_read_auxiliary_file( filename, &file, UTILS_AUXILIARY_LIB ) )
    return NULL;

  memory = GlobalAlloc( GMEM_MOVEABLE, file.length );
  if( !memory ) {
    utils_close_file( &file );
    return NULL;
  }

  data = GlobalLock( memory );
  memcpy( data, file.buffer, file.length );
  GlobalUnlock( memory );
  utils_close_file( &file );

  /* The stream takes ownership of the memory */
  if( FAILED( CreateStreamOnHGlobal( memory, TRUE, &stream ) ) ) {
    GlobalFree( memory );
    return NULL;
  }

  memset( &startup_input, 0, sizeof( startup_input ) );
  startup_input.GdiplusVersion = 1;
  if( GdiplusStartup( &token, &startup_input, NULL ) != Ok ) {
    stream->lpVtbl->Release( stream );
    return NULL;
  }

  if( GdipCreateBitmapFromStream( stream, &image ) == Ok &&
      GdipGetImageWidth( (GpImage *)image, &image_width ) == Ok &&
      GdipGetImageHeight( (GpImage *)image, &image_height ) == Ok &&
      /* transparent areas on white */
      GdipCreateHBITMAPFromBitmap( image, &bitmap, 0xffffffff ) == Ok ) {
    *width = image_width;
    *height = image_height;
  } else {
    bitmap = NULL;
  }

  if( image ) GdipDisposeImage( (GpImage *)image );
  GdiplusShutdown( token );
  stream->lpVtbl->Release( stream );

  return bitmap;
}

/* Show a picture decoded by Windows. Returns 0 on success */
static int
show_system_picture( const char *filename )
{
  int width = 0, height = 0;
  HBITMAP bitmap;

  bitmap = load_picture( filename, &width, &height );
  if( !bitmap ) return 1;

  hDialogPicture = create_dialog( width, height );
  if( hDialogPicture != NULL ) draw_bitmap( bitmap, width, height );

  DeleteObject( bitmap );

  return hDialogPicture == NULL;
}

/* Copy a bitmap into the picture buffer */
static void
draw_bitmap( HBITMAP bitmap, int width, int height )
{
  HDC screen_dc, source_dc, picture_dc;
  HGDIOBJ old_source, old_picture;

  GdiFlush();

  screen_dc = GetDC( NULL );
  source_dc = CreateCompatibleDC( screen_dc );
  picture_dc = CreateCompatibleDC( screen_dc );
  ReleaseDC( NULL, screen_dc );

  old_source = SelectObject( source_dc, bitmap );
  old_picture = SelectObject( picture_dc, picture_BMP );

  BitBlt( picture_dc, 0, 0, width, height, source_dc, 0, 0, SRCCOPY );

  SelectObject( source_dc, old_source );
  SelectObject( picture_dc, old_picture );
  DeleteDC( source_dc );
  DeleteDC( picture_dc );
}

#ifdef PNG_SIMPLIFIED_READ_SUPPORTED

static void
draw_png( libspectrum_byte *buffer, size_t size )
{
  GdiFlush();
  memcpy( picture, buffer, size );
}

static int
read_png_file( const char *filename, png_bytep *buffer, size_t *size,
               int *width, int *height )
{
  utils_file png_file;
  int error = utils_read_auxiliary_file( filename, &png_file,
                                         UTILS_AUXILIARY_LIB );
  if( error == -1 ) {
    return 1;
  }
  if( error ) return 1;

  /* Initialize the 'png_image' structure */
  png_image image;
  memset( &image, 0, sizeof( image ) );
  image.version = PNG_IMAGE_VERSION;
  image.opaque = NULL;

  if( png_image_begin_read_from_memory( &image, png_file.buffer,
                                        png_file.length ) == 0 ) {
    utils_close_file( &png_file );
    return 1;
  }

  image.format = PNG_FORMAT_BGRA;
  *buffer = malloc( PNG_IMAGE_SIZE( image ) );

  if( *buffer == NULL ) {
    png_image_free( &image );
    utils_close_file( &png_file );
    return 1;
  }
  *size = PNG_IMAGE_SIZE( image );

  if( !png_image_finish_read( &image, NULL, *buffer, 0, NULL ) ) {
    png_image_free( &image );
    free( *buffer );
    utils_close_file( &png_file );
    return 1;
  }

  *width = PNG_IMAGE_ROW_STRIDE( image ) /
           PNG_IMAGE_PIXEL_SIZE( PNG_FORMAT_BGRA );
  *height = PNG_IMAGE_SIZE( image ) / PNG_IMAGE_ROW_STRIDE( image );
  png_image_free( &image );
  utils_close_file( &png_file );

  return 0;
}

#endif			/* #ifdef PNG_SIMPLIFIED_READ_SUPPORTED */

/* sdl2_keyboard.c: SDL 2 keyboard handling
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/

#include "config.h"

#include <SDL.h>

#include "input.h"
#include "keyboard.h"
#include "settings.h"
#include "ui/ui.h"
#include "sdl2_keyboard.h"

keysyms_map_t keysyms_map[] = {
  { SDLK_TAB,         INPUT_KEY_Tab },
  { SDLK_RETURN,      INPUT_KEY_Return },
  { SDLK_ESCAPE,      INPUT_KEY_Escape },
  { SDLK_SPACE,       INPUT_KEY_space },
  { SDLK_EXCLAIM,     INPUT_KEY_exclam },
  { SDLK_QUOTEDBL,    INPUT_KEY_quotedbl },
  { SDLK_HASH,        INPUT_KEY_numbersign },
  { SDLK_DOLLAR,      INPUT_KEY_dollar },
  { SDLK_PERCENT,     INPUT_KEY_percent },
  { SDLK_AMPERSAND,   INPUT_KEY_ampersand },
  { SDLK_QUOTE,       INPUT_KEY_apostrophe },
  { SDLK_LEFTPAREN,   INPUT_KEY_parenleft },
  { SDLK_RIGHTPAREN,  INPUT_KEY_parenright },
  { SDLK_ASTERISK,    INPUT_KEY_asterisk },
  { SDLK_PLUS,        INPUT_KEY_plus },
  { SDLK_COMMA,       INPUT_KEY_comma },
  { SDLK_MINUS,       INPUT_KEY_minus },
  { SDLK_PERIOD,      INPUT_KEY_period },
  { SDLK_SLASH,       INPUT_KEY_slash },
  { SDLK_0,           INPUT_KEY_0 },
  { SDLK_1,           INPUT_KEY_1 },
  { SDLK_2,           INPUT_KEY_2 },
  { SDLK_3,           INPUT_KEY_3 },
  { SDLK_4,           INPUT_KEY_4 },
  { SDLK_5,           INPUT_KEY_5 },
  { SDLK_6,           INPUT_KEY_6 },
  { SDLK_7,           INPUT_KEY_7 },
  { SDLK_8,           INPUT_KEY_8 },
  { SDLK_9,           INPUT_KEY_9 },
  { SDLK_COLON,       INPUT_KEY_colon },
  { SDLK_SEMICOLON,   INPUT_KEY_semicolon },
  { SDLK_LESS,        INPUT_KEY_less },
  { SDLK_EQUALS,      INPUT_KEY_equal },
  { SDLK_GREATER,     INPUT_KEY_greater },
  { SDLK_QUESTION,    INPUT_KEY_question },
  { SDLK_AT,          INPUT_KEY_at },
  { SDLK_LEFTBRACKET, INPUT_KEY_bracketleft },
  { SDLK_BACKSLASH,   INPUT_KEY_backslash },
  { SDLK_RIGHTBRACKET, INPUT_KEY_bracketright },
  { SDLK_CARET,       INPUT_KEY_asciicircum },
  { SDLK_UNDERSCORE,  INPUT_KEY_underscore },
  { SDLK_a,           INPUT_KEY_a },
  { SDLK_b,           INPUT_KEY_b },
  { SDLK_c,           INPUT_KEY_c },
  { SDLK_d,           INPUT_KEY_d },
  { SDLK_e,           INPUT_KEY_e },
  { SDLK_f,           INPUT_KEY_f },
  { SDLK_g,           INPUT_KEY_g },
  { SDLK_h,           INPUT_KEY_h },
  { SDLK_i,           INPUT_KEY_i },
  { SDLK_j,           INPUT_KEY_j },
  { SDLK_k,           INPUT_KEY_k },
  { SDLK_l,           INPUT_KEY_l },
  { SDLK_m,           INPUT_KEY_m },
  { SDLK_n,           INPUT_KEY_n },
  { SDLK_o,           INPUT_KEY_o },
  { SDLK_p,           INPUT_KEY_p },
  { SDLK_q,           INPUT_KEY_q },
  { SDLK_r,           INPUT_KEY_r },
  { SDLK_s,           INPUT_KEY_s },
  { SDLK_t,           INPUT_KEY_t },
  { SDLK_u,           INPUT_KEY_u },
  { SDLK_v,           INPUT_KEY_v },
  { SDLK_w,           INPUT_KEY_w },
  { SDLK_x,           INPUT_KEY_x },
  { SDLK_y,           INPUT_KEY_y },
  { SDLK_z,           INPUT_KEY_z },
  { SDLK_BACKQUOTE,   INPUT_KEY_asciitilde },
  { SDLK_BACKSPACE,   INPUT_KEY_BackSpace },
  { SDLK_KP_ENTER,    INPUT_KEY_KP_Enter },
  { SDLK_UP,          INPUT_KEY_Up },
  { SDLK_DOWN,        INPUT_KEY_Down },
  { SDLK_LEFT,        INPUT_KEY_Left },
  { SDLK_RIGHT,       INPUT_KEY_Right },
  { SDLK_INSERT,      INPUT_KEY_Insert },
  { SDLK_DELETE,      INPUT_KEY_Delete },
  { SDLK_HOME,        INPUT_KEY_Home },
  { SDLK_END,         INPUT_KEY_End },
  { SDLK_PAGEUP,      INPUT_KEY_Page_Up },
  { SDLK_PAGEDOWN,    INPUT_KEY_Page_Down },
  { SDLK_CAPSLOCK,    INPUT_KEY_Caps_Lock },
  { SDLK_F1,          INPUT_KEY_F1 },
  { SDLK_F2,          INPUT_KEY_F2 },
  { SDLK_F3,          INPUT_KEY_F3 },
  { SDLK_F4,          INPUT_KEY_F4 },
  { SDLK_F5,          INPUT_KEY_F5 },
  { SDLK_F6,          INPUT_KEY_F6 },
  { SDLK_F7,          INPUT_KEY_F7 },
  { SDLK_F8,          INPUT_KEY_F8 },
  { SDLK_F9,          INPUT_KEY_F9 },
  { SDLK_F10,         INPUT_KEY_F10 },
  { SDLK_F11,         INPUT_KEY_F11 },
  { SDLK_F12,         INPUT_KEY_F12 },
  { SDLK_LSHIFT,      INPUT_KEY_Shift_L },
  { SDLK_RSHIFT,      INPUT_KEY_Shift_R },
  { SDLK_LCTRL,       INPUT_KEY_Control_L },
  { SDLK_RCTRL,       INPUT_KEY_Control_R },
  { SDLK_LALT,        INPUT_KEY_Alt_L },
  { SDLK_RALT,        INPUT_KEY_Alt_R },
  { SDLK_LGUI,        INPUT_KEY_Super_L },
  { SDLK_RGUI,        INPUT_KEY_Super_R },
  { SDLK_MODE,        INPUT_KEY_Mode_switch },
  { 0, 0 }
};

static input_key
sdl2keyboard_native_map( const SDL_KeyboardEvent *keyevent )
{
  return keysyms_remap( keyevent->keysym.sym );
}

/* Decode the first character of a UTF-8 string; 0 if invalid or empty */
static libspectrum_dword
utf8_first_character( const char *text )
{
  const unsigned char *s = (const unsigned char *)text;

  if( !s || !s[0] ) return 0;
  if( s[0] < 0x80 ) return s[0];
  if( ( s[0] & 0xe0 ) == 0xc0 && ( s[1] & 0xc0 ) == 0x80 )
    return ( ( s[0] & 0x1f ) << 6 ) | ( s[1] & 0x3f );
  if( ( s[0] & 0xf0 ) == 0xe0 && ( s[1] & 0xc0 ) == 0x80 &&
      ( s[2] & 0xc0 ) == 0x80 )
    return ( ( s[0] & 0x0f ) << 12 ) | ( ( s[1] & 0x3f ) << 6 ) |
           ( s[2] & 0x3f );
  if( ( s[0] & 0xf8 ) == 0xf0 && ( s[1] & 0xc0 ) == 0x80 &&
      ( s[2] & 0xc0 ) == 0x80 && ( s[3] & 0xc0 ) == 0x80 )
    return ( ( s[0] & 0x07 ) << 18 ) | ( ( s[1] & 0x3f ) << 12 ) |
           ( ( s[2] & 0x3f ) << 6 ) | ( s[3] & 0x3f );

  return 0;
}

/* The character produced by a key press arrives in the SDL_TEXTINPUT event
   which immediately follows the SDL_KEYDOWN. Take it from the queue */
static libspectrum_dword
sdl2keyboard_get_character( const SDL_KeyboardEvent *keyevent )
{
  SDL_Event next;
  SDL_Keymod mod = keyevent->keysym.mod;
  int altgr = ( mod & ( KMOD_RALT | KMOD_MODE ) ) ? 1 : 0;

  if( !settings_current.hungarian_keyboard ) return 0;

  /* Ctrl or left Alt without AltGr act as Symbol Shift: no character */
  if( !altgr && ( mod & ( KMOD_CTRL | KMOD_LALT ) ) ) return 0;

  if( SDL_PeepEvents( &next, 1, SDL_PEEKEVENT, SDL_FIRSTEVENT,
                      SDL_LASTEVENT ) == 1 && next.type == SDL_TEXTINPUT ) {
    SDL_PeepEvents( &next, 1, SDL_GETEVENT, SDL_TEXTINPUT, SDL_TEXTINPUT );
    return utf8_first_character( next.text.text );
  }

  return 0;
}

static void
sdl2keyboard_dispatch( input_event_type type, SDL_KeyboardEvent *keyevent )
{
  input_key keysym;
  input_event_t fuse_event;
  libspectrum_dword character = 0;

  keysym = sdl2keyboard_native_map( keyevent );

  if( type == INPUT_EVENT_KEYPRESS )
    character = sdl2keyboard_get_character( keyevent );

  /* Keys with no mapping are still passed on if they produced a character
     (e.g. accented letters), or on release, as they may have done so */
  if( keysym == INPUT_KEY_NONE && !character &&
      !( type == INPUT_EVENT_KEYRELEASE &&
         settings_current.hungarian_keyboard ) )
    return;

  fuse_event.type = type;
  fuse_event.types.key.native_key = keysym;
  fuse_event.types.key.spectrum_key = keysym;
  fuse_event.types.key.character = character;
  fuse_event.types.key.physical_key = keyevent->keysym.scancode;

  input_event( &fuse_event );
}

void
sdl2keyboard_init( void )
{
}

void
sdl2keyboard_end( void )
{
}

void
sdl2keyboard_release_all( void )
{
  keyboard_release_all();
}

void
sdl2keyboard_keypress( SDL_KeyboardEvent *keyevent )
{
  sdl2keyboard_dispatch( INPUT_EVENT_KEYPRESS, keyevent );
}

void
sdl2keyboard_keyrelease( SDL_KeyboardEvent *keyevent )
{
  sdl2keyboard_dispatch( INPUT_EVENT_KEYRELEASE, keyevent );
}

/* hungarian_keyboard.c: type with a Hungarian (or any other) host keyboard
   layout, using the character produced by the host
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

/* On a Hungarian (QWERTZ) keyboard the symbols are in different places than
   on an English one, and there are keys for accented vowels. Instead of
   mapping physical key positions, the UI tells us which character the host
   produced (e.g. Shift+2 = double quote) and we press the Spectrum key
   combination producing the same character (Symbol Shift + P).

   - lower case letters and digits: the key itself
   - upper case letters: Caps Shift + letter
   - symbols: Symbol Shift + the right key
   - Hungarian accented vowels: the vowel without accent
   - extended mode symbols ([ ] { } \ | ~ and the copyright sign): a timed
     sequence is sent: Caps Shift + Symbol Shift (E mode), release,
     Symbol Shift + key, release. This works in both the 48K and the 128K
     editors
   - characters with no Spectrum equivalent are swallowed

   Every host key is tracked by its physical key code, so that we release
   exactly what was pressed, even if no character is produced on release. */

#include "config.h"

#include "fuse.h"
#include "hungarian_keyboard.h"
#include "keyboard.h"
#include "settings.h"
#include "ui/ui.h"

/* Frames (1/50 s) every step of a timed sequence lasts */
#define STEP_FRAMES 3

#define MAX_HELD 32
#define QUEUE_SIZE 32
#define MAX_MODIFIERS 16

/* A host key currently held which we have handled */
typedef struct held_key_t {
  int physical_key;
  keyboard_key_name key;	/* KEYBOARD_NONE if nothing is held on the
				   Spectrum for it (swallowed, or queued) */
  int caps;
  int symbol;
  unsigned long order;
} held_key_t;

static held_key_t held_keys[ MAX_HELD ];
static int held_count = 0;
static unsigned long press_order = 0;

/* An entry of the timed sequence queue */
typedef struct sequence_t {
  keyboard_key_name key;
  int caps;
  int symbol;
  int extended;
} sequence_t;

static sequence_t queue[ QUEUE_SIZE ];
static int queue_start = 0, queue_count = 0;

/* Current step of the sequence at the head of the queue:
   0: press Caps + Symbol (E mode)  1: release  2: press key  3: release */
static int queue_step = 0, queue_step_frames = 0;

/* Host modifier keys currently held (their Fuse input key) */
static input_key held_modifiers[ MAX_MODIFIERS ];
static int held_modifier_count = 0;

typedef struct symbol_map_t {
  libspectrum_dword character;
  keyboard_key_name key;
} symbol_map_t;

/* Characters typed with Symbol Shift */
static const symbol_map_t symbols[] = {
  { '!', KEYBOARD_1 }, { '@', KEYBOARD_2 }, { '#', KEYBOARD_3 },
  { '$', KEYBOARD_4 }, { '%', KEYBOARD_5 }, { '&', KEYBOARD_6 },
  { '\'', KEYBOARD_7 }, { '(', KEYBOARD_8 }, { ')', KEYBOARD_9 },
  { '_', KEYBOARD_0 },
  { '<', KEYBOARD_r }, { '>', KEYBOARD_t }, { ';', KEYBOARD_o },
  { '"', KEYBOARD_p }, { '^', KEYBOARD_h }, { '-', KEYBOARD_j },
  { '+', KEYBOARD_k }, { '=', KEYBOARD_l }, { ':', KEYBOARD_z },
  { 0xa3, KEYBOARD_x },		/* pound sign */
  { '?', KEYBOARD_c }, { '/', KEYBOARD_v }, { '*', KEYBOARD_b },
  { ',', KEYBOARD_n }, { '.', KEYBOARD_m },
  { 0xd7, KEYBOARD_b },		/* multiplication sign (AltGr + u acute) */
  { 0xf7, KEYBOARD_v },		/* division sign (AltGr + o double acute) */
  { 0, KEYBOARD_NONE }
};

/* Characters typed in extended mode with Symbol Shift */
static const symbol_map_t extended_symbols[] = {
  { '[', KEYBOARD_y }, { ']', KEYBOARD_u }, { '{', KEYBOARD_f },
  { '}', KEYBOARD_g }, { '\\', KEYBOARD_d }, { '|', KEYBOARD_s },
  { '~', KEYBOARD_a },
  { 0xa9, KEYBOARD_p },		/* copyright sign */
  { 0, KEYBOARD_NONE }
};

/* Map accented letters (Hungarian and a few more) to the plain letter */
static libspectrum_dword
remove_accent( libspectrum_dword c )
{
  switch( c ) {
  case 0xe1: case 0xe4:             return 'a';
  case 0xc1: case 0xc4:             return 'A';
  case 0xe9:                        return 'e';
  case 0xc9:                        return 'E';
  case 0xed:                        return 'i';
  case 0xcd:                        return 'I';
  case 0xf3: case 0xf6: case 0x151: return 'o';
  case 0xd3: case 0xd6: case 0x150: return 'O';
  case 0xfa: case 0xfc: case 0x171: return 'u';
  case 0xda: case 0xdc: case 0x170: return 'U';
  }

  return c;
}

int
hungarian_keyboard_map_character( libspectrum_dword character, int *key,
                                  int *caps, int *symbol, int *extended )
{
  libspectrum_dword c = remove_accent( character );
  const symbol_map_t *ptr;

  *caps = 0; *symbol = 0; *extended = 0;

  if( c >= 'a' && c <= 'z' ) { *key = KEYBOARD_a + ( c - 'a' ); return 1; }

  if( c >= 'A' && c <= 'Z' ) {
    *key = KEYBOARD_a + ( c - 'A' ); *caps = 1; return 1;
  }

  if( c >= '0' && c <= '9' ) { *key = KEYBOARD_0 + ( c - '0' ); return 1; }

  for( ptr = symbols; ptr->character; ptr++ ) {
    if( ptr->character == c ) { *key = ptr->key; *symbol = 1; return 1; }
  }

  for( ptr = extended_symbols; ptr->character; ptr++ ) {
    if( ptr->character == c ) {
      *key = ptr->key; *symbol = 1; *extended = 1; return 1;
    }
  }

  return 0;
}

int
hungarian_keyboard_active( void )
{
  return settings_current.hungarian_keyboard &&
         !settings_current.recreated_spectrum &&
         ui_widget_level < 0;
}

static void
set_caps( int press )
{
  if( press ) keyboard_press( KEYBOARD_Caps );
  else keyboard_release( KEYBOARD_Caps );
}

static void
set_symbol( int press )
{
  if( press ) keyboard_press( KEYBOARD_Symbol );
  else keyboard_release( KEYBOARD_Symbol );
}

/* Is any held host modifier mapped to this Spectrum shift key? */
static int
modifiers_hold( keyboard_key_name shift )
{
  int i;

  for( i = 0; i < held_modifier_count; i++ ) {
    const keyboard_spectrum_keys_t *keys =
      keyboard_get_spectrum_keys( held_modifiers[i] );
    if( keys && ( keys->key1 == shift || keys->key2 == shift ) ) return 1;
  }

  return 0;
}

/* Set Caps and Symbol Shift according to the last key we still hold, or if
   there is none, according to the host modifiers (as the normal mapping
   would do) */
static void
restore_shifts( void )
{
  int i, last = -1;

  if( queue_count ) return;	/* the running sequence owns the shifts */

  for( i = 0; i < held_count; i++ ) {
    if( held_keys[i].key == KEYBOARD_NONE ) continue;
    if( last < 0 || held_keys[i].order > held_keys[ last ].order ) last = i;
  }

  if( last >= 0 ) {
    set_caps( held_keys[ last ].caps );
    set_symbol( held_keys[ last ].symbol );
  } else {
    set_caps( modifiers_hold( KEYBOARD_Caps ) );
    set_symbol( modifiers_hold( KEYBOARD_Symbol ) );
  }
}

static held_key_t*
find_held( int physical_key )
{
  int i;

  for( i = 0; i < held_count; i++ )
    if( held_keys[i].physical_key == physical_key ) return &held_keys[i];

  return NULL;
}

static void
queue_add( keyboard_key_name key, int caps, int symbol, int extended )
{
  sequence_t *entry;
  int i;

  if( queue_count >= QUEUE_SIZE ) return;

  entry = &queue[ ( queue_start + queue_count ) % QUEUE_SIZE ];
  entry->key = key; entry->caps = caps; entry->symbol = symbol;
  entry->extended = extended;

  if( queue_count == 0 ) {
    /* A new sequence starts: stop holding our keys on the Spectrum so they
       don't interfere. They stay held on the host */
    for( i = 0; i < held_count; i++ ) {
      if( held_keys[i].key != KEYBOARD_NONE ) {
        keyboard_release( held_keys[i].key );
        held_keys[i].key = KEYBOARD_NONE;
      }
    }

    queue_step = extended ? 0 : 2;
    queue_step_frames = 0;
  }

  queue_count++;
}

int
hungarian_keyboard_keypress( const input_event_key_t *event )
{
  held_key_t *held;
  int key, caps, symbol, extended;

  if( !hungarian_keyboard_active() ) return 0;

  /* Only keys which produced a printable character other than space */
  if( event->character <= 0x20 || event->character == 0x7f ) return 0;

  /* Host auto repeat of a key we already handle */
  if( find_held( event->physical_key ) ) return 1;

  if( held_count >= MAX_HELD ) return 1;

  held = &held_keys[ held_count++ ];
  held->physical_key = event->physical_key;
  held->key = KEYBOARD_NONE;
  held->caps = held->symbol = 0;
  held->order = ++press_order;

  /* No Spectrum equivalent: swallow the key */
  if( !hungarian_keyboard_map_character( event->character, &key, &caps,
                                         &symbol, &extended ) )
    return 1;

  /* Extended mode symbols, and anything typed while a sequence is running,
     go through the queue to keep the typing order */
  if( extended || queue_count ) {
    queue_add( key, caps, symbol, extended );
    return 1;
  }

  held->key = key;
  held->caps = caps;
  held->symbol = symbol;

  /* The shifts the character needs override the host modifiers */
  set_caps( caps );
  set_symbol( symbol );
  keyboard_press( key );

  return 1;
}

int
hungarian_keyboard_keyrelease( const input_event_key_t *event )
{
  held_key_t *held = find_held( event->physical_key );

  if( !held ) return 0;

  if( held->key != KEYBOARD_NONE ) keyboard_release( held->key );

  *held = held_keys[ --held_count ];

  restore_shifts();

  return 1;
}

int
hungarian_keyboard_modifier( const input_event_key_t *event, int press )
{
  const keyboard_spectrum_keys_t *keys;
  int i;

  keys = keyboard_get_spectrum_keys( event->spectrum_key );

  /* A modifier is a host key mapped only to Caps or Symbol Shift */
  if( !keys || keys->key1 != KEYBOARD_NONE ||
      ( keys->key2 != KEYBOARD_Caps && keys->key2 != KEYBOARD_Symbol ) )
    return 0;

  for( i = 0; i < held_modifier_count; i++ )
    if( held_modifiers[i] == event->spectrum_key ) break;

  if( press && i == held_modifier_count &&
      held_modifier_count < MAX_MODIFIERS )
    held_modifiers[ held_modifier_count++ ] = event->spectrum_key;
  else if( !press && i < held_modifier_count )
    held_modifiers[i] = held_modifiers[ --held_modifier_count ];

  /* While a sequence runs, host modifiers must not touch the shifts; they
     are applied when it ends */
  return queue_count && hungarian_keyboard_active();
}

void
hungarian_keyboard_frame( void )
{
  sequence_t *entry;

  if( !queue_count ) return;

  if( !hungarian_keyboard_active() ) {
    /* e.g. the menu has been opened: drop the sequence, releasing what it
       holds on the Spectrum */
    keyboard_release( queue[ queue_start ].key );
    set_caps( 0 ); set_symbol( 0 );
    queue_start = queue_count = 0;
    queue_step = queue_step_frames = 0;
    return;
  }

  entry = &queue[ queue_start ];

  /* Apply the state of the current step. Repeated every frame in case
     something else changed the shifts meanwhile */
  switch( queue_step ) {
  case 0:
    set_caps( 1 ); set_symbol( 1 );
    break;
  case 1:
  case 3:
    set_caps( 0 ); set_symbol( 0 );
    if( queue_step == 3 && queue_step_frames == 0 )
      keyboard_release( entry->key );
    break;
  case 2:
    set_caps( entry->caps ); set_symbol( entry->symbol );
    keyboard_press( entry->key );
    break;
  }

  if( ++queue_step_frames < STEP_FRAMES ) return;

  queue_step_frames = 0;
  if( ++queue_step <= 3 ) return;

  /* This entry is done */
  queue_start = ( queue_start + 1 ) % QUEUE_SIZE;
  queue_count--;

  if( queue_count ) {
    queue_step = queue[ queue_start ].extended ? 0 : 2;
  } else {
    restore_shifts();
  }
}

void
hungarian_keyboard_reset( void )
{
  /* The Spectrum side has been (or is about to be) released by the
     caller, so only our bookkeeping needs clearing */
  held_count = 0;
  queue_start = queue_count = 0;
  queue_step = queue_step_frames = 0;
  held_modifier_count = 0;
}

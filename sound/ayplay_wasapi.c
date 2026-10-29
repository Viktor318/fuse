/* ayplay_wasapi.c: The AY player's own WASAPI sound stream
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

/* The music of the AY player has a stream of its own on the default output
   device (Windows mixes it with the sound of the emulator). A thread asks
   ayplay_render() for the samples whenever the device wants more, so the
   music does not depend on the emulation running. When the default device
   changes or the one in use goes away, the stream is opened again on the
   new default device. */

#include "config.h"
#define COBJMACROS
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "ayplay.h"
#include "settings.h"

/* The GUIDs are defined (initguid.h) in wasapi_native.c */

/* About 40 ms of sound ahead */
#define BUFFER_DURATION 400000		/* 100 ns units */
#define RETRY_MS 500

static HANDLE thread = NULL, stop_event = NULL;
static volatile LONG device_changed = 0;
static WCHAR device_id[ 512 ];

/* ------------------------------------------------------------------------
   Following the default device
   ------------------------------------------------------------------------ */

static HRESULT STDMETHODCALLTYPE
watch_query( IMMNotificationClient *self, REFIID riid, void **object )
{
  if( IsEqualIID( riid, &IID_IUnknown ) ||
      IsEqualIID( riid, &IID_IMMNotificationClient ) ) {
    *object = self;
    return S_OK;
  }
  *object = NULL;
  return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE
watch_addref( IMMNotificationClient *self ) { (void)self; return 1; }
static ULONG STDMETHODCALLTYPE
watch_release( IMMNotificationClient *self ) { (void)self; return 1; }

static int
is_current( LPCWSTR id )
{
  return id && device_id[0] && !wcscmp( id, device_id );
}

static HRESULT STDMETHODCALLTYPE
watch_state( IMMNotificationClient *self, LPCWSTR id, DWORD state )
{
  (void)self;
  if( state != DEVICE_STATE_ACTIVE && is_current( id ) ) {
    InterlockedExchange( &device_changed, 1 );
  }
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
watch_added( IMMNotificationClient *self, LPCWSTR id )
{
  (void)self; (void)id; return S_OK;
}
static HRESULT STDMETHODCALLTYPE
watch_removed( IMMNotificationClient *self, LPCWSTR id )
{
  (void)self;
  if( is_current( id ) ) InterlockedExchange( &device_changed, 1 );
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
watch_default( IMMNotificationClient *self, EDataFlow flow, ERole role,
               LPCWSTR id )
{
  (void)self;
  if( flow == eRender && role == eConsole && id && !is_current( id ) )
    InterlockedExchange( &device_changed, 1 );
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
watch_property( IMMNotificationClient *self, LPCWSTR id, const PROPERTYKEY key )
{
  (void)self; (void)id; (void)key; return S_OK;
}

static IMMNotificationClientVtbl watch_vtbl = {
  watch_query, watch_addref, watch_release, watch_state, watch_added,
  watch_removed, watch_default, watch_property
};
static IMMNotificationClient watch = { &watch_vtbl };

/* ------------------------------------------------------------------------
   The stream
   ------------------------------------------------------------------------ */

enum sample_type { TYPE_FLOAT, TYPE_INT16, TYPE_INT32, TYPE_INT24 };

static const GUID subtype_pcm = {
  WAVE_FORMAT_PCM, 0, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }
};
static const GUID subtype_float = {
  WAVE_FORMAT_IEEE_FLOAT, 0, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }
};

static int
sample_type( const WAVEFORMATEX *format, enum sample_type *type )
{
  int is_float = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
  int is_pcm = format->wFormatTag == WAVE_FORMAT_PCM;

  if( format->wFormatTag == WAVE_FORMAT_EXTENSIBLE ) {
    const WAVEFORMATEXTENSIBLE *ext = (const WAVEFORMATEXTENSIBLE *)format;
    is_float = IsEqualGUID( &ext->SubFormat, &subtype_float );
    is_pcm = IsEqualGUID( &ext->SubFormat, &subtype_pcm );
  }
  if( is_float && format->wBitsPerSample == 32 ) *type = TYPE_FLOAT;
  else if( is_pcm && format->wBitsPerSample == 16 ) *type = TYPE_INT16;
  else if( is_pcm && format->wBitsPerSample == 32 ) *type = TYPE_INT32;
  else if( is_pcm && format->wBitsPerSample == 24 ) *type = TYPE_INT24;
  else return 1;
  return format->nChannels < 1;
}

/* `frames' frames of the music into the buffer of the device */
static void
fill( BYTE *data, UINT32 frames, const WAVEFORMATEX *format,
      enum sample_type type, short *scratch )
{
  int channels = format->nChannels;
  UINT32 i;
  int c;

  memset( scratch, 0, frames * 2 * sizeof( *scratch ) );
  ayplay_render( scratch, frames, 2, format->nSamplesPerSec );
  /* Sound turned off in the options: the song goes on silently */
  if( !settings_current.sound )
    memset( scratch, 0, frames * 2 * sizeof( *scratch ) );

  for( i = 0; i < frames; i++ ) {
    BYTE *frame = data + i * format->nBlockAlign;
    for( c = 0; c < channels; c++ ) {
      /* Left and right; one channel: both; more: the others silent */
      int sample = channels == 1 ?
                   ( scratch[ i * 2 ] + scratch[ i * 2 + 1 ] ) / 2 :
                   c < 2 ? scratch[ i * 2 + c ] : 0;
      switch( type ) {
      case TYPE_FLOAT:
        ( (float *)frame )[c] = sample / 32768.0f;
        break;
      case TYPE_INT16:
        ( (short *)frame )[c] = sample;
        break;
      case TYPE_INT32:
        ( (INT32 *)frame )[c] = (INT32)sample << 16;
        break;
      case TYPE_INT24:
        frame[ c * 3 ] = 0;
        frame[ c * 3 + 1 ] = sample & 0xff;
        frame[ c * 3 + 2 ] = ( sample >> 8 ) & 0xff;
        break;
      }
    }
  }
}

/* Play on the default device until asked to stop (0) or until the device
   changes or fails (1) */
static int
play( IMMDeviceEnumerator *enumerator )
{
  IMMDevice *device = NULL;
  IAudioClient *client = NULL;
  IAudioRenderClient *render = NULL;
  WAVEFORMATEX *format = NULL;
  HANDLE event = NULL, task = NULL;
  DWORD task_index = 0;
  enum sample_type type;
  UINT32 capacity, padding;
  short *scratch = NULL;
  LPWSTR id = NULL;
  BYTE *data;
  int result = 1;

  InterlockedExchange( &device_changed, 0 );
  device_id[0] = 0;

  if( FAILED( IMMDeviceEnumerator_GetDefaultAudioEndpoint( enumerator,
                eRender, eConsole, &device ) ) ) goto done;
  if( SUCCEEDED( IMMDevice_GetId( device, &id ) ) && id ) {
    wcsncpy( device_id, id, ARRAYSIZE( device_id ) - 1 );
    device_id[ ARRAYSIZE( device_id ) - 1 ] = 0;
    CoTaskMemFree( id );
  }
  if( FAILED( IMMDevice_Activate( device, &IID_IAudioClient,
                CLSCTX_INPROC_SERVER, NULL, (void **)&client ) ) ) goto done;
  if( FAILED( IAudioClient_GetMixFormat( client, &format ) ) ||
      sample_type( format, &type ) ) goto done;
  if( FAILED( IAudioClient_Initialize( client, AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK, BUFFER_DURATION, 0,
                format, NULL ) ) ) goto done;
  event = CreateEvent( NULL, FALSE, FALSE, NULL );
  if( !event || FAILED( IAudioClient_SetEventHandle( client, event ) ) ||
      FAILED( IAudioClient_GetBufferSize( client, &capacity ) ) ||
      FAILED( IAudioClient_GetService( client, &IID_IAudioRenderClient,
                (void **)&render ) ) ) goto done;
  scratch = malloc( capacity * 2 * sizeof( *scratch ) );
  if( !scratch ) goto done;

  /* The buffer full before starting */
  if( FAILED( IAudioRenderClient_GetBuffer( render, capacity, &data ) ) )
    goto done;
  fill( data, capacity, format, type, scratch );
  IAudioRenderClient_ReleaseBuffer( render, capacity, 0 );

  task = AvSetMmThreadCharacteristicsW( L"Audio", &task_index );
  if( FAILED( IAudioClient_Start( client ) ) ) goto done;

  for( ;; ) {
    HANDLE events[ 2 ] = { stop_event, event };
    DWORD status = WaitForMultipleObjects( 2, events, FALSE, 200 );
    UINT32 frames;

    if( status == WAIT_OBJECT_0 ) { result = 0; break; }
    if( InterlockedCompareExchange( &device_changed, 0, 0 ) ) break;
    if( status != WAIT_OBJECT_0 + 1 && status != WAIT_TIMEOUT ) break;

    if( FAILED( IAudioClient_GetCurrentPadding( client, &padding ) ) ) break;
    frames = capacity - padding;
    if( !frames ) continue;
    if( FAILED( IAudioRenderClient_GetBuffer( render, frames, &data ) ) )
      break;
    fill( data, frames, format, type, scratch );
    if( FAILED( IAudioRenderClient_ReleaseBuffer( render, frames, 0 ) ) )
      break;
  }
  IAudioClient_Stop( client );

done:
  if( task ) AvRevertMmThreadCharacteristics( task );
  free( scratch );
  if( render ) IAudioRenderClient_Release( render );
  if( event ) CloseHandle( event );
  if( format ) CoTaskMemFree( format );
  if( client ) IAudioClient_Release( client );
  if( device ) IMMDevice_Release( device );
  return result;
}

static DWORD WINAPI
run( void *unused )
{
  IMMDeviceEnumerator *enumerator = NULL;
  HRESULT com = CoInitializeEx( NULL, COINIT_MULTITHREADED );
  int watching = 0;

  (void)unused;
  if( SUCCEEDED( CoCreateInstance( &CLSID_MMDeviceEnumerator, NULL,
                   CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator,
                   (void **)&enumerator ) ) ) {
    watching = SUCCEEDED(
      IMMDeviceEnumerator_RegisterEndpointNotificationCallback( enumerator,
                                                                &watch ) );
    /* Device changed or failed: open the stream again on the default
       device, a little later if it cannot be done now */
    while( play( enumerator ) ) {
      if( WaitForSingleObject( stop_event, RETRY_MS / 5 ) == WAIT_OBJECT_0 )
        break;
      if( !InterlockedCompareExchange( &device_changed, 0, 0 ) &&
          WaitForSingleObject( stop_event, RETRY_MS ) == WAIT_OBJECT_0 )
        break;
    }
    if( watching )
      IMMDeviceEnumerator_UnregisterEndpointNotificationCallback( enumerator,
                                                                  &watch );
    IMMDeviceEnumerator_Release( enumerator );
  }
  if( SUCCEEDED( com ) ) CoUninitialize();
  return 0;
}

int
ayplay_output_start( void )
{
  if( thread ) return 0;
  if( !stop_event ) stop_event = CreateEvent( NULL, TRUE, FALSE, NULL );
  if( !stop_event ) return 1;
  ResetEvent( stop_event );
  thread = CreateThread( NULL, 0, run, NULL, 0, NULL );
  return thread ? 0 : 1;
}

void
ayplay_output_stop( void )
{
  if( !thread ) return;
  SetEvent( stop_event );
  WaitForSingleObject( thread, INFINITE );
  CloseHandle( thread );
  thread = NULL;
}

int
ayplay_output_independent( void )
{
  return 1;
}

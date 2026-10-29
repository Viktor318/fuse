/* wasapi_native.c: STA-owned shared event-driven WASAPI worker
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/
#include "config.h"
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "wasapi_native.h"

struct wasapi_native;

/* Endpoint notifications arrive on a system thread; they only raise a flag
   which the producer turns into a restart on the new default endpoint. */
struct device_watch {
  IMMNotificationClient iface; /* Must be first */
  struct wasapi_native *native;
};

struct wasapi_native {
  HANDLE thread, stop, ready, data, audio;
  struct wasapi_geometry geometry;
  struct wasapi_service *service;
  unsigned int prefill;
  void (*notify)( void * );
  void *observer;
  _Atomic int32_t error;
  _Atomic int started, attached, device_changed;
  const char *operation; /* Written before sticky error; read only after error. */
  /* Worker-only COM state. Never passed to the producer apartment. */
  IMMDeviceEnumerator *enumerator;
  IMMDevice *device;
  IAudioClient *client;
  IAudioRenderClient *render;
  WAVEFORMATEX *mix;
  HANDLE mmcss;
  int initialized, watching;
  struct device_watch watch;
  WCHAR device_id[512]; /* Immutable once watching starts */
};

static HRESULT
failure( struct wasapi_native *n, const char *operation, HRESULT error )
{
  if( !atomic_load( &n->error ) ) {
    n->operation = operation;
    atomic_store( &n->error, error );
    if( atomic_load( &n->attached ) && n->notify ) n->notify( n->observer );
  }
  return error;
}

/* STA worker owns first audio-client access, including Windows 8. Dispatch
   messages while waiting instead of blocking an STA on ordinary handle waits.
   Stop is first in the handle array and is rechecked before every packet. */
static DWORD
wait_events( struct wasapi_native *n, const HANDLE *events, DWORD count,
             DWORD milliseconds )
{
  ULONGLONG end = GetTickCount64() + milliseconds;
  for( ;; ) {
    MSG message;
    ULONGLONG now = GetTickCount64();
    DWORD result;
    if( WaitForSingleObject( n->stop, 0 ) == WAIT_OBJECT_0 ) return WAIT_OBJECT_0;
    result = MsgWaitForMultipleObjectsEx( count, events,
                 now < end ? (DWORD)( end - now ) : 0, QS_ALLINPUT,
                 MWMO_INPUTAVAILABLE );
    if( result != WAIT_OBJECT_0 + count ) return result;
    while( PeekMessage( &message, NULL, 0, 0, PM_REMOVE ) ) {
      if( message.message == WM_QUIT ) return WAIT_OBJECT_0;
      TranslateMessage( &message );
      DispatchMessage( &message );
      if( WaitForSingleObject( n->stop, 0 ) == WAIT_OBJECT_0 ) return WAIT_OBJECT_0;
    }
    if( GetTickCount64() >= end ) return WAIT_TIMEOUT;
  }
}

/* Canonical WAVEFORMATEXTENSIBLE subformats; KS declarations are not
   necessarily instantiated by initguid.h in MinGW's C headers. */
static const GUID subtype_pcm = {
  WAVE_FORMAT_PCM, 0, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }
};
static const GUID subtype_float = {
  WAVE_FORMAT_IEEE_FLOAT, 0, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }
};

static HRESULT
format_read( const WAVEFORMATEX *mix, struct wasapi_format *format )
{
  memset( format, 0, sizeof( *format ) );
  format->rate = mix->nSamplesPerSec;
  format->channels = mix->nChannels;
  format->container_bits = format->valid_bits = mix->wBitsPerSample;
  format->block_align = mix->nBlockAlign;
  if( mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE ) {
    const WAVEFORMATEXTENSIBLE *ext = (const WAVEFORMATEXTENSIBLE *)mix;
    if( mix->cbSize < sizeof( *ext ) - sizeof( *mix ) ) return E_INVALIDARG;
    format->valid_bits = ext->Samples.wValidBitsPerSample;
    format->mask = ext->dwChannelMask;
    if( IsEqualGUID( &ext->SubFormat, &subtype_float ) )
      format->encoding = WASAPI_FLOAT;
    else if( IsEqualGUID( &ext->SubFormat, &subtype_pcm ) )
      format->encoding = WASAPI_INTEGER;
    else return AUDCLNT_E_UNSUPPORTED_FORMAT;
  } else if( mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT )
    format->encoding = WASAPI_FLOAT;
  else if( mix->wFormatTag == WAVE_FORMAT_PCM )
    format->encoding = WASAPI_INTEGER;
  else return AUDCLNT_E_UNSUPPORTED_FORMAT;
  if( wasapi_format_validate( format ) ||
      mix->nAvgBytesPerSec != (uint64_t)mix->nSamplesPerSec * mix->nBlockAlign )
    return AUDCLNT_E_UNSUPPORTED_FORMAT;
  return S_OK;
}

static void
device_changed( struct wasapi_native *n )
{
  if( !atomic_exchange( &n->device_changed, 1 ) &&
      atomic_load( &n->attached ) && n->notify ) n->notify( n->observer );
}

static int
is_current( struct wasapi_native *n, LPCWSTR id )
{
  return id && n->device_id[0] && !wcscmp( id, n->device_id );
}

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
/* The object lives inside the native lifetime and is unregistered before
   that is freed, so reference counting is not needed. */
static ULONG STDMETHODCALLTYPE
watch_addref( IMMNotificationClient *self ) { (void)self; return 1; }
static ULONG STDMETHODCALLTYPE
watch_release( IMMNotificationClient *self ) { (void)self; return 1; }

static HRESULT STDMETHODCALLTYPE
watch_state( IMMNotificationClient *self, LPCWSTR id, DWORD state )
{
  struct wasapi_native *n = ( (struct device_watch *)self )->native;
  if( state != DEVICE_STATE_ACTIVE && is_current( n, id ) ) device_changed( n );
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
  struct wasapi_native *n = ( (struct device_watch *)self )->native;
  if( is_current( n, id ) ) device_changed( n );
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
watch_default( IMMNotificationClient *self, EDataFlow flow, ERole role,
               LPCWSTR id )
{
  struct wasapi_native *n = ( (struct device_watch *)self )->native;
  /* Follow the console default output, which is the one opened. With no
     output left there is nothing to move to; wait for the next change. */
  if( flow == eRender && role == eConsole && id && !is_current( n, id ) )
    device_changed( n );
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

/* Failing to watch is not fatal: output then stays on the opened endpoint */
static void
watch_start( struct wasapi_native *n )
{
  LPWSTR id = NULL;
  if( SUCCEEDED( IMMDevice_GetId( n->device, &id ) ) && id ) {
    wcsncpy( n->device_id, id, ARRAYSIZE( n->device_id ) - 1 );
    n->device_id[ ARRAYSIZE( n->device_id ) - 1 ] = 0;
    CoTaskMemFree( id );
  }
  n->watch.iface.lpVtbl = &watch_vtbl;
  n->watch.native = n;
  n->watching = SUCCEEDED(
    IMMDeviceEnumerator_RegisterEndpointNotificationCallback( n->enumerator,
                                                              &n->watch.iface ) );
}

static HRESULT
activate( struct wasapi_native *n )
{
  return IMMDevice_Activate( n->device, &IID_IAudioClient,
                             CLSCTX_INPROC_SERVER, NULL, (void **)&n->client );
}

static int
compatibility_error( HRESULT error )
{
  return error == E_NOINTERFACE || error == E_NOTIMPL ||
         error == AUDCLNT_E_INVALID_DEVICE_PERIOD ||
         error == AUDCLNT_E_ENGINE_PERIODICITY_LOCKED ||
         error == AUDCLNT_E_ENGINE_FORMAT_LOCKED;
}

static HRESULT
negotiate( struct wasapi_native *n )
{
  IAudioClient3 *client3 = NULL;
  HRESULT result;
  REFERENCE_TIME period;
  struct wasapi_geometry *g = &n->geometry;
#define NATIVE_CHECK( op, call ) do { result = (call); \
  if( FAILED( result ) ) return failure( n, op, result ); } while( 0 )
  NATIVE_CHECK( "endpoint enumerator", CoCreateInstance( &CLSID_MMDeviceEnumerator,
    NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator, (void **)&n->enumerator ) );
  NATIVE_CHECK( "default endpoint", IMMDeviceEnumerator_GetDefaultAudioEndpoint(
    n->enumerator, eRender, eConsole, &n->device ) );
  watch_start( n );
  NATIVE_CHECK( "activate client", activate( n ) );
  NATIVE_CHECK( "mix format", IAudioClient_GetMixFormat( n->client, &n->mix ) );
  NATIVE_CHECK( "validate mix format", format_read( n->mix, &g->format ) );
  result = IAudioClient_QueryInterface( n->client, &IID_IAudioClient3,
                                        (void **)&client3 );
  if( SUCCEEDED( result ) ) {
    result = IAudioClient3_GetSharedModeEnginePeriod( client3, n->mix,
                &g->default_period, &g->fundamental, &g->minimum, &g->maximum );
    if( SUCCEEDED( result ) ) {
      if( wasapi_period_select( g->format.rate, g->fundamental, g->minimum,
                                g->maximum, &g->period ) ) result = E_INVALIDARG;
      else result = IAudioClient3_InitializeSharedAudioStream( client3,
                   AUDCLNT_STREAMFLAGS_EVENTCALLBACK, g->period, n->mix, NULL );
    }
    IAudioClient3_Release( client3 );
    if( SUCCEEDED( result ) ) g->client3 = 1;
  }
  if( !g->client3 ) {
    g->low_period_error = result;
    if( !compatibility_error( result ) ) return failure( n, "client3 negotiation", result );
    /* Never reuse a client on which low-period initialization was attempted. */
    IAudioClient_Release( n->client ); n->client = NULL;
    NATIVE_CHECK( "activate compatibility client", activate( n ) );
    NATIVE_CHECK( "default device period", IAudioClient_GetDevicePeriod(
                   n->client, &period, NULL ) );
    if( period <= 0 || (uint64_t)period > UINT64_MAX / g->format.rate ||
        (uint64_t)period * g->format.rate / 10000000 > UINT_MAX )
      return failure( n, "default period geometry", E_INVALIDARG );
    g->period = (uint64_t)period * g->format.rate / 10000000;
    if( !g->period ) return failure( n, "default period geometry", E_INVALIDARG );
    NATIVE_CHECK( "initialize default-period WASAPI", IAudioClient_Initialize(
       n->client, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
       0, 0, n->mix, NULL ) );
  }
  n->initialized = 1;
  NATIVE_CHECK( "native capacity", IAudioClient_GetBufferSize( n->client, &g->capacity ) );
  if( !g->capacity || g->capacity > 1048576 )
    return failure( n, "native capacity geometry", E_INVALIDARG );
  NATIVE_CHECK( "register audio event", IAudioClient_SetEventHandle( n->client, n->audio ) );
  NATIVE_CHECK( "render service", IAudioClient_GetService( n->client,
                   &IID_IAudioRenderClient, (void **)&n->render ) );
  NATIVE_CHECK( "stream latency", IAudioClient_GetStreamLatency( n->client, &g->latency ) );
  if( g->latency < 0 ) return failure( n, "stream latency geometry", E_INVALIDARG );
#undef NATIVE_CHECK
  return S_OK;
}

static int32_t
native_padding( void *opaque, unsigned int *frames )
{
  struct wasapi_native *n = opaque;
  HRESULT error = IAudioClient_GetCurrentPadding( n->client, frames );
  return FAILED( error ) ? failure( n, "GetCurrentPadding", error ) : error;
}
static int32_t
native_acquire( void *opaque, unsigned int frames, unsigned char **data )
{
  struct wasapi_native *n = opaque;
  HRESULT error = IAudioRenderClient_GetBuffer( n->render, frames, data );
  return FAILED( error ) ? failure( n, "GetBuffer", error ) : error;
}
static int32_t
native_release( void *opaque, unsigned int frames, int silent )
{
  struct wasapi_native *n = opaque;
  HRESULT error = IAudioRenderClient_ReleaseBuffer( n->render, frames,
                            silent ? AUDCLNT_BUFFERFLAGS_SILENT : 0 );
  return FAILED( error ) ? failure( n, "ReleaseBuffer", error ) : error;
}

static int
service_wait( struct wasapi_native *n, const HANDLE *events )
{
  DWORD status = wait_events( n, events, 3, 100 );
  if( status == WAIT_OBJECT_0 ) {
    if( WaitForSingleObject( n->stop, 0 ) != WAIT_OBJECT_0 )
      failure( n, "unexpected consumer exit", E_ABORT );
    return 0;
  }
  if( status == WAIT_FAILED ) {
    failure( n, "worker wait", HRESULT_FROM_WIN32( GetLastError() ) );
    return 0;
  }
  if( status != WAIT_TIMEOUT && status != WAIT_OBJECT_0 + 1 &&
      status != WAIT_OBJECT_0 + 2 ) {
    failure( n, "unexpected wait status", E_FAIL );
    return 0;
  }
  return 1;
}

static int
service_ready( struct wasapi_native *n, int running )
{
  int used;
  /* The atomic handshake publishes service/observer geometry; a watchdog
     return must not race producer initialization or rely on an event token. */
  if( !atomic_load( &n->attached ) ) return 0;
  if( running ) return 1;
  used = pcm_fifo_consumer_used( n->service->fifo,
                                 n->service->channels * sizeof( int16_t ) );
  if( used < 0 ) {
    failure( n, "prefill FIFO", E_FAIL );
    return -1;
  }
  return (unsigned int)used >= n->prefill;
}

static HRESULT
stream_start( struct wasapi_native *n )
{
  DWORD task = 0;
  HRESULT result;
  n->mmcss = AvSetMmThreadCharacteristicsW( L"Audio", &task );
  n->geometry.mmcss = n->mmcss != NULL;
  if( !ResetEvent( n->audio ) )
    return failure( n, "reset start event", HRESULT_FROM_WIN32( GetLastError() ) );
  result = IAudioClient_Start( n->client );
  if( FAILED( result ) ) return failure( n, "start stream", result );
  atomic_store( &n->started, 1 );
  if( n->notify ) n->notify( n->observer );
  return result;
}

static void
service_loop( struct wasapi_native *n )
{
  const struct wasapi_packet_ops ops = { native_padding, native_acquire, native_release };
  HANDLE events[3] = { n->stop, n->data, n->audio };
  while( service_wait( n, events ) ) {
    int running = atomic_load( &n->started );
    int ready = service_ready( n, running );
    HRESULT result;
    if( ready < 0 ) break;
    if( !ready ) continue;
    if( WaitForSingleObject( n->stop, 0 ) == WAIT_OBJECT_0 ) break;
    result = wasapi_service_packet( n->service, &ops, n, running );
    if( FAILED( result ) ) { failure( n, "render packet", result ); break; }
    if( !running && FAILED( stream_start( n ) ) ) break;
  }
}

static void
stream_release( struct wasapi_native *n )
{
  HRESULT result;
  if( n->initialized ) {
    result = IAudioClient_Stop( n->client );
    if( FAILED( result ) ) failure( n, "stop stream", result );
  }
  if( n->watching )
    IMMDeviceEnumerator_UnregisterEndpointNotificationCallback( n->enumerator,
                                                                &n->watch.iface );
  n->watching = 0;
  if( n->render ) IAudioRenderClient_Release( n->render );
  if( n->client ) IAudioClient_Release( n->client );
  if( n->device ) IMMDevice_Release( n->device );
  if( n->enumerator ) IMMDeviceEnumerator_Release( n->enumerator );
  if( n->mix ) CoTaskMemFree( n->mix );
  if( n->mmcss && !AvRevertMmThreadCharacteristics( n->mmcss ) )
    failure( n, "revert MMCSS", HRESULT_FROM_WIN32( GetLastError() ) );
}

static DWORD
worker_run( struct wasapi_native *n )
{
  HRESULT result = CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );
  int com_initialized = SUCCEEDED( result );
  if( FAILED( result ) ) failure( n, "initialize STA", result );
  else result = negotiate( n );
  if( !SetEvent( n->ready ) )
    failure( n, "negotiation wake", HRESULT_FROM_WIN32( GetLastError() ) );
  if( SUCCEEDED( result ) && !atomic_load( &n->error ) ) service_loop( n );
  stream_release( n );
  if( com_initialized ) CoUninitialize();
  return 0;
}

/* Keep the Windows calling convention at the entry boundary. The ordinary
   helpers above also make the worker's control flow visible to C metrics. */
static DWORD WINAPI
worker( void *opaque )
{
  return worker_run( opaque );
}

static int32_t
open_failed( struct wasapi_native *n, struct wasapi_native **output, HRESULT error )
{
  HRESULT joined = wasapi_native_close( n, NULL );
  if( FAILED( joined ) ) *output = n; /* Retain any unjoined partial lifetime. */
  return error;
}

static HRESULT
state_init( struct wasapi_native *n )
{
  atomic_init( &n->error, 0 ); atomic_init( &n->started, 0 );
  atomic_init( &n->attached, 0 ); atomic_init( &n->device_changed, 0 );
  if( !atomic_is_lock_free( &n->error ) || !atomic_is_lock_free( &n->started ) ||
      !atomic_is_lock_free( &n->attached ) ||
      !atomic_is_lock_free( &n->device_changed ) ) return E_NOTIMPL;
  return S_OK;
}

static HRESULT
events_create( struct wasapi_native *n, const char **operation )
{
  HRESULT result;
  n->stop = CreateEvent( NULL, TRUE, FALSE, NULL );
  if( n->stop ) n->ready = CreateEvent( NULL, FALSE, FALSE, NULL );
  if( n->ready ) n->data = CreateEvent( NULL, FALSE, FALSE, NULL );
  if( n->data ) n->audio = CreateEvent( NULL, FALSE, FALSE, NULL );
  if( !n->audio ) {
    /* Stop at the first failed acquisition, preserving its GetLastError. */
    result = HRESULT_FROM_WIN32( GetLastError() );
    if( operation ) *operation = !n->stop ? "create stop event" :
                                !n->ready ? "create ready event" :
                                !n->data ? "create data event" : "create audio event";
    return result;
  }
  return S_OK;
}

static HRESULT
negotiation_wait( struct wasapi_native *n, const char **operation )
{
  HANDLE events[2] = { n->ready, n->thread };
  DWORD status = WaitForMultipleObjects( 2, events, FALSE, 10000 );
  HRESULT result = atomic_load( &n->error );
  if( result < 0 && operation ) *operation = n->operation;
  if( status != WAIT_OBJECT_0 && !result ) {
    result = E_FAIL;
    if( operation ) *operation = "negotiation wait";
  }
  return result;
}

int32_t
wasapi_native_open( struct wasapi_native **output, struct wasapi_geometry *geometry,
                    const char **operation )
{
  struct wasapi_native *n = calloc( 1, sizeof( *n ) );
  HRESULT result;
  *output = NULL;
  if( operation ) *operation = NULL;
  if( !n ) {
    if( operation ) *operation = "allocate lifetime";
    return E_OUTOFMEMORY;
  }
  result = state_init( n );
  if( FAILED( result ) ) {
    if( operation ) *operation = "initialize lock-free state";
    free( n ); return result;
  }
  result = events_create( n, operation );
  if( FAILED( result ) ) return open_failed( n, output, result );
  n->thread = CreateThread( NULL, 0, worker, n, 0, NULL );
  if( !n->thread ) {
    if( operation ) *operation = "create consumer";
    result = HRESULT_FROM_WIN32( GetLastError() );
    return open_failed( n, output, result );
  }
  result = negotiation_wait( n, operation );
  if( FAILED( result ) ) return open_failed( n, output, result );
  *geometry = n->geometry;
  *output = n;
  return S_OK;
}

void
wasapi_native_set_notify( struct wasapi_native *n,
                          void (*notify)( void * ), void *observer )
{
  /* Only before attach; worker has no reason to use these while stopped. */
  n->notify = notify; n->observer = observer;
}

int32_t
wasapi_native_attach( struct wasapi_native *n, struct wasapi_service *service,
                      unsigned int prefill )
{
  int capacity;
  HRESULT error = wasapi_native_error( n );
  if( FAILED( error ) ) return error;
  if( !service || n->service || prefill < n->geometry.capacity ||
      service->capacity != n->geometry.capacity ||
      memcmp( &service->format, &n->geometry.format, sizeof( service->format ) ) )
    return E_INVALIDARG;
  capacity = pcm_fifo_capacity( service->fifo, service->channels * sizeof( int16_t ) );
  if( capacity < 0 || prefill > (unsigned int)capacity ) return E_INVALIDARG;
  n->prefill = prefill;
  n->service = service;
  atomic_store( &n->attached, 1 );
  error = wasapi_native_data( n );
  return FAILED( error ) ? error : wasapi_native_error( n );
}

int32_t
wasapi_native_data( struct wasapi_native *n )
{
  return SetEvent( n->data ) ? S_OK : HRESULT_FROM_WIN32( GetLastError() );
}
int wasapi_native_started( struct wasapi_native *n ) { return atomic_load( &n->started ); }
int
wasapi_native_device_changed( struct wasapi_native *n )
{
  /* An invalidated endpoint (unplugged, disabled, format change) is also a
     reason to reopen the current default rather than to give up */
  return atomic_load( &n->device_changed ) ||
         atomic_load( &n->error ) == AUDCLNT_E_DEVICE_INVALIDATED;
}
int32_t
wasapi_native_error( struct wasapi_native *n )
{
  int32_t error = atomic_load( &n->error );
  if( !error && WaitForSingleObject( n->thread, 0 ) == WAIT_OBJECT_0 &&
      WaitForSingleObject( n->stop, 0 ) != WAIT_OBJECT_0 ) return E_ABORT;
  return error;
}
const char *wasapi_native_operation( struct wasapi_native *n ) { return n->operation; }

int32_t
wasapi_native_close( struct wasapi_native *n, int32_t *stream_error )
{
  HRESULT result = S_OK;
  if( !n ) return result;
  if( n->thread ) {
    if( !SetEvent( n->stop ) ) return HRESULT_FROM_WIN32( GetLastError() );
    /* A native call can hang. Timeout is not quiescence: retain storage on a
       failed join, and never free below a worker which might still execute. */
    if( WaitForSingleObject( n->thread, INFINITE ) != WAIT_OBJECT_0 ) return E_FAIL;
    if( stream_error ) *stream_error = atomic_load( &n->error );
    CloseHandle( n->thread );
  }
  if( n->audio ) CloseHandle( n->audio );
  if( n->data ) CloseHandle( n->data );
  if( n->ready ) CloseHandle( n->ready );
  if( n->stop ) CloseHandle( n->stop );
  free( n );
  return result;
}

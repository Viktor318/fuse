/* wasapitoolchaintest.c: endpoint-independent WASAPI SDK/link gate
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/
#include "config.h"
#define COBJMACROS
#include <windows.h>
/* MinGW's uuid import library need not contain Core Audio GUIDs. Define
   SDK GUIDs in exactly one translation unit, with no newer runtime import. */
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <stdio.h>

/* Compile every required C wrapper and link GUIDs without needing an endpoint.
   Never execute this function: its purpose is SDK coverage, not playback. */
HRESULT
wasapi_sdk_probe( IMMDeviceEnumerator *enumerator, IMMDevice *device,
                  IAudioClient *client, IAudioClient3 *client3,
                  IAudioRenderClient *render, HANDLE event )
{
  WAVEFORMATEX *format = NULL;
  UINT32 capacity, fundamental, minimum, maximum, period;
  BYTE *packet;
  DWORD task = 0;
  HANDLE mmcss;
  HRESULT result;
  result = CoCreateInstance( &CLSID_MMDeviceEnumerator, NULL,
                            CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator,
                            (void **)&enumerator );
  result = IMMDeviceEnumerator_GetDefaultAudioEndpoint( enumerator, eRender,
                                                       eConsole, &device );
  result = IMMDevice_Activate( device, &IID_IAudioClient, CLSCTX_INPROC_SERVER,
                              NULL, (void **)&client );
  result = IAudioClient_QueryInterface( client, &IID_IAudioClient3,
                                       (void **)&client3 );
  result = IAudioClient_GetMixFormat( client, &format );
  result = IAudioClient3_GetSharedModeEnginePeriod( client3, format, &period,
                                                   &fundamental, &minimum,
                                                   &maximum );
  result = IAudioClient3_InitializeSharedAudioStream( client3,
                   AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, format, NULL );
  result = IAudioClient_Initialize( client, AUDCLNT_SHAREMODE_SHARED,
                   AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, format, NULL );
  result = IAudioClient_GetBufferSize( client, &capacity );
  result = IAudioClient_SetEventHandle( client, event );
  result = IAudioClient_GetService( client, &IID_IAudioRenderClient,
                                  (void **)&render );
  result = IAudioClient_GetCurrentPadding( client, &capacity );
  result = IAudioRenderClient_GetBuffer( render, capacity, &packet );
  result = IAudioRenderClient_ReleaseBuffer( render, capacity,
                                            AUDCLNT_BUFFERFLAGS_SILENT );
  result = IAudioClient_Start( client );
  result = IAudioClient_Stop( client );
  IAudioRenderClient_Release( render );
  IAudioClient3_Release( client3 );
  IAudioClient_Release( client );
  IMMDevice_Release( device );
  IMMDeviceEnumerator_Release( enumerator );
  CoTaskMemFree( format );
  mmcss = AvSetMmThreadCharacteristicsW( L"Audio", &task );
  AvRevertMmThreadCharacteristics( mmcss );
  return result;
}

#ifdef main
/* SDL headers redefine main on Windows, but this test needs a normal entry point. */
#undef main
#endif

int
main( void )
{
  HRESULT result = CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );
  if( FAILED( result ) ) return 1;
  CoUninitialize();
  puts( "WASAPI C interfaces/GUIDs linked; STA initialized (no endpoint accessed)" );
  return 0;
}

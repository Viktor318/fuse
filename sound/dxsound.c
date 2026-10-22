/* dxsound.c: DirectX sound I/O
   Copyright (c) 2003-2008 Marek Januszewski, Philip Kendall

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

#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <string.h>

#include "settings.h"
#include "sound.h"
#include "ui/ui.h"

/* same as for SDL Sound */
#define MAX_AUDIO_BUFFER ( 8192 * 5 )

static LPDIRECTSOUND lpDS; /* DirectSound object */
static LPDIRECTSOUNDBUFFER lpDSBuffer; /* sound buffer */

static DWORD nextpos; /* next position in circular buffer */

/* How far ahead of the play cursor we have written. The circular buffer is
   played in a loop, so if playing catches up with writing, whatever is in
   the buffer gets played again. To avoid that being old audio (heard as a
   delayed echo of earlier sounds), we count bytes written and played, resync
   after an underrun, and fill all the rest of the buffer (from the end of
   the written data round to the play cursor) with the last sample, which
   plays as silence without a click. Then even a stall longer than the
   whole buffer finds no old audio to play again */
static unsigned long long written_total, played_total;
static DWORD last_playcursor;
static DWORD last_frame_time;
static DWORD block_align;	/* bytes per sample frame */
static DWORD buffer_ms;		/* buffer length in milliseconds */
static libspectrum_signed_word last_sample[2];
static int channels;

int
sound_lowlevel_init( const char *device, int *freqptr, int *stereoptr )
{

  WAVEFORMATEX pcmwf; /* waveformat struct */ 
  DSBUFFERDESC dsbd; /* buffer description */

  /* Initialize COM */
  CoInitialize(NULL);

  /* create DirectSound object */
  if( CoCreateInstance( &CLSID_DirectSound, NULL, CLSCTX_INPROC_SERVER,
			&IID_IDirectSound, (void**)&lpDS ) != DS_OK ) {
    settings_current.sound = 0;
    ui_error( UI_ERROR_ERROR, "Couldn't create DirectSound object.");
    CoUninitialize();
    return 1;
  }
  
  /* initialize it */    
  if( IDirectSound_Initialize( lpDS, NULL ) != DS_OK ) {
    settings_current.sound = 0;
    ui_error( UI_ERROR_ERROR, "Couldn't initialize DirectSound." );
    CoUninitialize();
    return 1;
  }
  
  /* set normal cooperative level */
  if( IDirectSound_SetCooperativeLevel( lpDS, GetDesktopWindow(),
					DSSCL_NORMAL ) != DS_OK ) {
    settings_current.sound = 0;
    ui_error( UI_ERROR_ERROR, "Couldn't set DirectSound cooperation level." );
    IDirectSound_Release( lpDS );
    CoUninitialize();
    return 1;
  }
  
  /* create wave format description */
  memset( &pcmwf, 0, sizeof( WAVEFORMATEX ) );

  pcmwf.cbSize = 0;
  pcmwf.wBitsPerSample = 16;
  pcmwf.nChannels = *stereoptr ? 2 : 1;
  pcmwf.nBlockAlign = pcmwf.nChannels * 2;
  pcmwf.nSamplesPerSec = *freqptr;

  pcmwf.nAvgBytesPerSec = pcmwf.nSamplesPerSec * pcmwf.nBlockAlign;

  pcmwf.wFormatTag = WAVE_FORMAT_PCM;

  /* create sound buffer description */
  memset( &dsbd, 0, sizeof( DSBUFFERDESC ) );
  dsbd.dwBufferBytes = MAX_AUDIO_BUFFER;

  dsbd.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLVOLUME | 
                 DSBCAPS_CTRLFREQUENCY | DSBCAPS_STATIC | DSBCAPS_LOCSOFTWARE;

  dsbd.dwSize = sizeof( DSBUFFERDESC );
  dsbd.lpwfxFormat = &pcmwf;
  
  /* attempt to create the buffer */  
  if( IDirectSound_CreateSoundBuffer( lpDS, &dsbd, &lpDSBuffer, NULL )
      != DS_OK ) {
    settings_current.sound = 0;
    ui_error( UI_ERROR_ERROR, "Couldn't create DirectSound buffer." );
    IDirectSound_Release( lpDS );
    CoUninitialize();
    return 1;
  }
  
  /* play buffer */
  if( IDirectSoundBuffer_Play( lpDSBuffer, 0, 0, DSBPLAY_LOOPING ) != DS_OK ) {
    settings_current.sound = 0;
    ui_error( UI_ERROR_ERROR, "Couldn't play sound." );
    IDirectSoundBuffer_Release( lpDSBuffer );
    IDirectSound_Release( lpDS );
    CoUninitialize();
    return 1;
  }

  nextpos = 0;
  written_total = played_total = 0;
  last_playcursor = 0;
  last_frame_time = GetTickCount();
  block_align = pcmwf.nBlockAlign;
  buffer_ms = MAX_AUDIO_BUFFER * 1000 / pcmwf.nAvgBytesPerSec;
  channels = pcmwf.nChannels;
  last_sample[0] = last_sample[1] = 0;

  return 0;      
}

void
sound_lowlevel_end( void )
{
  if( IDirectSoundBuffer_Stop( lpDSBuffer ) != DS_OK ) {
    settings_current.sound = 0;
    ui_error( UI_ERROR_ERROR, "Couldn't stop sound." );
  }

  IDirectSoundBuffer_Release( lpDSBuffer );
  IDirectSound_Release( lpDS );
  CoUninitialize();
}

/* Update the count of bytes played from the play cursor */
static void
update_played( DWORD *playcursor, DWORD *writecursor )
{
  IDirectSoundBuffer_GetCurrentPosition( lpDSBuffer, playcursor, writecursor );

  played_total += ( *playcursor + MAX_AUDIO_BUFFER - last_playcursor ) %
                  MAX_AUDIO_BUFFER;
  last_playcursor = *playcursor;
}

/* Write `bytes' bytes at `position': from `data', or if NULL, the last
   sample repeated. Returns 0 on success */
static int
write_buffer( DWORD position, const libspectrum_signed_word *data,
              DWORD bytes )
{
  HRESULT hres;
  void *part[2];
  DWORD length[2];
  int i;

  hres = IDirectSoundBuffer_Lock( lpDSBuffer, position, bytes,
                                  &part[0], &length[0], &part[1], &length[1],
                                  0 );
  if( hres != DS_OK ) return 1;

  for( i = 0; i < 2; i++ ) {
    if( !part[i] ) continue;

    if( data ) {
      memcpy( part[i], data, length[i] );
      data += length[i] / sizeof( *data );
    } else {
      libspectrum_signed_word *out = part[i];
      DWORD j, samples = length[i] / sizeof( *out );
      for( j = 0; j < samples; j++ ) out[j] = last_sample[ j % channels ];
    }
  }

  IDirectSoundBuffer_Unlock( lpDSBuffer, part[0], length[0], part[1],
                             length[1] );

  return 0;
}

/* Copying data to the buffer */
void
sound_lowlevel_frame( libspectrum_signed_word *data, int len )
{
  DWORD playcursor, writecursor, now, bytes, fill;
  long long ahead;

  bytes = len * sizeof( *data );

  update_played( &playcursor, &writecursor );

  /* Underrun: playing has caught up with writing (or we have not been
     called for so long that it may have gone round the buffer). Continue
     writing just after the write cursor, the first position safe to write */
  now = GetTickCount();
  if( written_total < played_total ||
      now - last_frame_time > buffer_ms * 3 / 4 ) {
    nextpos = ( writecursor + block_align - 1 ) / block_align * block_align;
    if( nextpos >= MAX_AUDIO_BUFFER ) nextpos -= MAX_AUDIO_BUFFER;
    written_total = played_total +
      ( nextpos + MAX_AUDIO_BUFFER - playcursor ) % MAX_AUDIO_BUFFER;
  }

  /* Too far ahead: wait for some of it to be played */
  while( (long long)( written_total - played_total ) >=
         (long long)bytes * 6 ) {
    Sleep( 10 );
    update_played( &playcursor, &writecursor );
  }

  last_frame_time = GetTickCount();

  if( write_buffer( nextpos, data, bytes ) ) return;

  nextpos = ( nextpos + bytes ) % MAX_AUDIO_BUFFER;
  written_total += bytes;

  if( len >= channels ) {
    last_sample[0] = data[ len - channels ];
    last_sample[1] = data[ len - 1 ];
  }

  /* Fill the rest of the buffer with the last sample, so an underrun plays
     silence instead of old audio. Never touch what is still to be played */
  ahead = written_total - played_total;
  fill = ahead + block_align < MAX_AUDIO_BUFFER ?
         ( MAX_AUDIO_BUFFER - ahead - block_align ) / block_align *
           block_align :
         0;
  if( fill ) write_buffer( nextpos, NULL, fill );
}

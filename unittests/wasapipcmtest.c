/* wasapipcmtest.c: deterministic WASAPI conversion/packet fault checks
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/
#include "config.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sound/wasapi_pcm.h"

#define CHECK( x ) do { if( !( x ) ) { \
  fprintf( stderr, "%d: %s\n", __LINE__, #x ); exit( 1 ); } } while( 0 )

static void
formats( void )
{
  struct wasapi_format f = { 48000, 2, 16, 16, 4, 0, WASAPI_INTEGER };
  int16_t input[] = { INT16_MIN, INT16_MAX, -1, 1, 127, 255 };
  unsigned char out[96];
  unsigned char s16[] = { 0, 128, 255, 127, 255, 255, 1, 0, 127, 0, 255, 0 };
  unsigned char s24[] = { 0, 0, 128, 0, 255, 127, 0, 255, 255, 0, 1, 0 };
  unsigned char s32[] = { 0, 0, 0, 128, 0, 0, 255, 127 };
  unsigned char flt[] = { 0, 0, 128, 191, 0, 254, 127, 63 };
  CHECK( !wasapi_convert( &f, 2, 0, input, 3, out ) );
  CHECK( !memcmp( out, s16, sizeof( s16 ) ) );
  f.container_bits = 24; f.valid_bits = 20; f.block_align = 6;
  CHECK( !wasapi_convert( &f, 2, 0, input, 2, out ) );
  CHECK( !memcmp( out, s24, sizeof( s24 ) ) );
  f.container_bits = 32; f.valid_bits = 24; f.block_align = 8;
  CHECK( !wasapi_convert( &f, 2, 0, input, 1, out ) );
  CHECK( !memcmp( out, s32, sizeof( s32 ) ) );
  f.encoding = WASAPI_FLOAT; f.valid_bits = 32;
  CHECK( !wasapi_convert( &f, 2, 0, input, 1, out ) );
  CHECK( !memcmp( out, flt, sizeof( flt ) ) );
  f.encoding = WASAPI_INTEGER; f.container_bits = f.valid_bits = 16;
  f.block_align = 4;
  CHECK( !wasapi_convert( &f, 2, 1, input + 2, 2, out ) );
  CHECK( out[0] == 0 && out[1] == 255 && out[2] == 0 && out[3] == 0 );
  CHECK( out[4] == 0 && out[5] == 0 && out[6] == 0 && out[7] == 0 );
  /* Mono duplicate, stereo average with half ties away from zero. */
  CHECK( !wasapi_convert( &f, 1, 0, input, 1, out ) );
  CHECK( out[1] == 128 && out[3] == 128 );
  f.channels = 1; f.block_align = 2; f.mask = WASAPI_FRONT_CENTER;
  CHECK( !wasapi_convert( &f, 2, 0, input, 1, out ) );
  CHECK( out[0] == 255 && out[1] == 255 );
  /* 7.1 mask: exact front mapping, all remaining speakers zero. */
  f.channels = 8; f.mask = 0x63f; f.block_align = 16;
  memset( out, 0xa5, sizeof( out ) );
  CHECK( !wasapi_convert( &f, 2, 0, input, 1, out ) );
  CHECK( !memcmp( out, s16, 4 ) );
  for( unsigned int i = 4; i < 16; i++ ) CHECK( out[i] == 0 );
  f.mask = 0; CHECK( wasapi_format_validate( &f ) );
  f.mask = 0x63e; CHECK( wasapi_format_validate( &f ) );
  f.mask = 0x8000063f; CHECK( wasapi_format_validate( &f ) );
  f.mask = 0x63f; f.block_align = 15; CHECK( wasapi_format_validate( &f ) );
  f.block_align = 16; f.valid_bits = 15; CHECK( wasapi_format_validate( &f ) );
  f.valid_bits = 16; f.rate = UINT_MAX; CHECK( wasapi_format_validate( &f ) );
}

static void
periods( void )
{
  unsigned int p, bytes;
  CHECK( !wasapi_period_select( 48000, 48, 96, 960, &p ) && p == 240 );
  CHECK( !wasapi_period_select( 48000, 96, 96, 960, &p ) && p == 288 );
  CHECK( !wasapi_period_select( 44100, 1, 1, 1000, &p ) && p == 221 );
  CHECK( !wasapi_period_select( 8000, 100, 100, 500, &p ) && p == 100 );
  CHECK( !wasapi_period_select( 384000, 32, 1, 100, &p ) && p == 96 );
  CHECK( wasapi_period_select( 48000, 0, 1, 1000, &p ) );
  CHECK( wasapi_period_select( 48000, 64, 1, 63, &p ) );
  CHECK( wasapi_period_select( 48000, 64, 100, 99, &p ) );
  CHECK( !wasapi_period_select( UINT_MAX, UINT_MAX, 1, UINT_MAX, &p ) );
  CHECK( p == UINT_MAX );
  CHECK( !wasapi_fifo_bytes( 1921, 480, 481, 4, &bytes ) );
  sfifo_t fifo = { 0 };
  CHECK( !sfifo_init( &fifo, bytes ) );
  CHECK( pcm_fifo_capacity( &fifo, 4 ) >= 1921 + 2 * 480 + 2 );
  sfifo_close( &fifo );
  /* Near a byte-ring rounding boundary, startup+2D alone can fit storage but
     leave too little room to finish stopped prefill with one whole batch. */
  CHECK( !wasapi_fifo_bytes( 1364, 1364, 241, 4, &bytes ) );
  CHECK( !sfifo_init( &fifo, bytes ) );
  CHECK( pcm_fifo_capacity( &fifo, 4 ) >= 2 * 1364 + 1364 + 171 );
  sfifo_close( &fifo );
  CHECK( wasapi_fifo_bytes( UINT_MAX, UINT_MAX, UINT_MAX, 4, &bytes ) );
  CHECK( wasapi_fifo_bytes( 1000, 1000, 1000, 3, &bytes ) );
  /* Exhaustively compare legal periods against scaled-distance oracle. */
  for( unsigned int rate = 8000; rate < 10000; rate += 137 )
    for( unsigned int step = 1; step < 100; step++ ) {
      unsigned int best = 0;
      uint64_t distance = UINT64_MAX;
      for( unsigned int candidate = step; candidate <= 500; candidate += step ) {
        uint64_t scaled = (uint64_t)candidate * 200;
        uint64_t d = scaled > rate ? scaled - rate : rate - scaled;
        if( d <= distance ) { distance = d; best = candidate; }
      }
      CHECK( !wasapi_period_select( rate, step, 1, 500, &p ) && p == best );
    }
}

struct mock {
  unsigned int padding, acquired, released, begin, finish, negative;
  unsigned int requested, delivered, invalid, silent;
  int32_t padding_error, acquire_error, release_error;
  int null_packet;
  unsigned char packet[256];
};
static int32_t
padding( void *opaque, unsigned int *frames )
{
  struct mock *m = opaque;
  *frames = m->padding;
  return m->padding_error;
}
static int32_t
acquire( void *opaque, unsigned int frames, unsigned char **data )
{
  struct mock *m = opaque;
  m->acquired = frames;
  *data = m->null_packet ? NULL : m->packet;
  return m->acquire_error;
}
static int32_t
release( void *opaque, unsigned int frames, int silent )
{
  struct mock *m = opaque;
  m->released = frames; m->silent = silent;
  return m->release_error;
}
static void begin( void *opaque ) { ((struct mock *)opaque)->begin++; }
static void negative( void *opaque ) { ((struct mock *)opaque)->negative++; }
static void
finish( void *opaque, unsigned int requested, unsigned int delivered,
        int invalid, int running )
{
  struct mock *m = opaque;
  (void)running;
  m->finish++; m->requested = requested; m->delivered = delivered;
  m->invalid = invalid;
}

static void
packets( void )
{
  const struct wasapi_packet_ops ops = { padding, acquire, release };
  sfifo_t fifo = { 0 };
  int16_t scratch[16], input[32];
  unsigned char expected[64];
  struct mock m = { 0 };
  struct wasapi_service s = { .fifo = &fifo,
    .format = { 48000, 2, 16, 16, 4, 0, WASAPI_INTEGER },
    .channels = 2, .capacity = 8, .scratch = scratch, .observer = &m,
    .begin = begin, .finish = finish, .negative = negative };
  CHECK( !sfifo_init( &fifo, 65 ) );
  for( int i = 0; i < 32; i++ ) {
    input[i] = i * 513 - 8192;
    expected[i * 2] = (uint16_t)input[i];
    expected[i * 2 + 1] = (uint16_t)input[i] >> 8;
  }
  CHECK( pcm_fifo_write( &fifo, 4, input, 16 ) == 16 ); /* B > native capacity. */
  m.acquire_error = WASAPI_PACKET_FAILURE;
  CHECK( wasapi_service_packet( &s, &ops, &m, 1 ) < 0 );
  CHECK( pcm_fifo_consumer_used( &fifo, 4 ) == 16 && !m.begin && !m.finish );
  m.acquire_error = 0; m.padding = 3;
  CHECK( !wasapi_service_packet( &s, &ops, &m, 1 ) );
  CHECK( m.acquired == 5 && m.released == 5 && m.delivered == 5 && !m.invalid );
  CHECK( !memcmp( m.packet, expected, 20 ) );
  m.release_error = WASAPI_PACKET_FAILURE;
  CHECK( wasapi_service_packet( &s, &ops, &m, 1 ) < 0 );
  CHECK( m.invalid && s.uncertain == 5 && s.submitted == 5 && s.consumed == 10 );
  /* The service caller must retire on this failure. Read remaining data here
     only to prove the failed packet is not replayed by FIFO positions. */
  CHECK( pcm_fifo_read( &fifo, 4, scratch, 6 ) == 6 );
  CHECK( !memcmp( scratch, input + 20, 24 ) );
  m.release_error = 0; m.padding = 0;
  CHECK( pcm_fifo_write( &fifo, 4, input, 3 ) == 3 );
  memset( m.packet, 0xa5, sizeof( m.packet ) );
  CHECK( !wasapi_service_packet( &s, &ops, &m, 0 ) );
  CHECK( m.released == 8 && m.delivered == 3 && !m.silent );
  CHECK( !memcmp( m.packet, expected, 12 ) );
  for( unsigned int i = 12; i < 32; i++ ) CHECK( m.packet[i] == 0 );
  CHECK( !wasapi_service_packet( &s, &ops, &m, 1 ) );
  CHECK( m.silent && m.released == 8 && !m.delivered && m.negative == 2 );
  CHECK( s.silence == 13 && m.begin == m.finish );
  m.padding = 9;
  CHECK( wasapi_service_packet( &s, &ops, &m, 1 ) < 0 );
  CHECK( m.begin == m.finish );
  m.padding = 8; m.acquired = 0;
  CHECK( !wasapi_service_packet( &s, &ops, &m, 1 ) && !m.acquired );
  /* Wrap publication and compare partitioned pulls with chronological PCM. */
  m.padding = 4;
  for( int cycle = 0; cycle < 100; cycle++ ) {
    CHECK( pcm_fifo_write( &fifo, 4, input, 4 ) == 4 );
    CHECK( !wasapi_service_packet( &s, &ops, &m, 1 ) );
    CHECK( !memcmp( m.packet, expected, 16 ) );
  }
  CHECK( pcm_fifo_write( &fifo, 4, input, 4 ) == 4 );
  unsigned int before = m.begin;
  m.padding_error = WASAPI_PACKET_FAILURE;
  CHECK( wasapi_service_packet( &s, &ops, &m, 1 ) < 0 );
  CHECK( pcm_fifo_consumer_used( &fifo, 4 ) == 4 && m.begin == before );
  m.padding_error = 0; m.null_packet = 1; m.released = UINT_MAX;
  CHECK( wasapi_service_packet( &s, &ops, &m, 1 ) < 0 );
  CHECK( m.released == 0 && pcm_fifo_consumer_used( &fifo, 4 ) == 4 );
  CHECK( m.begin == before && m.begin == m.finish );
  sfifo_close( &fifo );
}

#ifdef main
/* SDL headers redefine main on Windows, but this test needs a normal entry point. */
#undef main
#endif

int
main( void )
{
  formats(); periods(); packets();
  puts( "WASAPI PCM/period/packet checks passed" );
  return 0;
}

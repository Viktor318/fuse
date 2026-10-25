/* wasapi_pcm.h: portable WASAPI format and packet contracts
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/
#ifndef FUSE_WASAPI_PCM_H
#define FUSE_WASAPI_PCM_H

#include <stdint.h>
#include "pcm_fifo.h"

/* Windows speaker-mask values, independent of Windows headers. */
#define WASAPI_FRONT_LEFT  1u
#define WASAPI_FRONT_RIGHT 2u
#define WASAPI_FRONT_CENTER 4u

enum wasapi_encoding { WASAPI_INTEGER, WASAPI_FLOAT };
struct wasapi_format {
  unsigned int rate, channels, container_bits, valid_bits, block_align;
  uint32_t mask;
  enum wasapi_encoding encoding;
};

/* Plain mono/stereo descriptors use mask=0. Extensible multichannel output
   requires an unambiguous mask with exactly channels bits (at most eight).
   Each public operation validates geometry before writing output. */
int wasapi_format_validate( const struct wasapi_format *format );
int wasapi_convert( const struct wasapi_format *format, unsigned int channels,
                    int quantize, const int16_t *input, unsigned int frames,
                    unsigned char *output );
int wasapi_period_select( unsigned int rate, unsigned int fundamental,
                          unsigned int minimum, unsigned int maximum,
                          unsigned int *selected );
int wasapi_fifo_bytes( unsigned int startup, unsigned int demand,
                       unsigned int allowance, unsigned int width,
                       unsigned int *bytes );

/* Native HRESULT values stay intact. Geometry/transport failures use E_FAIL.
   One service call acquires/releases one complete packet, or no packet. */
#define WASAPI_PACKET_FAILURE ((int32_t)UINT32_C(0x80004005))
struct wasapi_packet_ops {
  int32_t (*padding)( void *context, unsigned int *frames );
  int32_t (*acquire)( void *context, unsigned int frames, unsigned char **data );
  int32_t (*release)( void *context, unsigned int frames, int silent );
};
struct wasapi_service {
  sfifo_t *fifo;
  struct wasapi_format format;
  unsigned int channels, capacity;
  int quantize;
  int16_t *scratch; /* Preallocated capacity * input channels, owner frees. */
  void *observer;
  void (*begin)( void *observer );
  void (*finish)( void *observer, unsigned int requested,
                  unsigned int delivered, int invalid, int running );
  void (*negative)( void *observer ); /* Native empty, not a missing magnitude. */
  uint64_t submitted, consumed, silence, uncertain, native_empty;
};
int32_t wasapi_service_packet( struct wasapi_service *service,
                               const struct wasapi_packet_ops *ops,
                               void *native, int running );

#endif

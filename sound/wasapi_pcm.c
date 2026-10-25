/* wasapi_pcm.c: portable WASAPI conversion and single-packet service
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/
#include "config.h"

#include <float.h>
#include <stddef.h>
#include <string.h>

#include "wasapi_pcm.h"

static int
encoding_validate( const struct wasapi_format *f )
{
  if( f->encoding == WASAPI_FLOAT ) {
    if( f->container_bits != 32 || f->valid_bits != 32 ||
        sizeof( float ) != 4 || FLT_RADIX != 2 || FLT_MANT_DIG != 24 )
      return -EINVAL;
  } else if( f->encoding == WASAPI_INTEGER ) {
    if( ( f->container_bits != 16 && f->container_bits != 24 &&
          f->container_bits != 32 ) || f->valid_bits < 16 ||
        f->valid_bits > f->container_bits ) return -EINVAL;
  } else return -EINVAL;
  return 0;
}

static int
channel_mask_validate( const struct wasapi_format *f )
{
  unsigned int bits = 0;
  uint32_t mask = f->mask;
  if( !mask ) return f->channels <= 2 ? 0 : -EINVAL;
  /* Windows speaker bits through TOP_BACK_RIGHT; reserved bits are ambiguous. */
  if( mask & ~UINT32_C(0x3ffff) ) return -EINVAL;
  while( mask ) { bits += mask & 1; mask >>= 1; }
  if( bits != f->channels ) return -EINVAL;
  if( f->channels == 1 ) return f->mask == WASAPI_FRONT_CENTER ? 0 : -EINVAL;
  return ( f->mask & 3 ) == 3 ? 0 : -EINVAL;
}

int
wasapi_format_validate( const struct wasapi_format *f )
{
  if( !f || f->rate < 8000 || f->rate > 384000 || !f->channels ||
      f->channels > 8 ) return -EINVAL;
  if( encoding_validate( f ) ) return -EINVAL;
  if( f->block_align != f->channels * ( f->container_bits / 8 ) )
    return -EINVAL;
  return channel_mask_validate( f );
}

int
wasapi_period_select( unsigned int rate, unsigned int fundamental,
                      unsigned int minimum, unsigned int maximum,
                      unsigned int *selected )
{
  uint64_t first, last, step, near, lower, upper, wanted;
  if( !selected || !rate || !fundamental || !minimum || minimum > maximum )
    return -EINVAL;
  first = ( (uint64_t)minimum + fundamental - 1 ) / fundamental;
  last = maximum / fundamental;
  if( first > last ) return -EINVAL;
  /* Compare in units of 1/200 frame: exactly 5ms, no truncated-rate bias. */
  step = (uint64_t)fundamental * 200;
  wanted = rate;
  near = wanted / step;
  lower = near < first ? first : near > last ? last : near;
  upper = lower < last ? lower + 1 : lower;
  if( wanted <= lower * step ) near = lower;
  else near = wanted - lower * step < upper * step - wanted ? lower : upper;
  *selected = near * fundamental;
  return 0;
}

int
wasapi_fifo_bytes( unsigned int startup, unsigned int demand,
                   unsigned int allowance, unsigned int width,
                   unsigned int *bytes )
{
  uint64_t frames, minimum, size, headroom;
  if( !bytes || !startup || !demand || !allowance ||
      ( width != 2 && width != 4 ) ) return -EINVAL;
  frames = (uint64_t)startup * 3;
  minimum = (uint64_t)startup + 2 * (uint64_t)demand + 2;
  if( frames < minimum ) frames = minimum;
  /* The shared controller's H=max(2,ceil(D/8)) is also needed for storage:
     prefill goal startup+D+H must leave room for a whole next startup batch.
     This sizes physical storage; it does not tune H or the ordinary target. */
  headroom = ( (uint64_t)demand + 7 ) / 8;
  if( headroom < 2 ) headroom = 2;
  minimum = 2 * (uint64_t)startup + demand + headroom;
  if( frames < minimum ) frames = minimum;
  minimum = (uint64_t)allowance + 2 * (uint64_t)demand + headroom + 2;
  if( frames < minimum ) frames = minimum;
  size = frames * width + 1;
  /* A field-trial stream must not allocate unbounded storage on bad geometry. */
  if( size > 16 * 1024 * 1024 || size > SFIFO_MAX_BUFFER_SIZE ) return -EINVAL;
  *bytes = size;
  return 0;
}

static int
quantized( int sample, int quantize )
{
  if( !quantize ) return sample;
  /* Drop the low eight bits, explicitly flooring negative values. */
  return ( sample >= 0 ? sample / 256 : -( ( -sample + 255 ) / 256 ) ) * 256;
}

static void
pack( unsigned char *out, const struct wasapi_format *f, int sample )
{
  uint32_t value;
  unsigned int byte;
  if( f->encoding == WASAPI_FLOAT ) {
    float floating = sample / 32768.0f;
    memcpy( &value, &floating, sizeof( value ) );
  } else {
    /* Unsigned multiplication and packing: no signed left-shift UB. S16
       supplies at most 16 significant bits, all left-aligned in the container. */
    value = (uint32_t)(uint16_t)sample << ( f->container_bits - 16 );
  }
  for( byte = 0; byte < f->container_bits / 8; byte++ )
    out[byte] = value >> ( byte * 8 );
}

struct speaker_indices { unsigned int left, right; };

static struct speaker_indices
front_speakers( const struct wasapi_format *f )
{
  struct speaker_indices speakers = { 0, 1 };
  unsigned int index = 0, bit;
  if( !f->mask || f->channels <= 1 ) return speakers;
  for( bit = 0; bit < 18; bit++ ) {
    if( !( f->mask & ( 1u << bit ) ) ) continue;
    if( ( 1u << bit ) == WASAPI_FRONT_LEFT ) speakers.left = index;
    if( ( 1u << bit ) == WASAPI_FRONT_RIGHT ) speakers.right = index;
    index++;
  }
  return speakers;
}

/* Format, geometry and pointers have been validated by wasapi_convert. */
static void
convert_frame( const struct wasapi_format *f, unsigned int channels,
               int quantize, const struct speaker_indices *speakers,
               const int16_t *input, unsigned char *output )
{
  unsigned int channel;
  int l = input[0];
  int r = channels == 2 ? input[1] : l;
  if( f->channels == 1 ) {
    int sum = l + r;
    l = sum >= 0 ? ( sum + 1 ) / 2 : ( sum - 1 ) / 2;
  }
  l = quantized( l, quantize );
  r = quantized( r, quantize );
  for( channel = 0; channel < f->channels; channel++ ) {
    int sample = f->channels == 1 || channel == speakers->left ? l :
                 channel == speakers->right ? r : 0;
    pack( output + channel * ( f->container_bits / 8 ), f, sample );
  }
}

int
wasapi_convert( const struct wasapi_format *f, unsigned int channels,
                int quantize, const int16_t *input, unsigned int frames,
                unsigned char *output )
{
  unsigned int frame;
  struct speaker_indices speakers;
  if( wasapi_format_validate( f ) || ( channels != 1 && channels != 2 ) ||
      ( frames && ( !input || !output ) ) ||
      frames > SIZE_MAX / f->block_align ||
      frames > SIZE_MAX / channels / sizeof( *input ) ) return -EINVAL;
  speakers = front_speakers( f );
  for( frame = 0; frame < frames; frame++ )
    convert_frame( f, channels, quantize, &speakers,
                   input + (size_t)frame * channels,
                   output + (size_t)frame * f->block_align );
  return 0;
}

static int
service_validate( const struct wasapi_service *s,
                  const struct wasapi_packet_ops *ops )
{
  if( !s || !ops || !ops->padding || !ops->acquire || !ops->release ||
      !s->fifo || !s->scratch || !s->capacity ||
      ( s->channels != 1 && s->channels != 2 ) ||
      s->capacity > INT_MAX / ( s->channels * sizeof( int16_t ) ) ||
      wasapi_format_validate( &s->format ) ||
      s->capacity > SIZE_MAX / s->format.block_align ) return WASAPI_PACKET_FAILURE;
  return 0;
}

int32_t
wasapi_service_packet( struct wasapi_service *s,
                       const struct wasapi_packet_ops *ops,
                       void *native, int running )
{
  unsigned int padding, requested;
  int delivered;
  int32_t result;
  unsigned char *packet = NULL;
  if( service_validate( s, ops ) ) return WASAPI_PACKET_FAILURE;
  result = ops->padding( native, &padding );
  if( result < 0 ) return result;
  if( padding > s->capacity ) return WASAPI_PACKET_FAILURE;
  if( running && !padding ) {
    s->native_empty++;
    if( s->negative ) s->negative( s->observer );
  }
  requested = s->capacity - padding;
  if( !requested ) return 0;
  result = ops->acquire( native, requested, &packet );
  if( result < 0 ) return result; /* No FIFO read before successful acquisition. */
  if( !packet ) {
    ops->release( native, 0, 0 ); /* Abandon, never an arbitrary partial release. */
    return WASAPI_PACKET_FAILURE;
  }
  if( s->begin ) s->begin( s->observer );
  delivered = pcm_fifo_read( s->fifo, s->channels * sizeof( int16_t ),
                             s->scratch, requested );
  if( delivered < 0 ) {
    ops->release( native, 0, 0 );
    if( s->finish ) s->finish( s->observer, requested, 0, 1, running );
    return WASAPI_PACKET_FAILURE;
  }
  s->consumed += delivered;
  result = wasapi_convert( &s->format, s->channels, s->quantize, s->scratch,
                            delivered, packet );
  if( result ) {
    ops->release( native, 0, 0 );
    s->uncertain += delivered;
    if( s->finish ) s->finish( s->observer, requested, delivered, 1, running );
    return WASAPI_PACKET_FAILURE;
  }
  memset( packet + (size_t)delivered * s->format.block_align, 0,
          (size_t)( requested - delivered ) * s->format.block_align );
  result = ops->release( native, requested, !delivered );
  if( result < 0 ) s->uncertain += delivered;
  else {
    s->submitted += delivered;
    s->silence += requested - delivered;
  }
  if( s->finish ) s->finish( s->observer, requested, delivered, result < 0, running );
  return result;
}

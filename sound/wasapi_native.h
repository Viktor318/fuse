/* Native WASAPI worker ownership. GPL-2.0-or-later. */
#ifndef FUSE_WASAPI_NATIVE_H
#define FUSE_WASAPI_NATIVE_H
#include <stdatomic.h>
#include "wasapi_pcm.h"

struct wasapi_native;
struct wasapi_geometry {
  struct wasapi_format format;
  unsigned int capacity, period, fundamental, minimum, maximum, default_period;
  int client3, mmcss;
  int32_t low_period_error;
  int64_t latency;
};
/* All COM interfaces remain private to the worker's STA. open returns with the
   stream stopped; attach publishes immutable service state before PCM service. */
/* A failure operation is a borrowed static string; no COM pointer escapes.
   A non-NULL native on failure denotes an unjoined, retained partial lifetime. */
int32_t wasapi_native_open( struct wasapi_native **native,
                            struct wasapi_geometry *geometry,
                            const char **operation );
int32_t wasapi_native_attach( struct wasapi_native *native,
                              struct wasapi_service *service,
                              unsigned int prefill );
int32_t wasapi_native_data( struct wasapi_native *native );
int wasapi_native_started( struct wasapi_native *native );
/* Nonzero once the default output changed or the opened one went away */
int wasapi_native_device_changed( struct wasapi_native *native );
int32_t wasapi_native_error( struct wasapi_native *native );
const char *wasapi_native_operation( struct wasapi_native *native );
/* Called only while service state exists; observer wake does not grant space. */
void wasapi_native_set_notify( struct wasapi_native *native,
                               void (*notify)( void * ), void *observer );
/* Join before freeing any service storage. No TerminateThread or timed free. */
/* Nonzero return means not joined: retain the entire lifetime. Device/Stop
   failure after a successful join is returned separately in stream_error. */
int32_t wasapi_native_close( struct wasapi_native *native, int32_t *stream_error );
#endif

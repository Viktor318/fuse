/* Exercise the existing Save Snapshot file path, without a new host API. */
#include "config.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fuse.h"
#include "fuse-internal.h"
#include "fuse-legacy.h"
#include "memory_pages.h"
#include "module.h"
#include "snapshot.h"
#include "z80/z80.h"

/* Track libspectrum allocations only during a save. This fresh-process fixture
   uses libc-compatible allocators; pre-existing core allocations are ignored. */
static void *allocations[4096];
static size_t allocation_count;
static int malformed_export;

static void
export_failure( libspectrum_snap *snap )
{
  /* Exercise a real writer error after it has emitted header/CPU chunks. */
  if( malformed_export ) libspectrum_snap_set_opus_active( snap, 1 );
}
static module_info_t export_module = { NULL, NULL, NULL, NULL, export_failure, NULL };

static void
remember( void *pointer )
{
  if( !pointer ) return;
  assert( allocation_count < sizeof(allocations)/sizeof(allocations[0]) );
  allocations[allocation_count++] = pointer;
}

static void
forget( void *pointer )
{
  size_t i;
  for( i = 0; i < allocation_count; i++ )
    if( allocations[i] == pointer ) {
      allocations[i] = allocations[--allocation_count];
      return;
    }
}

static void *tracked_malloc( size_t size )
{
  void *pointer = malloc( size );
  assert( pointer || !size ); remember( pointer ); return pointer;
}

static void *tracked_calloc( size_t count, size_t size )
{
  void *pointer = calloc( count, size );
  assert( pointer || !count || !size ); remember( pointer ); return pointer;
}

static void *tracked_realloc( void *pointer, size_t size )
{
  void *result;
  forget( pointer ); result = realloc( pointer, size );
  assert( result || !size ); remember( result ); return result;
}

static void tracked_free( void *pointer )
{
  forget( pointer ); free( pointer );
}

static void
compare_file( const char *filename, const unsigned char *expected, size_t length )
{
  FILE *file = fopen( filename, "rb" );
  unsigned char *actual = malloc( length );
  assert( file && actual );
  assert( fread( actual, 1, length, file ) == length && fgetc( file ) == EOF );
  assert( !memcmp( actual, expected, length ) );
  assert( !fclose( file ) && !remove( filename ) ); free( actual );
}

int
snapshot_file_test( int argc, char **argv )
{
  libspectrum_mem_vtable_t tracking = {
    tracked_malloc, tracked_calloc, tracked_realloc, tracked_free
  };
  libspectrum_mem_vtable_t normal = { malloc, calloc, realloc, free };
  libspectrum_snap *snap;
  unsigned char *expected = NULL;
  size_t length = 0;
  int flags = 0;
  FILE *file;
  assert( fuse_legacy_prepare( argc, argv ) == FUSE_INIT_READY );
  assert( !module_register( &export_module ) );
  z80.pc.w = 0x8000; z80.sp.w = 0x9000;
  writebyte_internal( 0x8100, 0x5a );
  snap = libspectrum_snap_alloc();
  assert( !snapshot_copy_to( snap ) );
  assert( !libspectrum_snap_write( &expected, &length, &flags, snap,
                                  LIBSPECTRUM_ID_SNAPSHOT_SZX, fuse_creator, 0 ) );
  assert( !libspectrum_snap_free( snap ) );
  libspectrum_mem_set_vtable( &tracking );
  assert( !snapshot_write( "saved.szx" ) );
  assert( allocation_count == 0 );
  compare_file( "saved.szx", expected, length );
  malformed_export = 1; /* Active Opus with no ROM: SZX returns partial output. */
  assert( snapshot_write( "partial.szx" ) );
  assert( allocation_count == 0 );
  file = fopen( "partial.szx", "rb" ); assert( !file );
  malformed_export = 0;
  z80.sp.w = 0; /* SNA cannot stack PC here; early error produces no file. */
  assert( snapshot_write( "failed.sna" ) );
  assert( allocation_count == 0 );
  file = fopen( "failed.sna", "rb" ); assert( !file );
  z80.sp.w = 0x9000;
  assert( snapshot_write( "missing-directory/failed.szx" ) );
  assert( allocation_count == 0 );
  assert( !snapshot_write( "recovered.szx" ) );
  assert( allocation_count == 0 );
  compare_file( "recovered.szx", expected, length );
  assert( z80.pc.w == 0x8000 && z80.sp.w == 0x9000 && readbyte_internal(0x8100) == 0x5a );
  libspectrum_mem_set_vtable( &normal ); libspectrum_free( expected );
  fuse_shutdown();
  return 0;
}

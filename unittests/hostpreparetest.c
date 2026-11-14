/* Private configuration/resource preparation contract, run in child processes. */
#include "config.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fuse-internal.h"
#include "machine.h"
#include "settings.h"
#include "snapshot.h"
#include "memory_pages.h"

static struct {
  const char *path;
  const char *mode;
  unsigned char *bytes;
  int acquired, released;
} provider;

static void
release_rom( void *context )
{
  assert( context == &provider );
  assert( provider.acquired == provider.released + 1 );
  free( provider.bytes );
  provider.bytes = NULL;
  provider.released++;
}

static int
acquire_rom( void *context, const char *name, fuse_host_rom *rom )
{
  FILE *file;
  assert( context == &provider );
  assert( !strcmp( name, "48.rom" ) );
  assert( provider.acquired == provider.released );
  provider.acquired++;
  assert( fuse_host_prepare( NULL, NULL ) == FUSE_LEGACY_INIT_BUSY );
  fuse_legacy_shutdown(); /* Must not tear down STARTING. */
  rom->context = &provider;
  rom->release = release_rom;
  if( !strcmp( provider.mode, "missing" ) ) return 1;
  provider.bytes = malloc( 16384 );
  assert( provider.bytes );
  file = fopen( provider.path, "rb" );
  assert( file );
  assert( fread( provider.bytes, 1, 16384, file ) == 16384 );
  assert( !fclose( file ) );
  rom->data = provider.bytes;
  rom->length = !strcmp( provider.mode, "bad-size" ) ? 1 : 16384;
  rom->custom = !strcmp( provider.mode, "custom" );
  if( !strcmp( provider.mode, "bad-provenance" ) ) rom->custom = 2;
  if( !strcmp( provider.mode, "null-bytes" ) ) rom->data = NULL;
  return !strcmp( provider.mode, "error-bytes" );
}

int
host_prepare_test( int argc, char **argv )
{
  fuse_host_config *config;
  fuse_host_resources resources = { &provider, acquire_rom };
  fuse_legacy_init_result result;
  char machine[] = "48";
  int success;
  assert( argc == 3 );
  provider.mode = argv[1];
  provider.path = argv[2];
  if( !strcmp( provider.mode, "legacy-state" ) ) {
    char program[] = "host-reference", sound[] = "--no-sound";
    char *args[] = { program, sound, NULL };
    assert( fuse_legacy_prepare( 2, args ) == FUSE_LEGACY_INIT_READY );
    assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_COMPLETED );
    assert( !snapshot_write( "state.szx" ) );
    fuse_legacy_shutdown();
    return 0;
  }
  success = !strcmp( provider.mode, "success" ) ||
            !strcmp( provider.mode, "host-state" ) ||
            !strcmp( provider.mode, "custom" );
  assert( fuse_host_prepare( NULL, &resources ) == FUSE_LEGACY_INIT_INVALID_ARGUMENT );
  config = fuse_host_config_alloc();
  assert( fuse_host_config_set_machine( NULL, machine ) );
  assert( fuse_host_config_set_machine( config, "" ) );
  assert( fuse_host_config_set_sound( config, 2 ) );
  assert( !fuse_host_config_set_machine( config, machine ) );
  machine[0] = 'x'; /* Configuration must not borrow the caller's string. */
  assert( !fuse_host_config_set_sound( config, 0 ) );
  assert( fuse_host_prepare( config, NULL ) == FUSE_LEGACY_INIT_INVALID_ARGUMENT );
  {
    fuse_host_resources invalid = { &provider, NULL };
    assert( fuse_host_prepare( config, &invalid ) == FUSE_LEGACY_INIT_INVALID_ARGUMENT );
  }
  result = fuse_host_prepare( config, &resources );
  fuse_host_config_free( config );
  fuse_host_config_free( NULL );
  assert( provider.acquired == 1 && provider.released == 1 );
  if( success ) {
    assert( result == FUSE_LEGACY_INIT_READY );
    assert( !strcmp( settings_current.start_machine, "48" ) );
    assert( !settings_current.sound );
    assert( machine_current );
    assert( !!memory_custom_rom() == !strcmp( provider.mode, "custom" ) );
    if( !strcmp( provider.mode, "custom" ) ) {
      libspectrum_snap *snap = libspectrum_snap_alloc();
      assert( !snapshot_copy_to( snap ) );
      assert( libspectrum_snap_custom_rom( snap ) );
      assert( !snapshot_copy_from( snap ) );
      assert( !libspectrum_snap_free( snap ) );
      /* Embedded snapshot ROMs precede the provider; no second acquisition. */
      assert( provider.acquired == 1 && provider.released == 1 );
    }
    assert( fuse_legacy_prepare( 0, NULL ) == FUSE_LEGACY_INIT_BUSY );
    assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_COMPLETED );
    if( !strcmp( provider.mode, "host-state" ) )
      assert( !snapshot_write( "state.szx" ) );
  } else {
    assert( result == FUSE_LEGACY_INIT_FAILED );
  }
  fuse_legacy_shutdown();
  fuse_legacy_shutdown();
  assert( !fuse_host_resources_active() );
  assert( fuse_host_prepare( NULL, NULL ) == FUSE_LEGACY_INIT_CLOSED );
  return 0;
}

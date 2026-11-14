/* Bounded direct consumer of the private, selected-null-UI worker API.
   Not a toolkit-free core, command server or replacement for the legacy CLI.
   Build/run instructions and limits: hacking/legacy-engine.md. */
#include <stdio.h>
#include <stdlib.h>

#include "fuse-internal.h"

typedef struct rom_provider {
  const char *directory;
  unsigned char *pending;
  unsigned int requests, releases;
} rom_provider;

static void
release_rom( void *context )
{
  rom_provider *provider = context;
  free( provider->pending );
  provider->pending = NULL;
  provider->releases++;
}

/* This host treats directory contents as its standard ROM set. Another host
   could supply packaged bytes here; the engine does not choose a filesystem
   search path or retry a missing ROM behind the host's back. */
static int
acquire_rom( void *context, const char *name, fuse_host_rom *rom )
{
  rom_provider *provider = context;
  char path[4096];
  FILE *file;
  long length;
  int written, failed;

  provider->requests++;
  rom->context = provider;
  rom->release = release_rom; /* Called even when acquisition/validation fails. */
  written = snprintf( path, sizeof(path), "%s/%s", provider->directory, name );
  if( written < 0 || (size_t)written >= sizeof(path) ) {
    fprintf( stderr, "host: ROM path is too long: %s\n", name );
    return 1;
  }
  file = fopen( path, "rb" );
  if( !file ) {
    fprintf( stderr, "host: cannot open ROM: %s\n", path );
    return 1;
  }
  if( fseek( file, 0, SEEK_END ) || ( length = ftell( file ) ) <= 0 ||
      fseek( file, 0, SEEK_SET ) ) {
    fprintf( stderr, "host: cannot size ROM: %s\n", path );
    fclose( file );
    return 1;
  }
  provider->pending = malloc( (size_t)length );
  failed = !provider->pending ||
           fread( provider->pending, 1, (size_t)length, file ) != (size_t)length;
  if( fclose( file ) ) failed = 1;
  if( failed ) {
    fprintf( stderr, "host: cannot read ROM: %s\n", path );
    return 1; /* release_rom owns any partial allocation. */
  }
  rom->data = provider->pending;
  rom->length = (size_t)length;
  rom->custom = 0; /* Explicit host provenance: standard ROM, not a custom override. */
  return 0;
}

static int
operation( const char *name, fuse_host_operation_result result )
{
  if( result == FUSE_HOST_OPERATION_OK ) return 0;
  fprintf( stderr, "host: %s failed (operation result %d)\n", name, (int)result );
  return 1;
}

static int
frames( unsigned int count, unsigned int *completed )
{
  unsigned int i;
  for( i = 0; i < count; i++ ) {
    fuse_legacy_run_result result = fuse_legacy_run_frame();
    if( result != FUSE_LEGACY_RUN_COMPLETED ) {
      fprintf( stderr, "host: frame turn stopped (run result %d)\n", (int)result );
      return 1;
    }
    (*completed)++;
  }
  return 0;
}

int
main( int argc, char **argv )
{
  rom_provider provider = { 0 };
  fuse_host_resources resources = { &provider, acquire_rom };
  fuse_host_config *config;
  fuse_legacy_init_result init;
  fuse_legacy_run_result run;
  unsigned int frame_turns = 0, budget_turns = 0;
  int prepared = 0, status = EXIT_FAILURE;

  if( argc != 2 ) {
    fprintf( stderr, "Usage: %s ROM_DIRECTORY\n", argv[0] );
    return EXIT_FAILURE;
  }
  provider.directory = argv[1]; /* Borrowed through shutdown, not passed as engine CLI. */
  config = fuse_host_config_alloc();
  if( fuse_host_config_set_machine( config, "48" ) ||
      fuse_host_config_set_sound( config, 0 ) ||
      fuse_host_config_set_joystick( config, FUSE_HOST_JOYSTICK_1,
                                    FUSE_HOST_JOYSTICK_OUTPUT_KEMPSTON ) ) {
    fprintf( stderr, "host: configuration rejected\n" );
    fuse_host_config_free( config );
    goto cleanup;
  }
  init = fuse_host_prepare( config, &resources );
  fuse_host_config_free( config ); /* Preparation owns its copied configuration. */
  if( init != FUSE_LEGACY_INIT_READY ) {
    fprintf( stderr, "host: preparation failed (init result %d)\n", (int)init );
    goto cleanup; /* A rejected prepare does not authorize another session's shutdown. */
  }
  prepared = 1;
  puts( "host: prepared 48" );
  if( frames( 2, &frame_turns ) ) goto cleanup;

  /* The host holds execution simply by not calling a run entry. Input can be
     delivered at this boundary. This is NOT the core's nested pause counter. */
  puts( "host: holding execution; delivering Space and joystick 1 fire" );
  if( operation( "key press", fuse_host_keyboard_event( INPUT_KEY_space, 1 ) ) ||
      operation( "joystick press", fuse_host_joystick_event( FUSE_HOST_JOYSTICK_1,
                                      FUSE_HOST_JOYSTICK_FIRE, 1 ) ) ) goto cleanup;
  puts( "host: resuming execution" );
  if( frames( 1, &frame_turns ) ) goto cleanup;
  if( operation( "focus loss", fuse_host_release_all() ) ) goto cleanup;
  puts( "host: focus lost; inputs released" );

  run = fuse_legacy_run_tstates( 997 );
  if( run != FUSE_LEGACY_RUN_COMPLETED ) {
    fprintf( stderr, "host: budget turn stopped (run result %d)\n", (int)run );
    goto cleanup;
  }
  budget_turns++;
  puts( "host: completed relative 997-T-state budget" );
  if( operation( "reset", fuse_host_reset( FUSE_HOST_RESET_SOFT ) ) ) goto cleanup;
  puts( "host: reset 48" );
  if( operation( "select 128", fuse_host_select_machine( "128" ) ) ) goto cleanup;
  puts( "host: selected 128" );
  if( frames( 2, &frame_turns ) ) goto cleanup;
  status = EXIT_SUCCESS;

cleanup:
  if( prepared ) fuse_legacy_shutdown(); /* Also valid after a failed mutation. */
  if( provider.requests != provider.releases || provider.pending ) {
    fprintf( stderr, "host: ROM ownership mismatch\n" );
    status = EXIT_FAILURE;
  }
  printf( "host: %s; frame turns=%u; budget turns=%u; ROM requests=%u; releases=%u\n",
          status == EXIT_SUCCESS ? "shutdown" : "stopped", frame_turns,
          budget_turns, provider.requests, provider.releases );
  return status;
}

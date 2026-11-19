/* Generic legacy-engine link consumer, without platform bootstrap.
   Exercises the private wrapper or split lifecycle, not a supported embedding
   execution API. */
#include "config.h"
#include <string.h>
#include "fuse-legacy.h"

int legacy_lifecycle_test( int argc, char **argv );
int host_prepare_test( int argc, char **argv );
int host_operations_test( int argc, char **argv );
int snapshot_file_test( int argc, char **argv );

int
main( int argc, char **argv )
{
  if( argc > 1 && !strcmp( argv[1], "--snapshot-file-test" ) )
    return snapshot_file_test( argc - 1, argv + 1 );
  if( argc > 1 && !strcmp( argv[1], "--host-operations-test" ) )
    return host_operations_test( argc - 1, argv + 1 );
  if( argc > 1 && !strcmp( argv[1], "--host-prepare-test" ) )
    return host_prepare_test( argc - 1, argv + 1 );
  if( argc > 1 && !strcmp( argv[1], "--lifecycle-test" ) )
    return legacy_lifecycle_test( argc - 1, argv + 1 );
  return fuse_legacy_main( argc, argv );
}

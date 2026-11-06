/* Deterministic shared refresh policy tests; no SDL, host clock or sleeps. */
#include "config.h"

/* Test checks must execute even when the emulator is built with NDEBUG. */
#undef NDEBUG
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "input_refresh.h"

typedef struct fixture {
  input_refresh_policy policy;
  long long now, duration;
  int clocks, polls, fail_clock, invalidate_clock;
  int recurse_clock, recurse_poll, invalidate_poll, replace_poll;
  input_refresh_result result;
} fixture;

static long long
clock_now( void *context )
{
  fixture *f = context;
  f->clocks++;
  assert( f->policy.refreshing );
  if( f->recurse_clock )
    assert( input_refresh_maybe( &f->policy, 1, 1, 1 ) == INPUT_REFRESH_SKIPPED );
  if( f->invalidate_clock == f->clocks ) input_refresh_invalidate( &f->policy );
  if( f->fail_clock == f->clocks ) return -1;
  return f->now;
}

static input_refresh_result
poll_input( void *context )
{
  fixture *f = context;
  f->polls++;
  assert( f->policy.refreshing );
  if( f->invalidate_poll ) input_refresh_invalidate( &f->policy );
  if( f->replace_poll ) input_refresh_set_frontend( &f->policy, NULL, NULL );
  if( f->recurse_poll )
    assert( input_refresh_maybe( &f->policy, 1, 1, 1 ) == INPUT_REFRESH_SKIPPED );
  f->now += f->duration;
  return f->result; /* Empty successful polls are deliberately sufficient. */
}

static void
init( fixture *f )
{
  memset( f, 0, sizeof( *f ) );
  f->result = INPUT_REFRESH_SUCCESS;
  input_refresh_init( &f->policy, clock_now, f );
  input_refresh_set_frontend( &f->policy, poll_input, f );
}

static void
test_window_and_completion( void )
{
  fixture f;
  int i;
  init( &f );
  f.now = 10000; f.duration = 3000;
  assert( input_refresh_maybe( &f.policy, 0, 1, 1 ) == INPUT_REFRESH_SUCCESS );
  assert( f.policy.valid && f.policy.last_frame == 0 );
  assert( f.policy.completed_us == 13000 );
  for( i = 0; i < 5; i++ ) {
    f.now = 14000 + i * 999;
    assert( input_refresh_maybe( &f.policy, 0, 1, 1 ) == INPUT_REFRESH_SKIPPED );
    assert( f.policy.completed_us == 13000 ); /* Reads never slide the window. */
  }
  f.now = 17999;
  assert( input_refresh_maybe( &f.policy, 0, 1, 1 ) == INPUT_REFRESH_SKIPPED );
  f.now = 18000;
  assert( input_refresh_maybe( &f.policy, 0, 1, 1 ) == INPUT_REFRESH_SUCCESS );
  assert( f.policy.completed_us == 21000 && f.polls == 2 );
}

static void
test_first_read_each_frame( void )
{
  fixture f;
  init( &f );
  f.now = 100;
  assert( input_refresh_maybe( &f.policy, 7, 1, 1 ) == INPUT_REFRESH_SUCCESS );
  f.now++;
  assert( input_refresh_maybe( &f.policy, 8, 1, 1 ) == INPUT_REFRESH_SUCCESS );
  assert( input_refresh_maybe( &f.policy, 8, 1, 1 ) == INPUT_REFRESH_SKIPPED );
  assert( input_refresh_maybe( &f.policy, ULONG_MAX, 1, 1 ) == INPUT_REFRESH_SUCCESS );
  assert( input_refresh_maybe( &f.policy, 0, 1, 1 ) == INPUT_REFRESH_SUCCESS );
  assert( f.polls == 4 );
}

static void
test_eligibility( void )
{
  fixture f;
  init( &f );
  assert( input_refresh_maybe( &f.policy, 1, 0, 1 ) == INPUT_REFRESH_SKIPPED );
  assert( input_refresh_maybe( &f.policy, 1, 1, 0 ) == INPUT_REFRESH_SKIPPED );
  assert( !f.policy.valid && f.polls == 0 && f.clocks == 0 );
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_SUCCESS );
  assert( f.polls == 1 );
}

static void
test_unsuccessful_polls( void )
{
  fixture f;
  input_refresh_result results[] = {
    INPUT_REFRESH_FAILED, INPUT_REFRESH_UNSUPPORTED, INPUT_REFRESH_SKIPPED
  };
  unsigned int i;
  for( i = 0; i < sizeof( results ) / sizeof( results[0] ); i++ ) {
    init( &f );
    f.result = results[i];
    assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == results[i] );
    assert( !f.policy.valid && !f.policy.refreshing );
    assert( f.clocks == 1 ); /* No success completion timestamp. */
    f.result = INPUT_REFRESH_SUCCESS;
    assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_SUCCESS );
    f.now = 5000;
    f.result = results[i];
    assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == results[i] );
    assert( f.policy.completed_us == 0 && f.policy.last_frame == 1 );
    f.result = INPUT_REFRESH_SUCCESS;
    assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_SUCCESS );
    assert( f.polls == 4 ); /* Failed attempts never consume the opportunity. */
  }
}

static void
test_invalidation_and_frontend( void )
{
  fixture f;
  init( &f );
  input_refresh_set_frontend( &f.policy, NULL, NULL );
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_UNSUPPORTED );
  assert( f.clocks == 0 );
  input_refresh_set_frontend( &f.policy, poll_input, &f );
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_SUCCESS );
  input_refresh_invalidate( &f.policy ); /* Reset/resume, same frame and time. */
  assert( !f.policy.valid && !f.policy.refreshing );
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_SUCCESS );
  input_refresh_set_frontend( &f.policy, poll_input, &f );
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_SUCCESS );
  input_refresh_set_frontend( &f.policy, NULL, NULL );
  assert( !f.policy.valid && f.polls == 3 );
}

static void
test_reentrancy_and_inflight_changes( void )
{
  fixture f;
  init( &f );
  f.recurse_clock = f.recurse_poll = 1;
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_SUCCESS );
  assert( f.polls == 1 && f.clocks == 2 && !f.policy.refreshing );
  input_refresh_invalidate( &f.policy );
  f.invalidate_poll = 1;
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_SKIPPED );
  assert( !f.policy.valid && !f.policy.refreshing );
  f.invalidate_poll = 0;
  f.replace_poll = 1;
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_SKIPPED );
  assert( !f.policy.valid && !f.policy.refreshing );
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_UNSUPPORTED );
  assert( f.polls == 3 );
}

static void
test_clock_errors( void )
{
  fixture f;
  init( &f );
  f.fail_clock = 1;
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_FAILED );
  assert( f.polls == 0 && !f.policy.valid && !f.policy.refreshing );
  f.fail_clock = 3; /* Completion clock fails after an empty successful poll. */
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_FAILED );
  assert( f.polls == 1 && !f.policy.valid && !f.policy.refreshing );
  f.fail_clock = 0;
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_SUCCESS );
  f.now = 5000; f.fail_clock = f.clocks + 2;
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_FAILED );
  assert( f.policy.completed_us == 0 ); /* Previous success remains historical. */
  f.fail_clock = 0;
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_SUCCESS );
  f.policy.clock = NULL;
  assert( input_refresh_maybe( &f.policy, 2, 1, 1 ) == INPUT_REFRESH_FAILED );
}

static void
test_clock_invalidation_and_regression( void )
{
  fixture f;
  int call;
  for( call = 1; call <= 2; call++ ) {
    init( &f );
    f.invalidate_clock = call;
    assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_SKIPPED );
    assert( !f.policy.valid && !f.policy.refreshing );
    assert( f.polls == call - 1 );
  }
  init( &f );
  f.now = 100;
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_SUCCESS );
  f.now = 50; /* A regressed clock must not suppress collection indefinitely. */
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_SUCCESS );
  f.now = 5050; f.duration = -1;
  assert( input_refresh_maybe( &f.policy, 1, 1, 1 ) == INPUT_REFRESH_FAILED );
  assert( f.policy.completed_us == 50 && !f.policy.refreshing );
}

int
main( void )
{
  test_window_and_completion();
  test_first_read_each_frame();
  test_eligibility();
  test_unsuccessful_polls();
  test_invalidation_and_frontend();
  test_reentrancy_and_inflight_changes();
  test_clock_errors();
  test_clock_invalidation_and_regression();
  puts( "input refresh policy tests passed" );
  return 0;
}

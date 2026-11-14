# Internal legacy-engine build

## Scope and build inputs

`libfuselegacy.la` is a non-installed Libtool convenience library containing the
configured application engine: machine emulation, selected UI, debugger, pacing
and audio backend. It is not a toolkit-free core, installed SDK or stable binary
ABI. Consumers must use coherent sources, generated files and build configuration.

`fuse-main.c` owns normal/SDL entry and platform bootstrap. Win32 also uses
`ui/win32/win32main.c` and its executable resources. Platform entry points and
Emscripten preload/link policy are outside the library; system libraries required
by engine code remain engine dependencies.

Inspect the configured source and link inputs with:

```sh
make print-legacy-engine-inputs
```

The relevant build variables are:

| Variable | Contents |
| --- | --- |
| `libfuselegacy_la_SOURCES` | Engine and selected frontend sources, assembled by the included Automake fragments. |
| `SOUND_LIBADD`, `TIMER_LIBADD` | Configured Libtool backend objects. |
| `LEGACY_SCALER_OBJECTS` | Two compilations of `ui/scaler/scalers.c`, with `SCALER_DATA_SIZE=2` and `4`. |
| `LEGACY_ENGINE_OBJECTS` | Combined backend/scaler object list. |
| `FUSE_LEGACY_LIBS` | Configured external service link flags. |

Canonical-source consumers also need the configured headers, generated
settings/options/keysyms/menus/Z80 files, include roots, feature definitions and
compiler flags. The printed list alone is not a standalone build recipe. Do not
compile alternative UI/audio adapters together or omit the special scaler rules.

## Lifecycle

The host API is declared in `fuse-internal.h`; CLI adapter entries are in
`fuse-legacy.h`, which includes the host header for shared types and operations.
`fuse.h` contains engine implementation globals/hooks, not the host interface.
There is one session attempt per process, with no restart, multiple-instance or
concurrent-entry support. All mutable engine calls belong to one owner thread compatible with the
selected frontend. Callbacks must not recursively enter the host API.

Choose one preparation entry:

- `fuse_host_prepare(config, resources)` uses explicit configuration and a ROM
  provider. It does not parse CLI options or read/write preferences.
- `fuse_legacy_prepare(argc, argv)` uses application configuration, CLI parsing
  and startup files. Its mutable argument vector and strings remain borrowed
  through shutdown. Zero arguments with NULL argv is permitted; otherwise argv
  must contain non-NULL entries followed by a NULL terminator.

Both return `fuse_init_result` (`FUSE_INIT_*`). READY retains a runnable session.
Help/version and initialization failure clean up before returning and consume the
attempt. Invalid arguments do not consume it. Active/reentrant preparation returns
BUSY; preparation after closure returns CLOSED.

After READY, use shared worker turns, typed operations or the continuous legacy
runner. `fuse_shutdown()` disposes a prepared, completed or failed-operation
session. It tolerates repeated calls and does nothing before preparation, after
shutdown or during active preparation/execution/cleanup callbacks. It is not a
cross-thread stop request.

`fuse_legacy_execute(&status)` runs the blocking application loop once after READY,
including after host turns. Its return value reports acceptance; `status` receives
the application exit code. Once that loop finishes, further execution is rejected.
`fuse_legacy_main()` combines legacy preparation, continuous execution and shutdown.

Automatic preference saving belongs only to `fuse_legacy_main()` after successful
preparation and also requires the user's autosave setting. Separate preparation,
execution and shutdown do not grant permission to save. Explicit settings-write
operations remain available to the application adapter.

The engine manages libspectrum/XML and restores the previous libspectrum error
callback after owned teardown. Completed startup registrations unwind in reverse
completion order; an initializer owns cleanup of its unfinished acquisition.
Disposal does not undo file writes or resolve media save/discard decisions.
Platform/backend partial-initialization cleanup and fatal paths are not uniformly
recoverable, particularly for Win32, Xlib, framebuffer, Wii and device backends.
Recoverable startup results are not a universal non-terminating lifecycle promise.

## Host configuration and ROMs

Allocate `fuse_host_config` with `fuse_host_config_alloc()` and use its machine,
sound, joystick-slot output and mouse-swap setters. The configuration owns copied
strings and may be freed after preparation returns, including failure. Other
settings use internal generated defaults; generated settings layouts are not host
configuration inputs. An unknown nonempty machine ID fails during preparation
and consumes the attempt.

`fuse_host_resources` supplies a synchronous ROM acquisition callback. The engine
copies the descriptor; its context remains borrowed until preparation failure
cleanup or successful-session shutdown finishes. Each callback receives a logical
ROM name and returns zero with bytes, length and explicit `custom` provenance
(0 or 1), or nonzero on failure.

ROM bytes are copied before release. A supplied response release callback runs
exactly once, including after acquisition or size-validation failure. Names and
response bytes are borrowed only for the request/release interval. Embedded
snapshot ROMs take precedence over provider requests.

The host path does not silently retry filesystem search or a default ROM: fallback
is provider policy. A reported ROM failure fails host preparation/reset/selection
even when an optional-peripheral callback would otherwise disable itself and
return success. The legacy adapter can disable those peripherals and continue.

Host preparation still initializes the selected UI/output adapters and retains
internal defaults, RNG seeding and process-global library/error handling. It does
not process application startup files/debugger commands, print the application
banner or drop process privileges. Preparation and resource access may block.
Only ROM acquisition uses this provider: media persistence, IDE, serial/network,
snapshot loading and synchronous decisions retain their existing internal paths.
In particular, legacy snapshot machine fallback can abort. There is no general
host-services or deterministic-checkpoint contract.

## Execution turns

`fuse_run_frame()` advances to the next completed frame boundary, not necessarily
a whole frame from the current position. `fuse_run_tstates(budget)` advances by a
relative emulated T-state budget. Both return `fuse_run_result` (`FUSE_RUN_*`) and
are repeatable after READY. Ordinary returns leave the session available for
commands or another turn; they do not finalize application output or shut down.

| Result | Meaning |
| --- | --- |
| COMPLETED | Requested frame boundary or budget reached. Budget completion does not imply a completed frame. |
| PAUSED | No execution on paused entry, or execution paused during dispatch. |
| INTERRUPTED | An active budget's machine/clock state was replaced; reconsider the next request in the new state. |
| EXITING | Exit requested inside the engine. |
| BUSY | Reentrant call during preparation, execution, another operation or cleanup. |
| NOT_READY | No runnable session. |
| INVALID_ARGUMENT | Invalid budget or unsupported starting-clock range; no execution. |

Budgets must be 1..0x7fffffff, with a starting clock below 0x7fffffff. Execution
finishes instructions and due-event/frame dispatch, so it can overshoot a budget.
Budget accounting compensates for frame rebasing and RZX clock normalization;
RZX forced dispatch can also overshoot. Ordinary reset does not interrupt a turn.
Machine selection, snapshot restoration or debugger clock assignment during an
active budget returns INTERRUPTED at a safe boundary. Replacement between calls
is ordinary; do not silently restart an interrupted request.

Exit takes precedence over interruption, then pause, then completion. Z80 HALT is
not a host pause: frame events and interrupt delivery continue. Turns preserve
complete frame/RZX/interrupt/input/UI dispatch rather than returning from inside
a partially processed frame callback.

The host owns command delivery and waits between turns. It may hold execution by
not requesting another turn, or stop by calling shutdown, including while paused.
Cross-thread requests must use host-owned transport. Legacy frontend pause bridges
use balanced `fuse_emulation_pause()` / `fuse_emulation_unpause()` calls in `fuse.h`;
release only pauses the caller owns. PAUSED does not authorize decrementing another
owner's count. The host API does not expose pause-count controls.

Audio, UI, debugger, traps, prefix chains and event work may block inside a turn.
There is no wall-clock latency bound, cross-thread cancellation or cooperative
execution guarantee. Keep native timers, audio pacing and fastload wall-time
policy in the host rather than converting them to emulated scheduler events.

For CLI automation, result writing and exit-status mapping belong to the legacy
runner. After a host turn reports EXITING, `fuse_legacy_execute(&status)` can
finalize them without another CPU batch. Without an exit request, that call
resumes continuous execution. Direct shutdown does not write an automation result.

## Machine and input operations

The `fuse_host_*` operations run between execution turns on the owner thread,
including while paused. They do not advance CPU time. Calls during active work
return BUSY; pending exit returns EXITING. Invalid arguments return
INVALID_ARGUMENT without core mutation. Before preparation, after shutdown or
legacy execution finalization, and after a destructive operation failure, they
return NOT_READY.

Machine selection borrows an ID for the call and validates it before mutation.
Selection/reset reuse the machine mechanics, UI notifications and ROM services,
without adding an input release. They preserve nested pause ownership. ID
selection does not use the legacy snapshot/enum fallback-to-48K path. FAILED may
leave partially modified state: further execution/operations are rejected and
shutdown is required. There is no rollback/retry promise. NMI queues the normal
NMI event; it is not delivered inline.

- Normal keyboard input uses portable `input_key` values with Fuse's composite
  keys, shifted arrows, recreated keyboard and configured keyboard-joystick
  translation. It does not invoke widgets, menus or mouse grabbing, even for
  Escape/function keys. Toolkit-keycode conversion belongs to the host.
- `fuse_host_spectrum_key_event` is explicit low-level Spectrum matrix injection
  for uses such as tests or an on-screen Spectrum keyboard.
- Joystick input identifies logical slots (1, 2 or keyboard), not emulated
  interfaces. Fuse applies the configured mapping. Injection does not enable
  disabled/unavailable hardware.
- Relative mouse motion uses positive y down and wraps hardware counters.
  Logical left/right buttons respect configured swapping; native pointer capture
  and middle-button grab remain host/UI policy.
- Release-all clears physical keyboard and joystick/mouse button state without
  moving the mouse or cancelling synthetic phantom/disciple typing. Hosts must
  aggregate overlapping physical sources and avoid competing native writers;
  these are set/clear operations, not reference-counted holds.

Existing reset/module semantics apply. No input-sampling policy or owned state
query is provided; do not read live globals concurrently from a UI thread.

## Direct null-backed example

[null-host.c](null-host.c) has its own `main` and uses `fuse-internal.h` directly.
It configures a 48K machine, supplies a directory ROM provider, runs frame/budget
turns, delivers input, handles focus loss, resets, selects 128 and shuts down.
The directory needs `48.rom`, `128-0.rom` and `128-1.rom`.

In a configured null-UI build:

```sh
make unittests/nullhostlink unittests/nullhostsource
./unittests/nullhostlink /path/to/fuse/roms
./unittests/nullhostsource /path/to/fuse/roms
make check-null-host
```

The first consumer links the engine archive; the second compiles canonical engine
sources. The main thread is the owner thread. Holding execution means not calling
another turn, not manipulating the nested core pause count. Reported frame counts
are completed host calls, not monotonic machine frame IDs across reset/selection.
This is a selected-null-adapter example, not a replacement CLI or toolkit-free core.

## Checks

Null-UI builds include these checks in `make check`:

| Target | Coverage |
| --- | --- |
| `check-legacy-engine-link` | Archive/source linkage and legacy help/version entry. |
| `check-startup-ownership` | Startup storage, preference-save permission and callback restoration. |
| `check-legacy-lifecycle` | One-attempt lifecycle, reentry, pause, frame/budget boundaries and clock replacement; automation builds also cover bounded RZX/finalization cases. |
| `check-host-prepare` | Explicit configuration, ROM ownership/provenance and preference isolation. |
| `check-host-operations` | Typed machine/input behaviour and failures. |
| `check-snapshot-file` | Existing file serialization and error cleanup. |
| `check-null-host` | Direct driver transcripts, ROM failures and preference isolation in both linking forms. |
| `check-startup-unwind` | Instrumented startup failures and reverse cleanup; requires null UI with automation. |

Startup-unwind hooks compile out of the production engine. Tests run accepted
session attempts in fresh processes with isolated preferences. These checks do
not establish native frontend/device failure safety or a supported public SDK.

For uninstalled legacy CLI captures, resource search depends on argv[0] and the
usual directories. Supply explicit ROM paths when needed. For tape-autoload
checks, use standard ROM filenames resolved through those directories: a different
`--rom-*` filename marks the ROM custom and suppresses autoload even if its bytes
match the default. The direct host provider has its own resource policy instead.

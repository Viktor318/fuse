# Keyboard read-time input refresh

SDL2 refreshes physical keyboard input immediately before an eligible Spectrum
keyboard-matrix read. This supplements normal frame input handling; it does not
replace it. SDL2 enables the capability unconditionally, with no runtime switch,
new thread or audio dependency. Other frontends keep normal frame handling.

## Timing and eligibility

`input_refresh.c` owns the small, frontend-independent timing policy;
`keyboard_refresh.c` supplies the host clock and execution-mode gates.
`keyboard_read()` invokes the adapter before combining selected physical rows
with the existing synthetic overlays.

- The first selected-row read each frame can collect input. A read selecting no
  rows (`porth == 0xff`) does nothing.
- Further same-frame collection is eligible at least 5000 host microseconds
  after the last completed successful refresh. Reads do not slide this window.
- Only a successful collection and valid completion timestamp establish
  freshness. Empty success counts; blocked, incomplete and failed collection
  do not. Normal frame handling never publishes policy freshness.
- Live refresh is excluded during pause, widgets, debugger handling, RZX
  playback and automation. RZX recording retains its existing port-recording
  path. Synthetic sources are not changed; joystick-only reads gain no refresh.
- Reset, frontend/focus changes and transitions through excluded execution
  modes invalidate freshness, including when the frame number repeats.
- Same-thread recursion is guarded. Invalidation during collection prevents
  that collection from publishing stale freshness. This is not thread safety.

The 5 ms window limits repeated collection *at reads*: it is not a periodic
host timer and cannot collect input while the guest is not reading its keyboard.

## Shared mapping and ordered effects

`input_event_dispatch(..., INPUT_DISPATCH_READ_TIME)` uses the normal physical
mapping, including modifiers, shifted arrows, Recreated Spectrum translation
and keyboard-to-joystick routing. It does not execute menus, widgets or mouse
operations inside a Z80 instruction.

The result distinguishes consumed input (`APPLIED` or `DEFERRED`) from untouched
input (`BLOCKED` or `ERROR`). Never replay a consumed physical transition.
A deferred UI action is an ordering barrier: collection stops until normal
handling drains it, before dequeuing later native events. One allocation-free
action slot is sufficient because later events cannot overtake it. The action
is removed before execution, permitting nested normal widget dispatch without
replaying it. SDL2 uses a pure F1-F10 popup classifier; normal dispatch is
unchanged. Grabbed Escape is deferred before physical effects, preserving its
original consume/fall-through decision exactly once.

`input_cancel_physical()` clears the physical matrix, shifted-arrow/Recreated
state and direct joystick masks. It neither changes synthetic overlays nor
executes or discards a pending UI action.

## SDL2 collection boundary

`ui/sdl2/sdl2_events.c` serves normal and read-time collection on the existing
UI/emulation thread. It pumps once, then peeps events in their original order.
Read-time collection dequeues at most 64 events and does not claim freshness
if it stops at that limit. The limit does **not** bound native pump duration.

Keyboard transitions use shared dispatch. Non-key events form a barrier: one
raw event is retained for normal handling, with later events left in the SDL
queue. Focus loss cancels physical input immediately without UI operations;
normal handling later processes the retained event without repeating that
cancellation. Read-time collection remains blocked until normal focus-gain
handling. Owned retained SDL payloads are freed after dispatch or at teardown.

Normal collection drains deferred actions and the retained event before newer
events. Recursive read-time collection and read-time entry from normal callbacks
are blocked. General `ui_event()` is not a safe read-time callback; another
frontend must supply an equivalent audited collection/ordering boundary before
registering this capability. SDL pumping can still execute platform-native code.

## Win32 collection boundary

The Windows frontend of this fork registers the capability too, behind the
`keyboard_refresh` setting (on by default; off returns UNSUPPORTED).
`win32ui_refresh_input()` in `ui/win32/win32ui.c` peeks only the input queue
(`PM_QS_INPUT`), so no sent message is dispatched during a read. It takes
`WM_KEYDOWN`/`WM_KEYUP` of the main window from the front of the queue, in
order, through `TranslateMessage()` and the same handling as the window
procedure (`win32keyboard_key_message()`), and drops the characters a key
press posted but did not use. Anything else at the front is a barrier left
for the message loop: mouse and system key messages, keys of the accelerator
table, keys for other windows, a waiting character message, or the left Alt
held alone. It skips while frames run from a modal loop or without focus.
`ui_event()` drains a deferred action first.

## Tests

Run `make -j4 fuse`, then `make -j4 check` separately. Focused targets are:

- `check-input-refresh`: fake-clock timing, completion timestamps, retries,
  invalidation, clock errors/regression and recursion.
- `check-keyboard-refresh`: real runtime adapter, execution-mode gates,
  lifecycle invalidation, RZX recording versus playback and throttling.
- `check-input-dispatch`: shared physical mappings, exactly-once deferred
  actions, ordering, grabbed Escape and physical cancellation.
- `check-sdl2-events`: real SDL queue ordering, barriers, nested dispatch,
  focus handling, collection limits, retained payloads and queue errors.

These tests do not simulate native desktop focus/window/IME interactions.
The investigation code and measurements remain in git history at `0def6b92`;
keyboard/audio tracing and its analysis tools are not part of this implementation.

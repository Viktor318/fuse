# Libspectrum-owned filesystem: implementation handoff

## Native compatibility follow-ups

- **File-only unlink:** Fuse previously used `unlink()`, which rejects directories.
  Libspectrum's native `remove()` can delete an empty directory. Restore file-only
  semantics in libspectrum with the platform's file-deletion primitive, not a
  Fuse-side host-path probe. Document the same rule for custom callbacks. Test
  regular-file removal, rejection of an empty directory, and removal of a symlink
  without touching its target where supported.
- **Wii writability:** Fuse previously skipped `access()` under `GEKKO` and treated
  disk images as writable. The shared native backend now calls `access(W_OK)` on
  non-Windows platforms. Build and link with devkitPPC/libogc to determine whether
  that calls a usable implementation. If not, supply a Wii-native implementation
  or an explicit compatibility policy in the native backend. Do not bypass the
  writable callback in Fuse: a custom Wii backend must still be able to report a
  read-only image. Wii has not been verified in this checkout.

## Architecture and scope

**Libspectrum owns the single filesystem contract, backend registry, stream
lifetime rules and native implementation. Fuse consumes it.** This supersedes the
earlier proposal for a separate library table forwarding into a Fuse-owned table.

A platform installs its effective filesystem once. Both libraries and Fuse's
existing filename-taking operations use that same namespace and implementation.
There must not be two independently configured backends, two native defaults,
parallel mode/handle types, or synchronization of tables between the projects.

This session is the **libspectrum implementation**. Do not modify Fuse as part of
it. The current uncommitted Fuse prototype (`fuse-filesystem.h`,
`compat/unix/file.c`) demonstrates requirements, but is not an interface to copy
into the library while retaining both implementations. Its registry/native code
will be removed in the later Fuse integration. Libspectrum must not include a Fuse
header or otherwise depend on Fuse.

Keep existing filename APIs, codec behavior, format inference, retained names,
cache policy and caller workflows. Buffers remain normal internal codec machinery;
new per-format source/destination or snapshot-buffer APIs are not the solution.
Directory browsing, host environment/search-path providers, devices, asynchronous
IO and a general URI/resource framework are outside this first regular-file seam.

## One canonical public interface

Expose the contract through the normal installed libspectrum headers/exports.
Names such as `libspectrum_fs_*` would avoid confusion with the existing
`libspectrum_file` encoded/identified-data descriptor; exact names are flexible.

The shared interface must serve **Fuse consumers as well as library codecs**:

| Facility | Required semantics / actual consumer |
| --- | --- |
| Opaque handle | Not a public `FILE*`, native fd, or `libspectrum_file` descriptor. NULL denotes failed open. |
| Open modes | Read existing; write/create/truncate; read-write existing (`rb+`); append/create. One canonical mode type, no libc mode-string dependency. |
| Read/write some | Requested byte count plus transferred output count, never exceeding the request. Short success and EOF/zero progress differ from error. Errors can follow partial transfer; do not retry as success. |
| Exact read/write | Shared helpers loop successful progress, fail at EOF/stall/error. Do not require every Fuse codec to implement its own transfer loop. |
| Length | Signed 64-bit byte length, with documented failure. Query must not unexpectedly reposition the stream. |
| Seek/tell | Checked signed 64-bit byte offsets/positions and canonical start/current/end origins. No narrowing through `long` in codecs/backing stores. Reject unsupported positions rather than truncate. |
| Flush | Report output finalization errors; needed by WAV and PNG/output adapters. |
| Close | Consumes a handle exactly once, including on failure. Callers must never retry close. |
| Exists/unlink | Shared namespace operations already used by Fuse related-file lookup and cleanup; no native probe fallback. |
| Writable | Query an existing logical file's write permission for Fuse disk policy, without opening/truncating it or consulting native `access()` behind the backend. |

Use one documented callback/error convention consistent with libspectrum. The
error result, not POSIX errno alone, is authoritative; native implementations can
still provide native diagnostics. Counts must be initialized/validated on every
transfer, including failures. Optional unsupported operations must fail explicitly,
not fall through to native IO; a missing writable query must not imply writable.

Provide an explicit backend context/userdata so a platform implementation need
not use its own global variables. Copy the callback table, borrow backend state
for its documented lifetime, and retain no transient caller-owned table pointer.

### Backend installation and lifetime

- Default to native IO for existing standalone library users/tools. Resetting to
  native is explicit, not a fallback after custom backend failure.
- Reject an incomplete table without changing the installed backend.
- Reject replacing/resetting a backend while any library-filesystem handle or
  callback is active. This includes handles opened by **Fuse**, not only internal
  IDE/WAV handles: all users go through the same library entry points/counters.
- Names/data passed to synchronous methods are borrowed during those calls.
  Retained filenames remain logical names within that namespace; retained state
  owns copies where it already did so.
- Keep backend context valid through the last close. Failed opens/inserts must
  clean up their handles; error cleanup must not close a handle twice.
- State the threading contract. Owner-thread/non-concurrent configuration is
  sufficient for this work; do not promise a thread-safe process-wide swap.
- Do not silently reset the platform backend in `libspectrum_init`/`end` or make
  codec teardown invalidate unrelated Fuse file handles. Fuse needs filesystem
  use across startup/configuration and shutdown. Document whether filesystem
  setup/use is independent of codec initialization; avoid a lifecycle cycle.

The native implementation may use stdio internally, but those types and platform
size/seek limitations must not escape into the shared contract. The custom backend
must never be bypassed by an unimplemented method or a failed lookup.

## Route existing libspectrum consumers

### `file.c`

Route `libspectrum_file_load_path` through the new open/length/read/close methods.
Keep identification, decompression, storage classification and owned-data rules
unchanged. Existing take-data/read-buffer helpers remain valid internal machinery.
They are not replacement platform APIs.

### `ide.c`, `internals.h`, shared MMC drive

`libspectrum_ide_insert_into_drive` opens `rb+`, reads an HDF header and retains a
`FILE*` in `libspectrum_ide_drive.disk`. Sector reads/cache commits seek/read/write
later; eject closes it. MMC uses this same drive/backing implementation.

Replace the internal stream with the canonical opaque handle. Preserve HDF header
and data offsets, geometry, sector packing/size, insert/eject ordering and cache
ownership/sharing. Compute sector byte positions using checked 64-bit arithmetic
before seek; current code uses `long` and narrower multiplication. Failed insert
consumes its new handle; eject/teardown consumes the retained handle once.

**Separate repair:** Failed cached writes are not reliably reported to Fuse.
Filesystem routing does not change cache, commit, or eject policy.
Preserve/document existing conventions unless it is explicitly undertaken
separately; do not claim durable/reliable commit merely because the callback
interface can report errors.

### `wav_pcm.c`, `wav.c`, `wav_internals.h`

`wav.c` tape loading delegates to the PCM reader. Route the reader's RIFF scan,
chunk selection, seek/tell/length, sample reads and close through the filesystem.
Keep supported PCM formats, validation, tape conversion, timing and filename APIs
unchanged. No native reopen or temporary-file materialization for custom names.

### `wav_write.c`

Route the long-lived PCM writer through the same interface: open, incremental
writes, header-rewrite seek, flush and close. Preserve lifetime and existing
conventions. Distinguish short progress from errors, including header/flush/close
failures. Preserve intentional stdout output for NULL names explicitly; do not
pass a native stdout `FILE*` to callbacks expecting an opaque custom handle.

## Validation in the library session

Use existing filename-taking APIs with a RAM backend whose handles are not FILEs:

1. Native defaults retain existing behavior and pass the existing suite.
2. A custom namespace cannot fall back to a real host file with the same name.
3. Shared public file methods exercise modes, successful short transfers, EOF,
   stalls, invalid counts, length, seek/tell, flush, permission, exists and unlink.
4. Table copying, invalid installation, callback-time replacement and open-handle
   replacement rejection work for external clients as well as internal codecs.
5. Generic path loading/compressed identification/metadata remain equivalent.
6. HDF insert/sector read-write/cache commit/eject and MMC shared backing use the
   backend throughout; test failure cleanup and retained handle lifetimes.
7. WAV tape readers and PCM writers round-trip existing fixtures, including seeked
   RIFF layouts, partial IO and finalization errors.
8. No implicit init/end reset of the installed platform filesystem; all error
   paths consume handles once, including failed insert and failed close.

No allocation/leak/durability/threading/full-codec claim beyond actual evidence.

## Later Fuse integration — not work for this library session

1. Remove Fuse's provisional backend registry/native implementation. Platform code
   installs the **library** backend directly, once.
2. Replace `fuse-filesystem.h` with canonical library types/declarations, or retire
   it. Where compatibility names remain, use aliases to library types/constants,
   not a second vtable definition, registry or mode-translation model.
3. Reduce `compat_file_*` to thin library calls where still useful, or call the
   library methods directly. Drop Fuse's separate counters/lifetime enforcement.
   Do not leave a library-to-Fuse-to-library forwarding cycle.
4. Reuse the RAM example, unchanged operation flows and failure/lifetime tests
   against the library-owned filesystem. Fuse's binary floppy/PNG/config/PSG and
   common IO must share it with IDE/MMC/WAV, including all open-handle pinning.
5. Remove temporary custom-filesystem refusals for WAV/HDF/IDE/MMC only after the
   relevant library paths are routed. Current guards are in `tape.c`, `utils.c`,
   `peripherals/ide/ide.c`, `zxcf.c`, `zxmmc.c`, and `divmmc.c`.

Fuse retains media admission, autoload/replacement/save policy, related-name
lookup/search paths, ROM provenance and UI decisions. They do not move into the
filesystem layer. Fuse-owned FMF, printer, SVG, profile and disk text-log writers now use the
compatibility calls and will follow them to the library-owned backend at cutover.
They remain Fuse features. Directory/path/environment/UI providers remain
platform services.

<!--
Copyright (c) 2026 Fredrick Meunier
This document is free software; you can redistribute it and/or modify it under
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later version.
It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this
program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street,
Fifth Floor, Boston, MA 02110-1301 USA.
-->

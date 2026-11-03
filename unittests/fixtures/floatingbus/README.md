# Optional +2A/+3 floating-bus TAP fixtures

Place the original programs in this directory using these exact filenames:

| Local filename | Original program | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| `hikaru.tap` | Hikaru/Intense +2A/+3 floating-bus test v1.2 (2017) | 1148 | `48dd8eb1eb7b92e2fc7ee99b0065d7dd7da7fe87d188447795df0072e111a6b9` |
| `universal.tap` | `Plus 2A test 2.tap` from the Universal attachment | 127 | `d703d838adba23eafdd4e0457486ce1318e488667ebb12a96f72ec5e06114b10` |
| `woody.tap` | `Floating Bus Test (Universal) 2.tap` from Woody's Special | 387 | `b80c4ab7690434451fcb0c15d0af7975c7808e22e562b14186008666f4628bf3` |

A null-UI build's `make check` or `./fuse --unittests` discovers these files
automatically. No per-TAP environment variables are required or consulted.
Missing files are reported as optional skips independently; an unreadable or
hash-mismatched file fails its test. Present programs run on +2A, +3 and +3e.

The test suite uses its existing `FUSE_TEST_SRCDIR` source-root convention;
`make check` sets it automatically, including out-of-tree builds. Direct runs
from the source root need no environment variables. Generated screenshots,
snapshots and CSVs default to `unittests/floatingbus-output/` relative to the
working/build directory, which is created automatically. The optional
`FUSE_TEST_FLOATING_VISUAL_OUTPUT` variable overrides only that artifact directory.
Hikaru captures are named `hikaru-<machine>-<stage>.scr` there.

## Provenance and distribution

- Hikaru: https://github.com/redcode/ZXSpectrum/wiki/2A-3-Floating-Bus-Test
- Original attachment discussion: https://sourceforge.net/p/fuse-emulator/bugs/360/

These are third-party programs, not Fuse-generated test data. Their hashes pin
the researched versions; they do not grant redistribution rights. TAP files
are ignored locally and are not included in the source distribution. Resolve
permission before adding any of them to version control or release archives.

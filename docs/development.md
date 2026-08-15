# Development

## Host environment

* The modern Linux machine is the primary source-control and test
  environment: repository history, CI, and unit tests for the portable core.
* Classic Mac-specific builds will initially happen on the Power Mac G4
  (Metrowerks CodeWarrior), not cross-compiled on Linux.
* The portable core (`core/`) must compile and pass tests on Linux with no
  Classic Mac SDK present. Target-specific code (`classic/`, `oms/`,
  `freemidi/`, `probe/`) is deliberately not part of the Linux build.

## Building and testing

```sh
make                # portable core library + test binary
make test           # run unit tests (default compiler: cc)
make test CC=clang  # run unit tests with Clang
make test-sanitize  # run tests under AddressSanitizer + UBSan
make clean
```

Flags: `-std=c89 -Wall -Wextra -Wpedantic -Werror -O2`. CI runs GCC and
Clang jobs plus a sanitizer run on ubuntu-latest.

## Code conventions (portable code)

* Conservative C89/C90: no C99 for-loop declarations, no VLAs, no `//`
  comments in portable code, no compiler-specific extensions.
* Descriptors and packets are untrusted byte streams: every parser takes a
  buffer plus an explicit length, validates before reading, rejects
  zero-length/truncated input, and never walks past the buffer.
* USB multi-byte fields are explicitly little-endian; the PowerPC G4 target
  is big-endian, so no host-order assumptions and no packed-struct casting.
* No fabricated Classic Mac OS, OMS, or FreeMIDI APIs. Unverified interfaces
  are documented TODOs (see `docs/research.md`).

## Classic Mac specifics

* CodeWarrior project files and resources may carry Classic Mac metadata and
  resource forks; handle them carefully and never convert them casually.
* Keep source files ordinary portable text (LF line endings, no BOM)
  wherever possible.
* The repository documents where external SDK material (Apple USB DDK, OMS
  SDK, FreeMIDI SDK) must be obtained; do not commit proprietary material.

# Development

## Host environment

* The modern Linux machine is the primary source-control and test
  environment: repository history, CI, and unit tests for the portable core.
* Classic Mac-specific builds happen on the Power Mac G4 (Metrowerks
  CodeWarrior), not cross-compiled on Linux. The real target environment
  is now proven (see below).
* The portable core (`core/`) must compile and pass tests on Linux with no
  Classic Mac SDK present. Classic-target code (`classic/`, `probe/`) is
  not built into the Linux test binary but IS compile-checked against
  minimal stub headers (`classic/host-check/`) via `make check-classic`;
  `oms/` and `freemidi/` remain reserved placeholders.

## Building and testing

```sh
make                # portable core library + test binary (incl. ring)
make test           # run unit tests (default compiler: cc)
make test CC=clang  # run unit tests with Clang
make test-sanitize  # run tests under AddressSanitizer + UBSan
make check-classic  # compile-check classic/usb_driver.c + probe/probe.c
                    # against classic/host-check/ stub headers
make clean
```

Flags: `-std=c89 -Wall -Wextra -Wpedantic -Werror -O2` for the portable
core; `check-classic` uses `-Wall -Wextra -Werror` without `-Wpedantic`
(Classic sources carry CodeWarrior `\p` strings — resolved by the
`__MWERKS__` conditional — and the 32-bit `Ptr`<->`UInt32` alignment
idiom). CI runs GCC and Clang jobs plus a sanitizer run and
`check-classic` on ubuntu-latest.

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

## Real target environment (proven on hardware)

```text
Host (Linux development server):  10.0.3.200, Ubuntu — repository served
                                   over AFP (Netatalk) to the G4
Target:                           Power Mac G4, Mac OS 9
Toolchain:                        CodeWarrior Pro 5.3 (IDE 4.0.4)
Headers:                          Universal Interfaces & Libraries 3.3.x
                                  + Apple Mac OS USB DDK 1.4.1
```

* The Git working tree is exported over AFP and mounted live on the G4:
  editing `classic/` or `probe/` on Linux is immediately visible to the
  Mac build. The live CodeWarrior project area (`USBMIDI9/` in the tree)
  is **not versioned** (gitignored): it holds the `.µ` project files,
  per-target settings, build products, and Apple/Metrowerks sample
  material. Do not edit `.µ`/`.stg`/`.tdt` files from Linux.
* CodeWarrior project files and resources carry Classic Mac metadata and
  resource forks; handle them carefully and never convert them casually.
* Real build results (M1B): the driver builds with 0 errors / 43
  warnings as an `ndrv`/`usbd` shared library with the expected exports;
  the Probe builds as a Std C Console (SIOUX) PPC application and
  launches on Mac OS 9. See `docs/classic-usb-driver.md` §9.5/§9.7.
* Classic-target sources use **logical include names** (`<MacTypes.h>`,
  `"usbmidi9_dispatch.h"`) resolved through CodeWarrior Access Paths —
  never Unix-style relative paths (`../`, `classic/...`). The Linux
  `check-classic` target mirrors the G4 access paths via
  `-Iclassic -Iclassic/host-check`.
* Keep source files ordinary portable text (LF line endings, no BOM)
  wherever possible.
* The repository documents where external SDK material (Apple USB DDK, OMS
  SDK, FreeMIDI SDK) must be obtained; do not commit proprietary material.

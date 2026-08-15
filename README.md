# USBMIDI9

Generic USB-MIDI 1.0 class support for Classic Mac OS 9.

**The project is in early development. No working Mac OS 9 driver has been
released yet.**

USBMIDI9 aims to provide a generic USB-MIDI 1.0 class transport for Mac OS 9:
a portable, standards-oriented core plus a Classic Mac OS USB transport,
exposed to MIDI systems first through OMS and later through FreeMIDI. A
diagnostic utility, "USBMIDI9 Probe", is planned.

## Goals

* Generic USB-MIDI 1.0 class support for Mac OS 9, not a device-specific driver.
* A portable C core: USB-MIDI descriptor parsing, Event Packet (CIN)
  decoding, and logical port/cable discovery, testable on Linux.
* A clean internal service boundary between the transport and MIDI-system
  integrations (OMS first, FreeMIDI later).
* USBMIDI9 Probe: a diagnostic tool to inspect devices, descriptors, and
  live USB-MIDI traffic on the Power Mac G4.

## Current status

* M0 (repository and portable core) is in progress: the portable packet
  decoder, safe descriptor walker, and MIDIStreaming topology parser exist
  with host tests; CI builds with GCC and Clang.
* The Classic Mac OS USB layer, the service boundary, the OMS driver, the
  FreeMIDI driver, and the probe are reserved placeholders. They will not be
  implemented until the relevant historical APIs are verified from primary
  Apple/Opcode/MOTU documentation (see `docs/research.md`).
* No hardware has been demonstrated on Mac OS 9 yet.

## Hardware

The first validation device is the **Evolution eKeys-49 / Keystation 49e**
(VID:PID `0a4d:0090`), a class-compliant USB-MIDI 1.0 keyboard. It is a
validation target, not a hardcoded supported device; the portable core matches
any interface with `bInterfaceClass = 0x01` (Audio) and
`bInterfaceSubClass = 0x03` (MIDIStreaming). See `docs/hardware.md`.

## Repository layout

```text
core/       portable USB-MIDI core (descriptors, packets, ports) - host-testable
classic/    Classic Mac OS USB transport + service boundary (reserved)
oms/        OMS integration driver (reserved)
freemidi/   FreeMIDI integration driver (reserved)
probe/      USBMIDI9 Probe diagnostic utility (reserved)
tests/      host test suite for the portable core
fixtures/   device data fixtures (Keystation 49e)
docs/       architecture, development, hardware, research, roadmap
```

## Building and testing on Linux

The portable core and its tests build on any Linux machine with a C89
compiler; no Classic Mac OS SDK is needed.

```sh
make            # build the portable core library and the test binary
make test       # build and run the unit tests
make test-sanitize   # build and run under AddressSanitizer + UBSan
make clean
```

The default compiler is `cc`; use `make test CC=clang` for Clang. CI (GitHub
Actions) runs both GCC and Clang jobs plus a sanitizer run. See
`docs/development.md` for details.

## Classic Mac OS 9 target

Classic Mac components will initially be built on the Power Mac G4 (e.g.
Metrowerks CodeWarrior), not cross-compiled on Linux. The target-specific
layers (`classic/`, `oms/`, `freemidi/`) are intentionally not compiled in the
Linux build. Before any Classic USB, OMS, or FreeMIDI code is written, the
exact APIs must be verified from primary historical documentation; open
research questions are tracked in `docs/research.md`.

## Contribution expectations

* Do not fabricate Classic Mac OS, OMS, or FreeMIDI APIs. If an API has not
  been verified from primary sources, leave a documented TODO instead.
* Portable code stays conservative C89/C90, decodes USB fields explicitly
  little-endian, and treats descriptors/packets as untrusted buffers.
* Do not commit proprietary SDK material (Apple DDK, OMS SDK, FreeMIDI SDK,
  vendor driver code). Document where external SDKs must be obtained.
* Host tests must pass (`make test`) before committing.

## License

MIT. See `LICENSE`. Copyright (c) 2026 Vadim Petrov.

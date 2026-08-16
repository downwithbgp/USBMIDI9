# USBMIDI9

USBMIDI9 is an open-source USB-MIDI 1.0 class driver for Classic Mac OS 9.
It matches any standards-compliant USB-MIDI interface (interface class 1,
subclass 3 — no vendor/product restrictions), delivers received MIDI to
OMS, and is designed so a FreeMIDI driver can be added against the same
internal service.

Current status:

- **M1B hardware gate PASSED** on a real Power Mac G4 (CodeWarrior Pro
  5.3, Mac OS 9): the driver matched the MIDIStreaming interface, and
  real Keystation 49e Note On/Off packets (`09 90 30 50` / `09 90 30 00`)
  were received through the ring buffer and the dispatch API.
- **M4 OMS source gate**: an OMS driver shim (`'OMdv'` file, creator
  `'USM9'`) implementing the verified Opcode OMS driver contract exists
  with host tests. It has NOT yet been run inside OMS on the G4 — that is
  the next hardware gate.
- MIDI output is not implemented yet (no USB bulk-OUT path; the OMS send
  hook drops and counts).
- FreeMIDI: research only (file format verified; driver protocol not
  authenticated — no FreeMIDI SDK found).
- A known hot-plug freeze exists; see `docs/classic-usb-driver.md` §9.9.

One device (M-Audio / Evolution Keystation 49e) has been validated on one
G4; that is not universal compatibility. See the user-facing `Read Me`
for the tested configuration.

## Layout

```text
core/       portable USB-MIDI core: descriptors, event packets, logical
            ports, and the stream <-> message converter (host-tested)
classic/    the Classic USB transport: class driver, ring, dispatch ABI
oms/        OMS driver shim (source gate; uses the dispatch API)
freemidi/   FreeMIDI shim (reserved; research in docs/)
probe/      USBMIDI9 Probe diagnostic console utility
docs/       architecture, research, distribution, roadmap, G4 notes
spec/       milestone specs (m1b, m4-oms ...)
```

## Building and testing on Linux

The portable core and its tests build with any C89 compiler; the Classic
sources are compile-checked against stub headers (`make check-classic`);
real builds happen in CodeWarrior on the Power Mac G4.

```sh
make                # build the portable core and the test binary
make test           # run the unit tests
make test-sanitize  # run under AddressSanitizer + UBSan
make check-classic  # compile-check the Classic sources
make clean
```

`make test CC=clang` uses Clang. CI runs GCC and Clang plus a sanitizer
run.

## Documentation

- `Read Me` — the user-facing manual (period style; shipped with the
  release).
- `docs/architecture.md` — the layered design.
- `docs/research.md` — historical research and provenance; the OMS driver
  API is verified from the Opcode OMS 2.0 SDK, the OMS spec, and period
  binaries (material stays outside the repo, per `~/research`).
- `docs/freemidi-driver-research.md` — FreeMIDI findings and open ABI
  questions.
- `docs/classic-usb-driver.md` — the Classic USB driver research and the
  real-G4 hardware log.
- `docs/distribution.md` — the period-correct release layout.
- `docs/ROADMAP.md` — milestones and the acceptance matrix.

## License

MIT. See `LICENSE`. Copyright (c) 2026 Vadim Petrov.

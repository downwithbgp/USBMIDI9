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
- **M4 OMS**: the OMS driver shim (`'OMdv'` file, creator `'USM9'`)
  implements the verified Opcode OMS driver contract, with host tests, and
  the PPC/CFM entry path is proven on the G4 — **PPCC entry gate CLOSED**
  (G4 runtime PASS, 2026-08-18: OMS calls the PPC main and handles the
  failed-init path cleanly; `docs/g4-handoff.md`). The production
  `add1Device` UPP crash found by the first production run is root-caused
  and fixed (`spec/add1device-upp-fix/`). The next hardware gate is the
  post-fix production run: OMS Setup discovery plus MIDI delivery.
- MIDI output is not implemented yet (no USB bulk-OUT path; the OMS send
  hook drops and counts).
- FreeMIDI: research only. The outer 68K driver ABI is recovered from the
  authenticated FreeMIDI 1.45 corpus (`docs/freemidi-driver-abi.md`); the
  remaining message/record semantics are unresolved, and no implementation
  is started until they are.
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
run, the RE-tool smoke tests and the trace gate (`make check-re-tools`,
`make check-trace`), and the `pefcheck` crate (`cargo test`, `fmt`,
`clippy`).

## Documentation

- `Read Me` — the user-facing manual (period style; shipped with the
  release).
- `docs/architecture.md` — the layered design.
- `docs/research.md` — historical research and provenance; the OMS driver
  API is verified from the Opcode OMS 2.0 SDK, the OMS spec, and period
  binaries (material stays outside the repo, per `~/research`).
- `docs/freemidi-driver-research.md` — FreeMIDI findings and open ABI
  questions.
- `docs/freemidi-driver-abi.md` — the recovered FreeMIDI 1.45 driver ABI
  and the remaining semantic questions; `docs/freemidi-driver-abi-evidence.md`
  is the raw byte/offset ledger behind it.
- `docs/classic-usb-driver.md` — the Classic USB driver research and the
  real-G4 hardware log.
- `docs/distribution.md` — the period-correct release layout.
- `docs/ROADMAP.md` — milestones and the acceptance matrix.

## License

MIT. See `LICENSE`. Copyright (c) 2026 Vadim Petrov.

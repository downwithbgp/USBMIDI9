# Architecture

USBMIDI9 is layered so that the portable, standards-oriented core has no
dependency on Classic Mac OS, OMS, or FreeMIDI.

```text
USB-MIDI 1.0 device
        |
        v
Classic Mac OS USB stack
        |
        v
classic/usb_driver.c      Classic Mac USB transport (M1B, G4-verified)
  - device/interface discovery and matching
  - descriptor acquisition
  - bulk endpoint I/O, completions, hotplug/removal
        |
        v
USBMIDI9DispatchTable + core/midi_stream   (M4 service boundary)
        +-------------------+
        |                   |
        v                   v
oms/oms_driver.c       freemidi/freemidi_driver.c (reserved)
        |                   |
        v                   v
       OMS               FreeMIDI
```

## Portable core (`core/`)

Host-testable C89 code, built and tested on Linux:

* `packets.{h,c}` — USB-MIDI 1.0 Event Packets: CIN table, decode, encode.
* `descriptors.{h,c}` — bounds-checked walker over arbitrary USB descriptor
  buffers with explicit little-endian field readers; no packed-struct casts.
* `ports.{h,c}` — MIDIStreaming topology: interface matching
  (`bInterfaceClass = 0x01`, `bInterfaceSubClass = 0x03`), MS header,
  MIDI IN/OUT jacks, class-specific endpoint associations, and derived
  logical ports (endpoint + cable + embedded jack).

## Classic Mac layer (`classic/`)

M1B: the interface class driver (`usb_driver.c`), the resident byte ring
(`ring.c`), and the `USBMIDI9DispatchTable` ABI (`usbmidi9_dispatch.h`)
are implemented and compile-checked on Linux against stub headers
(`make check-classic`); the real build happens in CodeWarrior on the
Power Mac G4. Implemented against APIs verified from Apple USB
DDK/SDK documentation (see `docs/research.md`); markers of the form

```c
/* TODO(classic-usb-api): ... */
```

remain for anything not yet verified.

## Service boundary

The internal API through which MIDI-system integrations consume USB-MIDI
devices. Since M4 it consists of two verified pieces:

- `USBMIDI9DispatchTable` (classic/usbmidi9_dispatch.h, version 0x0001):
  enumeration, interface info, and polled dequeue of raw bytes — the
  transport-facing half.
- `core/midi_stream.{h,c}`: the neutral USB-MIDI stream <-> conventional
  MIDI message converter (SysEx continuation handling, cable numbers,
  malformed-CIN policy) — host-tested, shared by the OMS shim and any
  future FreeMIDI shim.

The binary/API mechanism is the driver-exported dispatch table located via
`USBGetNextDeviceByClass` + `FindSymbol` (the same mechanism the Probe
uses and Opcode's own OMS USB Manager uses); see `docs/research.md`.

## OMS and FreeMIDI (`oms/`, `freemidi/`)

`oms/` is the OMS driver shim: a classic `'OMdv'` OMS driver (creator
`'USM9'`, `'OMdi'` 128 params, `'OMdv'` 128 PEF code resource) consuming
`USBMIDI9DispatchTable` + `core/midi_stream`; source gate complete, G4
hardware gate pending. It contains no USB data-path code.

`freemidi/` remains reserved: the FreeMIDI driver file format is verified
(`'DDef'`/`'IDvr'`, `'Code'`/`'DDef'` resources) but the driver message
protocol is not authenticated — see `docs/freemidi-driver-research.md`.

## Probe (`probe/`)

M1B source: diagnostic console utility for the Power Mac G4 — locates the
USBMIDI9 dispatch table, displays attached interface information, and
prints live received bytes in hex (see `probe/README.md`). Built on the G4
(CodeWarrior console app); compile-checked on Linux via `make
check-classic`. Descriptor inspection and decoded-MIDI display are planned
later (post-M1B).

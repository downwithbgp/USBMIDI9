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
classic/usb_driver.c      Classic Mac USB transport (reserved)
  - device/interface discovery and matching
  - descriptor acquisition
  - bulk endpoint I/O, completions, hotplug/removal
        |
        v
classic/usb_service.c     USBMIDI9 service boundary (reserved)
  - internal API for MIDI-system consumers
        |
        +-------------------+
        |                   |
        v                   v
oms/oms_driver.c       freemidi/freemidi_driver.c
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

Reserved. The internal API through which MIDI-system integrations consume
USB-MIDI devices. Conceptually it will provide:

* enumerate devices
* enumerate logical input/output ports
* open input / open output
* receive MIDI events / send MIDI data
* close port
* device attach/remove notifications

The actual binary/API mechanism (Component Manager, driver control/status
calls, Gestalt, shared service, callback table, or something else) is an open
architectural question — see `docs/research.md`. It will not be chosen merely
because it resembles a modern Unix/macOS API.

## OMS and FreeMIDI (`oms/`, `freemidi/`)

Reserved. OMS integration comes first; FreeMIDI later. Neither may be
embedded in the portable transport.

## Probe (`probe/`)

M1B source: diagnostic console utility for the Power Mac G4 — locates the
USBMIDI9 dispatch table, displays attached interface information, and
prints live received bytes in hex (see `probe/README.md`). Built on the G4
(CodeWarrior console app); compile-checked on Linux via `make
check-classic`. Descriptor inspection and decoded-MIDI display are planned
later (post-M1B).

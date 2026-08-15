# Research checklist

Unresolved historical research questions. **Do not fill these with guesses.**
Each item should be resolved only against primary historical documentation
with provenance recorded (see "Source standards" below).

## Classic Mac OS 9 USB

- [ ] Exact Apple USB DDK/SDK release that targets Mac OS 9, and where it can
      be obtained today (provenance to be recorded).
- [ ] USB device/interface matching model: how a Classic driver/system
      extension attaches to a device or interface.
- [ ] USB bulk transfer API: opening pipes, read/write calls, max packet
      handling.
- [ ] Asynchronous completion mechanism: completion routines, I/O queues,
      interrupt-time constraints.
- [ ] Device insertion/removal notification mechanism.
- [ ] System extension packaging/resource requirements (CFM, resource types,
      etc.).
- [ ] Memory/lifetime constraints for callbacks and buffers.

## OMS

- [ ] Exact OMS SDK/API version and its availability (Opcode).
- [ ] OMS driver discovery/loading mechanism: how OMS finds third-party
      drivers.
- [ ] OMS MIDI receive/send semantics: buffers, scheduling, port model.
- [ ] OMS timing semantics: driver timing model, clock resolution.
- [ ] OMS SDK/header redistribution terms.

## FreeMIDI

- [ ] Exact FreeMIDI SDK/API (MOTU) and its availability.
- [ ] FreeMIDI driver discovery/loading mechanism.
- [ ] FreeMIDI MIDI receive/send and timing semantics.
- [ ] FreeMIDI SDK/header redistribution terms.

## Service boundary design

- [ ] Best IPC/service mechanism between the USBMIDI9 extension and MIDI-
      system adapters: Component Manager, driver control/status calls,
      Gestalt mechanism, shared service, callback table, or another design.
      Do not choose a mechanism merely because it resembles a modern Unix or
      macOS API.
- [ ] Whether the mechanism must be a CFM shared library, a system extension,
      or both.

## Period drivers

- [ ] Behavior of existing period USB MIDI drivers (MOTU, Roland, Yamaha,
      Opcode, etc.): how they enumerate, how they present ports to OMS and
      FreeMIDI, and how they handle hotplug.

## Source standards

1. Prefer original Apple developer documentation and SDK headers.
2. Prefer original Opcode material for OMS.
3. Prefer original MOTU material for FreeMIDI.
4. Contemporary manuals from hardware vendors are useful secondary evidence.
5. Archive mirrors are acceptable when originals are unavailable; record
   provenance.
6. Do not treat modern macOS documentation as evidence for Classic Mac OS 9
   APIs.
7. Record sources here as they are found.

## Notes

* Nothing in `classic/`, `oms/`, or `freemidi/` may reference an API that is
  not verified against a real historical declaration or documentation source.
* Marker convention in source: `TODO(classic-usb-api)`, `TODO(oms-api)`,
  `TODO(freemidi-api)`, `TODO(classic-service-api)`.

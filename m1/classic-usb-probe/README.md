# M1 — Classic USB Probe (workspace)

M1A (research/design gate) output lives in `docs/classic-usb-driver.md`.
This directory is the workspace for M1 development.

## Status

**M1A complete** (research + design, verified against the authentic USB
DDK 1.4.1 kit; see `docs/classic-usb-driver.md`).

**M1B source gate complete, hardware gate NOT done.** The interface class
driver and the Probe are implemented as source:

* `classic/usb_driver.{h,c}` — generic interface driver (class 0x01,
  subclass 0x03, no VID/PID), per-interface instances, refcon state
  machine (configure -> find bulk IN -> MaxPacketSize -> resident
  buffers -> async read loop), removal handling, exports
  `TheUSBDriverDescription`, `TheClassDriverPluginDispatchTable`,
  `USBMIDI9DispatchTable`.
* `classic/ring.{h,c}` — fixed resident byte ring (host prop-tested).
* `classic/usbmidi9_dispatch.h` — the versioned dispatch ABI.
* `probe/probe.c` — Mac OS 9 console probe: locates the dispatch table,
  displays interface info, polls/dequeues received bytes, prints hex.
* `codewarrior/USBMIDI9.exp` — linker export list.
* Host gates: `make test`, `make test-sanitize`, `make check-classic`
  (compile-check of the Classic sources against stub headers).

The M1B goal remains:

```text
Keystation 49e
   -> Classic Mac USB stack
   -> USBMIDI9 interface class driver
   -> find bulk IN pipe for endpoint 0x81
   -> asynchronous read
   -> receive real four-byte USB-MIDI Event Packet
   -> make packet observable to the Probe
```

Example eventual observation: `09 90 3C 57`. No OMS is required for M1.

## M1B remaining work — hardware gate (NOT done)

Source alone does not complete M1B. Final acceptance (definition of done
item 11) requires, on the Power Mac G4:

1. Build the driver with CodeWarrior (shared library, 'ndrv'/'usbd',
   exports from `codewarrior/USBMIDI9.exp`, link USBServicesLib) and
   install it in the Extensions folder.
2. Build the Probe as a CodeWarrior console app (SIOUX) linking
   InterfaceLib + USBManagerLib.
3. Boot Mac OS 9 with the driver installed; attach the Keystation 49e.
4. Verify the driver loads for the MIDIStreaming interface and the Probe
   prints real received bytes in hex (e.g. `09 90 3C 57`).
5. Verify hot unplug/replug: no crash, driver finalizes, replug restores
   data flow.

Full checklist: `docs/classic-usb-driver.md` §9.5.

## Anti-hallucination rules in force

- No IOKit / IOUSBDeviceInterface / CoreMIDI / DriverKit — those are Mac
  OS X APIs and are rejected for this target.
- No invented OMS/FreeMIDI APIs. No hardcoded Keystation VID/PID in the
  driver (generic class 0x01/subclass 0x03 interface match only).
- Every API used must trace to a primary source (see
  `docs/classic-usb-driver.md` source table; the M1B code was written
  against patterns re-verified from the actual DDK 1.4.1 kit).

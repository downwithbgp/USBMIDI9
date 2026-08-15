# Research checklist

Unresolved historical research questions. **Do not fill these with guesses.**
Each item should be resolved only against primary historical documentation
with provenance recorded (see "Source standards" below).

## Classic Mac OS 9 USB

Primary source obtained: **Apple, *Mac OS USB DDK API Reference*,
Preliminary Working Draft, Revision 26, 12/23/99** (from Apple's own
archive; local research copy outside the repository). Verified findings and
citations live in `docs/classic-usb-driver.md`. Remaining open items:

- [ ] Apple USB DDK media/headers (USB.h, USBClassDriver.h, USBDriver.h,
      USBServicesLib import library, sample class drivers) — USB DDK 1.4.2
      media not yet obtained; leads: Macintosh Garden / period Apple
      developer CDs. Do not commit to the repository (proprietary).
- [x] USB device/interface matching model — verified (Rev 26 Ch 4,
      p. 54-59; generic interface matching for class 0x01/subclass 0x03 in
      `docs/classic-usb-driver.md` §1.4, §5.1).
- [x] USB bulk transfer API — verified (USBBulkRead/USBBulkWrite, USBPB,
      Rev 26 Ch 5, p. 128-129).
- [x] Asynchronous completion mechanism — verified (completion routines at
      secondary interrupt or task level; USBPB residency; Rev 26 Ch 5,
      p. 101-102).
- [x] Device insertion/removal notification — verified
      (notificationProc/kNotifyDriverBeingRemoved + USBInstallDeviceNotification,
      Rev 26 Ch 4 p. 70-71, Ch 6 p. 185-187).
- [ ] System extension packaging/resource requirements — partially verified
      (CFM shared library, file type 'ndrv', creator 'usbd'; CodeWarrior
      Mac OS Merge); exact DDK-era CodeWarrior version and extension
      packaging details still need the DDK ReadMe.
- [x] Memory/lifetime constraints for callbacks — verified (USBAllocMem;
      USBPB must outlive the async call; finalize must have no pending
      calls; Rev 26 Ch 4 p. 86, Ch 5 p. 101, 150).
- [ ] USB software version on the target G4 (Mac OS 9.x) and any
      USB 1.4.2-specific changes not covered by Rev 26 (which documents up
      to 1.4; 1.3.5 current at 12/23/99).

## Service boundary design

- [x] Probe/driver communication mechanism for M1 — documented choice:
      driver-exported versioned dispatch table located via
      `USBGetNextDeviceByClass` + `FindSymbol`, hotplug via
      `USBInstallDeviceNotification` (Rev 26 Ch 4 p. 73-74, 83-85;
      Ch 6 p. 179, 185-187). See `docs/classic-usb-driver.md` §5.7.
- [ ] Whether the mechanism must be a CFM shared library, a system
      extension, or both — driver is a CFM 'ndrv'/'usbd' extension
      (verified); the OMS/FreeMIDI compatibility-shim form is deferred to
      M4/M6.
- [ ] OMS/FreeMIDI shim design — deferred (no OMS API design in M1A).

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

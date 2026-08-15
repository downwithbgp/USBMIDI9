# USBMIDI9 Probe

Diagnostic utility for the Power Mac G4 (Mac OS 9). It displays:

* detected USB-MIDI interfaces (VID/PID, interface class/subclass/protocol,
  bulk IN MaxPacketSize, queued bytes)
* live incoming USB-MIDI traffic as raw hex bytes (e.g. `09 90 3C 57`)

## Status

**M1B source complete; not yet built or run on hardware.** `probe/probe.c`
is a Mac OS 9 console application (CodeWarrior + SIOUX):

* locates the USBMIDI9 driver's exported `USBMIDI9DispatchTable` via
  `USBGetNextDeviceByClass` + `FindSymbol` (re-located on every poll so a
  table pointer can never dangle after the driver fragment unloads),
* prints the attached interface table when it changes,
* polls `dequeueBytes` per interface and prints received bytes in hex,
* quits on 'q'.

Planned later (post-M1B, requires the service boundary): decoded MIDI
messages, logical MIDI ports/cables, descriptor inspection, hotplug
notifications.

## Build (Power Mac G4)

CodeWarrior console app; link InterfaceLib and USBManagerLib; include the
Universal Headers + DDK USB.h; add `classic/` to the access paths for
`usbmidi9_dispatch.h`. On Linux, `make check-classic` compile-checks
`probe/probe.c` against the stub headers in `classic/host-check/`.

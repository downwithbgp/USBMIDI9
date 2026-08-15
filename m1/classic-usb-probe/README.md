# M1 — Classic USB Probe (workspace)

M1A (research/design gate) output lives in `docs/classic-usb-driver.md`.
This directory is the workspace for M1 development.

## Status

**M1A complete (research + design, no driver code yet).** The verified
Classic Mac OS USB driver model, the proposed M1 state machine, matching
descriptor, bulk-read and hot-unplug lifecycles, the Probe communication
mechanism, and CodeWarrior requirements are documented with citations in
`docs/classic-usb-driver.md`.

## M1B (next gate, NOT started)

Build the interface class driver and the USBMIDI9 Probe such that, on the
Power Mac G4:

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

## Hard prerequisites for M1B

1. Obtain the **USB DDK 1.4.2 media** (headers: USB.h, USBClassDriver.h,
   USBDriver.h; USBServicesLib import library; sample class drivers; DDK
   ReadMe) — local research material, NOT committed to the repository.
2. Verify against the real USB.h: `kClassDriverPluginVersion`,
   `kInitialUSBDriverDescriptor`, `kTheUSBDriverDescriptionSignature`,
   USBPB constants, error codes, and the notification-proc signature for
   the USB software version on the target G4.
3. Confirm the USB software version on the target Power Mac G4 and any
   USB 1.4.2-specific differences (Rev 26 documents up to 1.4).
4. Set up CodeWarrior on the G4 (shared-library project, 'ndrv'/'usbd'
   output, export list).

## Anti-hallucination rules in force

- No IOKit / IOUSBDeviceInterface / CoreMIDI / DriverKit — those are Mac
  OS X APIs and are rejected for this target.
- No invented OMS/FreeMIDI APIs. No hardcoded Keystation VID/PID in the
  driver (generic class 0x01/subclass 0x03 interface match only).
- Every API used must trace to a primary source (see
  `docs/classic-usb-driver.md` source table).

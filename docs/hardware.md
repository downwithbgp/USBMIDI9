# Hardware: Evolution eKeys-49 / Keystation 49e

First validation device. **This page records empirical first-device data
only.** The Keystation is a validation target, not a hardcoded design basis:
the portable core matches any MIDIStreaming interface
(`bInterfaceClass = 0x01`, `bInterfaceSubClass = 0x03`).

## Observed data

```text
Vendor:     Evolution Electronics Ltd.
Product:    eKeys-49 USB MIDI Keyboard / Keystation 49e
VID:PID:    0a4d:0090
USB:        1.00
Speed:      Full Speed
```

Interface 0:

```text
Class:       Audio (1)
Subclass:    AudioControl (1)
Endpoints:   0
```

Interface 1:

```text
Class:       Audio (1)
Subclass:    MIDIStreaming (3)
Endpoints:   2

Bulk IN:    address 0x81, max packet 64 bytes
Bulk OUT:   address 0x02, max packet 64 bytes
```

Descriptor topology (observed):

```text
Embedded MIDI IN Jack      ID 1
External MIDI IN Jack      ID 2
Embedded MIDI OUT Jack     ID 3, source Jack 2
External MIDI OUT Jack     ID 4, source Jack 1

Endpoint 0x81 -> associated embedded jack 3
Endpoint 0x02 -> associated embedded jack 1
```

This follows the standard USB-MIDI 1.0 descriptor model
(USB Device Class Definition for MIDI Devices, Release 1.0).

## Fixtures

* `fixtures/keystation-49e-lsusb.txt` — the text record above, formatted for
  easy diffing; clearly not a captured `lsusb -v` dump (fields marked
  "not observed" were not available).
* `fixtures/keystation-49e.bin` — reserved for a binary descriptor capture
  from the real device; currently empty.
* `tests/test_descriptors.c` — a synthetic descriptor buffer modeled on this
  topology (labeled as such in the source).

## Status

* Not yet validated on Mac OS 9: no working driver exists yet (M0).
* M1 (Classic USB probe) will enumerate this device on the Power Mac G4,
  inspect descriptors, open the MIDIStreaming interface, and read endpoint
  `0x81` — pending verified Classic USB APIs (see `docs/research.md`).
* No device-specific quirk code exists, and none should be added unless
  empirical testing on this device demonstrates a real need.

# USBMIDI9 Probe (reserved)

Planned diagnostic utility for the Power Mac G4 (Mac OS 9). It will
eventually display:

* detected USB-MIDI devices
* VID/PID
* descriptor information
* interfaces
* bulk endpoints
* logical MIDI ports/cables
* incoming/outgoing USB-MIDI packets
* decoded MIDI messages

Status: **reserved — no implementation yet.** The probe will link the
portable core (`core/`) for descriptor parsing and packet decoding; the
Classic Mac USB access layer it needs is not implemented (see
`docs/research.md`). Nothing here may use unverified APIs.

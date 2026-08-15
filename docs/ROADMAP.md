# Roadmap

Current status: **M1B source gate complete** (driver + probe source,
host compile-checked; hardware validation pending on the Power Mac G4).

## M0 — Repository and portable core

* scaffold repository, docs, license, CI
* USB-MIDI Event Packet decoder (CIN rules)
* safe descriptor parser
* MIDIStreaming topology / logical ports
* host tests, sanitizers, GCC + Clang CI

## M1 — Classic USB probe

* enumerate the Keystation 49e on Mac OS 9
* inspect descriptors
* open the MIDIStreaming interface
* read endpoint `0x81`
* display raw USB-MIDI traffic

**M1A complete** (research/design gate; `docs/classic-usb-driver.md`).
**M1B source complete** (interface class driver + probe source,
`classic/`, `probe/`, `codewarrior/USBMIDI9.exp`; host-compile-checked via
`make check-classic`). **M1B hardware gate NOT done** — acceptance
requires building on real CodeWarrior, booting Mac OS 9, attaching a real
Keystation, and receiving real USB bytes (`docs/classic-usb-driver.md`
§9.5 checklist).

## M2 — Generic USB-MIDI transport

* generic interface matching
* logical ports/cables
* input/output
* SysEx packetization/reassembly
* multiple devices

## M3 — USBMIDI9 service boundary

* establish a verified Classic-compatible consumer API

## M4 — OMS

* OMS discovers USBMIDI9 port(s)
* Keystation produces MIDI in OMS
* ReBirth receives keyboard input

## M5 — Compatibility testing

* test additional class-compliant devices
* document quirks
* improve hotplug/error handling

## M6 — FreeMIDI

* implement FreeMIDI integration against the same USBMIDI9 service

# Roadmap

Current status: **M1B hardware gate PASSED for matching, dispatch, and
bulk receive on the real Power Mac G4 (CodeWarrior Pro 5.3); one
unresolved defect: hard freeze on unrelated-device hot-plug while
USBMIDI9 is active (code audit done, hardware isolation pending —
`docs/classic-usb-driver.md` §9.9).**

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
`make check-classic`). **Real-target builds done**: driver 0 errors / 43
warnings on CodeWarrior Pro 5.3 (`ndrv`/`usbd`), Probe links and launches
(SIOUX console) — see `docs/classic-usb-driver.md` §9.7. **M1B hardware
gate done** — on the real G4 the driver matched the MIDIStreaming
interface, dispatched, and received real USB-MIDI packets
(`09 90 30 50` / `09 90 30 00`) without hanging
(`docs/classic-usb-driver.md` §9.5 checklist, §9.8). Open items: the
Keystation hot-unplug checklist entry, and the unrelated-device hot-plug
freeze audit (§9.9, plan in `spec/m1b-hotplug/tasks.md`).

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

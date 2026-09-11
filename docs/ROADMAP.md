# Roadmap

Current status: **M1B hardware gate PASSED** (real G4: matching, dispatch,
bulk receive — `docs/classic-usb-driver.md` §9.5/§9.8; one unresolved
defect: hard freeze on unrelated-device hot-plug while USBMIDI9 is active,
§9.9). **M4 OMS: research gate PASSED; OMS shim implemented and
host-tested; the G4 build/entry gates PASSED — the PPCC entry gate is
CLOSED (G4 runtime PASS, 2026-08-18), and the production `add1Device` UPP
crash found by the first production run is root-caused and fixed
(`spec/add1device-upp-fix/`).** The OMS receive-scheduling correction
(push callback + USB Manager device notifications, no poll timer, no
per-tick USB walk) is implemented and host-tested (`spec/oms-g4-audit/`).
The next hardware gate is the post-fix production integration run on the
G4 (OMS Setup discovery + MIDI delivery).

## M0 — Repository and portable core

* scaffold repository, docs, license, CI
* USB-MIDI Event Packet decoder (CIN rules)
* safe descriptor parser
* MIDIStreaming topology / logical ports
* host tests, sanitizers, GCC + Clang CI

**Complete.**

## M1 — Classic USB probe

* enumerate the Keystation 49e on Mac OS 9
* inspect descriptors
* open the MIDIStreaming interface
* read endpoint `0x81`
* display raw USB-MIDI traffic

**M1A complete** (research/design gate; `docs/classic-usb-driver.md`).
**M1B complete** (driver + probe source; real-target builds; **hardware
gate PASSED** — real packets `09 90 30 50` / `09 90 30 00` received).
Open: the Keystation hot-unplug checklist entry; hardware isolation of the
unrelated-device hot-plug freeze (`spec/m1b-hotplug/tasks.md`).

## M2 — Generic USB-MIDI transport

* generic interface matching — done (M1B)
* logical ports/cables — done (core/ports)
* input — done, G4-verified (M1B)
* output — **TODO** (bulk-OUT + `enqueueBytes`; part of the OMS output gate)
* SysEx packetization/reassembly — done (core/midi_stream)
* multiple devices — designed (dispatch table enumerates; OMS shim
  registers one device per interface)

## M3 — USBMIDI9 service boundary

* established (M4): `USBMIDI9DispatchTable` + `core/midi_stream`
  (`docs/architecture.md`)

## M4 — OMS

* OMS research gate — **PASSED** (primary sources; `docs/research.md`)
* OMS adapter logic — **implemented + host-tested** (omdv dispatch + device
  registration; receive scheduling corrected in the oms-g4-audit pass: push
  event callback + USB Manager device notifications, no Notification
  Manager timer, no per-tick USB walk)
* OMS real-target source gate — **entry path PASSED** (PPCC entry gate
  CLOSED, G4 runtime PASS 2026-08-18; production `add1Device` UPP defect
  root-caused and fixed, `spec/add1device-upp-fix/`)
* OMS G4 build/test — **build gates + entry diagnostics PASSED**; the
  post-fix production run is the next gate
* OMS Setup discovers USBMIDI9 — **hardware gate (next)**
* Keystation produces MIDI in OMS — **hardware gate**
* ReBirth receives keyboard input — **hardware gate**
* output — **UNVERIFIED** (send hook drops; needs enqueueBytes + bulk OUT)

## M5 — Compatibility testing

* test additional class-compliant devices
* document quirks
* improve hotplug/error handling

## M6 — FreeMIDI

* research — **expanded** (`docs/freemidi-driver-research.md`): the outer
  68K driver ABI is recovered from the authenticated FreeMIDI 1.45 corpus
  (`docs/freemidi-driver-abi.md` + `docs/freemidi-driver-abi-evidence.md`);
  the remaining message/record semantics are unresolved
* implementation — **blocked on the remaining protocol semantics**

## v0.1 acceptance matrix

USB CLASS DRIVER
  [PASS] generic Audio/MIDIStreaming matching
  [PASS] real bulk input
  [PASS] real Note On/Off reception
  [TODO] hot-plug stability
  [TODO] output

OMS
  [TODO] recognized by OMS Setup (G4 gate 1)
  [TODO] receives MIDI in an OMS application (G4 gate 2)
  [TODO] Keystation -> OMS -> ReBirth (G4 gate 3)
  [TODO] output (blocked on the USB bulk-OUT path)

FreeMIDI
  [research] file format verified; outer 68K driver ABI recovered from the
  1.45 corpus; message/record semantics unresolved — no implementation
  until then

DISTRIBUTION
  [TODO] resource forks preserved (StuffIt; docs/distribution.md)
  [TODO] clean-machine installation (G4)
  [TODO] uninstall (G4)
  [TODO] release archive expands correctly on OS 9 (G4)

Do NOT declare v0.1 released on the strength of host tests; the G4 gates
above are the release gates.

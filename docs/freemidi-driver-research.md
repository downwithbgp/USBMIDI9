# FreeMIDI driver research (M4)

Status: **research + design only — no implementation.** The FreeMIDI driver
*file format and system architecture* are verified from primary material; the
*driver message protocol* (what the FreeMIDI system sends to a driver's code
entry, the `'DDef'` params layout, the receive call) is NOT authenticated, and
per the session's non-negotiable rule we do not implement against guesses.

Primary material (full provenance in `~/research/oms/PROVENANCE.md`):

- **MOTU FreeMIDI 1.45 full installer** (Sept 2000; Macintosh Garden,
  `Install_FreeMIDI_1.45.sit`, MD5 `f6e55588fb68925fd74f34614d4b8f8d`),
  extracted with `unar` + `unvise`.
- **Roland SC-8850 USB FreeMIDI driver** (Nov 1999; archive.org
  `roland-sc-8850-driver-tools`, `sc8850_usb_fm_v20e.hqx`) + its period
  README.
- **MOTU USB FreeMIDI Driver** binary (from the 1.45 installer).
- FreeMIDI version history (Macintosh Garden "FreeMIDI" page): USB support
  arrived in FreeMIDI 1.41 (Aug 1999); 1.45 (Sept 2000) and 1.48 (2002,
  the final release) are the Mac OS 9-era versions to target.

## Verified facts

### System structure (verified — FreeMIDI 1.45 installer catalog + binaries)

| Component | File type / creator | Location | Notes |
|---|---|---|---|
| FreeMIDI System Extension | `'INIT'` (creator `FMS_`) | System Folder:Extensions | the FreeMIDI runtime (663 KB rsrc) |
| FreeMIDI Setup | `'APPL'`/`'FMSs'` | FreeMIDI Applications folder | configuration app |
| PatchList Manager | `'APPL'`/`'FMSs'` | FreeMIDI Applications folder | |
| **FreeMIDI PowerPlug** | `'shlb'`/`'FMS_'` | (installer) | glue library importing ~200 `FMS*` entry points |
| **FreeMIDI Folder** | folder | System Folder:FreeMIDI Folder | holds the drivers |
| drivers | `'DDef'` or `'IDvr'` | FreeMIDI Folder | see below |
| MOTU USB Driver | `'ndrv'`/`'usbd'` | Extensions | the USB class driver |

The FreeMIDI System Extension's UI strings confirm the support folders:
"FreeMIDI Preferences", "FreeMIDI Icons", "FreeMIDI Folder", "OMS Data",
"Studio Setup Database"; preferences file type `'pref'` creator `FMS_`.

### Driver files (verified — 1.45 catalog Finder info)

- **`'DDef'`** (driver definition) — hardware and software-synth drivers:
  "MOTU USB FreeMIDI Driver" (`mUSB`), "SCC Driver" (`SCCd`),
  "InterApplication Driver" (`APPd`), "MacWaveMaker Driver" (`MWM~`),
  "SampleCell Driver" (`Digi`), "SoftSynth Driver" (`FMss`).
- **`'IDvr'`** (interface driver) — serial-interface drivers: "~Standard
  Interface Driver" (`StdD`), "Studio 4 Driver" (`OS4d`), "AG-3 Driver"
  (`AG3d`), "MIDI Express Driver" (`MUxp`), "MIDI Time Piece Driver"
  (`MTPd`), "Studio 5 Driver" (`OS5d`), "Jambox Driver" (`SJmb`).
- Driver files are resource-fork-only (like OMS `'OMdv'` drivers).

### Driver resources (verified — MOTU USB FreeMIDI Driver.rsrc)

Type list (in catalog order): `'Code'`, `'ICON'`, `'STR#'`, `'icl8'`,
`'ICN#'`, `'BNDL'`, `'mUSB'` (signature type, like OMS drivers' `ddSC`/
`mp32`/`RdSU`), `'FREF'`, `'vers'`, `'SICN'`, **`'DDef'`** (params).

The `'Code'` resource contains a **PEF container** (`Joy!peffpwpc` at
offset 0x124) — PPC driver code, the same mechanism as the Roland `'OMdv'`
OMS driver's code resource.

### Driver code (verified — PEF imports/exports of the MOTU USB FreeMIDI Driver)

Imports: InterfaceLib, **USBManagerLib** (`USBGetNextDeviceByClass`,
`USBGetDriverConnectionID`, `USBInstallDeviceNotification`,
`USBRemoveDeviceNotification`, `USBGetDeviceDescriptor`), **NameRegistryLib**
(`RegistryEntrySearch`, `RegistryEntryIterateCreate/Dispose`,
`RegistryPropertyGet`), USBServicesLib, plus `FindSymbol`, `Get1Resource`,
`DetachResource`, `HoldMemory`, `UnholdMemory`.

Exports: **`TheMOTUShimInterface`** (the shim interface the driver presents;
the same dispatch-table pattern as Opcode's `TheOMSUSBManagerDispatchTable`).

Device-name strings: "MIDI Timepiece", "MIDI Timepiece II", "MIDI Timepiece
AV", "MIDI Express", "MIDI Express XT", "MicroExpress", "Digital Timepiece",
"Mark of the Unicorn", "MOTU USB Driver" — the driver registers known
interfaces by name.

### Driver model (verified — System Extension strings + PowerPlug imports)

- The System Extension's C++ classes: `FreeMIDI::OutputMonitor`,
  `FreeMIDI::OutputChannel`, `FreeMIDI::NewStyleOutputChannel`
  (`DriverGetBuffer(ifaceID, &numBytes, &buf)`, `SendNextCableByte`,
  `CallSendAddressImpl`) — **cable-byte-oriented output**; and the
  InterfaceDriver → PortDriver → MIDIPort object model.
- The PowerPlug glue exports the `FMS*` API surface: `FMSGetPortInterface`,
  `FMSSetPortInterface`, `FMSReadInputQueue`, `FMSSendMidi`,
  `FMSSendParamBlock`, `FMSGetDriverInfo`, `FMSGetDriverPorts`,
  `FMSMidiDrivers`, `FMSGetFMSAddress`, `FMSConfigSignIn`, ~200 total.

### USB linkage (verified)

The MOTU USB FreeMIDI Driver connects to the `'ndrv'`/`'usbd'` class driver
via the USB Manager API + the Name Registry + `FindSymbol` — the same
architecture as our USBMIDI9 dispatch-table shim split. Roland's FreeMIDI
driver ("SC8850 USB Driver", `'DDef'`/`'RdSU'`) uses the same small
NameRegistry PEF fragment as its OMS driver.

### Coexistence with OMS (verified)

FreeMIDI 1.4x can use OMS 2.0+ as its backend ("Use OMS when available",
"requires OMS version 2.0 or higher" — System Extension strings); the
installer ships an "OMS Emulator" component and MOTU's own OMS drivers
(`'OMdv'`/`'MOTU'`, `'OMdv'`/`'mUSB'`). FreeMIDI Setup shows "FreeMIDI is
currently using OMS" / "not using OMS" states.

### Period user-visible behavior (verified — Roland README, Nov 1999)

"SC8850 Driver" appears in FreeMIDI Setup's MIDI Configuration; its port is
"SC8850 Port"; the SC-8850 driver supports six ports; FreeMIDI 1.35+ is
required; virtual memory must be off.

## Unanswered ABI questions (NOT to be guessed)

1. **The `'DDef'` driver message protocol**: what messages/parameters the
   FreeMIDI System Extension sends to a driver's `'Code'` entry (the
   analog of OMS's `omdv*` set), and the entry-point convention.
2. **The `'DDef'` params resource layout** (the analog of OMS's `'OMdi'`
   128 = OMSDriverParams).
3. **The receive call** the driver uses to deliver input to FreeMIDI (the
   analog of `OMSReceivedFromPort`), and whether it may run at interrupt
   level.
4. Whether `'Code'` is invoked directly or via a PowerPlug-glue registration
   (the PowerPlug's `FMS*` exports suggest a glue-mediated contract).

No FreeMIDI SDK was found: archive.org metadata search ("FreeMIDI SDK"):
0 hits; no SDK folder inside the FreeMIDI 1.42–1.45 installers. Authenticating
the protocol would require disassembling the FreeMIDI System Extension's PPC
code (663 KB resource fork) or acquiring an SDK.

## Implementation plan (when the protocol is authenticated)

1. Update `docs/research.md` "FreeMIDI" with the authenticated protocol +
   provenance.
2. `freemidi/freemidi_driver.{h,c}`: a `'DDef'` driver (creator `'USM9'` —
   or a dedicated signature) with resources `'Code'` (PEF, built like the
   OMS driver's), `'DDef'` (params), signature type, icons, `vers`; no USB
   data-path code — consumes `USBMIDI9DispatchTable` (the same
   `oms_rx`/`oms_tx` conversion logic, shared via `core/midi_stream`).
3. Host mock harness mirroring `tests/test_oms_driver.c` once the message
   set is known.
4. G4 gate: FreeMIDI Setup lists "USBMIDI9 Driver"; Keystation input in a
   FreeMIDI application.

Until then: **no FreeMIDI code is shipped** (research-only, per the M4
session brief).

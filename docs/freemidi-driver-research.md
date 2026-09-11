# FreeMIDI driver research (M4)

Status: **research + design only — no implementation.** The FreeMIDI driver
*file format and system architecture* are verified from primary material; the
*driver message protocol* is being recovered statically from the authenticated
1.45 binaries.  The corpus now proves the outer 68K entry frame, executable
`'DDef'` handlers, several `'IDvr'` handlers, and exact receive/output
boundaries. Internal/native callback contracts that end at opaque indirect or
cross-fragment calls remain explicitly unresolved.

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
`mp32`/`RdSU`), `'FREF'`, `'vers'`, `'SICN'`, **`'DDef'`** (executable 68K
entry resource).

The `'Code'` resource contains a **PEF container** (`Joy!peffpwpc` at
extracted Code-resource body offset `0x20`) — PPC driver code, the same
mechanism as the Roland `'OMdv'` OMS driver's code resource.  The sibling
`'DDef'` resource is executable 68K entry code; it is not a proven parameter
resource and must not be described as one.

### Driver code (verified — PEF imports/exports of the MOTU USB FreeMIDI Driver)

Imports: InterfaceLib, **USBManagerLib** (`USBGetNextDeviceByClass`,
`USBGetDriverConnectionID`, `USBInstallDeviceNotification`,
`USBRemoveDeviceNotification`, `USBGetDeviceDescriptor`), **NameRegistryLib**
(`RegistryEntrySearch`, `RegistryEntryIterateCreate/Dispose`,
`RegistryPropertyGet`), USBServicesLib, plus `FindSymbol`, `Get1Resource`,
`DetachResource`, `HoldMemory`, `UnholdMemory`.

The literal **`TheMOTUShimInterface`** is present in the binary, but it is not
an authenticated PEF export. The analyzed MOTU Code resource has no verified
export by that name; it must not be used as a required driver symbol.

Device-name strings: "MIDI Timepiece", "MIDI Timepiece II", "MIDI Timepiece
AV", "MIDI Express", "MIDI Express XT", "MicroExpress", "Digital Timepiece",
"Mark of the Unicorn", "MOTU USB Driver" — the driver registers known
interfaces by name.

The native transition is now partly binary-proven. MOTU Code 128 begins with a
32-byte AAFE Mixed Mode RoutineDescriptor: ProcInfo `0x00000EE5`, PowerPC ISA
`1`, and a container pointer to resource offset `0x20`, where the `Joy!peffpwpc`
PEF begins. The DDef wrapper obtains the Code handle and calls the first long
through `jsr (a0)`; it does not construct a descriptor in the 68K wrapper.
ProcInfo `0xEE5` mechanically encodes Think-C stack calling convention, a
4-byte result, and stack widths 4/2/4. This is the internal 68K-to-PPC bridge,
separate from the direct outer DDef frame.

Roland is a different implementation: its DDef loads `XCOF 10000`, performs
CFM setup, resolves `mixd`, `cfrg`, and `sysa`, and uses a no-code XCOF whose
unpacked data contains Name Registry UPP descriptors. The two bridges must not
be merged, and the opaque CFM service calls do not block analysis of a pure
68K DDef.

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

## Static ABI update (FreeMIDI 1.45)

The System Extension `CODE1` provides a directly observed outer binary ABI.
At `CODE1+0x1d0dc` it pushes a long, a selector word, and a long before
`jsr (a0)`, stores the low word of `d0`, and caller-cleans the stack. The MOTU
`DDef 128` wrapper reads the selector as a word and calls its native entry with
the same frame. The recovered binary shape is therefore
`SInt16 entry(SInt32 par1, SInt16 selector, SInt32 par2)`, subject to the
separate native bridge described below; the original source language is not an
ABI uncertainty.

Selectors 1, 7, 9, and 8 are direct System Extension observations for setup,
descriptor/setup information, optional callback registration, and teardown.
The selector-7 local is copied as 67 longwords (268 bytes) at
`CODE1+0x1d160`; an independent 70-byte constructor copy elsewhere was
previously conflated with it and is now documented separately. The selector-9
block contains a context word and a function-pointer-sized word, but the
literal target does not decode as a valid raw CODE1 entry without resolving
the loader's code-resource address bias.

Independent executable dispatchers corroborate the family but not one enum:
Roland DDef has paths for 1–8, SCC DDef accepts 0–13, InterApplication DDef
accepts 0–8, Standard IDvr accepts 0–10, and Roland IDvr accepts 0–12. Their
selector-7 paths repeatedly write a creator, icon handle, and `0x100` field to
a descriptor-like record. The full ledger is in
`docs/freemidi-driver-abi-evidence.md`.

PowerPlug is a separate client layer. Its PPC queue helper writes a 32-bit
queued value and an 8-byte `lfd/stfd` timestamp, while timestamped send stores
PPC `f1` as an 8-byte value. This proves client timestamp behavior only; it does
not authenticate the native DDef receive/output record.

## Unanswered ABI questions (NOT to be guessed)

1. **The remaining semantic message/record protocol**: the meanings of
   selectors beyond the directly observed lifecycle calls and the opaque
   parameter records. The binary entry-point convention itself is recovered:
   `(long par1, word selector, long par2)`, caller cleanup, callee `rts`, and
   low-word `d0` return.
2. The complete semantic layout of selector 7's 268-byte copied record and
   the separate 70-byte constructor record.
3. **The receive call** the driver uses to deliver input to FreeMIDI (the
   analog of `OMSReceivedFromPort`), and whether it may run at interrupt
   level.  Roland's exact boundary is now known: `ReceiveFreeMidi` calls a
   callback at the per-port record's `+0x6a` field through a raw indirect jump.
4. The output-buffer record and ownership rules after the System Extension's
   `DriverGetBuffer`/`SendNextCableByte` path.
5. The exact Classic Mac code-resource relocation that materializes the
   System Extension selector-9 callback literal.

No FreeMIDI SDK was found in the local installer corpus.  That negative result
does not limit the 1.45 static analysis: the report and evidence ledger record
the exact instruction boundaries that remain opaque and the smallest dynamic
observation that would resolve each one.

## Implementation plan (when the protocol is authenticated)

1. Update `docs/research.md` "FreeMIDI" with the authenticated protocol +
   provenance.
2. `freemidi/freemidi_driver.{h,c}`: a pure-68K inert `'DDef'` driver can be
   specified from the recovered outer ABI and selector-7 behavior.  A
   production driver with resources `'Code'` (PEF, built like the OMS
   driver's), executable `'DDef'`, signature type, icons, and `vers`; no USB
   data-path code — consumes `USBMIDI9DispatchTable` (the same
   `oms_rx`/`oms_tx` conversion logic, shared via `core/midi_stream`).
3. Host mock harness mirroring `tests/test_oms_driver.c` once the message
   set is known.
4. G4 gate: FreeMIDI Setup lists "USBMIDI9 Driver"; Keystation input in a
   FreeMIDI application.

Until then: **no FreeMIDI code is shipped** (research-only, per the M4
session brief).

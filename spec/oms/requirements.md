# M4 — OMS integration and MIDI-system session (requirements)

Status: **research gate PASSED (OMS); FreeMIDI gate PARTIAL (format + architecture
verified, driver message protocol not fully verified).**

This session moves USBMIDI9 from "raw USB-MIDI packets demonstrably work on the
G4" toward a period-appropriate Mac OS 9 MIDI product. Scope, in order:

1. Freeze the neutral service boundary against what the OMS driver API actually
   requires (Phase A of the session brief).
2. OMS: implement an authentic OMS hardware driver for USBMIDI9 (Phases B/C).
3. FreeMIDI: research and document; implement ONLY if the driver protocol is
   authenticated (Phase D). Research-only is an accepted outcome.
4. Distribution/packaging design for the era (Phase E).
5. Period-correct user documentation (Phase F), developer-doc cleanup (Phase G),
   acceptance matrix + G4 handoff (Phase H).

## Non-negotiable historical rule

Every Classic Mac / OMS / FreeMIDI symbol, structure, callback, resource, file
type, creator, folder, calling convention, or execution-context claim must be
derived from an authentic SDK/header/sample, a period vendor document, or a
clearly identified period binary/resource inspection; provenance recorded;
"verified" vs "inferred" distinguished. No invented APIs. No proprietary files
in the repository. Research artifacts stay in `~/research/` with PROVENANCE.md.

## Research gate result — OMS (PASSED, primary sources)

All facts below are verified from: the Opcode **OMS 2.0 SDK** (28-Jan-98,
recovered from the Opcodeusers Yahoo group via Macintosh Garden; SHA-256
`1a0a62c0ab6d9f9e9b07aef464e29dbb7f05b2d3f0dc1f0d03f9a1d9f0d3d4c8` of the
`sit` — see `~/research/oms/PROVENANCE.md`), the **OMS Programming Interface
spec** (Mar 1995, from the SDK), the **OMS 2.3.8 full installer** (from the
Evolution eKeys 37 CD on archive.org and Macintosh Garden; extracted with
`unvise`), and the **Roland SC-8850 USB OMS driver package** (Dec 1999,
archive.org `roland-sc-8850-driver-tools`).

| # | Fact | Status |
|---|---|---|
| 1 | OMS drivers: file type `'OMdv'`, unique 4-char creator = driver signature, located in **System Folder:OMS Folder** | verified (Spec; 2.3.8 installer catalog; Roland package) |
| 2 | Resources: `'OMdi'` 128 = `OMSDriverParams`; `'OMdv'` 128 = driver code; `'SICN'` 128 = icons (pairs); plus `BNDL`/`FREF`/`vers` | verified (Spec; SDK sample; 2.3.8 drivers; Roland) |
| 3 | `OMSDriverParams`: id (load order, assigned by Opcode), xxisSmart, hasMenuOrWindows, xxportNumM/B (obsolete), flags (`kNoSyncRouting 0x80`, `kUseDeviceInfoDialog 0x20`, `kAllowIDEditing 0x10`, `kAlwaysLoad 0x08`, `kOmitFromAutoSetup 0x04`), `driverCompatibilityLevel`, reservedFlags[6] | verified (Spec; OMSDriver.h) |
| 4 | Entry point: `OMSCALLBACK(long) main(short msg, long par1, long par2)`; OMS-defined messages 0..4095: `omdvInit`(par1=OMSFile*), `omdvDispose`, `omdvAddDevices` (compat≥1: par1=`OMSDvrAdd1DevProc1UPP`, par2=`OMSAddDevParams*`), `omdvConfigure`, `omdvSetInterfaceList` (**par1 = `OMSDeviceListH`** of all interfaces in the current studio setup; remember, never alter/dispose), `omdvStartMIDI` (par1 = port-availability bits; 2.0: ignore, use Serial Hardware Manager), `omdvStartMIDI2` (no parameters; sent soon after omdvStartMIDI once the system is consistent), `omdvStopMIDI` (release all resources), `omdvGetPortSendProc` (par1=`OMSPortID*`, par2=`OMSSendParams*`), `omdvRemoveOutput`, `omdvSetPortReceiveRefNum` (par1=`OMSPortID*`, par2=short ioRefNum, −1 = don't send; initially assume −1 for all sources), `omdvTestDevice`, `omdvDifferentStudioSetup`, `omdvConnectsChanged` | verified (OMSDriver.h; Spec ch. OMS Drivers) |
| 5 | Calling convention: pascal, via UPPs (`kPascalStackBased`) in CFM builds (`OMSDrvUPPs.h`) | verified (SDK header) |
| 6 | Device registration: zero an `OMSDevice`; set whichOut, ownerDriver=signature, flags1 (`kInConnected|kIsReceiver|kIsMultitimbral`), flags2 (`kIsMIDIInterface|kNoChildDevices`), midiChannels=0xFFFF, devName/manuf/model, iconID (index into SICN), driverSpecific[4]; call `CallOMSDvrAdd1DevProc1(add1Device, &dev, sizeof(dev))` | verified (Spec; SampleCell.c) |
| 7 | Output: `omdvGetPortSendProc` returns `OMSSendParams{proc=OMSReadHook2UPP, paramD0, paramD1}`; OMS fills `omsUniqueID`; **send proc may be called at interrupt level**; compat≥1 proc is `OMSReadHook2` = `pascal void (OMSMIDIPacket *packet, long readHookRefCon)`; paramD0 = readHookRefCon, low word of paramD1 → pkt->appConnRefCon | verified (Spec) |
| 8 | Input: driver parses bytes into `OMSPacket`/`OMSMIDIPacket` (single MIDI messages; **SysEx split with continuation flags**), optionally timestamps, delivers via **`OMSReceivedFromPort(pkt, ioRefNum)`** (may be called at interrupt level; for this call `OMSPacket.len` = MIDI data bytes only) | verified (Spec) |
| 9 | Timestamps: `OMSTimerGetOMSClockPosition()` / `OMSTimerGetOMSSMPTETime()`; flags `omsPktBeatTStamped 0x80` / `omsPktSMPTETStamped 0x40` | verified (Spec) |
| 10 | Packet structs: `OMSPacket{flags, len (incl. 6-byte header), srcIORefNum, appConnRefCon, data[4]}`; `OMSMIDIPacket` = + beatTimeStamp(4) + smpteTimeStamp(4) + len=data bytes; continuation mask `omsContMask 0x03`: `omsNoCont 0`, `omsStartCont 1`, `omsMidCont 3`, `omsEndCont 2` | verified (OMS.h) |
| 11 | `driverCompatibilityLevel` 1 = OMS 2.0+ interface; OMS 2.3.8 ships compat-level-1 drivers (MIDIPort 32 etc.) and its INIT code references `'OMdv'`/`'OMdi'`/`'SICN'` | verified (Spec; 2.3.8 binaries) |
| 12 | **PPC driver code form**: the `'OMdv'` code resource contains a PEF container (PPC code); verified in the Roland SC-8850 OMS driver (`Joy!peffpwpc` at rsrc offset 0x104, preceded by a 4-byte length header) | verified (binary inspection) |
| 13 | OMS Folder constants: `kOMSFolder='OMS '`, `kOMSPrefFolder='pref'`, `kOMSFacforyNamesFolder='FNAM'`; OMS Folder is inside the System Folder; device database files `'Odvi'` (OMS 2.x) / TEXT with creator `OmsI` (1.x) | verified (OMS.h; Spec App C; 2.3.8 Read Me; eKeys guide) |
| 14 | Opcode's own USB architecture (2.3.8): `USB OMSMIDIDriver` (`ndrv`/`usbd` class driver, exports `TheUSBMIDIInterface` + `TheMIDIClassDispatchTable`) + `OMS USB Manager` (`shlb`, creator `'OMdv'`, exports `TheOMSUSBManagerDispatchTable`; **verified imports: `USBGetNextDeviceByClass`, `USBGetDriverConnectionID`, `USBInstallDeviceNotification`, `USBRemoveDeviceNotification`, `FindSymbol`** — it finds and talks to the class driver through the USB Manager API + FindSymbol) | verified (2.3.8 binaries, PEF import lists) — period precedent for OUR ndrv + dispatch-table + shim split |
| 15 | The eKeys/Keystation-era Mac OS 9 install layout (vendor guide): USB drivers → **System Folder:Extensions**, OMS description file → **System Folder:OMS Folder** | verified (eKeys CD "Driver Installation Guide") |

## Research gate result — FreeMIDI (PARTIAL)

Verified from: **FreeMIDI 1.45 full installer** (Sept 2000, Macintosh Garden),
**Roland SC-8850 USB FreeMIDI driver** (Nov 1999), **MOTU USB FreeMIDI Driver**
binary (FreeMIDI 1.45).

| # | Fact | Status |
|---|---|---|
| 1 | FreeMIDI system = "FreeMIDI System Extension" (INIT) + "FreeMIDI Setup" (APPL/FMSs) + **FreeMIDI Folder** (System Folder) with drivers + "FreeMIDI PowerPlug" (`shlb`/`FMS_`, the glue with ~200 `FMS*` entry points: `FMSGetPortInterface`, `FMSSetPortInterface`, `FMSReadInputQueue`, `FMSSendMidi`, `FMSGetDriverInfo`, ...) | verified (1.45 installer; PowerPlug binary) |
| 2 | FreeMIDI drivers: type **`'DDef'`** (hardware/softsynth drivers; creators e.g. `mUSB`, `SCCd`, `APPd`, `Digi`, `FMss`) or **`'IDvr'`** (serial interface drivers; `StdD`, `OS4d`, `AG3d`, `MUxp`, `MTPd`, `OS5d`, `SJmb`); resource-fork-only; in the FreeMIDI Folder | verified (1.45 installer catalog) |
| 3 | FreeMIDI driver resources: **`'Code'`** (code — PEF container verified), **`'DDef'`** (params), signature-type resource (e.g. `mUSB`), icons, BNDL/FREF, vers | verified (MOTU USB FreeMIDI Driver rsrc) |
| 4 | Driver model: InterfaceDriver → PortDriver → MIDIPort; FreeMIDI System Extension classes `NewStyleOutputChannel` (`DriverGetBuffer(ifaceID, &n, &buf)`, `SendNextCableByte`, `CallSendAddressImpl`) — cable-oriented output; `FMSGetPortInterface`/`FMSSetPortInterface` in glue | verified (binary strings) |
| 5 | USB driver linkage: the MOTU USB FreeMIDI Driver connects to the `ndrv`/`usbd` class driver via USB Manager API + Name Registry + FindSymbol; exports `TheMOTUShimInterface` | verified (binary imports/exports) |
| 6 | Coexistence: FreeMIDI 1.4x can use OMS 2.0+ as backend ("Use OMS when available"), ships an "OMS Emulator" and OMS drivers (`'OMdv'`/`MOTU`, `'OMdv'`/`mUSB`) | verified (1.45 installer; System Extension strings) |
| 7 | **FreeMIDI driver message protocol (the messages FreeMIDI sends to a `'DDef'` driver's `'Code'` entry, the `'DDef'` params structure layout, the exact receive call)**: NOT yet authenticated — no FreeMIDI SDK found; would require disassembly of the System Extension | **UNVERIFIED — no implementation without it** |

## Design decisions (from the findings)

1. **OMS shim form**: classic `'OMdv'`-type OMS driver file (fully verified
   contract), creator `'USM9'`, in the OMS Folder. Resources: `'OMdi'` 128
   (driverCompatibilityLevel = 1), `'OMdv'` 128 (PPC code as a PEF container),
   `'SICN'` 128 + `'ICN#'` icons, `BNDL`/`FREF`/`vers`.
2. **No USB data-path code in the OMS shim**: the shim consumes
   `USBMIDI9DispatchTable` (enumeration + dequeue) exactly like the Probe does.
   The only USB-adjacent call allowed in `oms/` is the driver-lookup pattern
   `USBGetNextDeviceByClass` + `FindSymbol` used to locate the dispatch table —
   the same verified pattern the Probe uses, and precisely what Opcode's own
   OMS USB Manager does (fact 14; its imports include USBGetNextDeviceByClass
   and FindSymbol). All USL transfer/pipe calls (USBBulkRead/USBBulkWrite/
   USBIntRead/USBIntWrite/USBDeviceRequest/USBConfigureInterface/USBAllocMem/
   USBDeallocMem, USBFindNextPipe) are forbidden in `oms/`.
3. **Neutral service additions (Phase A)**: the OMS input contract requires
   message-level delivery with SysEx continuation flags, so the neutral layer
   gains a verified, host-tested USB-MIDI event-packet stream ⇄ conventional
   MIDI byte-stream converter (CIN handling, cable preservation, SysEx
   fragmentation/reassembly, malformed CIN policy). Output encoding is designed
   and host-tested; the USB bulk-OUT transport is implemented as a driver TODO
   marked UNVERIFIED until the G4 gate.
4. **FreeMIDI**: research + design only this session (accepted outcome). The
   FreeMIDI driver protocol is documented with provenance + unanswered ABI
   questions in `docs/freemidi-driver-research.md`.
5. **Distribution**: period-correct layout documented (StuffIt `.sit` primary,
   `.sit.hqx` for transport; resource forks preserved; type/creator per
   component; install targets System Folder:Extensions + System Folder:OMS
   Folder; release naming; verification + uninstall procedures). No proprietary
   tools added to the repo; final artifact assembly on the G4.
6. **Docs**: user-facing Read Me in period voice (plain text, MacRoman, CR);
   README becomes a landing page; research log preserved as developer notes.

## Acceptance criteria

- [ ] `make test`, `make test-sanitize`, `make check-classic` green after every
      commit.
- [ ] OMS facts above carry provenance in `~/research/oms/PROVENANCE.md` and
      `docs/research.md`.
- [ ] Neutral converter (input + output) host-tested: Note On/Off, CC, Program
      Change, Pitch Bend, realtime, SysEx boundaries, malformed/reserved CIN,
      cable preservation, multi-cable.
- [ ] OMS shim source compiles under `check-classic`; driver logic (message
      dispatch, device registration data, packet formatting, continuation
      flags, **`OMSSendParams` paramD0/paramD1 wiring**) covered by a host mock
      harness; no USL transfer calls in `oms/` (lookup-only
      USBGetNextDeviceByClass+FindSymbol permitted, per design decision 2).
- [ ] No invented Classic API names; every symbol in `oms/` traces to the SDK
      headers or the Spec (citation comments).
- [ ] FreeMIDI research doc complete with provenance + implementation plan;
      no FreeMIDI code shipped.
- [ ] Distribution + user-doc deliverables exist and read period-correct.
- [ ] Real-G4 gates listed in order, with exact CodeWarrior instructions.

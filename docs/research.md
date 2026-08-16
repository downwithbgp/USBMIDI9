# Research checklist

Unresolved historical research questions. **Do not fill these with guesses.**
Each item should be resolved only against primary historical documentation
with provenance recorded (see "Source standards" below).

## Classic Mac OS 9 USB

Primary sources obtained (all kept OUTSIDE this repository; exact
provenance and SHA-256/MD5 hashes in `~/research/usbddk/PROVENANCE.md`):

1. **Apple, *Mac OS USB DDK API Reference*, Preliminary Working Draft,
   Revision 26, 12/23/99** (from Apple's own archive; local research copy
   outside the repository).
2. **Mac OS USB DDK 1.4.1 installed kit** — Apple's ADC Developer CD-ROM
   January 2001 (Internet Archive item
   `Apple_Developer_Connection_Developer_CD-ROM_January_2001`), folder
   `Development Kits/Hardware/Mac OS USB DDK/Mac OS USB DDK 1.4.1/`:
   `Interfaces/USB.h` + `HID.h` + `.exp` export lists,
   `Libraries/USBServicesLib` (import library), `Documentation/` (Readme,
   Change History, Compatibility Notes), `Examples/` (USBKeypad,
   KeyboardModule, MouseModule, UniversalModule, CompositeClassDriver,
   PrinterClassDriver, USBSampleStorageDriver, USBModem, USBTabletModule,
   HIDReader, PowerClassQueryDevice, DropPrint¥USB, USBMultModule),
   `Extensions-AppleBuilt/` (USB 1.4.1f4 binaries; not distributable).
3. **USB DDK 1.4.6f12 and USB DDK 1.5.1f1 disk images** — acquired (ADC
   CD-ROM Jan 2001; Macintosh Garden). Both images are CD-mastered with
   Apple's FileCrusher/tome tooling: coherent HFS MDB but zeroed
   catalog/extents descriptors, not extractable with unar/7-Zip/hfsutils/
   machfs. Contents are covered by the 1.4.1 kit; recorded for completeness.
4. **MacErrors.h** (Universal Interfaces snapshot; GitHub `msftguy/ssh-rd`
   @ `a5f3a79daeac5844edebf01916c9613563f1c390`, `_3rd/CF/MacErrors.h`) —
   authoritative USB error values (`kUSBNoErr=0`, `kUSBPending=1`,
   `kUSBOverRunErr=-6908`, `kUSBNotRespondingErr=-6911`,
   `kUSBBadDispatchTable=-6950`, `kUSBDeviceDisconnected=-6972`,
   `kUSBDeviceBusy=-6977`, `kUSBPipeStalledError=-6979`,
   `kUSBAbortedError=-6982`, `kUSBInternalErr=-6999`).

**Finding: there is no standalone "USB DDK 1.4.2".** Apple's change
histories (the DDK 1.4.1 kit's "Mac OS USB Change History", which
pre-documents the upcoming 1.4.2 release; Apple's "Mac OS USB 1.5.1f1
Change History" page, Wayback capture 2001-06-24) state that USB 1.4.2 fixes one issue (Apple Studio Display hubs unusable
after wake-from-sleep), changes no API, reports gestalt 'usbv' =
0x01428000, and ships only inside the Mac OS 9 Update (Mac OS 9.0.4) — the
USB Device Extension file remains 1.4.1. The DDK 1.4.1 kit (USB software
1.4.1f4) is therefore the authentic header set for 1.4.x-era development.

Checklist (updated by the M1A.1 pass):

- [x] Apple USB DDK media/headers (USB.h, .exp export lists, USBServicesLib
      import library, sample class drivers) — DDK 1.4.1 kit obtained;
      provenance/hashes in `~/research/usbddk/PROVENANCE.md`; NOT committed
      (proprietary; DDK license permits use for development, not
      redistribution).
- [x] Symbol-by-symbol verification against the authentic header — see the
      M1A.1 table below and `docs/classic-usb-driver.md` §8.
- [x] Apple class-driver sample patterns — verified against USBKeypad,
      KeyboardModule, UniversalModule, CompositeClassDriver,
      PrinterClassDriver, USBModem (summary in
      `docs/classic-usb-driver.md` §8.2).
- [x] DDK-era CodeWarrior version and packaging — CodeWarrior Pro 1 /
      IDE 2.0 (projects compatible with IDE 2.1, 3.0+); Universal
      Interfaces 3.3 required; USB.h copied into the Universal Headers
      folder; merged multi-CFM `'ndrv'` files (Mac OS Merge in CW,
      `mergefragment` in the MPW makefiles); output into
      `:Extensions-MCWBuilt:` (DDK 1.4.1 Readme; USBKeypad.make).
- [x] USB software version on the target G4 — Mac OS 9.0.4 / 9 Update
      ships USB 1.4.2 ('usbv' 0x01428000); headers identical to the DDK
      1.4.1 kit. Rev 26 (12/23/99) documents up to 1.4; nothing
      1.4.2-specific is missing (it is a bug-fix release).
- [x] USB device/interface matching model — verified (Rev 26 Ch 4,
      p. 54-59; generic interface matching for class 0x01/subclass 0x03 in
      `docs/classic-usb-driver.md` §1.4, §5.1; loading-option flags verified
      in USB.h 1.4.1).
- [x] USB bulk transfer API — verified (USBBulkRead/USBBulkWrite, USBPB,
      Rev 26 Ch 5, p. 128-129; MaxPacketSize alignment = performance
      consideration, Rev 26 App A p. 230 — see
      `docs/classic-usb-driver.md` §5.5).
- [x] Asynchronous completion mechanism — verified (completion routines at
      secondary interrupt or task level; USBPB residency; Rev 26 Ch 5,
      p. 101-102; DDK PrinterClassDriver `SafeUSBBulkRead`/
      `CallSecondaryInterruptHandler2` pattern).
- [x] Device insertion/removal notification — verified
      (notificationProc/kNotifyDriverBeingRemoved + USBInstallDeviceNotification,
      Rev 26 Ch 4 p. 70-71, Ch 6 p. 185-187; removal mechanics verified in
      KeyboardModule: driverRemovalPending/kCompletionPending/
      USBAbortPipeByReference/kUSBDeviceBusy).
- [x] Memory/lifetime constraints for callbacks — verified (USBAllocMem;
      USBPB must outlive the async call; finalize must have no pending
      calls; Rev 26 Ch 4 p. 86, Ch 5 p. 101, 150; sample pattern
      `pb.pbLength = sizeof(whole driver PB struct)`).
- [ ] `USBConfigureInterface` set-interface behavior in later USB software
      (Rev 26 documents that it does not set the interface; later behavior
      unverified — requires a 1.4.6+ USB software test).
- [ ] Whether sleep notifications must be handled for the target G4
      (desktop Macs do not receive them per Rev 26, p. 70).

## M1A.1 symbol verification table (vs. authentic DDK 1.4.1 header / Rev 26)

"Exact declaration verified?" = the declaration was read in the authentic
`USB.h` (DDK 1.4.1 kit, `Interfaces/USB.h`, SHA-256
`6794dcf2…61df`) or in Rev 26's function reference, or was confirmed from
DDK sample call sites. USB.h 1.4.1 is a types/constants-only header: USL
function prototypes lived in the CodeWarrior-era Universal Headers' USB.h
(Apple's 1.4.6 change notes, as republished in the "Mac OS USB 1.5.1f1
Change History" page [Wayback 2001-06-24], record a "USB.h file" change,
"Removed support for OLDCLASSNAMES"); their signatures are verified here
from Rev 26 (the API reference for exactly this software) plus sample call
sites.

| Symbol | Header/source | Exact declaration verified? | Relevant USB software version | Notes |
|---|---|---|---|---|
| `USBDriverDescription` | USB.h 1.4.1 | Yes (struct + typedef; export name `TheUSBDriverDescription`) | 1.4.1 (DDK 1.4.1) | Fields match Rev 26 p. 60-64; `usbDriverDescSignature` uses `kTheUSBDriverDescriptionSignature` ('usbd'); `usbDriverDescVersion` = `kInitialUSBDriverDescriptor` (0) |
| `USBInterfaceInfo` | USB.h 1.4.1 | Yes | 1.4.1 | 5 × UInt8: config value, interface num, class, subclass, protocol — as documented |
| `USBClassDriverPluginDispatchTable` | USB.h 1.4.1 | Yes (struct + typedef; export name `TheClassDriverPluginDispatchTable`) | 1.4.1 | 6 fields: pluginVersion, validateHWProc, initializeDeviceProc, initializeInterfaceProc, finalizeProc, notificationProc |
| `USBDInitializeInterfaceProcPtr` | USB.h 1.4.1 | Yes | 1.4.1 | `(UInt32 interfaceNum, USBInterfaceDescriptorPtr pInterface, USBDeviceDescriptorPtr pDevice, USBInterfaceRef interfaceRef) → OSStatus` |
| `USBDNotificationProcPtr` | — | **Name does not exist in the DDK 1.4.1 header** | — | The header defines `USBDDriverNotifyProcPtr` `(USBDriverNotification notification, void *pointer, UInt32 refcon) → OSStatus` ("Added refcon for 1.1 version of dispatch table"). Use `USBDDriverNotifyProcPtr`. |
| `USBDFinalizeProcPtr` | USB.h 1.4.1 | Yes | 1.4.1 | `(USBDeviceRef device, USBDeviceDescriptorPtr pDesc) → OSStatus` |
| `USBCompletion` | USB.h 1.4.1 | Yes | 1.4.1 | `CALLBACK_API_C(void, USBCompletion)(USBPB *pb)`; `kUSBNoCallBack = (USBCompletion)-1L` |
| `USBPB` | USB.h 1.4.1 | Yes (exact layout) | 1.4.1 | qlink..reserved8 as in Rev 26 Ch 5; `usb` = `USBVariantBits` union (cntl/isoc/hub); `OLDUSBNAMES` macro for `usb.cntl.*` |
| `USBFindNextPipe` | Rev 26 p. 115 + USBKeypad.c | Yes (prototype in Rev 26; call pattern in sample) | 1.4.1 | `OSStatus USBFindNextPipe(USBPB *pb)`; sample: `usbFlags=kUSBIn`, `usbClassType=kUSBInterrupt`; pipe ref ← `usbReference`; max packet size ← `usb.cntl.WValue` |
| `USBBulkRead` | Rev 26 p. 128 + PrinterClassDriver.c | Yes (prototype + field rules) | 1.4.1 | `OSStatus USBBulkRead(USBPB *pb)`; `usbReqCount` multiple of MaxPacketSize = performance (App A p. 230); short packet terminates; overrun ⇒ `kUSBOverRunErr` |
| `USBBulkWrite` | Rev 26 p. 129 + PrinterClassDriver.c | Yes (prototype + `SafeUSBBulkWrite` pattern) | 1.4.1 | `OSStatus USBBulkWrite(USBPB *pb)` |
| `USBAbortPipeByReference` | Rev 26 p. 137 + KeyboardModule.c / UniversalModule.c | Yes | 1.4.1 | `OSStatus USBAbortPipeByReference(USBReference ref)`; pipe ref, or device ref for implicit pipe 0; pending transactions return `kUSBAbortedError`; data-toggle caveat |
| `USBAllocMem` | Rev 26 p. 150 + UniversalModule.c | Yes | 1.4.1 | `OSStatus USBAllocMem(USBPB *pb)`; `usbReqCount` = size, `usbFlags` = 0; memory ← `usbBuffer` |
| `USBDeallocMem` | Rev 26 p. 151 + USBKeypadHeader-MacAlly.c | Yes | 1.4.1 | `OSStatus USBDeallocMem(USBPB *pb)`; `usbBuffer` in/out; `kUSBNoCallBack` allowed at task time only |
| `kClassDriverPluginVersion` | USB.h 1.4.1 | Yes | 1.4.1 | **0x00001100** (was "TBD" in the docs) |
| `kUSBCurrentPBVersion` / `kUSBIsocPBVersion` / `kUSBCurrentHubPB` | USB.h 1.4.1 | Yes | 1.4.1 | 0x0100 / 0x0109 / `kUSBIsocPBVersion` |
| `kUSBIn` / `kUSBOut` / `kUSBBulk` | USB.h 1.4.1 | Yes | 1.4.1 | 1 / 0 / 2 (`kUSBNone`=2 is the *direction* enum: kUSBOut=0, kUSBIn=1, kUSBNone=2, kUSBAnyDirn=3; endpoint types: kUSBControl=0, kUSBIsoc=1, kUSBBulk=2, kUSBInterrupt=3, kUSBAnyType=0xFF) |
| `kUSBDeviceBusy` | MacErrors.h (UI snapshot) | Yes | 1.4.1 era | −6977 (USB Services error) |
| `kUSBNoErr` | MacErrors.h (UI snapshot) | Yes | 1.4.1 era | 0 (same value as `noErr`) |
| `kUSBAbortedError` | MacErrors.h (UI snapshot) | Yes | 1.4.1 era | −6982 ("Pipe aborted") |
| `kNotifyDriverBeingRemoved` | USB.h 1.4.1 | Yes | 1.4.1 | 0x0000000B (11) — as documented |
| `kUSBDoNotMatchGenericDevice` | USB.h 1.4.1 | Yes | 1.4.1 | 0x00000001 |
| `kUSBDoNotMatchInterface` | USB.h 1.4.1 | Yes | 1.4.1 | 0x00000002 |
| `kUSBProtocolMustMatch` | USB.h 1.4.1 | Yes | 1.4.1 | 0x00000004 |

Additional values verified in the same headers: `kTheUSBDriverDescriptionSignature`='usbd',
`kInitialUSBDriverDescriptor`=0, `kUSBDriverFileType`='ndrv',
`kUSBDriverRsrcType`='usbd', `kUSBShimRsrcType`='usbs',
`kUSBInterfaceMatchOnly`=0x00000008 (defines the "InterfaceMatchOnly" flag
that Rev 26's matching figures reference but its p. 64 list omits),
`kUSBNotRespondingErr`=-6911 (Rev 26 prose writes "kUSBNotRespondingError";
the header constant is `kUSBNotRespondingErr`), `kUSBOverRunErr`=-6908,
`kUSBPending`=1, `kUSBBadDispatchTable`=-6950, `kUSBPipeStalledError`=-6979,
`kUSBInternalErr`=-6999.

## Service boundary design

- [x] Probe/driver communication mechanism for M1 — documented choice:
      driver-exported versioned dispatch table located via
      `USBGetNextDeviceByClass` + `FindSymbol`, hotplug via
      `USBInstallDeviceNotification` (Rev 26 Ch 4 p. 73-74, 83-85;
      Ch 6 p. 179, 185-187). See `docs/classic-usb-driver.md` §5.7.
- [x] OMS/FreeMIDI shim design — M4: the OMS shim is a classic `'OMdv'`
      OMS driver (verified contract, see OMS section above) consuming the
      same `USBMIDI9DispatchTable`; the neutral layer gains a host-tested
      USB-MIDI stream ⇄ MIDI message converter (`core/midi_stream.{h,c}`).
      FreeMIDI: research-only until the driver protocol is authenticated
      (see FreeMIDI section and `docs/freemidi-driver-research.md`).
- [ ] Whether the mechanism must be a CFM shared library, a system
      extension, or both — driver is a CFM 'ndrv'/'usbd' extension
      (verified); the OMS driver is an `'OMdv'` file whose code resource
      contains a PEF container (Roland driver verified; CodeWarrior
      construction validated on the G4).

## OMS

**M4 research gate PASSED.** Primary material obtained (all OUTSIDE this
repository; exact provenance, hashes, and extraction notes in
`~/research/oms/PROVENANCE.md`):

1. **Opcode OMS 2.0 SDK (28-Jan-98)** — from the Opcodeusers Yahoo Group via
   Macintosh Garden (`OMS_SDK.sit`, MD5 `33e43e0c973fe0c667ed7613fff88800`):
   `OMS.h`, `OMSDriver.h` (the driver API), `OMSDrvUPPs.h` (pascal UPP
   info), `OMSTypes.h`, the OMS Programming Interface spec (Mar 1995, Word
   for Mac; converted), and the complete `SampleDriver - SampleCell`
   example with its `'OMdi'`/`'OMdv'`-bearing resource fork.
2. **OMS 2.3.8 full installer** — Internet Archive
   `evolution-ekeys37-cdrom` (eKeys 37 CD: `Install OMS 2.3.8`, VISE;
   `Driver Installation Guide.pdf`) + Macintosh Garden
   `Install_OMS_2_3_8.sit`. Extracted with `unvise`; component inventory
   with Finder type/creator, driver resource forks, the OMS INIT, OMS Setup,
   the **USB OMSMIDIDriver** (`ndrv`/`usbd`) and **OMS USB Manager**
   (`shlb`, creator `'OMdv'`) PEF binaries.
3. **Roland SC-8850 USB OMS driver** (Dec 1999; archive.org
   `roland-sc-8850-driver-tools`) — a real third-party `'OMdv'` driver
   (`'RdSU'`) whose `'OMdv'` code resource contains a **PEF container
   (PPC code)**.

Verified facts (each traces to the SDK headers/spec, the 2.3.8 binaries, or
the Roland driver; full table in `spec/oms/requirements.md`):

- Driver file: type `'OMdv'`, unique creator = driver signature, located in
  **System Folder:OMS Folder** (`kOMSFolder='OMS '`).
- Resources: `'OMdi'` 128 = `OMSDriverParams` (id, flags, reservedFlags,
  **driverCompatibilityLevel**); `'OMdv'` 128 = code (68k in Opcode's own
  drivers; **PPC PEF container** in Roland's); `'SICN'` 128 = icons;
  plus BNDL/FREF/vers/signature-type resource.
- Entry: `OMSCALLBACK(long) main(short msg, long par1, long par2)`, pascal,
  via UPPs (`kPascalStackBased`) in CFM builds; messages omdvInit .. 42
  (omdvInit, omdvDispose, omdvAddDevices, omdvConfigure, omdvSetInterfaceList,
  omdvStartMIDI, omdvStartMIDI2, omdvStopMIDI, omdvGetPortSendProc,
  omdvOutputRemoved, omdvSetPortReceiveRefNum, omdvTestDevice,
  omdvDifferentStudioSetup, omdvConnectsChanged).
- Registration: zero `OMSDevice`; whichOut, ownerDriver, flags1
  (kInConnected|kIsReceiver|kIsMultitimbral), flags2 (kIsMIDIInterface|
  kNoChildDevices), midiChannels=0xFFFF, devName/manuf/model, iconID,
  driverSpecific[4]; add via `CallOMSDvrAdd1DevProc1(add1Device, &dev,
  sizeof(dev))` (compat level ≥ 1).
- Output: omdvGetPortSendProc returns `OMSSendParams{proc=OMSReadHook2UPP,
  paramD0, paramD1}`; the send proc **may be called at interrupt level**;
  paramD0 = readHookRefCon, low word of paramD1 → pkt->appConnRefCon.
- Input: driver formats `OMSPacket` (6-byte header: flags, len incl.
  header, srcIORefNum, appConnRefCon) or `OMSMIDIPacket` (+ beatTimeStamp,
  smpteTimeStamp; len = data bytes) with SysEx continuation flags
  (omsContMask: start/cont/end), optionally stamps
  `OMSTimerGetOMSClockPosition()`/`OMSTimerGetOMSSMPTETime()`, and delivers
  via **`OMSReceivedFromPort(pkt, ioRefNum)`** (interrupt-safe; len = data
  bytes only for this call).
- compat level 1 = OMS 2.0+ interface; OMS 2.3.8 loads such drivers (its
  own 2.3.8 drivers use exactly this mechanism; the INIT references
  'OMdv'/'OMdi'/'SICN').
- OMS 2.3.8's own USB architecture (period precedent for ours): USB
  OMSMIDIDriver (`ndrv`/`usbd`, exports `TheUSBMIDIInterface` +
  `TheMIDIClassDispatchTable`, receive = USBIntRead completion at
  interrupt level + an installed read proc (`MIDIInstallRead` export,
  `DefaultMIDIReadProcPtr` fallback), deferral via
  `QueueSecondaryInterruptHandler`) + OMS USB Manager (`shlb`, creator
  `'OMdv'`, exports `TheOMSUSBManagerDispatchTable`/`OMSUSBMgr*` API,
  imports USBGetNextDeviceByClass/USBGetDriverConnectionID/FindSymbol/
  **USBInstallDeviceNotification/USBRemoveDeviceNotification**, and
  locates the class driver via `USBGetDriverConnectionID(deviceRef)` +
  FindSymbol from the device notification's deviceRef — no polling).

**OMS receive-scheduling correction (v0.1 audit, G4-gate BLOCKED until
this lands; implemented + host-tested in the oms-g4-audit spec):** the
first OMS shim draft used the Notification Manager as a periodic poll
timer (`NMInstall` re-arm every tick, with a per-tick
USBGetNextDeviceByClass walk) — **historically false**. PEF disassembly
of both Opcode 2.3.8 USB components shows NMInstall/NMRemove are used
ONLY for user-visible alert notifications (static NMRec with qType=8,
nmStr = alert text, nmResp = response UPP; the manager's alert strings
are the version-mismatch messages); neither uses a timer at all. The
authentic mechanisms, now implemented:

- receive = **push**: the class driver's read completion invokes an
  optional registered event callback (dispatch table v0x0002
  `setEventCallback`); the OMS shim's callback drains the ring and
  calls `OMSReceivedFromPort` (interrupt-level legal, Spec);
- lifecycle = **locate once + USB Manager notifications**:
  USBInstallDeviceNotification (Rev 26 Ch 4: "Use the
  USBInstallDeviceNotification mechanism to be alerted when a device or
  interface is added or removed"; same imports as the real OMS USB
  Manager). kNotifyAdd* re-attaches through the notification's
  deviceRef (no walk); kNotifyRemove* with a matching deviceRef drops
  the cached dispatch pointer before the class driver fragment unloads;
- the walk (`USBGetNextDeviceByClass`) remains only at OMS lifecycle
  transitions (omdvInit/omdvAddDevices/omdvStartMIDI2) for presence
  determination — never per tick.

**Remaining OMS unknowns** (documented, not guessed): how OMS loads the
shlb-form USB Manager (loading contract unverified — we use the fully
verified `'OMdv'` form); the exact header of the PEF-in-`'OMdv'` resource
(inferred "0000 <len>" layout; validated on the G4 against the Roland
binary); the USB Manager notification parameter-block `pbVersion` value
(Rev 26 defines no constant; Apple's StorageClassShim.c sample leaves it
unset — the shim passes 0, to be confirmed on the G4); the
`pb->usbDeviceRef` semantics for INTERFACE notification events (the
event VALUES are disassembly-verified; the ref is expected to be the
containing device's ref — hardware-verify on the G4, see
docs/g4-handoff.md gate 5).

## FreeMIDI

**M4 research gate PARTIAL (file format + architecture verified; driver
message protocol NOT authenticated — research-only, no implementation).**
Primary material (provenance in `~/research/oms/PROVENANCE.md`):

1. **FreeMIDI 1.45 full installer** (Sept 2000; Macintosh Garden,
   `Install_FreeMIDI_1.45.sit`, MD5 `f6e55588fb68925fd74f34614d4b8f8d`);
   extracted with `unvise`.
2. **Roland SC-8850 USB FreeMIDI driver** (Nov 1999; archive.org
   `roland-sc-8850-driver-tools`, `sc8850_usb_fm_v20e.hqx`) + its README.
3. **MOTU USB FreeMIDI Driver** binary (from 1.45).

Verified facts (full table in `spec/oms/requirements.md`):

- FreeMIDI system = "FreeMIDI System Extension" (INIT), "FreeMIDI Setup"
  (APPL/FMSs), "FreeMIDI PowerPlug" (`shlb`/`FMS_` — glue importing ~200
  `FMS*` entry points incl. FMSGetPortInterface/FMSSetPortInterface/
  FMSReadInputQueue/FMSSendMidi/FMSGetDriverInfo), and a **FreeMIDI Folder**
  (inside the System Folder) holding the drivers.
- FreeMIDI drivers: type **`'DDef'`** (hardware/softsynth; creators e.g.
  `mUSB`, `SCCd`, `APPd`, `Digi`, `FMss`) or **`'IDvr'`** (serial
  interfaces; `StdD`, `OS4d`, `MUxp`, `MTPd`, ...); resource-fork-only.
  Driver resources: **`'Code'`** (code — PEF container verified), **`'DDef'`**
  (params), signature-type resource, icons, BNDL/FREF, vers.
- Driver model: InterfaceDriver → PortDriver → MIDIPort; the System
  Extension's `NewStyleOutputChannel` does cable-byte output
  (DriverGetBuffer/SendNextCableByte); the MOTU USB FreeMIDI Driver exports
  `TheMOTUShimInterface` and finds the `ndrv`/`usbd` class driver via the
  USB Manager API + Name Registry + FindSymbol.
- Coexistence: FreeMIDI 1.4x can use OMS 2.0+ as a backend, ships an "OMS
  Emulator" and its own OMS drivers (`'OMdv'`/`MOTU`, `'OMdv'`/`mUSB`).

**UNVERIFIED (blocks implementation)**: the FreeMIDI driver message protocol
(what FreeMIDI sends to a `'DDef'` driver's `'Code'` entry; the `'DDef'`
params layout; the receive call). No FreeMIDI SDK was found (archive.org
search: 0 hits; no SDK folder in the 1.42–1.45 installers). Would require
disassembly of the FreeMIDI System Extension or an SDK acquisition. See
`docs/freemidi-driver-research.md`.

## Period drivers

- [x] Behavior of existing period USB MIDI drivers (MOTU, Roland, Yamaha,
      Opcode, etc.): how they enumerate, how they present ports to OMS and
      FreeMIDI, and how they handle hotplug. — M4: Opcode OMS 2.3.8
      (ndrv + OMS USB Manager shlb + dispatch-table exports), Roland
      SC-8850 (ndrv + `'OMdv'` OMS driver with PEF code + `'DDef'` FreeMIDI
      driver), MOTU FreeMIDI 1.45 (ndrv + DDef/IDvr drivers + PowerPlug
      glue). All documented in the OMS/FreeMIDI sections above and in
      `~/research/oms/PROVENANCE.md`. Hot-plug behavior of those drivers is
      not documented in the sources we hold (out of scope for M4).

## Source standards

1. Prefer original Apple developer documentation and SDK headers.
2. Prefer original Opcode material for OMS.
3. Prefer original MOTU material for FreeMIDI.
4. Contemporary manuals from hardware vendors are useful secondary evidence.
5. Archive mirrors are acceptable when originals are unavailable; record
   provenance.
6. Do not treat modern macOS documentation as evidence for Classic Mac OS 9
   APIs.
7. Record sources here as they are found.

## Notes

* Nothing in `classic/`, `oms/`, or `freemidi/` may reference an API that is
  not verified against a real historical declaration or documentation source.
* Marker convention in source: `TODO(classic-usb-api)`, `TODO(oms-api)`,
  `TODO(freemidi-api)`, `TODO(classic-service-api)`.

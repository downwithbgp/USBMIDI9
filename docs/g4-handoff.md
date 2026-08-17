# G4 build-and-test handoff (M4 OMS)

This is the precise handoff for building and testing the OMS driver on the
Power Mac G4 (CodeWarrior Pro 5.3, Mac OS 9, AFP-mounted working tree).
Read `docs/research.md` "OMS" and `~/research/oms/PROVENANCE.md` first:
every requirement below traces to a verified source.

## What to build

Two new CodeWarrior targets, in addition to the existing USBMIDI9 driver
and Probe targets:

### Target A — USBMIDI9 OMS Driver (the OMS shim)

A **CFM shared library** (PPC) whose **PEF container becomes the `'PPCC'`
1 code resource** of the OMS driver file (the authenticated native-PPC
carrier: OMS 2.3.8 loads `'PPCC'` codeResID first and loads the fragment
via GetDiskFragment; the `'OMdv'` path is the 68K fallback and must NOT
hold a PEF).

Sources (already in the tree; compile-checked on Linux):

- `oms/oms_driver.c` — the omdv* message dispatch + device registration
- `oms/oms_rx.c` — receive path (drain -> convert -> 68K bridge)
- `oms/oms_tx.c` — send hook (chunk re-chunking; transport seam drops)
- `core/midi_stream.c` — the neutral USB-MIDI stream converter
- `core/packets.c` — USB-MIDI Event Packet encode/decode
  (`um9_packet_decode`/`um9_packet_encode`); **REQUIRED membership**:
  `midi_stream.c` calls both routines, so the OMS PEF target must list
  `core/packets.c` explicitly — the Linux build composes it
  automatically (`Makefile` CORE_SRCS), which is why the first G4 link
  gate failed with `um9_packet_decode`/`um9_packet_encode` undefined
  (from `um9_rx_packet`/`um9_tx_message` in midi_stream.c) while every
  host test passed. `packets.h` needs no access-path change: it is
  included by `midi_stream.h`, which already resolves via
  `{Project}::core:`.
- `oms/oms_driver.h` — shared declarations

Access paths (CodeWarrior), **in this order, and nothing else**:

- `{Project}::classic:` — `usbmidi9_dispatch.h`, `ring.h`
- `{Project}::oms:` — `oms_driver.h`
- `{Project}::core:` — `midi_stream.h`
- `OMS SDK:Headers:` — `OMS.h`, `OMSDriver.h`, `OMSDrvUPPs.h`,
  `OMSTypes.h` (`~/research/oms/sdk/OMS 2.0 SDK 28-Jan-98/Headers/`)
- `USB DDK ...:Interfaces:` — the authentic `USB.h`
- plus CodeWarrior's default Universal Headers (Mac OS Support) for
  `MacTypes.h`, `MacErrors.h`, `Notifications.h`, `Memory.h`,
  `OSUtils.h`, `CodeFragments.h`, `DriverServices.h`

**Do NOT add the repository root** (the AFP-mounted tree) and **never
search `host-check/`**: the Linux-only stub tree lives at the top level
in `host-check/` and must never be on a G4 access path. The first real
build failed with `identifier 'UInt32' redeclared` and `tag 'OMSDevice'
redefined` because a `{Project}::classic:` access path reached the old
`classic:host-check:` stubs (`MacTypes.h`, `OMS.h`, `OMSDriver.h`) and
they were included alongside the authentic OMS SDK headers. The stubs
are used only by `make check-classic` on Linux — do NOT use the Linux
stub headers on the G4, and do not add include guards to the authentic
SDK headers.

After re-setting the access paths, **invalidate the target's
precompiled-header cache**: the OMS PEF target's `TargetDataMacOS.tdt`
(present in the AFP-synced project tree) still lists a `:host-check`
entry in its Access Paths/PCH state — delete or force-rebuild it so a
stale PCH cannot resurface the collision.

Libraries: link **`OMS SDK:Libraries:OMSGluePPC.lib`** — the authentic
CodeWarrior PowerPC OMS glue (SDK `Libraries/README.OMSGlue`: "OMSGluePPC.lib -
for CodeWarrior PowerPC"). The shim imports `LinkToOMSGlue` and
`OMSGetCallAddress` through that glue; everything else is provided by
the OMS system at load time. Also link **the USB DDK
`Libraries/USBManagerLib`** — the CodeWarrior import library that
exports the four USB Manager calls the shim makes
(`USBGetNextDeviceByClass`, `USBGetDriverConnectionID`,
`USBInstallDeviceNotification`, `USBRemoveDeviceNotification`).
Evidence: (1) the authentic DDK 1.4.1 `Libraries/USBManagerLib` is a
`Joy!peffpwpc` PPC container whose symbol list (read directly) is
`USBGetDeviceDescriptor USBGetInterfaceDescriptor
USBGetDriverConnectionID USBGetNextDeviceByClass
USBInstallDeviceNotification USBRemoveDeviceNotification
USBDeviceRefToBusRef USBExpertNotifyParent USBDriverNotify
USBAddDriverForFSSpec USBAddShimFromDisk USBReferenceToRegEntry` —
all four shim imports are in it; (2) the real MOTU USB FreeMIDI Driver
(PEF import list, docs/freemidi-driver-research.md) imports exactly
`USBGetNextDeviceByClass, USBGetDriverConnectionID,
USBInstallDeviceNotification, USBRemoveDeviceNotification,
USBGetDeviceDescriptor` from **USBManagerLib** — a shipping PPC USB
MIDI driver with the same function set; (3) the shim makes **no USL
transfer calls** (no USBBulkRead/USBBulkWrite — those stay in the
low-level class driver), so **USBServicesLib must NOT be added** unless
a later linker error names a symbol it actually exports. Rev 26
Ch 6 (p. 183) distinguishes the two: the USB Manager functions come
from USBManagerLib (USBFamilyExpertLib exports only the expert
interface). **`OMSReceivedFromPort` is NOT an import on
PPC**: it is a 68K assembly routine (authentic `OMSDriver.h` declares it
only `#ifndef powerc` with `#pragma parameter OMSReceivedFromPort(__A1,
__D0)`; the Spec: "68K assembly language routine", A1 = pkt, D0 =
sourceIORefNum, may be called at interrupt level). The shim resolves it
once at omdvInit — `LinkToOMSGlue()` then
`OMSGetCallAddress(callOMSReceivedFromPort)` (= 112, OMSGlueProcs.h) —
caches the 68K address and invokes it through
`CallUniversalProc(addr, kRegisterBased |
REGISTER_ROUTINE_PARAMETER(1, kRegisterA1, SIZE_CODE(sizeof(OMSPacket *)))
| REGISTER_ROUTINE_PARAMETER(2, kRegisterD0, SIZE_CODE(sizeof(short))))`
(`<MixedMode.h>` resolves from the Universal Interfaces access path;
`<OMSGlueProcs.h>` from `OMS SDK:Headers:`). If resolution fails the
driver still loads and receive delivery is disabled (drain continues).
USB calls (lookup/lifecycle only — **no USL transfer calls**):
- `USBGetNextDeviceByClass`, `FindSymbol`, `GetZone`/`SetZone`/`SystemZone`
  (one-time dispatch-table location at OMS lifecycle transitions);
- `USBGetDriverConnectionID` (attach via the device notification);
- `USBInstallDeviceNotification`/`USBRemoveDeviceNotification` (USB
  Manager device lifecycle — the same API Opcode's OMS 2.3.8 OMS USB
  Manager imports; USBManagerLib);
- **NO** `NMInstall`/`NMRemove`: the Notification Manager is an alert
  API, not a timer (verified by disassembly of Opcode's own USB
  components; see docs/host-check-audit.md). There is no poll task —
  receive is push: the class driver's read completion invokes the shim's
  registered event callback (dispatch table v0x0002 `setEventCallback`),
  which drains the ring and delivers each message through the cached
  68K `OMSReceivedFromPort` address via `CallUniversalProc`
  (interrupt-level legal per the Spec; see "Libraries" above).

The shim requires the **v0x0002** `USBMIDI9DispatchTable` (the class
driver must be rebuilt from this tree — the Probe, with its 0x0001
minimum, still works against the new driver).

Exports (linker): use `codewarrior/USBMIDI9_OMS.exp` — exactly one
symbol: **`main`** (the driver entry
`OMSCALLBACK(long) main(short msg, long par1, long par2)`; the Spec's
documented entry name).

### OMS PEF target manifest (canonical — G4 link gate 2026-08-16)

The first G4 link gate failed with exactly six undefined symbols,
classified as **CodeWarrior project-definition defects** (not source
defects — the Linux build/test system composes the same closure
automatically from the Makefile, so no host test can catch a manually
created .µ target that lists fewer sources/libraries). The canonical
membership:

**G4 status (2026-08-16): PEF gate PASS** — the OMS PEF target links on
the real G4: **0 errors, 43 warnings** (CodeWarrior's usual
unsigned/short-conversion noise; none in the shim's contract surface).
The only membership changes needed were the two below (core/packets.c +
USBManagerLib); no C/ABI change was required or made after the source
gates.

**Sources (5):** `oms/oms_driver.c`, `oms/oms_rx.c`, `oms/oms_tx.c`,
`core/midi_stream.c`, `core/packets.c`.

**Libraries (imports):** `OMS SDK:Libraries:OMSGluePPC.lib`
(`LinkToOMSGlue`, `OMSGetCallAddress`), USB DDK
`Libraries:USBManagerLib` (`USBGetNextDeviceByClass`,
`USBGetDriverConnectionID`, `USBInstallDeviceNotification`,
`USBRemoveDeviceNotification`), `InterfaceLib` + the MSL runtime
libraries from the existing PPC shared-library target (CodeWarrior
defaults).

**Exports:** `codewarrior/USBMIDI9_OMS.exp` — exactly `main`.

**Must stay OUT of this target:** `oms/oms_driver.r` (Rez'd separately,
Target B); `core/descriptors.c`, `core/ports.c` (not referenced by the
shim closure); `classic/usb_driver.c`, `classic/usb_service.c`,
`classic/ring.c` (the low-level class driver is a separate PEF);
`USBServicesLib`, `USBPowerClassLib`, `CursorDevicesLib` (no USL
transfer calls in the shim — add a library only when a linker error
names a symbol it exports).

Symbol -> provider map for the six gate errors:

| Undefined symbol | Provider |
|---|---|
| `um9_packet_decode`, `um9_packet_encode` | `core/packets.c` (missing from target) |
| `USBGetNextDeviceByClass`, `USBGetDriverConnectionID`, `USBInstallDeviceNotification`, `USBRemoveDeviceNotification` | DDK `Libraries/USBManagerLib` (missing from target) |

Apply both membership changes in the CodeWarrior project (Remove Object
Code -> Make on the G4). No C source change is required.

### Target B — resources for the OMS driver file

`oms/oms_driver.r` is **NOT part of the OMS PEF target** (Target A
compiles the C sources only); it is compiled separately here.
Compile `oms/oms_driver.r` with Rez (CodeWarrior's or MPW's) to produce
the resource fork: `'OMdi'` 128 (id 0x7F10, xxportNumB = 1 = the
'PPCC' codeResID, flags 0, compat level 1), `'SICN'` 128 (keyboard
icon + mask), `'vers'` 1. The `'PPCC'` 1 code resource is the raw
Target-A PEF imported by `oms/ppcc.r` — add it to the resource fork as
a data resource (see "The 'PPCC' 1 resource" below).

**Resource gate (2026-08-16):** `oms/oms_driver.r` now starts with
`#include "Types.r"` — the authentic Universal Interfaces 3.3.2 Rez
preamble (resolved from the Universal Interfaces RIncludes access path
in CodeWarrior's Rez settings). Nothing standard is hand-declared:
- `'vers'` template + `development/alpha/beta/final` stage constants —
  authentic `MacTypes.r` (via `Types.r`);
- `'SICN'` template — authentic `Icons.r` (via `Types.r`): an array of
  32-byte hex strings, one per 16x16 bitmap (element 1 = icon, element
  2 = mask). Rez body syntax per Apple TN1019 (canonical): the SICN
  array field is written with braces and `$"..."` hex strings — plain
  `"..."` strings and missing inner braces fail with `Expected '{'`;
- `verUS` region code — authentic `Script.r` (via `IntlResources.r` via
  `Types.r`).
`'OMdi'` has no Apple template (Opcode's own resource) and is **not**
declared as a typed Rez template: the real G4 build proved Rez packs
`boolean` fields as **bits**, not as the one-byte `OMSBool` members of
`OMSDriverParams`. The typed template emitted
`7F 10 00 00 00 00 40 00 4C 0C 0C ...` — the 0x40 at byte 6 made the
OMS 2.3.8 loader read codeResID = 0x4000, `Get1Resource('PPCC',
0x4000)` returned NULL, and the driver failed to load with OMS error
**-192**. The resource is written as raw data instead: `data 'OMdi'
(128)` with the exact 16-byte hex string — the canonical Rez `data`
form, the same syntax Apple's own `USBClassDriverIcons.r` (Mac OS USB
DDK 1.4.1) uses. The host test `tests/test_omdi_resource.c` pins the
payload bytes and forbids the typed template from returning. If Rez
complains about `Types.r` not found, add the Universal Interfaces
`RIncludes` folder to the Rez search path (same folder that provides
the C headers' `CIncludes`).

**Target gate (2026-08-16, real G4):** a resource-only target containing
`oms/oms_driver.r`, configured with **Linker: None**, Makes with **0
errors**. The Rez source PASSES and the resource-only target
configuration is valid; **no 68K linker / `main` workaround is
required**. That target validates the source and the Rez settings only:
with Linker: None there is no link step to assemble an output file (CW's
Rez is a plug-in compiler whose output goes into the project build — it
cannot write an external file; CW Pro 4 "Targeting Mac OS", ch. 8), so
the final driver file is produced by the documented mechanism below.

**OMdi byte gate — FAIL, then FIX (real G4, ResEdit byte inspection):**
the installed `'OMdi'` 128 read `7F 10 00 00 00 00 40 00 4C 0C 0C ...`
— the typed Rez template packed the `boolean` fields as bits, so the
word at +6 (`xxportNumB`, the 'PPCC' codeResID) became `0x4000` instead
of `1` and OMS -192 ("The OMS driver ... could not be loaded") was
exactly expected: `Get1Resource('PPCC', 0x4000)` returned NULL. Fixed
by replacing the typed template with a raw `data 'OMdi' (128)`
resource. The required logical 16 bytes are:
`7F 10 00 00 00 00 00 01 00 01 00 00 00 00 00 00`
(id 0x7F10, xxisSmart 0, hasMenuOrWindows 0, xxportNumM 0,
xxportNumB 1, flags 0, driverCompatibilityLevel 1, reservedFlags[6] 0).
`tests/test_omdi_resource.c` (host, `make test`) pins these bytes and
bans the typed template. The 4C 0C ... tail showed the same
non-byte-exact emission in the trailing fields; no mechanism was
pursued — raw data removes the ambiguity. Next G4 gate: rebuild the
resource target and byte-inspect `OMdi` 128 in ResEdit **before
installation**; do not runtime-test until the bytes match AND `PPCC` 1
still reads 9501 bytes starting `Joy!peffpwpc`.

### Assembling the OMS driver file

**Outer-container gate — PASS (2026-08-16, real G4):** the **MacOS
Merge linker with Project Type = "Resource File"** (CW Pro 4 "Targeting
Mac OS", ch. 9) produced the real `USBMIDI9 OMS Driver` file; ResEdit
shows `'OMdi'` 128, `'SICN'` 128, `'vers'` 1. The container production
mechanism is therefore **proven** (Rez output is merged into the target
output file's resource fork; the panel's **File Name / Creator / Type /
Copy Resources / Skip Resource Types** fields control the artifact).

The **remaining packaging change** is adding `'PPCC'` 1 via
Rez `read` from the Target-A PEF (see below). Final acceptance:

1. `'PPCC'` 1 present, imported byte-for-byte from the Target-A PEF
   (raw PEF container; **no** 4-byte length prefix — the length seen at
   the start of resource records in authentic forks is the resource
   fork's own record framing, not payload).
2. MacOS Merge must **not** skip resource type `PPCC` (review the Skip
   Resource Types list on the MacOS Merge panel; `OMdi`/`SICN`/`vers`
   must not be skipped either).
3. ResEdit inspection requires **exactly**: `OMdi` 128, `PPCC` 1,
   `SICN` 128, `vers` 1. There must be **no** `OMdv` resource (the old
   PEF-in-OMdv packaging is disproven).
4. Do **not** install into System Folder:OMS Folder until step 3
   passes.
5. Finder type `'OMdv'`, creator `'USM9'` — already set via
   CodeWarrior's **Creator/Type fields** on the MacOS Merge panel ("The
   Creator and Type fields let you change the file type and creator of
   the linked output file that your project produces" — CW Pro 4
   "Targeting Mac OS", ch. 3/9), or after the fact with MPW
   `SetFile -t 'OMdv' -c 'USM9'` or ResEdit's Get Info. (The Finder's
   Get Info does not expose type/creator on classic Mac OS.)
6. Name: `USBMIDI9 OMS Driver` (31 chars max is fine); destination for
   testing: **System Folder:OMS Folder**.

### The 'PPCC' 1 resource

The OMS 2.3.8 native-PPC carrier is `'PPCC' <codeResID>`, where
codeResID = `OMSDriverParams.xxportNumB` (the word at +6 of the
`'OMdi'` 128 data; disassembly-verified: `move.w $6(a0),-(a7); A81F`
= Get1Resource('PPCC', xxportNumB)). The authentic OMS Time Manager
component ships `'PPCC'` 1 + `'PROC'` 1; its PPCC logical payload is
the **raw PEF container** — `Joy!peffpwpc` at byte 0, no length
prefix (fork record framing `[be32 length][data]` is added by the
Resource Manager and stripped by Get1Resource; earlier readings of it
as payload were wrong). Resource attributes of the authentic TM
'PPCC' 1 = 0x00 (none).

Produce the same shape:

- Build Target A as a PEF container ("shared library" output); the
  container's main symbol is `main` (exported via
  `codewarrior/USBMIDI9_OMS.exp`; the loader-info string area of the
  2026-08-16 build contains `main` as its main-symbol string).
- **`oms/ppcc.r`** (in the repo) adds the resource with **Rez's `read`
  statement**: `read 'PPCC' (1) "::USBMIDI9_OMS";` — `read` takes the
  file's data fork verbatim as the resource data (Rez language
  reference), so the PEF is embedded byte-for-byte with **no length
  prefix**. The path is relative to Rez's working directory (the
  resource project folder `USBMIDI9:USBMIDI9 OMS Resources:`), so
  `::USBMIDI9_OMS` = `USBMIDI9:USBMIDI9_OMS` = the Target-A PEF
  (no copy needed). Fallback if CW Rez rejects `::`: copy the PEF into
  the project folder and use `read 'PPCC' (1) "USBMIDI9_OMS";`.
- Add `oms/ppcc.r` to the SAME MacOS Merge resource target as
  `oms_driver.r`.
- Ensure **MacOS Merge does not skip resource type `PPCC`**: open the
  MacOS Merge panel (Target Settings) and review the **Skip Resource
  Types** list; remove `PPCC` (and `OMdi`/`SICN`/`vers`) if present.
- There is **no `'OMdv'` resource** in the driver file (the PEF-in-OMdv
  packaging — `oms/omdv.r`, `tools/omdvdata.c` — was removed from the
  repo as disproven: the OMdv path is the 68K fallback and the 4-byte
  prefix it added was fork framing, not payload).
- Fallback (verified by construction): a small Resource Manager tool on
  the G4 — `FSpCreateResFile` / `AddResource` / `UpdateResFile` with
  the **raw PEF bytes** (no prefix) — or ResEdit (manual resource add;
  no practical raw byte import).
- NOT the CW "PPC Code Resource" project type: it builds code from
  source, cannot import an existing PEF byte-for-byte, and emits no
  `'PPCC'` resource. Likewise no 68K Code Resource linker target.
- Validate the result against the authentic Time Manager before
  installing (hexdump of the PPCC 1 resource data, offset 0:
  `4A 6F 79 21 70 65 66 66` = "Joy!peffpwpc" — NOT `00 00 <len>`).

### Exact G4 procedure — add 'PPCC' 1

Complete remaining packaging work. Files needed: `oms/ppcc.r` (in the
repo), the Target-A PEF output file (`USBMIDI9:USBMIDI9_OMS`), and
ResEdit.

1. **Build the Target-A PEF.** Build Target A (the PPC PEF "shared
   library" target) as already done; the output file is
   `USBMIDI9:USBMIDI9_OMS` (its data fork = the PEF container, 9501
   bytes as of the 2026-08-16 build). It must stay at that location —
   `oms/ppcc.r` reads it via the relative path `::USBMIDI9_OMS` from
   the resource project folder.
2. **Remove the obsolete OMdvData artifacts from the resource
   project** (left over from the disproven PEF-in-OMdv packaging):
   - Remove `oms/omdv.r` from the resource target (Project > Remove) —
     the file no longer exists in the repo, so a stale member breaks
     the next Make.
   - Delete the `USBMIDI9:omdvdata:` CodeWarrior project folder, the
     `USBMIDI9 OMS Resources:OMdvData` file, and
     `USBMIDI9 OMS Resources:omdvdata.out` — all untracked leftovers.
3. **Add `oms/ppcc.r` to the resource target.** Select the resource
   target (the MacOS Merge one) in the project window, then **Project >
   Add Files...**, choose `oms/ppcc.r`, **Add**. (It must NOT be a
   member of Target A.)
4. **Check the MacOS Merge panel.** **Project > Target Settings...**
   (or double-click the target), MacOS Merge (68K Linker) panel:
   - Project Type = **Resource File**
   - File Name = `USBMIDI9 OMS Driver`; Creator = `USM9`; Type = `OMdv`
   - **Copy Resources: checked**
   - **Skip Resource Types: must NOT contain `PPCC`** (nor `OMdi`,
     `SICN`, `vers`) — remove them if present.
5. **Make** (**Project > Make**, ⌘K). Rez compiles `oms/ppcc.r` and the
   `read` statement embeds the PEF data fork verbatim as `'PPCC'` 1.
   - If Rez cannot find `USBMIDI9_OMS`: the Rez working directory is
     not the resource project folder, or the `::` path is rejected —
     copy `USBMIDI9_OMS` into the project folder and change
     `oms/ppcc.r` to `read 'PPCC' (1) "USBMIDI9_OMS";` (or use the
     absolute path `USBMIDI9:USBMIDI9_OMS`).
6. **ResEdit inspection (required before install).** Open the output
   `USBMIDI9 OMS Driver` in ResEdit. The resource list must show
   **exactly**:
   - `OMdi` 128
   - `PPCC` 1
   - `SICN` 128
   - `vers` 1
   and **no** `OMdv`. Double-click `OMdi` 128 and verify the **exact
   16 bytes**: `7F 10 00 00 00 00 00 01 00 01 00 00 00 00 00 00` — the
   earlier typed-template build emitted `... 40 00 ...` at bytes 6-7
   (codeResID 0x4000) and OMS failed with -192, so this byte check is
   **required before install**. Optionally double-click `PPCC` 1: the
   data must start `4A 6F 79 21 70 65 66 66` = "Joy!peffpwpc" — **no**
   length prefix — total size = the PEF size (9501 bytes for the
   2026-08-16 build).
7. **Install only after step 6 passes:** copy the file into
   **System Folder:OMS Folder**, then run the runtime gates (probe
   regression, OMS driver loads, OMS receives MIDI).

If OMS 2.3.8 does not load the driver, first check: file type/creator,
resource IDs (128/1), the `'OMdi'` 128 exact bytes (`7F 10 00 00 00 00
00 01 00 01 00 00 00 00 00 00` — the word at +6 must be `00 01`, the
'PPCC' codeResID), and the PPCC 1 data shape (`4A 6F 79 21` =
"Joy!peffpwpc" at byte 0, no length prefix) against the authentic OMS
Time Manager PPCC 1. The
'OMdi'/'PPCC' loading mechanism itself is verified by disassembly of
the OMS 2.3.8 library (loadCode pref=2 = Get1Resource('PPCC',
xxportNumB), then materialize + GetDiskFragment).

## Gate order (test on the G4, in this order)

**PPCC entry gate — CURRENT (2026-08-17, G4 result: type-2/type-3
crash in OMS Setup during Search, with or without the device).**
The byte gates pass and the driver loads (-192 gone); the first
native-PPC entry call crashes. Root-cause analysis (evidence in
docs/oms-ppcc-entry-crash.md): the authentic OMS Time Manager PPCC's
`.main` export points at plain PPC code, while our PEF's `main` symbol
points at a **transition vector** (`[code,TOC]` data object) in the
loader info — executing the vector as code reproduces the observed
type-2/type-3. Next G4 gate = the minimal-entry diagnostic
(`USBMIDI9_OMS_DIAG_MINIMAL_ENTRY` in oms/oms_driver.c) + the
plain-code-export fix hunt (PPC PEF panel / .exp), with the main-symbol
byte gate (`7c 08 02 a6`) checked BEFORE install.

0. **PEF link gate — PASS (2026-08-16)**: OMS PEF target builds and
   links: 0 errors, 43 warnings. Membership per the manifest above.
1. **Resource-assembly gate — CURRENT (outer-container PASS
   2026-08-16)**: Rez source PASS, resource-only target config PASS
   (Linker: None, 0 errors), and the **MacOS Merge / Project Type =
   Resource File** container mechanism PASS (real file with `'OMdi'`
   128, `'SICN'` 128, `'vers'` 1 verified in ResEdit). Remaining —
   the packaging change: add `'PPCC'` 1 via Rez `read` from the
   Target-A PEF (`oms/ppcc.r` does the read; `::USBMIDI9_OMS` resolves
   to `USBMIDI9:USBMIDI9_OMS`), remove the stale `oms/omdv.r` member
   and the untracked OMdvData/omdvdata artifacts, ensure MacOS Merge
   does not skip `PPCC`, then ResEdit must show **exactly** `OMdi`
   128 / `PPCC` 1 / `SICN` 128 / `vers` 1 (and **no** `OMdv`), and
   `OMdi` 128 must byte-inspect as `7F 10 00 00 00 00 00 01 00 01 00
   00 00 00 00 00` (the typed-template build failed this with
   `... 40 00 ...` at +6 — OMS -192). Do not
   install into System Folder:OMS Folder until that inspection passes.
   Then continue with the runtime gates:
2. **Probe regression**: the existing Probe still enumerates and receives
   Keystation packets (nothing broke — the Probe's 0x0001 minimum accepts
   the v0x0002 driver).
3. **OMS driver loads**: install `USBMIDI9 OMS Driver` in the OMS Folder;
   open OMS Setup → File → New Studio Setup → the interface search finds
   "USBMIDI9 Port 1" (the SICN icon should appear). The driver must load
   with or without the Keystation attached (omdvInit no longer fails on a
   missing device).
4. **OMS receives MIDI**: in OMS Setup, create the studio setup with the
   Keystation connected to "USBMIDI9 Port 1"; open an OMS application
   (ReBirth RB-338 is the acceptance target) and play the keyboard. If
   OMS Setup's Test Studio mode exists (omdvTestDevice), click the device
   there first. Receive is push-based (class driver → event callback →
   OMSReceivedFromPort); there is no poll task to observe.
5. **ReBirth acceptance**: Keystation -> USBMIDI9 -> OMS -> ReBirth.
6. **Hot-plug**: with MIDI running, unplug/replug the Keystation. The
   shim learns of the removal via the USB Manager notification
   (kNotifyRemove*) and drops the cached dispatch pointer; on replug the
   add notification re-attaches (no device-list walk). Re-open the OMS
   Setup to re-add devices if they do not reappear automatically. The
   known unrelated-device hot-plug freeze (docs/classic-usb-driver.md
   §9.9) is still open — but the OMS shim no longer contributes a 60 Hz
   USBGetNextDeviceByClass walk (the previous poll task is deleted).
   **Hardware-verify item**: the notification event VALUES
   (kNotifyAddDevice=0/RemoveDevice=1/AddInterface=2/RemoveInterface=3)
   are disassembly-verified from the real OMS USB Manager, but the
   `pb->usbDeviceRef` semantics for INTERFACE events are not — if the
   USB Manager reports a different ref for kNotifyRemoveInterface than
   the one returned by USBGetNextDeviceByClass/USBGetDriverConnectionID,
   the detach match would not fire and the cached dispatch pointer would
   dangle until the next OMS message. Confirm with a debug print in the
   notification callback on the G4; the fallback (drop the table on any
   kNotifyRemove* when no other USBMIDI9 is attached) is a one-line
   change.
7. **Output**: NOT testable yet (the send hook drops; the bulk-OUT path
   is a separate milestone — `TODO(oms-output)`).

If the driver is not discovered by OMS Setup, capture: the driver file's
type/creator (Get Info), the resource list (ResEdit), and whether OMS
Setup's search sees anything at all. Report back with those.

## Files to refresh on the G4 for the EXISTING targets

The USBMIDI9 class driver MUST be rebuilt before the OMS driver is
built: since the previous handoff, `classic/usb_driver.c`,
`classic/usb_driver.h` and `classic/usbmidi9_dispatch.h` changed — the
dispatch table is now **v0x0002** with a new `setEventCallback` entry
(the interrupt-level push hook the OMS shim registers; see
`classic/usbmidi9_dispatch.h`). The OMS driver requires v0x0002.

Required G4 sequence:

1. **Rebuild the USBMIDI9 class driver** (the existing CodeWarrior
   driver target, from the current tree).
2. **Verify ndrv/usbd**: the rebuilt driver file has Finder type
   `'ndrv'`, creator `'usbd'` and exports `TheUSBDriverDescription`
   (Get Info / ResEdit / CodeWarrior project settings).
3. **Probe regression**: with the new driver installed, the existing
   Probe must still enumerate and receive Keystation packets (the
   Probe's 0x0001 dispatch minimum accepts the v0x0002 driver;
   `probe/probe.c` changed only by the `kProbeMinDispatchTableVersion`
   constant — behavior identical, rebuilding the Probe target is
   optional but cheap).
4. **Then build/install the OMS driver** (Targets A and B above).

The `-Dmain` host-check naming is Linux-only, irrelevant on the G4.

## Reference files on the G4/Linux

- `~/research/oms/PROVENANCE.md` — every source + hash.
- `~/research/oms/sdk/OMS 2.0 SDK 28-Jan-98/` — the SDK (headers, Spec,
  SampleCell driver).
- `~/research/oms/oms238c/` — extracted OMS 2.3.8 (component inventory,
  driver resource forks, the INIT).
- `~/research/oms/sc8850/` — Roland SC-8850 OMS/FreeMIDI packages (the
  `'XCOF'` 10000 raw-PEF resource reference; the `'DDef'` FreeMIDI
  driver reference).
- `~/research/oms/fm145x/` — FreeMIDI 1.45 (DDef/IDvr references).

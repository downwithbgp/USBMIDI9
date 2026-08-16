# G4 build-and-test handoff (M4 OMS)

This is the precise handoff for building and testing the OMS driver on the
Power Mac G4 (CodeWarrior Pro 5.3, Mac OS 9, AFP-mounted working tree).
Read `docs/research.md` "OMS" and `~/research/oms/PROVENANCE.md` first:
every requirement below traces to a verified source.

## What to build

Two new CodeWarrior targets, in addition to the existing USBMIDI9 driver
and Probe targets:

### Target A — USBMIDI9 OMS Driver (the OMS shim)

A **CFM shared library** (PPC) whose **PEF container becomes the `'OMdv'`
128 code resource** of the OMS driver file.

Sources (already in the tree; compile-checked on Linux):

- `oms/oms_driver.c` — the omdv* message dispatch + device registration
- `oms/oms_rx.c` — receive path (drain -> convert -> OMSReceivedFromPort)
- `oms/oms_tx.c` — send hook (chunk re-chunking; transport seam drops)
- `core/midi_stream.c` — the neutral USB-MIDI stream converter
- `oms/oms_driver.h` — shared declarations

Access paths (CodeWarrior): add the repo root (the AFP-mounted tree) and
`classic/`; the sources use logical include names (`<OMS.h>`,
`<OMSDriver.h>`, `<Notifications.h>`, `"usbmidi9_dispatch.h"`,
`"core/midi_stream.h"`). The OMS headers (`OMS.h`, `OMSDriver.h`,
`OMSDrvUPPs.h`, `OMSTypes.h`) come from the **Opcode OMS 2.0 SDK**
(`~/research/oms/sdk/OMS 2.0 SDK 28-Jan-98/Headers/`) — do NOT use the
Linux stub headers on the G4.

Libraries: link against nothing OMS-specific — the driver calls OMS via
`OMSReceivedFromPort`/`OMSOpenDriverResFile`/`OMSCloseDriverResFile`, which
are provided by the OMS system itself at load time (the OMS runtime
exports them; the 68k-glue story from the SDK's `OMSDriver.h` does not
apply to a PPC driver calling them directly — verify with a link test; if
the linker cannot resolve them, obtain the OMS glue from the SDK's
`Libraries/` and link it). USB calls: `USBGetNextDeviceByClass`,
`FindSymbol`, `GetZone`/`SetZone`/`SystemZone` (lookup only — no USL
transfer calls), plus `NMInstall`/`NMRemove` from InterfaceLib.

Exports (linker): use `codewarrior/USBMIDI9_OMS.exp` — exactly one
symbol: **`main`** (the driver entry
`OMSCALLBACK(long) main(short msg, long par1, long par2)`; the Spec's
documented entry name).

### Target B — resources for the OMS driver file

Compile `oms/oms_driver.r` with Rez (CodeWarrior's or MPW's) to produce
the resource fork: `'OMdi'` 128 (id 0x7F10, flags 0, compat level 1),
`'SICN'` 128 (keyboard icon + mask), `'vers'` 1. The `'OMdv'` 128 code
resource is the PEF container from Target A — add it to the resource fork
as a data resource (see "The 'OMdv' resource" below).

### Assembling the OMS driver file

The final `USBMIDI9 OMS Driver` file (resource-fork only):

1. Resource fork = Rez output + the `'OMdv'` 128 resource (Target A PEF).
2. Finder type `'OMdv'`, creator `'USM9'` (Set File Info in CodeWarrior's
   project settings or ResEdit).
3. Name: `USBMIDI9 OMS Driver` (31 chars max is fine).
4. Destination for testing: **System Folder:OMS Folder**.

### The 'OMdv' resource

The Roland SC-8850 OMS driver is the format reference
(`~/research/oms/sc8850/usboms-x/x/SC8850-USB.rsrc`): its `'OMdv'` code
resource is a **PEF container** (`Joy!peffpwpc`) preceded by a 4-byte
length header (`00 00 <len>` big-endian = the PEF container length).
Produce the same shape:

- Build Target A as a PEF container ("shared library" output); the
  container's exported symbol is `main`.
- Wrap: 2 bytes `00 00` + 2 bytes big-endian container length + the
  container bytes → this is the `'OMdv'` 128 resource data.
- The CodeWarrior "PPC code resource" mechanism (if available in CW Pro
  5.3) may do this directly; otherwise assemble with ResEdit or a small
  tool on the G4. Validate the result against the Roland file before
  installing (hexdump offset 0: `00 00 <len> 4A 6F 79 21`).

If OMS 2.3.8 does not load the driver, first check: file type/creator,
resource IDs (128), the `'OMdi'` id/flags bytes, and the PEF header shape
against the Roland reference. The 'OMdi'/'OMdv' loading mechanism itself is
verified (the OMS INIT references these resource types; real 2.3.8 and
Roland drivers use them).

## Gate order (test on the G4, in this order)

1. **Probe regression**: the existing Probe still enumerates and receives
   Keystation packets (nothing broke).
2. **OMS driver loads**: install `USBMIDI9 OMS Driver` in the OMS Folder;
   open OMS Setup → File → New Studio Setup → the interface search finds
   "USBMIDI9 Port 1" (the SICN icon should appear).
3. **OMS receives MIDI**: in OMS Setup, create the studio setup with the
   Keystation connected to "USBMIDI9 Port 1"; open an OMS application
   (ReBirth RB-338 is the acceptance target) and play the keyboard. If
   OMS Setup's Test Studio mode exists (omdvTestDevice), click the device
   there first.
4. **ReBirth acceptance**: Keystation -> USBMIDI9 -> OMS -> ReBirth.
5. **Hot-plug note**: with MIDI running, unplugging/replugging the
   Keystation exercises the poll re-locate; the known unrelated-device
   hot-plug freeze (docs/classic-usb-driver.md §9.9) is still open.
6. **Output**: NOT testable yet (the send hook drops; the bulk-OUT path
   is a separate milestone — `TODO(oms-output)`).

If the driver is not discovered by OMS Setup, capture: the driver file's
type/creator (Get Info), the resource list (ResEdit), and whether OMS
Setup's search sees anything at all. Report back with those.

## Files to refresh on the G4 for the EXISTING targets

Nothing changed in `classic/` or `probe/` this session. The existing
driver and Probe targets are untouched and do not need rebuilding unless
you want the `-Dmain` host-check naming — that is Linux-only, irrelevant
on the G4.

## Reference files on the G4/Linux

- `~/research/oms/PROVENANCE.md` — every source + hash.
- `~/research/oms/sdk/OMS 2.0 SDK 28-Jan-98/` — the SDK (headers, Spec,
  SampleCell driver).
- `~/research/oms/oms238c/` — extracted OMS 2.3.8 (component inventory,
  driver resource forks, the INIT).
- `~/research/oms/sc8850/` — Roland SC-8850 OMS/FreeMIDI packages (the
  `'OMdv'` PEF format reference; the `'DDef'` FreeMIDI driver reference).
- `~/research/oms/fm145x/` — FreeMIDI 1.45 (DDef/IDvr references).

# M1B — Interface class driver + Probe (source gate)

Status: **implementation complete (source gate).** Acceptance for the
SOURCE gate is the M1B definition of done (items 1-10). Item 11 (real
hardware) is explicitly NOT claimable from this repo — it requires
CodeWarrior on the Power Mac G4, Mac OS 9, and a real Keystation; this
spec only produces the source, build wiring, and host verification
(hardware checklist: docs/classic-usb-driver.md §9.5).

## Context

M1A (docs/classic-usb-driver.md) verified the Classic Mac OS USB driver
model from primary sources (Rev 26 API reference + authentic USB DDK 1.4.1
kit at ~/research/usbddk/, outside the repo). M1B implements the driver and
probe against that verified surface. Every API/constant/pattern below was
re-verified against the actual DDK 1.4.1 kit during M1B planning
(KeyboardModule.c, UniversalModule.c, CompositeClassDriver.c,
HIDReader.c, PrinterClassDriver.c, USB.h, USBClassDriver.exp,
MacErrors.h snapshot).

## Verified facts that shape the design (from the kit, not memory)

1. Interface drivers DO call `USBConfigureInterface(&pb)` on the
   interfaceRef passed to initializeInterfaceProc; then `USBFindNextPipe`
   (usbFlags=kUSBIn, usbClassType=kUSBBulk) returns the pipe ref in
   `pb.usbReference` and the endpoint max packet size in
   `pb.usb.cntl.WValue`. [KeyboardModule.c, UniversalModule.c; Rev 26
   p. 113-115]
2. USBBulkRead: usbReference=pipeRef, usbReqCount=MaxPacketSize (64),
   usbBuffer aligned, usbFlags=0, usbCompletion set. Short/full packet
   terminates; check usbActCount. [Rev 26 p. 128-129]
3. One USBPB embedded FIRST in a driver struct; `pb.pbLength =
   sizeof(whole struct)`; `InitParamBlock`-style re-init per state;
   refcon = state selector; `kCompletionPending` bit set before each USL
   call, cleared in the completion; completion resubmits unless
   kReturnFromDriver / driverRemovalPending. [KeyboardModule.c,
   UniversalModule.c]
4. Removal: notify(kNotifyDriverBeingRemoved) sets removalPending, aborts
   pipes via USBAbortPipeByReference (on abort error, clear the pending
   bit — "don't expect the completion"), returns kUSBDeviceBusy while any
   completion is pending; completions stop resubmitting; finalize returns
   noErr when drained. [KeyboardModuleHeader.c; Rev 26 p. 70-71]
5. USBAllocMem (async, PB-based: usbReqCount in, usbBuffer/usbActCount
   out) for resident memory; USBDeallocMem with `usbCompletion =
   kUSBNoCallBack` at finalize. [UniversalModule.c InterfaceExit; Rev 26
   p. 150]
6. The dispatch finalize proc receives (ref, pDesc); UniversalModule's
   finalize passes that first arg to InterfaceExit AS the interface ref —
   per-connection finalize, so finalize can identify its instance by
   interfaceRef. [UniversalModule.c/Header.c]
7. kNotifyDriverBeingRemoved carries no documented identifying data in
   `pointer`/`refcon` (all samples ignore them). M1B therefore drains
   ALL instances on removal (exact for one device — the acceptance
   target; multi-device teardown-on-one-unplug is a documented M1B
   limitation, M2 work). No crash risk: aborts are handled per rule 4.
8. Probe lookup: USBGetNextDeviceByClass(&ref, &connID, class, subclass,
   protocol) from kNoDeviceRef; FindSymbol(connID, "\pSymbolName", (Ptr
   *)&dest, &symClass) inside SetZone(SystemZone()); HIDReader.c is the
   shipped example of exactly this pattern (including the 4-arg
   FindSymbol call shape used by DDK-era code). [HIDReader.c; Rev 26
   p. 83-85, 179]
9. USB.h 1.4.1 DOES declare the USL prototypes (USBConfigureInterface,
   USBFindNextPipe, USBBulkRead, USBAbortPipeByReference, USBAllocMem,
   USBDeallocMem, ...) — the M1A.1 note "types-only" (§2, §8.1) is
   WRONG and is corrected in this change.
10. `typedef struct {...} T; T T;` is illegal C — the exported symbol
    `USBMIDI9DispatchTable` must be declared as `struct
    USBMIDI9DispatchTable USBMIDI9DispatchTable;` (tag namespace).

## Deliverables (files)

- `classic/ring.h`, `classic/ring.c` — fixed resident byte ring
  (SPSC, volatile indices, drop-new on overflow), portable C89, host-tested.
- `classic/usbmidi9_dispatch.h` — shared ABI: `struct
  USBMIDI9InterfaceInfo` + `struct USBMIDI9DispatchTable` (version,
  enumerateInterfaces, getInterfaceInfo, dequeueBytes). ONLY these.
- `classic/usb_driver.h`, `classic/usb_driver.c` — the class driver:
  per-interface instances in a fixed static registry (8 max),
  refcon state machine (alloc ring → configure → find bulk IN →
  alloc read buffer → read loop), completion → ring, removal handling,
  the three exports.
- `probe/probe.c` — Mac OS 9 console probe: locate table (per poll,
  self-healing across unplug), display interface info, poll/dequeue,
  hex-print packets (4-byte groups), quit on 'q'.
- `codewarrior/USBMIDI9.exp` — export list for the linker:
  TheUSBDriverDescription, TheClassDriverPluginDispatchTable,
  USBMIDI9DispatchTable.
- `host-check/*.h` — minimal stub headers (MacTypes.h,
  MacErrors.h, USB.h, CodeFragments.h, Memory.h) declaring ONLY the
  verified surface used by the code, each constant with its citation;
  used by `make check-classic` to syntax/type-check the Classic sources
  on Linux. NOT the real headers; the G4 build uses the real Universal
  Headers + DDK USB.h.
- `tests/test_ring.c` — host tests incl. randomized prop-style tests
  for the ring (order preservation, wraparound, overflow drop-new,
  model comparison).
- `Makefile` + `.github/workflows/ci.yml` — build ring into the host
  test binary; add `check-classic` target and run it in CI.
- docs updates: classic-usb-driver.md (M1B status + the two
  corrections: prototypes-in-USB.h, FindSymbol shape), ROADMAP.md,
  README.md, m1/classic-usb-probe/README.md, probe/README.md.

## DoD → task mapping

| DoD | Implementation |
|---|---|
| 1. Generic metadata class 0x01/subclass 0x03, no VID/PID | USBDriverDescription: interface info 0x01/0x03/0, device info all 0, loading options 0 |
| 2. Exports | TheUSBDriverDescription, TheClassDriverPluginDispatchTable (+ USBMIDI9DispatchTable via .exp) |
| 3. Per-interface state | Static registry array of instances, one slot per interface, claimed in initializeInterfaceProc, disposed in finalize |
| 4. initializeInterfaceProc | Validate class/subclass/endpoints → decline with error; store refs; start state machine; return kUSBNoErr |
| 5. Completion | Record status/count; enqueue usbActCount bytes to ring; resubmit unless removing/aborted/not-responding |
| 6. Removal | removing flag stops resubmit; USBAbortPipeByReference; kUSBDeviceBusy while kCompletionPending; finalize only when drained |
| 7. Dispatch table | Version 0x0001 + exactly the 3 procs |
| 8. Probe | USBGetNextDeviceByClass + FindSymbol; info display; poll/dequeue; hex |
| 9. No OMS/FreeMIDI | none included |
| 10. No Keystation-specific behavior | no VID/PID anywhere; generic matching only |

## Tasks

1. ring.{h,c} + tests/test_ring.c (incl. prop-style fuzz) — host green.
2. usbmidi9_dispatch.h.
3. usb_driver.{h,c}.
4. probe/probe.c.
5. USBMIDI9.exp + host-check stubs + Makefile/CI wiring; `make test`,
   `make test-sanitize`, `make check-classic` all green.
6. Docs.
7. /review on the full diff; fix findings; commit.

## Review findings (spec review, pre-implementation) — resolutions

1. `CALL_NOT_IN_CARBON` gates the USL prototypes in USB.h 1.4.1; the stub
   USB.h must define `CALL_NOT_IN_CARBON 1` (the DDK-era Classic default) so
   the checked surface matches the G4 build.
2. Async init-failure path (pinned down): any init-stage (alloc
   ring/configure/find/alloc read buffer) completion with an error, or any
   immediate error from a USL call, clears kCompletionPending and stops the
   machine (refcon = kReturnFromDriver). The instance stays registered so
   the removal notification/finalize disposes it; the probe just shows an
   interface with no data. No retries in M1B (samples retry 3x; not needed
   for the acceptance path, documented).
3. `check-classic` flag risk: `\p` string escapes and CodeWarrior pragmas
   may trip `-Wall -Werror` even without `-Wpedantic`. Resolve empirically
   during step 5; if needed use `-Wno-unknown-pragmas` and keep
   `-Wall -Wextra -Werror` otherwise. If the \p escape itself warns, the
   accepted fallback is a narrowly-scoped `-Wno-*` for the classic check
   only (NOT for the portable core, which keeps the strict flags).

## Verification gates

- `make test` (gcc + clang), `make test-sanitize` — ring prop-tests.
- `make check-classic` — driver + probe compile against stub headers
  with -std=c89 -Wall -Wextra -Werror (no -Wpedantic: Classic sources
  legitimately use \p Pascal strings and CodeWarrior pragmas).
- Manual: every API/constant in the sources traces to a citation in the
  stub headers or docs/classic-usb-driver.md.
- NOT claimable here: real CodeWarrior build, Mac OS 9 boot, real
  Keystation bytes (DoD item 11) — recorded as hardware acceptance
  checklist in the docs.

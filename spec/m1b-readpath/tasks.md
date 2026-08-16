# M1B read-path fix — safe bulk-read submission from completion context

Status: **implemented** (hardware-gate follow-up; source + host tests
committed, hardware re-run pending).

## Context

The real G4 hardware run of M1B succeeded through interface matching,
dispatch/enumeration, USBConfigureInterface and USBFindNextPipe, then the
machine became hard-unresponsive immediately after the probe printed the
interface table — the driver's first `USBBulkRead` path had been entered
(read loop started from init, then the completion resubmitted). Treat as a
driver/interrupt-context failure, not a Probe UI hang.

Current driver behavior: `USBMIDI9CompletionProc` calls
`USBMIDI9InitiateTransaction()`, whose `kReadBulkInPipeState` case calls
`USBBulkRead(&inst->pb)` directly — including when the completion itself
runs at secondary interrupt level. The DDK's own PrinterClassDriver does
not do this: it routes every bulk read/write submission through
`SafeUSBBulkRead`.

This spec implements the smallest historically correct fix, per the
hardware-gate report: an execution-level-aware safe bulk-read submission
helper using the authentic `CurrentExecutionLevel()` /
`CallSecondaryInterruptHandler2` mechanism demonstrated by the DDK
samples.

## Verified facts (from the authentic DDK 1.4.1 kit at
~/research/usbddk/, and the DDK API reference — NOT from memory)

1. **PrinterClassDriver's completion routine resubmits reads through
   SafeUSBBulkRead.** `ReadCompletion` (the read completion proc) calls
   `err = SafeUSBBulkRead( pb );` directly from completion context.
   [PrinterClassDriver.c ~line 1648]
2. **SafeUSBBulkRead's authentic shape** (same shape in
   PrinterClassDriver.c and USBModem/ModemDriver.c):
   - `OSStatus SecondaryUSBBulkRead(void *pb, void *result) {
     *(OSStatus*)result = USBBulkRead((USBPB*)pb); return noErr; }`
   - `SafeUSBBulkRead`: `if (gUSBVersionNeedsBulkFixPresent)
     CallSecondaryInterruptHandler2(SecondaryUSBBulkRead, nil, pb,
     &result); else result = USBBulkRead(pb); return result;`
   - `gUSBVersionNeedsBulkFixPresent = (Gestalt('usbv') < kUSBv12)`
     where `kUSBv12 = 0x01200000`. The samples gate on **USB version**,
     not execution level; the version gate is inert on a Mac OS 9 G4
     (USB ≥ 1.2), so this fix gates on execution level instead — the
     DDK-documented generic mechanism — while keeping the sample's exact
     trampoline shape. [PrinterClassDriver.c; ModemDriver.c]
3. **Execution level is discoverable.** Rev 26 Ch 5 "Asynchronous Call
   Support" (p. 102): "The execution level that the completion routine may
   be called back at is not guaranteed... Completion usually occurs at
   secondary interrupt level, or at system task level. If the execution
   context is important to the operation of the code, the driver services
   call CurrentExecutionLevel can be used to discover the current
   execution level." It "returns a constant that defines the current
   execution level: kHardwareInterruptLevel, kSecondaryInterruptLevel,
   kTaskLevel". A hardware-interrupt level "should not be seen for USL
   functions".
4. **CurrentExecutionLevel call shape.** USBModem/ShimSerialHAL.c:
   `err = CurrentExecutionLevel(); if (err) { ...not task time... }` —
   a function returning 0 at task level.
5. **CallSecondaryInterruptHandler2 call shape.** 4 args:
   `(handler, nil, pb, &result)`; handler type is
   `OSStatus (*)(void *callerRefCon, void *result)`; the USL result comes
   back in `result`; the trampoline's own return is ignored by the
   samples. The functions are declared in `<DriverServices.h>` (both
   samples include it; Rev 26: "The CurrentExecutionLevel function is
   defined in the Driver Services Library chapter of Designing PCI Cards
   and Drivers for Power Macintosh Computers"). [PrinterClassDriver.c;
   ModemDriver.c line 20]
6. **Synchronous USL calls are called DIRECTLY from completion context by
   the samples.** PrinterClassDriver's ReadCompletion calls
   `USBClearPipeStallByReference` directly (no trampoline) while retrying
   an errored read. The only USL calls Apple routes through the trampoline
   are `USBBulkRead`/`USBBulkWrite`. → our `USBClearPipeStallByReference`
   in the completion proc needs NO change. [PrinterClassDriver.c
   ~lines 1574, 1586-1587]
7. **Audit of OUR completion-driven USL transitions:** the read resubmit
   is the only completion-driven **bulk** USL call; the other
   completion-driven transitions (`USBAllocMem`, `USBConfigureInterface`,
   `USBFindNextPipe`) and the stall-clear are issued from completion
   context too, but Apple's samples do exactly the same for their
   non-bulk calls (`USBDeviceRequest`, `USBFindNextInterface`,
   `USBOpenDevice`, `USBClearPipeStallByReference` are all called
   directly from PrinterClassDriver/USBModem completion procs — only
   `USBBulkRead`/`USBBulkWrite` get the SafeUSBBulkRead treatment).
   → Only `USBBulkRead` gets the safe helper; nothing else is broadened.
   [classic/usb_driver.c; PrinterClassDriver.c; USBModem/ModemDriver.c]

## Design

In `classic/usb_driver.c`:

```c
/* Driver Services (Rev 26 p. 102; ShimSerialHAL.c): 0 == kTaskLevel. */
static OSStatus USBMIDI9SecondaryUSBBulkRead(void *pb, void *result)
{
    *(OSStatus *)result = USBBulkRead((USBPB *)pb);
    return noErr;
}

static OSStatus USBMIDI9SafeUSBBulkRead(usbmidi9_instance *inst)
{
    OSStatus result;

    if (CurrentExecutionLevel() != kTaskLevel) {
        CallSecondaryInterruptHandler2(USBMIDI9SecondaryUSBBulkRead, nil,
                                       &inst->pb, &result);
    } else {
        result = USBBulkRead(&inst->pb);
    }
    return result;
}
```

- `kReadBulkInPipeState` in `USBMIDI9InitiateTransaction` calls
  `USBMIDI9SafeUSBBulkRead(inst)` instead of `USBBulkRead(&inst->pb)`.
- No other state, pending/removal semantics, or synchronous-return
  protection changes.
- No printf/logging/allocation/Toolbox calls added to the completion
  path.
- `#include <DriverServices.h>` added to usb_driver.c (matches the
  samples' include for these functions).
- New stub `classic/host-check/DriverServices.h` with ONLY the verified
  surface: `CurrentExecutionLevel`, `CallSecondaryInterruptHandler2`
  (raw proc-pointer parameter — the kit's typedef name is not verifiable
  from the samples, so the stub types the handler as the sample-verified
  raw function-pointer shape), and the constants `kTaskLevel`,
  `kHardwareInterruptLevel`, `kSecondaryInterruptLevel` (names from
  Rev 26; `kTaskLevel == 0` verified from ShimSerialHAL usage — the
  driver only compares against `kTaskLevel`).

## Tasks

1. **Add the stub header** `classic/host-check/DriverServices.h` and
   include `<DriverServices.h>` from `classic/usb_driver.c`.
   Gate: `make check-classic` compiles.
2. **Implement the safe helper** and route `kReadBulkInPipeState`
   through it (design above).
   Gate: `make check-classic` compiles.
3. **Extend the mock USL** in `tests/test_machine.c`:
   - `static int gExecLevel;` + `OSStatus CurrentExecutionLevel(void)`
     returning it (default `kTaskLevel` in `mock_reset`).
   - `static int gTrampolineCalls;` +
     `OSStatus CallSecondaryInterruptHandler2(handler, refCon,
     callerRefCon, result)` that increments the counter, ignores the
     trampoline refCon, and invokes the handler exactly as the real
     mechanism does — `handler(callerRefCon, result)` with the USL
     result delivered through `*result` and the handler's return value
     returned (samples ignore it) — so test (c) cannot pass vacuously.
   Gate: existing tests still pass (`make test`).
4. **Add the regression tests** (user report item 9):
   a. task-level submission: init + first read at `kTaskLevel` uses
      direct `USBBulkRead`, `gTrampolineCalls == 0`;
   b. secondary-interrupt submission: a successful data completion
      delivered at `kSecondaryInterruptLevel` resubmits through the
      trampoline (`gTrampolineCalls` +1);
   c. the secondary case reaches `USBBulkRead` through the trampoline
      (`gReadCalls` +1 with `gTrampolineCalls` +1, i.e. no direct
      recursion into `USBBulkRead` from completion context);
   d. stall-retry at secondary level: `kUSBPipeStalledError` completion
      clears the stall directly (sample-verified) and resubmits through
      the trampoline;
   e. removal at secondary level still prevents resubmission: abort →
      `kUSBAbortedError` completion → no trampoline, no new read.
   Gate: `make test` and `make test-sanitize` green.
5. **Docs** (`docs/classic-usb-driver.md`):
   - correct §8.2's "SafeUSBBulkRead checks the execution level" claim
     (the samples gate on USB version `'usbv' < kUSBv12`; our driver
     gates on execution level per Rev 26 p. 102 — record both, labeled
     as such);
   - new **§9.8**: M1B read-path fix + hardware record update
     (§9.7 already exists: "Real-target build status (Power Mac G4)
     and Probe corrections"):
     generic interface matching PASSED on real G4; dispatch/enumeration
     PASSED; bulk receive NOT YET PASSED; first read-path run caused a
     hard system hang; fix description + evidence; hardware gate NOT
     complete.
   - update the doc header status line (currently "M1B source gate
     complete ... NOT yet validated on hardware") to record the G4 run.
   Gate: docs prose review.
6. **Verification + commit**: `make test`, `make test-sanitize`,
   `make check-classic`, `/review` on the diff, focused commit, push.

## Definition of done

- All 6 tasks complete; the three make targets green; diff reviewed;
  committed and pushed.
- The M1B hardware record says bulk receive NOT YET PASSED (no claim of
  a completed hardware gate).

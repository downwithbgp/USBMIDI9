# PPCC packaging fix — tasks

Authorized (user, 2026): implement the authenticated native-PPC PPCC packaging
for the OMS driver. Resource-fork framing issue resolved: the 4-byte length in
earlier dumps was fork record framing, NOT payload. The 'PPCC' 1 logical
payload = the RAW Target-A PEF ('Joy!peffpwpc...'), no length prefix.

## Context (verified this session)

- Authentic TM 'PPCC' 1: attrs = 0x00, record framing len = 1579, logical =
  1579 B = raw PEF container ('Joy!peffpwpc' + version 1 + Mac time). XCOF
  10000 (SC8850) = same raw-PEF shape.
- Authentic OMdi 128s (IAC/MIDIPort 32/SC8850) = 16 B exactly per
  OMSDriver.h: id@+0, xxportNumB@+6. All authentic standard drivers
  have xxportNumB = 0 (verified receipts: IAC id 0x7F01, MIDIPort 32 id
  0x4000 + hasMenuOrWindows=1, SC8850 id 0x1001) and NO 'PPCC'
  resource — the OMdv fallback is their path. The only 'PPCC'
  resources in OMS 2.3.8 = OMS-internal components (Time Manager PPCC
  1, OMS Name Manager PPCC 2, the OMS library PPCC 601) — loaded
  internally, not via the OMdi driver search; the TM's codeResID = 1
  (PPCC 1 + PROC 1) is the authentic id convention we adopt.
- Target-A PEF exists: USBMIDI9/USBMIDI9_OMS (9501 B). Verified: magic
  'Joy!peffpwpc' + version 1; structure matches the authentic PPCC samples;
  loader-info string area = 3 libraries (DriverServicesLib, InterfaceLib,
  USBManagerLib) + 13 imports (BlockMove, DisposeRoutineDescriptor, GetZone,
  NewRoutineDescriptor, Gestalt, SystemZone, SetZone, FindSymbol,
  CallUniversalProc, USBGetNextDeviceByClass, USBGetDriverConnectionID,
  USBRemoveDeviceNotification, USBInstallDeviceNotification) + terminal main
  symbol string 'main\0'. Code = PPC (mflr prologues, blr epilogues, CW
  mtctr/bctr stubs). Sections: loader-info 0x90-0x280, code 0x280-0x2410
  (size 0x2190 = image-header field), data 0x2410-0x251D (size 0x10D).
- Entry ABI (source, unchanged): OMSCALLBACK(long) main(short,long,long) =
  pascal stack-based = uppOMSDriverProcInfo (kPascalStackBased) compatible.

## Tasks

1. oms/oms_driver.r: keep 16-byte OMSDriverParams; xxportNumB 0 -> 1 (codeResID
   = 1 -> Get1Resource('PPCC', 1)); fix the stale OMdv/PEF-comment (lines
   17-19, 54-58) to describe the PPCC packaging and the fork-framing finding.
2. Add oms/ppcc.r: `read 'PPCC' (1) "::USBMIDI9_OMS";` — the file's data fork
   (the raw Target-A PEF) becomes 'PPCC' 1 verbatim; NO length prefix; no Rez
   attributes (authentic TM PPCC 1 attrs = 0x00). Document the `::` path
   (resource project in USBMIDI9 OMS Resources:, PEF at USBMIDI9:USBMIDI9_OMS)
   and the fallback (copy the PEF into the project folder + plain filename).
3. Remove the obsolete OMdvData mechanism: oms/omdv.r, tools/omdvdata.c,
   tests/test_omdvdata.sh, fixtures/omdvdata/, Makefile check-omdvdata target
   + .PHONY entry, and the docs describing length+PEF as OMdv payload.
4. docs/g4-handoff.md: replace the OMdvData procedure with the PPCC procedure
   (resources = OMdi 128, PPCC 1, SICN 128, vers 1; NO OMdv; Rez read path
   ::USBMIDI9_OMS; MacOS Merge Skip-Resource-Types must check PPCC (not OMdv);
   G4 cleanup of stale members/untracked OMdvData artifacts; ResEdit inspection
   list updated; validation hex = "Joy!peffpwpc" at byte 0, NOT 00 00 <len>).
   Attrs documented: authentic TM PPCC 1 attrs = 0x00 (none) — the spec's
   "locked" advice applies to the 68K OMdv path, not the PPCC fragment.
5. Run host gates: make test, make test-sanitize, make check-classic.
6. Review the diff, commit, report (hash, files, verification, authenticated
   PPCC attrs, PEF main-entry verification, beginner G4 steps).
   NOTE: the G4 runtime load test is the NEXT gate (user: no install/runtime
   test now); ResEdit presence is not loadability.

## Acceptance

- [a1] oms_driver.r: 16-byte OMdi, xxportNumB = 1, no stale OMdv references.
- [a2] oms/ppcc.r present with the exact read statement + rationale comment.
- [a3] No OMdvData/omdv.r/omdvdata.c/check-omdvdata anywhere in the tree.
- [a4] g4-handoff.md documents the PPCC procedure end-to-end.
- [a5] make test, test-sanitize, check-classic all green.
- [a6] Commit with conventional message; report contains hash, files,
  verification results, PPCC attrs, PEF main-entry verification, G4 steps.
- [a7] No changes to oms_driver.c/oms_rx.c/oms_tx.c/USB transport/dispatch ABI.
- [a8] No install/runtime test performed.

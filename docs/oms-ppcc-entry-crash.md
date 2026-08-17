# OMS PPCC entry gate — type-2/type-3 crash on first native-PPC call

Status: **AUTHENTICATED root-cause candidate; G4 fix gate NOT yet run.**

The byte gates passed (OMdi 128 exact, PPCC 1 = 9501-byte raw PEF beginning
`Joy!peffpwpc`), the driver installs, and OMS -192 is gone. OMS Setup now
crashes during Search with "unexpectedly quit, error of type 2/type 3"
(illegal instruction / address error), with or without the Keystation
attached. This document records the evidence for the entry-ABI analysis
(A–D) and the smallest next G4 experiment (E–F).

---

## A. OMS 2.3.8 GetDiskFragment → entry-call dataflow (disassembly-verified)

All offsets are into the 68K code resource `PROC` 1 of `Open Music
System.rsrc` (the OMS library/Setup component; 86507 bytes, extracted
with tools in /tmp — `rsrc_list.py`, `dis68k.py`).

1. **loadCode** (`0xDCE4`): `Get1Resource(type, word at +6 of the
   driver's OMdi params)` — trap `$A81F` at `0xDD08`; type = `'PPCC'`
   (pref=2) or `'PROC'` (pref=1); the resource ID is read from
   `OMDriverParams.xxportNumB` (offset +6 of the 16-byte OMdi 128
   data — our `00 01`).
2. **Fragment load** (`$AA5A` = GetDiskFragment trap at `0xDBDC`):
   pushed args include the driver's FSSpec (table+0x52), the driver
   filename (table+0x62), options 5, and stack out-params for
   `mainSymbolClass` / `connID` / `mainAddr`. On success the 32 result
   bytes are copied into the driver table entry at +0x66 (the
   `moveq #7 / move.l (a0)+,(a1)+ / dbra` loop at `0xDBF6`).
3. **Entry invocation**: the SDK's driver contract (OMS 2.0 SDK
   `OMSDrvUPPs.h`) invokes the entry as a **Mixed Mode UPP**:
   `NewOMSDriverProc = NewRoutineDescriptor(proc, uppOMSDriverProcInfo,
   GetCurrentArchitecture())` with `uppOMSDriverProcInfo = 0xFB0`
   (kPascalStackBased | RESULT_SIZE(4) | STACK_ROUTINE_PARAMETER(1,2B) |
   (2,4B) | (3,4B)); calls go through `CallUniversalProc(UPP, 0xFB0,
   msg, par1, par2)`. The ProcInfo carries **no ISA bits** — a 68K
   caller can only reach PPC code through a RoutineDescriptor whose
   `theProc` is the address GetDiskFragment returned.
4. **First message**: `omdvInit` = 0, `par1 = OMSFile*` (the driver
   file), returns `OMSErr` (OMS spec §"Driver Entry Point" +
   `OMSDriver.h`). Returning 0 is safe for every message — the driver
   simply provides no devices/ports.

## B. What the authentic Time Manager mainAddr points to

The OMS Time Manager component (`OMS Time Manager.rsrc`, `PPCC` 1 =
1579-byte raw PEF named `OMSTimerPPC`) is the only authentic
`PPCC`-carrying OMS component with the same shape as ours.

- `.main` export → **code section offset 0x16c** (Ghidra PEF loader:
  symbol address = section[sectionIndex].base + symbolValue, verified
  from `PefLoader` bytecode).
- Object bytes at that address: `7c 08 02 a6` = **`mflr r0`** — an
  ordinary CodeWarrior PPC pascal function (mflr/stw/stwu prologue,
  args in r3/r4/r5, TOC via r2). **Plain machine code.**
- Decompiled behavior: `msg==0` (omdvInit) writes a word to `*par1`
  and returns 0; private messages −1/−2 bridge to the 68K `PROC` 1
  entry via `CallUniversalProc(entry, 0x3A5, ...)` after
  `Get1Resource('PROC', 1)` + `DetachResource`/`HLock`; everything
  else returns 0. It never manufactures a descriptor for itself and
  never uses a transition vector.
- Conclusion: **the OMS PPCC driver convention requires the main
  symbol to be a plain PPC code address.**

## C. What USBMIDI9_OMS's mainAddr points to

- `main` export → **loader-info section offset 0x80** (flat
  0x10002210 in Ghidra's layout).
- Object bytes: `10 00 14 84 10 00 a1 90 ...` = an 8-byte **PPC
  transition vector `[code = 0x10001484, TOC = 0x1000a190]`** — a
  DATA object, not code.
- The code at 0x10001484 is our real pascal main: `mflr r0; stw r0,
  8(r1); stwu r1,-64(r1); sth r3,0x5a(r1)` (msg) `; stw r4,0x5c(r1);
  stw r5,0x60(r1)` (par1/par2) `; ...; bl` (the dispatcher). The
  loader info also carries vectors for the address-taken callbacks
  (`oms_rx_event` @0x10000338, the init counter @0x10000000,
  `oms_tx_send` @0x100007c0, the USB notification callback
  @0x10000cd4).
- The fragment's designated main symbol (loader-info main fields) and
  the `main` export entry both point at the VECTOR, not at the code.

## D. Verdict — our mainAddr is NOT a legal OMS UniversalProcPtr

OMS (68K) receives the mainAddr from GetDiskFragment and invokes it
as a Mixed Mode UPP (RoutineDescriptor built from mainAddr). The
2026-08-17 G4 + host analysis corrected the picture:

- **The authentic TM PPCC's loader info has a VALID main symbol**:
  `mainSection=1, mainOffset=0x3c` — i.e. GetDiskFragment returns
  `data section base + 0x3c` = the transition vector
  `[.main code, TOC]` stored in its data section (its export hash
  table is EMPTY — exportCount=0). The TM WORKS on the G4, so a
  transition-vector mainAddr is **callable by OMS** — the vector is
  the normal CodeWarrior PEF main representation and Mixed Mode/CFM
  handles it.
- **Both of OUR PEFs (production 9501 B and the 257 B diagnostic)
  have `mainSection = 0xFFFFFFFF` = NO main symbol in the loader
  info**, plus a `'main'` export entry of class 2 pointing at the
  transition vector in the data section. This is the structural
  difference from the TM.
- E1 (runtime, 2026-08-17): the 257-byte minimal-entry PEF (code =
  exactly `li r3,0; blr`, zero imports, production init absent) STILL
  crashed with type-2/type-3 → **production init exonerated**; the
  crash is in the entry-call path with a `mainSection=-1` main
  representation.

The fix direction: **give the PEF a valid special main symbol** —
first as E2a (loader-header `mainSection` → 1, keeping the existing
transition vector, mirroring the TM), then as the direct-code E2 if
needed.

## E. Smallest next G4 build

The repo now contains a macro-guarded minimal entry
(`oms/oms_driver.c`, `USBMIDI9_OMS_DIAG_MINIMAL_ENTRY`): a trivial
`return 0` for every message — no OMS glue, no Mixed Mode, no USB,
no dispatch. Return 0 is semantically safe (A4).

**E1 — diagnostic (same export behavior):** add
`USBMIDI9_OMS_DIAG_MINIMAL_ENTRY` to the target's preprocessor
settings, rebuild the OMS PEF target, and **byte-check the new PEF's
main symbol BEFORE installing** (host-side: import into Ghidra and
dump the `main` symbol bytes — the scripts used for this analysis are
in /tmp/omsdiag/ghidra/; or compare the PPCC 1 bytes with the old
PEF's main-symbol offset). If the main symbol is STILL a transition
vector (expected — same linker behavior), the crash should persist
even with the trivial entry; that makes the export-form problem
**strongly implicated** and **exonerates the production init code**.
**Alternate outcome to keep in mind:** if
OMS dereferences `*par1` (an `OMSFile*`) after a successful omdvInit
— the authentic TM writes a word to `*par1` before returning 0, while
the minimal entry leaves it untouched — the crash could persist
independently of the vector issue; in that case the minimal entry must
be extended to write a valid `OMSFile*` (or a null-safe dummy) into
`par1` before returning 0.

**E2a — primary A/B (prepared 2026-08-17, host-side, not yet run on
the G4):** controlled loader-header patch of the preserved 257-byte
E1 PEF — NO rebuild, NO linker change, export table untouched. Per
Apple's PEF docs the fragment main symbol is a SPECIAL symbol
separate from the exported-symbol list, and `mainSection == -1` means
the fragment has no main symbol — so E1's exported `main` did NOT
establish a fragment main for CFM's mainAddr. E2a = byte copy of E1
with exactly 4 bytes changed:

| offset | E1 | E2a | meaning |
|---|---|---|---|
| 0x080–0x083 | `FF FF FF FF` | `00 00 00 01` | loader-info `mainSection`: -1 (no main) → 1 (PackedData section) |

`mainOffset` (0x84) stays 0 → **CFM special main resolves to
section 1 offset 0 = the EXISTING transition vector**
`[0x10000000, 0x10000010]` (section 1's relocated content, confirmed:
`10 00 00 00 10 00 00 10` at 0x10000010; code at 0x10000000 =
`38 60 00 00 4e 80 00 20` = `li r3,0; blr`). The exported `main`
entry (class 2, value 0, section 1), its hash slot/key (len 4,
hash 0x250), the string table, and ALL other bytes are unchanged —
byte-level diff vs E1 = exactly 4 bytes at 0x80–0x83.

Ghidra PEF-loader verification (distinguishing the two mechanisms):
- **CFM special main (loader fields):** `.main @ 0x10000010` — new
  in E2a (absent in E1, whose mainSection was -1), = section 1 + 0 =
  the vector.
- **Exported symbol named `main`:** `main @ 0x10000010` — present in
  E1 and E2a identically (class 2, value 0, section 1 → the vector).
- (Ghidra's PefDebugAnalyzer additionally labels the vector's
  dereferenced code target `main`/`entry @ 0x10000000`, then errors
  reading past the 8-byte code block at 0x10000008 — benign
  import-time analyzer noise; import succeeded.)

E2a = 257 bytes, sha256
`fa86b26d440fbefe4875b255a73df94985304e587a27f71e46b7396700b99907`
(E1 sha256 `9b5f6182...` and the direct-code E2 sha256
`54fec171...` both preserved untouched).

**E2a G4 RUNTIME RESULT (2026-08-18):** installed and run — OMS
Setup → Search now fails with **System Error Type 10**, whereas the
preserved E1 produced the previous failure (type-2/type-3). The
changed failure mode is evidence that adding the PEF special main
materially changed execution: the fragment main is now found and
invoked (E1's mainSection=-1 never established a CFM mainAddr), and
the crash now occurs somewhere in the entry-call path with a valid
special main pointing at the transition vector. Direct-code E2 is
NOT staged or runtime-tested yet; E1/E2a/E2 all preserved unchanged.
Next step (in progress): read-only static trace of the OMS 2.3.8
driver invocation path (GetDiskFragment result → mainAddr call
mechanism → msg/par1/par2 values → return handling → subsequent
messages), then propose E2b.

**Runtime interpretation (per the E1/E2a A/B):** E2a succeeds while
E1 crashes ⇒ the missing PEF special main symbol is the root cause;
the exported `main` was insufficient. Transition vectors are
exonerated (matches the authentic TM, whose special main =
mainSection=1/mainOffset=0x3c → its vector, works on the G4).
E2a still crashes ⇒ a valid special main pointing at our vector is
insufficient; then test the already-prepared direct-code **E2**
(mainSection → Code, export class/section → code) or investigate the
OMS entry ABI/resource metadata next.

**E2a G4 packaging gates:** (1) replace the preserved E1 file with
the E2a bytes (`sha256 fa86b26d...`, 257 bytes) at
`USBMIDI9:USBMIDI9_OMS`; (2) repackage with the SAME MacOS Merge /
Resource File target (Rez `read 'PPCC' (1) "::USBMIDI9_OMS"`);
(3) ResEdit BEFORE install: OMdi 128 =
`7F 10 00 00 00 00 00 01 00 01 00 00 00 00 00 00` AND PPCC 1 =
257 bytes beginning `4A 6F 79 21 70 65 66 66`; (4) install to System
Folder:OMS Folder; (5) OMS Setup → Search → record crash type / none.

## F. Beginner G4 rebuild steps

1. In the `USBMIDI9 OMS PEF` target: Project → Target Settings →
   Preprocessor: add `USBMIDI9_OMS_DIAG_MINIMAL_ENTRY` (E1) to the
   defined symbols. (E2: also visit the PPC PEF panel.)
2. **Project → Make** (⌘K). The output file is
   `USBMIDI9:USBMIDI9_OMS` (its data fork = the new PEF).
3. Copy it to the Linux host (or check on the G4 with the procedure in
   E1) and verify the main-symbol byte gate.
4. Repackage the resource file exactly as before (MacOS Merge /
   Resource File target; `oms/ppcc.r` reads `::USBMIDI9_OMS`), then
   ResEdit-check OMdi 128 (`7F 10 00 00 00 00 00 01 00 01 00 00 00 00
   00 00`) and PPCC 1 (9501 bytes, `Joy!peffpwpc`).
5. Copy `USBMIDI9 OMS Driver` into System Folder:OMS Folder; open OMS
   Setup → Search.
6. Report: crash type (or none) + the main-symbol byte check result.

## G. OMS 2.3.8 driver invocation trace + E2b (2026-08-18, read-only)

Static trace of the OMS 2.3.8 68K driver path (`omslib_proc1.bin`,
the OMS library PROC 1; addresses below are file/68K addresses in
that blob; the Ghidra project + scripts are in /tmp/omsdiag/). The
purpose: identify exactly how OMS invokes the returned mainAddr, the
msg/par1/par2 values, the return handling, and the post-return flow,
then propose E2b. **No staging/runtime changes were made; E1/E2a/E2
are preserved byte-identical.**

### G.1 Loader → mainAddr (already established, reconfirmed)

- `fn_0DAA2` loads the PPCC resource: Get1Resource → A9A8
  materialization → **GetDiskFragment = trap 0xAA5A at 0x0DBDC**
  with `connID` out at driver-record +0x52 and **`mainAddr` out at
  driver-record +0x62** (0x0DBCC/0x0DBD0 pushes; the FSSpec copied to
  +0x66; loaded-mark at +0x57). No GetMemFragment / no FindSymbol —
  mainAddr = the fragment's own main symbol (loader-info main fields).
- OMSDriverTableEntry (OMS 2.0 SDK OMSDriver.h): first field =
  `driverEntryPt` = the UPP (mainAddr).

### G.2 Invocation mechanism — the one and only call site

All driver messages (any msg) funnel through ONE site:

- `fn_10A36(driverRec, msgSlot, byteOut, flags)` = the driver-message
  dispatcher; a driver-type check (0x1075E) picks the PPC path
  (`0x10B1C`). It builds a 14-byte argument block and calls the
  fragment at `0x10C62` (61FF FFFF marker at 0x10B66):
  - `0x10C62–0x10C9A`: HLock(handle) → helper calls → A0 = the UPP
    (mainAddr, dereferenced from the locked handle) → A1 = A6 →
    **trap A9E2 at 0x10C86** → 16-bit result in D0 → HUnlock → return.
  - **A9E2 = _CallUniversalProc** — the ONLY Mixed Mode trap in the
    blob (grep: one occurrence); the OMSLib CFM-68K SLB imports
    `CallUniversalProc` by name; **no NewRoutineDescriptor anywhere
    in the 68K path** — the UPP is mainAddr itself.
  - The 14-byte stack block = `[msg 2B][par1 4B][par2 4B][procInfo
    4B]`, matching the SDK's own macro (OMS 2.0 SDK
    `OMSDrvUPPs.h`):
    `CallOMSDriverProc(userRoutine, msg, par1, par2)` =
    `CallUniversalProc(UPP, uppOMSDriverProcInfo, msg, par1, par2)`
    with `uppOMSDriverProcInfo = kPascalStackBased |
    RESULT_SIZE(kFourByteCode) | STACK_ROUTINE_PARAMETER(1,
    kTwoByteCode /* msg */) | (2, kFourByteCode /* par1 */) | (3,
    kFourByteCode /* par2 */)`.

### G.3 First invocation and the return branch

- First message = **omdvInit (msg=0)** — OMS spec "Driver Entry
  Point": "The code resource has just been loaded"; the authentic TM
  PPCC stub's switch handles msg==0 (omdvInit) first, then -2/-1
  (private), msg==1 (omdvDispose), and a default (everything ≥ 2
  returns 0). par1 = `OMSFile *driverFile` (the driver's FSSpec,
  NULL in OMS 1.x), par2 = unused for init.
- Return handling at 0x10B6C–0x10B76: result → d3 (16-bit);
  `tst.w d3`; **`bne → 0x10BEE` (error path); fall-through → return 0**.
- Error path 0x10BEE: if `flags & 4` → return 2 (silent); else the
  lazily-installed reporter `0x10934` (at (0x894,A4)) is called with
  driver-context args (FSSpec vRefNum/parID etc.) → its byte result
  selects: return 3, or mark + retry the driver call.
- Post-return flow (OMS spec, "Driver Entry Point" / omdvInit):
  "The driver should return a result code indicating whether it was
  loaded successfully. **If it returns a non-zero error value, the
  driver will receive an omdvDispose message immediately after
  returning.**" — i.e. nonzero init ⇒ **no omdvAddDevices, the
  driver is skipped**; init 0 ⇒ OMS proceeds (omdvAddDevices during
  Setup Search / Studio Setup creation).

### G.4 Is `li r3,0; blr` safe for every message? — no, empirically

- The spec sanctions zero: "The driver must return zero in response
  to any message, except when the driver is returning a specific
  value which is appropriate for the message"; the SDK sample driver
  (SampleCell.c) returns 0 for every message; the authentic TM stub
  returns 0 for everything except its private -1/-2 messages.
- BUT the authentic TM's omdvInit **writes a word to `*par1` before
  returning 0** (`stw r0,0(r28)` at 0x1000026C of the TM stub; the
  value = a TM global). Our minimal entry returns 0 without writing
  `*par1` — and E2a (li r3,0; blr, valid special main → vector)
  crashed with **System Error Type 10** on the G4 (2026-08-18),
  while E1 (mainSection=-1, no established main) failed differently
  (type-2/3). Conclusion: return-0-everything is NOT safe for our
  8-byte entry; the failure moved into the post-init handling.

### G.5 TM vs E2a at the call boundary — same mechanism

Both the TM and E2a deliver mainAddr = a **transition vector** in the
fragment's data section via VALID loader-info main fields, and OMS
invokes both through the identical A9E2/CallUniversalProc path with
the same pascal procInfo. The vector representation is therefore
exonerated; the divergence is the ENTRY BEHAVIOR (the TM writes
*par1 and returns 0 / returns 0 for all messages; E2a just returns 0).

### G.6 E2b — proposed (STAGED HOST-SIDE, NOT installed; G4 run TBD)

Purpose: prove E2a's native entry can be called and can return
cleanly to OMS **without letting Search proceed into later driver
messages** — by returning a known nonzero OSErr from omdvInit, which
per G.3 triggers OMS's documented dispose-and-skip path (no
omdvAddDevices, no *par1 read).

- Code (20 bytes at container 0xE2; msg-aware — the 8-byte
  return-error-to-all-calls form is NOT used because a nonzero return
  on omdvDispose/other messages would hit the generic error path of
  G.3 instead of the documented init-skip path):

  ```
  54 60 04 1f   clrlwi. r0,r3,16   ; normalize the 2B msg, set CR0
  38 60 ff ff   li r3,-1           ; error -1
  4d 82 00 20   beqlr              ; msg == omdvInit → return -1
  38 60 00 00   li r3,0            ; every other message → 0
  4e 80 00 20   blr
  ```

- The error value **-1** is not invented: OMSErr = "standard
  Macintosh Operating System result code (always a negative number)"
  (OMS spec); the authentic TM PPCC stub itself returns -1 on an
  internal failure (`li r3,-1` at 0x100001DC); the omdvInit contract
  only requires nonzero.
- Expected control flow after return (proven from G.2/G.3 + the
  spec): msg=0 → -1 → fn_10A36: `bne` → error path → nonzero result
  → **omdvDispose (msg=1) → E2b returns 0** → clean. Caveat: the
  G.3 error path may present the 0x10934 reporter alert unless the
  Search flow passes the silent flag (flags bit 2); the G4 run will
  show which — either way the entry's call+return is proven.
- Staged artifact: `/tmp/omsdiag/e2b_oms.pef` = 257 bytes, sha256
  `87d12ec09db1d411e261d648f99b93cd04eb96977053e6702d1abf50b5d2dd60`.
  Byte diff vs E1: 0x30-0x3B (sec0 total/unpacked/packed 8 → 20),
  0x3C-0x3F (containerOff 0xF0 → 0xE2), 0x80-0x83 (mainSection -1 →
  1), 0xE2-0xF5 (the 20 code bytes). Ghidra: the CFM special main
  (loader-info mainSection=1/mainOffset=0 → section 1 + 0) is
  **`.main @ 0x10000020`** — the data section base — whose 8 bytes
  are the transition vector `10 00 00 00 10 00 00 20` =
  [code 0x10000000, TOC 0x10000020]; the exported `main` entry also
  resolves to 0x10000020 (class 2, value 0, section 1). The bytes at
  the vector's code target 0x10000000 = the 5 instructions above.
  (The `main`/`entry @ 0x10000000` labels are the PefDebugAnalyzer's
  vector-dereference inference at the code target, NOT the special or
  exported main — benign; its disassembly error at 0x10000014 is the
  expected read past the 20-byte code block.) E1/E2a/E2 all preserved (9b5f6182… / fa86b26d… /
  54fec171…).
- G4 gate for E2b (when authorized): same as E2a — put the E2b bytes
  at USBMIDI9:USBMIDI9_OMS (sha256 87d12ec0…), repackage the same
  MacOS Merge target, ResEdit-verify OMdi 128 + PPCC 1 = 257 bytes
  `Joy!peffpwpc`, install, OMS Setup → Search, record: crash type /
  alert text / clean-skip.

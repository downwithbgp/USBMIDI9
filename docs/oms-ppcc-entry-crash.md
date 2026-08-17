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

OMS (68K) receives the vector address as `mainAddr` and must treat it
as a routine address (wrap it in a RoutineDescriptor / hand it to
Mixed Mode). Executing the vector bytes as PPC code runs
`lwz r0,0x1484(r1)`-style loads over the vector + the following loader
strings ("USBMIDI9 Port ..."), faulting with an illegal instruction or
address error — **exactly the observed device-independent type-2/type-3
crash at the first entry call**. The authentic TM `.main` is plain
code; ours is a transition vector. The fix direction: **make the PEF's
main symbol point at the plain code**, i.e. get the CodeWarrior PPC
PEF linker to emit a code-class export for `main` (see E2).

Note: this is the primary hypothesis and matches all observations; it
is not yet proven on the G4 (the crash could still originate inside
our init code, which the staged diagnostic below isolates).

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
even with the trivial entry; that confirms the export-form problem and
rules out our init code.

**E2 — fix candidate (plain-code export):** with the same minimal
entry, change the export form until the main symbol bytes start
`7c 08 02 a6` (mflr). Try in order:
1. Target Settings → **PPC PEF** panel: inspect for "Export
   TOC-relative symbols" / transition-vector / main-symbol options and
   change them; rebuild and re-check the bytes.
2. `codewarrior/USBMIDI9_OMS.exp` syntax variants (plain `main` is
   current; try the linker's documented export modifiers if the panel
   offers none).
3. Fallback (documented only, non-trivial): post-process the PEF
   container's loader info so the main fields + export entry point at
   the code offset instead of the vector (requires re-encoding the
   reloc-form loader info; not attempted).

**Acceptance for the byte gate:** main symbol bytes = `7c 08 02 a6`
and the vector table still present at the same offsets (vectors for
the callbacks are harmless — only the MAIN symbol must be code).

**Then:** install and run OMS Setup Search with the trivial entry. No
crash → native PPC entry proven; proceed with the staged init
binary-search (state reset → LinkToOMSGlue → OMSGetCallAddress → UPP
creation → dispatch discovery → notifications → enumeration →
device/port registration), one stage per build, keeping the byte gate
on every rebuild.

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

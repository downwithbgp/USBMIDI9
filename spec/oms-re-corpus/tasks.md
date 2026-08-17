# OMS/PEF/Mixed Mode RE knowledge preservation corpus (docs/re + tools/re)

## Goal

Create a durable, source-controlled reverse-engineering corpus capturing
everything learned about OMS 2.3.8, PEF/CFM, and Mixed Mode, so future
sessions and the G4 work do not depend on /tmp, chat logs, session
memory, or the opaque local Ghidra database. Documentation/tooling ONLY —
no production behavior change (guard-off object must stay md5-identical).

## Deliverables

1. **docs/re/** corpus (see user's exact filename list; content below).
2. **tools/re/** promoted /tmp scripts, cleaned, documented, smoke-tested.
3. Artifact manifest `docs/re/artifacts.toml` (sha256 + size + provenance
   + status). NO proprietary Opcode/Apple binaries committed — hash and
   describe only.
4. Ghidra knowledge as text: `docs/re/ghidra-functions.csv`
   (offset → name) + `tools/re/ghidra/ApplyLabels.java` to reapply labels
   on a fresh import.
5. Separate docs/tooling-only commit; report added/promoted/discarded +
   knowledge that exists only externally.

## Content plan per file

- README.md: index, confidence classes, offset conventions (resource-/
  file-relative; runtime addresses only in runtime-traces.md, marked
  load-specific), pointer to g4-handoff.md as the operational doc.
- evidence-ledger.md: PROVEN / STRONG INFERENCE / HYPOTHESIS / RETRACTED
  classifications with evidence, for at least:
  * PPC Linker Main: main populates the PEF special-main fields (PROVEN)
  * ordinary exported main != PEF special main (PROVEN)
  * authentic TM/OMSLib transition-vector representation + relocation
    behavior (PROVEN: [0x1000016C,0x10000470] etc.)
  * E2b invalid: code-section container alignment (PROVEN)
  * packed-data op3/op4 semantics; earlier "reserved opcode" false
    (PROVEN / RETRACTED)
  * uppOMSDriverProcInfo=0xFB0, uppOMSDvrAdd1DevProc1Info=0x2F0
    mechanical decodes (PROVEN — verify against SDK headers)
  * direct add1Device() UPP call was a real bug but did not explain
    FFFFFFF3 (PROVEN bug / PROVEN not-the-FFFFFFF3-cause)
  * DebugStr not transparent across PPC↔68K (RETRACTED transparency
    assumption), 0x7F800008 trap chosen (PROVEN decision)
  * entry-call path exonerated/closed (b46c7251 gate) (PROVEN)
- oms-2.3.8-map.md: base-independent map of the OMS 2.3.8 library 68K
  PROC 1 blob + OMS Setup + TM PPCC stub: driver load path (loadCode
  0xDCE4, Get1Resource A81F, GetDiskFragment AA5A at 0xDBDC), mainAddr
  at +0x62, FSSpec at +0x66, fn_10A36 dispatcher + A9E2
  _CallUniversalProc at 0x10C86, 14-byte arg block, return/error paths
  (0x10B6C, 0x10BEE, 0x10934), hardcoded msgs 0,1,2,0x10..0x13,0x23,
  0x29,0x2b,0x1000 + callers (0x54FC → TQna, 0x9F4A → 0x99B6 with 0x23),
  variable-message dispatcher 0x99B6, no literal 0x00FF site, OMS Setup
  PPC driver-call sites (from oms_setup_code1.bin analysis), named
  functions; per-row: component/resource | offset | name | calling
  convention | inputs/outputs | evidence | confidence.
- mixed-mode.md: UPP/RoutineDescriptor mechanics, CallUniversalProc,
  ProcInfo bit layout + the mechanical decodes (0xFB0, 0x2F0,
  OMSReceivedFromPort A1/D0 register-based ProcInfo), NewOMSDriverProc/
  NewRoutineDescriptor, CallOMSDvrAdd1DevProc1 trampoline requirement
  (direct UPP call = Address Error), CallOMSReadHook2/OMSReadHook2UPP,
  DebugStr/Debugger cross-TOC caveat, 0x7F800008 low-level trap.
- pef-cfm.md: PEF v1 container layout (magic, header, 0x1C section
  headers, 14xU32 loader info, exports, relocation stream), packed-data
  opcodes 0-4 with the CORRECTED op3/op4 semantics, special main vs
  export, TVector8 relocations, PPCC resource = raw PEF (no length
  prefix; fork framing is Resource Manager's), 'OMdv' = 68K fallback,
  CFM fragments move between boots → relative offsets only, alignment
  rules (code sections 16-byte aligned in container).
- runtime-traces.md: compact ledger — artifact sha, build config,
  PPCC-relative stop, observed args/registers, result, interpretation:
  repeatable FFFFFFF3; register invariants WITHOUT unsupported semantics
  (object 0x01856100 larger than SDK OMSDriverTableEntry, name at +0x0E,
  A0=A2+0x66, FFFFFFF3 not in 0x80 dump); E0 msg=00FF par1=<ptr> par2=1
  + independent stack proof; no I0..IR/T0..T5 fired (falsifies
  reached-omdvAddDevices); E1/E2a/E2b results; entry gate CLOSED run;
  stale-absolute-address GT experiment (INVALID); valid relative-offset
  GT (DebugStr did not return to main+0x1A58).
  **Provenance caveat (review-mandated):** the 00FF/GT/1A58 rows have NO
  preserved raw source in the repo (no doc, no log, no /tmp transcript —
  verified by grep). They are session-memory-sourced: mark each such row
  `source: session-memory (G4 MacsBug transcript not yet preserved)`,
  confidence HYPOTHESIS/STRONG INFERENCE as appropriate, and add a
  "TO PRESERVE" note: transcribe the G4 MacsBug transcript into
  runtime-traces.md (or hash the transcript file into artifacts.toml)
  before these rows may be upgraded to PROVEN.
- false-leads.md: retracted/invalid conclusions with one-line why:
  E2b, OMdvData packaging, typed-Rez OMdi, "reserved opcode 5/6",
  .main-is-the-vector attribution, DebugStr transparency, reached-
  omdvAddDevices assumption, stale-absolute GT, 0x00FF-caller claim.
- artifacts.toml: TM PPCC 1, OMSLib 601, production, E1/E2/E2a/E2b,
  omslib_proc1.bin, oms_setup_code1/proc1.bin, omslib_ppc.slb,
  ppcc1_extracted.bin, oms_driver_resfork.bin, omdi128.bin,
  keystation-49e.bin; sha256+size (compute from repo fixtures + /tmp
  artifacts), provenance, known-good/bad/diagnostic, notes. Hash-only
  for proprietary binaries — INCLUDING the Apple Universal Interfaces
  header /tmp/MixedMode332.h (the source used to verify the ProcInfo
  decodes; Apple header, hash-only, note its /tmp location and that it
  is regenerable from the Universal Interfaces 3.3.2 mirror in
  ~/research/ui332).
- ghidra-functions.csv: distilled offset → name for OMS 2.3.8 library
  (68K) + our driver (PPC) from docs + functions.txt (auto FUN_ names
  NOT preserved — no knowledge content; the 41KB full dump stays in
  /tmp, regenerable via tools/re/dis68k.py).

## tools/re/

- dis68k.py (68K capstone disasm), disppc.py (PPC capstone at container
  offset), pef_unpack.py (packed-data decompressor — MUST be ported to
  the CORRECTED op3/op4 semantics from pef.rs b08bf7c; the /tmp version
  is pre-correction and wrong), pef_analyze.py, pef_loaderinfo.py,
  rsrc_list.py, appledouble.py, omsabi.py, procinfo_check.c (extended to
  also mechanically decode uppOMSDriverProcInfo=0xFB0 and
  uppOMSDvrAdd1DevProc1Info=0x2F0 from the MixedMode.h macros),
  bdiff.py (generalized to argv), oms_errors.py (generalized path),
  README.md, ghidra/ApplyLabels.java, smoke.sh + tiny synthetic
  fixtures. Makefile target `check-re-tools` runs smoke.sh.
- Discarded: pefdump*.py, pef_dis.py, ctx_dis.py/entry_dis.py/fn_dis.py
  (hardcoded; superseded), pdiff.py (superseded by bdiff), rsrcscan.py
  (subset), omsz*.c (superseded by omsabi.py), hfs_*.py (one-off DDK
  image inspection), fp_check.py/cache_value.py/deepseek_rates.py/etc.
  (unrelated), omdvdata (disproven packaging), oms_*.o binaries,
  current_oms_driver.c copy, web-scrape files, logs.

## Tasks

1. Write spec (this file) → /review.
2. Build tools/re (promote + clean + correct pef_unpack op3/op4 + extend
   procinfo_check + bdiff argv + oms_errors argv + README + smoke.sh +
   fixtures) and Makefile `check-re-tools`; run smoke.
3. Write docs/re/* (README, evidence-ledger, oms-2.3.8-map, mixed-mode,
   pef-cfm, runtime-traces, false-leads, artifacts.toml,
   ghidra-functions.csv).
4. Write tools/re/ghidra/ApplyLabels.java (documented; host smoke
   optional — Ghidra headless exists at /opt/ghidra_12.1_PUBLIC but a
   full import is slow; note if not run).
5. Verify SDK-header claims for 0xFB0/0x2F0 (read OMSDrvUPPs.h +
   MixedMode332.h from ~/research) before writing mixed-mode.md.
6. Gates: make test, make test-sanitize, make check-classic,
   make check-trace, make check-re-tools, cargo test/fmt/clippy,
   guard-off md5 (unchanged since no production code touched).
7. /review the full diff → commit as a separate docs/tooling commit with
   the required report (files added, /tmp promoted, discarded,
   external-only knowledge).

## Acceptance

- [ ] docs/re/ has all 8 files + ghidra-functions.csv; artifacts.toml
      parses (toml) and every sha256 matches the actual artifact.
- [ ] tools/re/ scripts run on repo fixtures; smoke.sh passes; Makefile
      target added.
- [ ] pef_unpack.py op3/op4 match pef.rs (TM 167/167, production
      468/468 complete decodes).
- [ ] 0xFB0/0x2F0 decodes verified against the SDK headers.
- [ ] No proprietary binary added to the repo (git diff --stat shows
      only text/tool files).
- [ ] All existing gates green; guard-off object unchanged.
- [ ] Separate commit; report lists added/promoted/discarded/external.

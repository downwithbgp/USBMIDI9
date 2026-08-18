# Evidence ledger — OMS / PEF / Mixed Mode conclusions

Every important conclusion, classified and backed by the evidence that
supports it. Classifications: PROVEN / STRONG INFERENCE / HYPOTHESIS /
RETRACTED. See `false-leads.md` for the one-line retraction list and
`README.md` for conventions.

## PROVEN

### P1. PPC Linker "Main: main" populates the PEF special-main fields
- **Claim:** setting the CodeWarrior PPC Linker → Entry points → Main to
  `main` makes the linker itself populate the PEF loader-info special-main
  fields (`mainSection=1, mainOffset=0`) and emit the relocation-generated
  transition vector.
- **Evidence:** G4 link gate 2026-08-16→18 (docs/oms-ppcc-entry-crash.md
  §H, docs/g4-handoff.md "PPCC entry gate — CLOSED"): the USBMIDI9 OMS
  target had Main left blank → `mainSection=0xFFFFFFFF`; setting it to
  `main` produced a valid special main; the call/return diagnostic
  (sha256 b46c7251…) passed every static gate (pefcheck PASS,
  mainSection=1, COMPLETE decode+replay, VALID TVector8, aligned) and
  then **OMS Setup → Search completed with NO crash**.
- **Consequence (operational):** the OMS PEF target MUST keep Main = main
  for all future builds.

### P2. An ordinary exported `main` is distinct from the PEF special main
- **Claim:** the exported-symbol-table entry named `main` (class 2, value
  0x80, section 1 in the production build) does NOT establish the CFM
  fragment main that GetDiskFragment reports as mainAddr; that comes only
  from the loader-info special-main fields.
- **Evidence:** E1 (minimal entry, `mainSection=-1`, export `main`
  present) crashed on the G4 with type-2/3; E2a (byte-identical except
  `mainSection: -1 → 1`) changed the failure to System Error Type 10
  (docs/oms-ppcc-entry-crash.md §E/G, fixtures e1_oms.pef / e2a_oms.pef).
  The E1→E2a byte diff is exactly 4 bytes at 0x80–0x83
  (`bdiff.py e1_oms.pef e2a_oms.pef`).

### P3. Authentic TM/OMSLib transition-vector representation + relocation
- **Claim:** authentic OMS PPC code fragments represent the fragment main
  as a **transition vector** [code, TOC] materialized in the data section
  by a TVector8 relocation at load time; the container stores
  pre-relocation bytes.
- **Evidence:** `pefcheck --trapcheck`/structural checks on
  `tm_ppcc1.pef` (authentic Time Manager PPCC 1, works on the G4):
  special main = section 1 + 0x3C; relocation simulator reconstructs
  `[0x1000016C, 0x10000470]` (entry = code section base + 0x16C, TOC =
  data section base + 0); OMSLib PPCC 601 = section 1 + 0x4 →
  `[0x10000000, 0x100000D0]` (docs/pefcheck-report.md). The TM's
  mainAddr is callable by OMS through Mixed Mode (runtime: the TM works).

### P4. E2b is INVALID: code-section container alignment
- **Claim:** E2b (the direct-code diagnostic) is an invalid PEF: its code
  section containerOffset 0xE2 is not 16-byte aligned (0xE2 % 16 = 2),
  violating the PEF rule that code sections be ≥16-byte aligned in the
  container. Its G4 runtime failure (illegal instruction; MacsBug showed
  misaligned code) is explained by that defect alone and carries NO
  evidence about OMS entry behavior.
- **Evidence:** pefcheck VERDICT INVALID with the alignment error
  (docs/pefcheck-report.md; fixture e2b_oms.pef, sha256 87d12ec0…);
  G4 MacsBug evidence frozen with the artifact.

### P5. Packed-data op3/op4 semantics (and the false "reserved opcode")
- **Claim:** PEF packed-data opcodes: 0 Zero, 1 Block, 2 Repeat, 3
  RepeatBlock, 4 RepeatZero; op3 = commonSize(c=value), customSize
  (varint), repeatCount (varint): emit common, then repeatCount × [common
  + FRESH custom], then common; op4 = commonSize, customSize, repeatCount:
  repeatCount × [commonSize ZEROS + FRESH custom], then commonSize ZEROS.
  Values are big-endian 7-bit varints (high bit = continuation).
- **Evidence:** pefcheck/src/pef.rs `unpack_packed` (b08bf7c) decodes the
  authentic TM (75 packed → 167) and production (269 → 468) data sections
  COMPLETELY (exact output AND exact consumption); the earlier
  "reserved opcode 5/6" errors were parser bugs from wrong op3/op4
  semantics — see false-leads.md F2. tools/re/pef_unpack.py carries the
  corrected semantics (smoke-tested against both fixtures).

### P6. uppOMSDriverProcInfo = 0xFB0 (mechanical decode)
- **Claim:** `uppOMSDriverProcInfo` (OMS 2.0 SDK OMSDrvUPPs.h,
  `CallOMSDriverProc`/`NewOMSDriverProc`) decodes to 0xFB0:
  kPascalStackBased(0) | RESULT_SIZE(4)=3<<4=0x30 |
  STACK_ROUTINE_PARAMETER(1,2B)=2<<6=0x80 |
  STACK_ROUTINE_PARAMETER(2,4B)=3<<8=0x300 |
  STACK_ROUTINE_PARAMETER(3,4B)=3<<10=0xC00 → 0xFB0.
- **Evidence:** SDK header composition + tools/re/procinfo_check.c
  recomputes and asserts 0xFB0 from the authentic UI 3.3.2 MixedMode.h
  macros; also docs/oms-ppcc-entry-crash.md §G.2 (the 14-byte argument
  block `[msg 2B][par1 4B][par2 4B][procInfo 4B]` at the A9E2 call site).

### P7. uppOMSDvrAdd1DevProc1Info = 0x2F0 (mechanical decode)
- **Claim:** `uppOMSDvrAdd1DevProc1Info` (OMSDrvUPPs.h,
  `CallOMSDvrAdd1DevProc1`) = kPascalStackBased(0) |
  RESULT_SIZE(4)=0x30 | STACK_ROUTINE_PARAMETER(1,4B)=3<<6=0xC0 |
  STACK_ROUTINE_PARAMETER(2,2B)=2<<8=0x200 → 0x2F0 (pascal OMSDeviceH
  result; stack params `OMSDevice *device` (4B), `short devSize` (2B)).
- **Evidence:** SDK header composition + procinfo_check.c assertion.

### P8. Direct add1Device() invocation was a real UPP bug, but did NOT explain the persistent FFFFFFF3 crash
- **Claim (both halves):** (a) invoking the add1device UPP by a direct PPC
  call executes the RoutineDescriptor's bytes as code → Address Error;
  the fix is the Mixed Mode trampoline `CallOMSDvrAdd1DevProc1` (commit
  b88c5a7). (b) That bug is NOT the cause of the OMS Search-time
  PC=FFFFFFF3 crash: FFFFFFF3 still reproduced after b88c5a7 (it is a
  68K-side Address Error during Search; see runtime-traces.md T1).
- **Evidence:** (a) b88c5a7 + the trace build's T3/T4 checkpoints;
  (b) runtime-traces.md T1 (repeatable FFFFFFF3 after the fix).

### P9. DebugStr tracing was NOT transparent across the PPC↔68K boundary
- **Claim:** the DebugStr-based trace instrumentation is invalidated by
  DebugStr's own Mixed Mode cross-TOC behavior; DebugStr did NOT cause
  the production crash, it only invalidated the instrumentation.
- **Evidence:** the trace run observed E0 msg=00FF returning 0 with NO
  I0..IR/T0..T5 firing while FFFFFFF3 still reproduced (runtime-traces.md
  T3, source: session-memory); the replacement mechanism
  (kPowerPCLowLevelDebuggerTrap = 0x7F800008, a native PPC instruction)
  is committed in 67d7aed (docs/g4-handoff.md "OMS Search trace build").

### P10. Entry-call path problem is CLOSED (call/return diagnostic)
- **Claim:** OMS Setup Search completes without crashing when the driver
  entry is a linker-generated special-main fragment whose omdvInit
  returns -1; the PEF/CFM/Mixed Mode entry problem is closed; do NOT
  create further PEF diagnostics (no E2c).
- **Evidence:** G4 runtime PASS of the b46c7251 diagnostic (docs/
  g4-handoff.md gate order); the E-series manual patching approach closed
  (docs/oms-ppcc-entry-crash.md §H).

### P11. The OMS PPCC driver convention requires the main symbol to be a plain PPC code address (callable as a UPP)
- **Claim:** the authentic TM PPCC stub's mainAddr is plain CodeWarrior
  PPC machine code (`mflr r0` prologue), NOT a transition vector in the
  export sense; a transition vector delivered via valid loader-info
  special-main fields is ALSO callable (E2a vector worked far enough to
  change the failure; TM works). The convention: mainAddr must be a
  callable Mixed Mode UPP target.
- **Evidence:** docs/oms-ppcc-entry-crash.md §B (TM `.main` → code offset
  0x16c = `mflr r0`), §G.5 (TM vs E2a same call mechanism).

### P12. The primary bad transfer is OMS 68K `RTS -> 00000000`; FFFFFFF3 is secondary
- **Claim:** the PPC entry and Mixed Mode return are not the direct fault:
  `main(0x00FF)` returns, PPC LR is sane, and the first active 68K sequence
  pops a zero longword and then RTSes through a second zero longword. Address
  zero's `68F1` branch produces `FFFFFFF3` only afterward.
- **Evidence:** verbatim raw G4 single-step transcript in
  `runtime-traces.md` T8/T9 and
  `raw-t8-t9-breakthrough-2026-08-18.txt`; static byte match in OMS PROC 1
  at resource offset `0x98BE`.
- **Consequence:** direct `FFFFFFF3` pointer/JSR/JMP searches are closed.
  The remaining static question is the malformed stack construction feeding
  the `0x98A2` routine's post-`JSR (A0)` continuation. A subsequent raw-byte
  audit corrected the displacement at `0x98B4` to `0x0018`: with entry SP
  `S`, the load reads `S+0x0A`, not `S+0x04`; only the separate `0x0052`
  displacement at `0x98B8` is proven as an object-field offset.

### P13. The `+0x98BC` target is a Mixed Mode RoutineDescriptor with ProcInfo zero
- **Claim:** live A0 before `+0x98BC JSR (A0)` was `0x017D7A26`, whose bytes
  begin with `0xAAFE`; its relevant RoutineRecord has
  `procInfo=0x00000000`, PowerPC ISA, `routineFlags=0x0004`, and
  `procDescriptor=0x01AEDF58`.
- **Evidence:** raw transcript in `runtime-traces.md` T8/T9-A0 and
  `raw-t8-t9-a0-routinedescriptor-2026-08-18.txt`.
- **Consequence:** 68K `JSR (A0)` entered Mixed Mode. ProcInfo zero explains
  why the caller's pushed `FFFF,0,0` frame was not unmarshaled/cleaned:
  the return reached `+0x98BE` with A7 still at `S-0x0E`, producing the zero
  RTS target. This is distinct from the established OMS driver-entry
  `CallUniversalProc(...,0x0FB0,...)` path; `0x0FB0` remains unchanged.

## STRONG INFERENCE

### S1. PC=FFFFFFF3 is a 68K Address Error in the OMS library's driver invocation path
- **Claim:** the Search-time crash is a 68K `Address Error` (PC
  FFFFFFF3 = the 68K exception vector) reached during/after a driver
  entry call, not inside our PPC code (our entry returns 0 for 0x00FF
  without touching invalid memory — P1 of the 00FF analysis).
- **Evidence:** repeatable FFFFFFF3 (runtime-traces.md T1); E0 msg=00FF
  → default path → `li r3,0` → clean return (static analysis of the
  trace build, runtime-traces.md T3); the OMS library has exactly ONE
  Mixed Mode call site (A9E2 at 0x10C86) through which all driver
  messages funnel (oms-2.3.8-map.md M5).
- **Caveat:** the exact 68K instruction that faults has not been
  captured; the 0x00FF caller is not conclusively identified (S2).

### S2. The msg=0x00FF caller is a variable-message path in the OMS library
- **Claim:** no code site passes 0x00FF as a literal; the 0x00FF seen at
  E0 arrives through the generic list-dispatcher (0x99B6) or an
  equivalent variable-msg path.
- **Evidence:** static scan of omslib_proc1.bin: hardcoded msgs are
  0,1,2,0x10,0x11,0x12,0x13,0x23,0x29,0x2b,0x1000 (oms-2.3.8-map.md);
  dispatcher 0x99B6 reads msg from caller `$10(a7)`; its only direct
  caller 0x9F4A passes 0x23.

## HYPOTHESIS

### H1. Register/stack invariants at the FFFFFFF3 stop (session-memory)
- Object 0x01856100 is an OMS-internal record LARGER than the SDK
  OMSDriverTableEntry (name at +0x0E not +0x0C; SDK sizeof 0x4E);
  A0 = A2+0x66 matches the loader's interior-pointer pattern
  (`lea.l $66(a2),a0` at 0x0DC16; +0x62 = mainAddr, +0x66 = FSSpec);
  FFFFFFF3 is NOT in the 0x80-byte dump at the stop.
- **Status:** recorded WITHOUT assigning unsupported semantics;
  source: session-memory — see runtime-traces.md T2 "TO PRESERVE".

### H2. OMS Setup PPC driver-call sites in oms_setup_code1.bin
- The OMS Setup binary's PPC-side driver-call flow mirrors the library's
  A9E2 pattern (single Mixed Mode call site). Partially mapped;
  confidence low — see oms-2.3.8-map.md M9.

## RETRACTED

See false-leads.md for the full list with one-line reasons:
F1 E2b usable diagnostic, F2 "reserved opcode 5/6", F3 OMdvData
packaging, F4 typed-Rez 'OMdi', F5 ".main IS the vector" attribution,
F6 DebugStr transparency, F7 "reached omdvAddDevices", F8
stale-absolute-address GT, F9 0x00FF-caller identification,
F10 "special-main missing is the root cause" (E2a runtime moved the
failure — the missing special main was a real defect but not the whole
story; the entry path was subsequently closed by P10).

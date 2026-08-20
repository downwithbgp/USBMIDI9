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
  `tm_ppcc1.pef` (authentic Time Manager PPCC 1 static fixture):
  special main = section 1 + 0x3C; relocation simulator reconstructs
  `[0x1000016C, 0x10000470]` (entry = code section base + 0x16C, TOC =
  data section base + 0); OMSLib PPCC 601 = section 1 + 0x4 →
  `[0x10000000, 0x100000D0]` (docs/pefcheck-report.md). The TM's
  mainAddr has the expected Mixed Mode-callable representation. The
  repository contains only this extracted PPCC 1 fixture, not a complete
  installable Time Manager/OMS driver file; no G4 installation or runtime
  Search result is established by this artifact.

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

### P14. The proven defect is a call-site/object contract mismatch
- `procInfo=0x00000000` is not, by itself, evidence that the live
  RoutineDescriptor is malformed. It is a valid zero-parameter/zero-result
  contract for whatever PPC procedure that descriptor represents.
- PROC1 `+0x98A2` takes the `0xFFFF` branch, reserves `0x04` bytes, and
  pushes `0xFFFF` (word) plus two zero longs before `JSR (A0)`. That frame
  is consistent with a callee consuming `short,long,long` and returning a
  long. The live object field at `+0x52` instead points to a descriptor
  declaring no parameters/result, so the extra caller-built words remain
  on the 68K stack.
- The two live alternatives are: (1) the intended callback has a wrongly
  constructed descriptor, or (2) `+0x98A2` received the wrong object/field
  and `+0x52` legitimately contains a zero-argument UPP for another
  purpose. Neither is resolved.
- Do not substitute `0x0FB0` for the descriptor ProcInfo without proving
  descriptor provenance. `0x0FB0` remains proven for the separate generic
  OMS driver-entry path.

### P15. The `+98B4` source object and historical loader field are identical
- **Claim:** In the live run, `OBJ=A2=0x01809EE0` is proven. At
  `PROC1+0x98BE`, `A7=0x2EB55A6E`; `[A7+0x18]` is read from `0x2EB55A86`
  and contains `0x01809EE0`.
- **Field evidence:** `OBJ+0x52=0x01809F32` contains `0x01809F46`, and
  `OBJ+0x66=0x01809F46`. The live `+0x52` field is therefore an interior
  pointer to the same record's `+0x66`, matching the historical loader
  relationship.
- **Descriptor evidence:** `0x01809F46` begins `0xAAFE` and contains a
  valid Mixed Mode RoutineRecord with `procInfo=0x00000000`, PowerPC ISA,
  `routineFlags=0x0004`, and `procDescriptor=0x018E9B78`.
- **Consequence:** the wrong-object alternative is closed for this path.
  The proven mismatch is between the `+98A2` frame and the valid
  zero-argument descriptor stored in the loader record's `+0x52`.

### P16. The live RoutineDescriptor targets the PPC main transition vector
- **Claim:** The live `procDescriptor` value `0x018E9B78` contains
  `[0x018E8C80, 0x018F1AF0]`. With current PPCC base `0x018E73B0`, the
  code address is code offset `0x1650`, matching the PPC `main` prologue;
  the second longword is its TOC. The following pair
  `[0x018E8304,0x018F1AF0]` is a neighboring vector.
- **Confidence:** PROVEN from the live target dump and current trace PEF
  layout.

### P17. ProcInfo zero is CFM descriptor metadata, not an OMS/driver resource write
- **Proven static facts:** OMS PROC1 copies eight longwords from the
  `GetDiskFragment` result area into record `+0x66` at `+0x0DBF0`–`+0x0DBF8`.
  It then aliases record `+0x52` to `record+0x66` at `+0x0DC10`–`+0x0DC16`.
  OMS PROC1 contains no `NewRoutineDescriptor` call and no instruction
  writing the embedded RoutineRecord `procInfo`.
- **Strong inference:** The RoutineDescriptor at record `+0x66` is supplied
  by the Code Fragment Manager/GetDiskFragment path, and its embedded
  `procInfo=0` is CFM-generated metadata for an untyped/non-dispatched
  entry descriptor. The OMS generic A9E2 call supplies the authentic
  `0x0FB0` ProcInfo externally; that does not rewrite the descriptor.
- **Consequence:** A known-good OMS PPC entry need not have embedded
  `procInfo=0x0FB0`; it can work when invoked through
  `CallUniversalProc(...,0x0FB0,...)`. The failure occurs when this path
  directly `JSR`s the descriptor and thereby relies on its embedded zero
  contract. Exact CFM implementation provenance remains an external
  platform fact, but OMS OMdi/PPCC bytes are not the source of the zero.

### P18. PPC `msg=0x00FF`, `par1`, and `par2` from the direct-descriptor call are not protocol evidence
- **Claim:** Once the live descriptor's embedded `procInfo=0` is proven,
  the PPC register values observed during the `+0x98BC JSR (A0)` invocation
  cannot be treated as unmarshaled OMS arguments. The zero-ProcInfo contract
  marshals no parameters. They may be inherited register state or values
  produced by another invocation path.
- **Consequence:** The observed PPC `0x00FF`/pointer/`1` tuple is demoted
  from evidence of an undocumented OMS message protocol. The proven failure
  mechanism is the valid zero-argument descriptor returning without
  consuming the `+98A2` frame.

### S3. OMS has a concrete direct-callback consumer through object `+0x8A`
- **Claim:** `+0x94A2` copies `object+0x52` to `object+0x8A` and directly
  calls `object+0x8A` at `+0x94EE`; related walkers at `+0x99D6` and
  `+0xA30A` use the same indirect field.
- **Status:** STRONG INFERENCE as a possible route to the live descriptor,
  not yet the proven consumer of the raw `+0x147D4` word or the proven live
  caller of `+0x98A2`. The final OMS-vs-driver root cause remains open.

### P23. Loader +0xDC44 is the proven caller of the live +0x98A2 invocation
- **Claim:** the live wrapper return PC was `0186BE26`; with PROC1 base
  `0185E1E0`, this is `PROC1+0xDC46`. Static bytes at `+0xDC44` are
  `JSR (A1)`, so `+0xDC46` is its return address.
- **Claim:** immediately before that call, the loader executes:
  `CLR.L -(A7)`; `MOVE.L A2,-(A7)`; `MOVE.W #$FFFF,-(A7)`;
  `MOVE.L (A2),-(A7)`; `MOVEA.L (A7),A0`;
  `MOVEA.L 0x0C(A0),A1`; `MOVEA.L 0x0C(A1),A1`; `JSR (A1)`.
- **Evidence:** PROC1 bytes `+0xDC30..+0xDC46` and raw live frame:
  `[S]=0186BE26`, `[S+0x08]=FFFF`, `[S+0x0A]=017BA020`.
- **Consequence:** the loader deliberately supplies `FFFF` and the same
  driver-record pointer to the indirect target. The raw `+0x147D4=98A2`
  word is not needed for this call edge and remains unrelated/unresolved.

### S5. A second generic `object+0x52` consumer exists
- **Claim:** `+0xE160` directly loads `object+0x52`, forwards an incoming
  word plus two longwords, and returns the callback's word result.
- **Evidence:** disassembly at `+0xE160` through `+0xE178`.
- **Caveat:** no static edge connects `+0xE160` to the raw `+0x147D4`
  word or to the live `+0x98A2` invocation.

### P24. The dispatch topology is two `+0x0C` dereferences
- **Claim:** the loader's `+0xDC44` target is resolved as
  `O0=[A2]; A0=O0; A1=[O0+0x0C]; A1=[A1+0x0C]; JSR (A1)`. The live invocation
  proves the final value was `PROC1+0x98A2`.
- **Writers:** static code initializes several object `+0x0C` fields from
  A4-relative method objects around `+0x3292..+0x32F2`; a relocation/copy
  path at `+0xD584` rewrites an object `+0x0C` through the pointer mapper
  at `+0xD2B0`. These writes are proven, but the artifact does not bind
  one writer to live record `017BA020`.
- **Status:** call topology PROVEN; exact intermediate object/table identity
  is unresolved statically. The orphan `+0x147D4` word is not required by
  the proven runtime chain.

### S10. Loader commits the method/pointer combination after materialization
- **Proven:** after PPC `GetDiskFragment` materialization and
  `record+0x52 = &record+0x66`, the common loader block pushes `FFFF` and
  the record, resolves the two-level method pointer, and calls it.
- **Proven for the captured run:** that method is `+0x98A2`, and its
  `record+0x52` target is the captured `procInfo=0` PPC RoutineDescriptor.
- **UNKNOWN:** whether `+0x98A2` is the intended adapter for every PPC OMS
  driver or whether metadata/dispatch-object selection assigned the wrong
  adapter to USBMIDI9.

### P25. Static `+0x0C` writers are multiple object initializers/relocators
- **Claim:** syntactic writes to `0x0C(A2)` occur at `+0x1CFE`,
  `+0x3292`, `+0x329A`, `+0x32A2`, `+0x32E2`, `+0x32EA`, `+0x32F2`,
  `+0x3332`, `+0x333A`, `+0x3342`, `+0x3B56`, `+0x4770`, `+0x676C`,
  `+0x6774`, `+0x67D6`, `+0xD584`, and `+0xDFE8`.
- **Claim:** the `+0x3292..+0x32F2`, `+0x676C..+0x6774`, `+0x4770`, and
  `+0xDFE8` families assign A4-relative method objects; `+0xD584` assigns
  the result of the pointer mapper at `+0xD2B0`.
- **Status:** PROVEN as code writes. Which writer initialized live record
  `017BA020`, and which intermediate object contains `+0x98A2`, remains
  unresolved without the object addresses/contents.

### S11. Neighboring `+0x147D0` words are not yet proven as the active method table
- **Candidate data:** `+0x147D0` contains neighboring code offsets
  `0xDEC6`, `0x95D6`, `0xA3E8`, `0x98A2`, and `0xE17C`.
- **Sibling observations:** `+0x95D6` handles an `OMdv` resource/byte path;
  `+0xA3E8` allocates/initializes an object; `+0xE17C` performs an indirect
  two-longword comparison/list operation; `+0xDEC6` handles a record and
  byte flag. These are not enough to establish a shared vtable or common
  ProcInfo contract.
- **Status:** HYPOTHESIS. No static reference binds this data block to the
  two-level `+0x0C` chain selected for the live driver record.

### P19. Latest run re-proves zero-RTS, not the static caller
- **Claim:** PPC `main` reached its final `blr`, Mixed Mode returned to 68K,
  and the post-callback continuation ran with `A7=2DE935FE`, `[A7]=0`, and
  `[A7+4]=0`. After `MOVE.L (A7)+,D0`, `RTS` consumed the next zero and
  produced `PC=00000000`.
- **Evidence:** `docs/re/raw-t10-zero-rts-2026-08-18.txt`.
- **Consequence:** the immediate malformed-stack transfer is PROVEN;
  `FFFFFFF3` is downstream. This capture does not identify the static
  caller of `+0x98A2` or the current value of its `0x18(A7)` source.

### P20. Corrected wrapper provenance is `S+0x0A`, not `S+0x04`
- **Claim:** with entry SP `S`, `+0x98A2` tests `[S+0x08]`; after its
  `SUBQ.W #4,A7` and three pushes, `+0x98B4` loads `[S+0x0A]` because its
  raw displacement is `0x0018` (`+24` decimal).
- **Evidence:** the raw PROC1 bytes documented in M4a.
- **Consequence:** only `source=[S+0x0A]`, `target=[source+0x52]`, and the
  indirect `JSR` are statically proven. “First argument” and “callback
  object” remain unproven until the caller constructs the frame. Any old
  `206F 0012` / `S+0x04` provenance is RETRACTED.

### S6. `+0x147D4` remains an unresolved indirect-table hypothesis
- **Claim:** the raw data word `+0x147D4=0x000098A2` has no identified
  consumer in the extracted PROC1. The blob contains no PC-relative call to
  `+0x98A2`, and no code reference to the `+0x147D0` data region was found.
- **Status:** HYPOTHESIS only: table base, stride, indexed lookup, and its
  relationship to the live wrapper remain unresolved.

### P21. Captured `record+0x52` descriptor decodes as one zero-ProcInfo PPC record
- **Claim:** the captured bytes at `01809F46` decode as a version-7,
  non-dispatched, one-record Mixed Mode descriptor. The header is
  `AAFE 0700 00000000 0000 0000`; the record is
  `00000000 0001 0004 018E9B78 00000000 00000000`.
- **Field decode:** `+0x00` `AAFE` = `_MixedModeMagic`; `+0x02` `07` =
  `kRoutineDescriptorVersion`; `+0x03` `00` = no descriptor flags;
  `+0x04..07` zero reserved; `+0x08` and `+0x09` zero reserved/selector
  info; `+0x0A` `0000` = final-record index zero, hence one record;
  record `+0x0C` procInfo `00000000`; `+0x10` reserved `00`; `+0x11`
  ISA `01` = PowerPC; `+0x12` routineFlags `0004` = `kUseNativeISA`;
  `+0x14` procDescriptor `018E9B78`; `+0x18` reserved zero; `+0x1C`
  selector zero.
- **Status:** PROVEN for the captured descriptor.

### P22. OMS's static record lifecycle aliases CFM output; it does not build the descriptor
- **Claim:** at `+0x0DBDC`, OMS calls `GetDiskFragment`; on success it
  copies eight longwords from the result area into `record+0x66` at
  `+0x0DBF0`–`+0x0DBF8`. It saves the old `record+0x52` value at `+0x7A`
  and writes `record+0x52 = record+0x66` at `+0x0DC0A`–`+0x0DC16`.
- **Claim:** the OMS PROC1 blob contains no `NewRoutineDescriptor` call and
  no instruction that writes the copied RoutineRecord's `procInfo`.
- **Status:** PROVEN statically that OMS is not the constructor or ProcInfo
  writer. STRONG INFERENCE is that the copied bytes originate from the CFM
  load result; the exact internal CFM constructor is outside the preserved
  OMS/PEF artifacts. The public CFM contract does not specify the embedded
  ProcInfo value, so “CFM normally returns procInfo=0” remains UNKNOWN.
  `uppOMSDriverProcInfo=0x0FB0` is supplied separately at the A9E2
  `CallUniversalProc` site and is not copied into this descriptor.

### S9. Public CFM documentation does not settle mainAddr's embedded ProcInfo
- **Proven from Apple material:** `GetDiskFragment` returns `mainAddr` as
  the fragment's main entry point. Apple Mixed Mode material separately
  describes a PPC procedure pointer as a transition vector and a UPP as a
  RoutineDescriptor carrying calling metadata.
- **Not specified:** the public `GetDiskFragment` contract does not say that
  `mainAddr` is a CFM-created RoutineDescriptor, nor does it specify the
  RoutineRecord `procInfo` value if one is present.
- **Status:** UNKNOWN whether zero is the normal CFM-generated value; TM or
  lower-level CFM evidence is required.

### S7. Zero ProcInfo explains cleanup, not the exact PPC register tuple
- **Strong inference:** invoking the captured descriptor through embedded
  `procInfo=0` declares no stack parameters and no result, so it does not
  consume or clean the wrapper's `FFFF,0,0` frame. This explains unchanged
  A7 and the subsequent `RTS -> 00000000`.
- **Unknown:** zero ProcInfo does not statically determine PPC r3/r4/r5.
  The observed `00FF,<record>,1` tuple cannot be derived from the captured
  descriptor or wrapper pushes and must not be treated as marshaled args.

### S8. TM comparison is static at the PEF/loader level, not record memory
- **Proven:** the checked-in TM fixture is a known-good PPCC 1 PEF with a
  valid special-main entry/vector. The shared OMS loader path is the same
  `GetDiskFragment` -> copy to `+0x66` -> alias `+0x52` sequence.
- **Unknown:** the corpus contains no captured TM OMS record or CFM
  descriptor, so TM's runtime `record+0x52` bytes and embedded ProcInfo
  cannot be compared directly.
- **Consequence:** no different OMS descriptor constructor is statically
  established; the earliest proven USBMIDI9/TM difference is PEF/main-entry
  metadata and/or later invocation selection, not ProcInfo construction.

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

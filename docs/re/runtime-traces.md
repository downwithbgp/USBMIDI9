# Runtime traces — MacsBug / G4 experiment ledger

Compact record of the important G4/MacsBug experiments. **Runtime
addresses (0x018Cxxxx style) appear ONLY here** and are load-specific:
the fragment moves between boots, so never reuse them across sessions.
PPCC-relative stops are container-relative and reusable.

Provenance: rows marked `source: session-memory` were recorded from
G4 session notes/chat that have NOT yet been preserved as raw files in
this repo. **TO PRESERVE:** transcribe the G4 MacsBug transcript (or
hash the transcript file) into this ledger before upgrading those rows
to PROVEN (see evidence-ledger H1 and the review note in
spec/oms-re-corpus/tasks.md).

## T1. The repeatable FFFFFFF3 crash (source: session-memory; PROVEN fact, exact faulting instruction not captured)

- artifact sha: trace-build PEF of commit 3d76191 (hash not preserved —
  the G4-built PEF was not copied back; see artifacts.toml "missing").
- build config: USBMIDI9_OMS_TRACE_SEARCH DebugStr trace build.
- observed: OMS Setup → Search → **68K Address Error, PC=FFFFFFF3**
  (the 68K exception vector). Repeatable across runs.
- result: reproduced AFTER b88c5a7 (the add1device UPP fix), so that
  fix does not explain FFFFFFF3 (evidence-ledger P8).
- interpretation: a 68K-side fault in the OMS library's driver
  invocation path (STRONG INFERENCE S1); the exact faulting 68K
  instruction was never captured — that is the remaining objective of
  the low-level-trap trace build.

## T2. Register/stack invariants at a FFFFFFF3 stop (source: session-memory; HYPOTHESIS H1 — no semantics assigned)

- object at 0x01856100 is an OMS-internal record LARGER than the SDK
  OMSDriverTableEntry (name field at +0x0E, not +0x0C; SDK sizeof
  0x4E).
- A0 = A2 + 0x66, matching the loader's interior-pointer pattern
  (`lea.l $66(a2),a0` at 0x0DC16; +0x62 = mainAddr, +0x66 = FSSpec).
- FFFFFFF3 is NOT present in the 0x80-byte dump at the stop.
- Recorded without assigning unsupported semantics; TO PRESERVE: raw
  register dump.

## T3. E0 msg=00FF trace stop (source: session-memory; STRONG INFERENCE)

- artifact: trace build 3d76191 (DebugStr era).
- observed at E0 (main entry): **msg=0x00FF, par1=<runtime pointer>,
  par2=1**; the stack independently contained those arguments
  (proof captured at the stop).
- result: static analysis of the same build shows main reads msg in r3;
  oms_handle_message does `extsh r0,r29; cmplwi r0,0x2b; bgt default`;
  0x00FF = 255 > 43 → default → `li r3,0` → epilogue blr. **Returns 0
  cleanly — the 0x00FF entry call is NOT the fault site.**
- **NO I0..IR or T0..T5 checkpoint fired** in the same run — the
  failing Search never reached oms_init/omdvAddDevices. This falsifies
  the earlier "the failing run reached omdvAddDevices" assumption
  (false-leads F7).
- interpretation: msg 0x00FF has no literal site in the library
  (oms-2.3.8-map M3); it arrives via a variable-message path; caller
  not conclusively identified (S2).

## T4. E-series G4 runtime results (PROVEN — docs/oms-ppcc-entry-crash.md)

| artifact (sha prefix) | build/config | result | interpretation |
|---|---|---|---|
| E1 9b5f6182 (257 B minimal entry, mainSection=-1) | USBMIDI9_OMS_DIAG_MINIMAL_ENTRY, Main blank | System Error type 2/type 3 on Search | exported `main` did not establish a CFM mainAddr; production init exonerated |
| E2a fa86b26d (E1 + mainSection 0x80-0x83: -1→1) | patched 4 bytes | **System Error Type 10** (failure moved) | valid special main changes execution; crash in entry-call path with vector main |
| E2b 87d12ec0 (direct-code, code @0xE2) | patched | illegal instruction; MacsBug: code not word-aligned | **INVALID PEF** (code containerOffset 0xE2 % 16 = 2); no evidence value — FROZEN |
| b46c7251 (call/return diag: omdvInit→-1) | linker-generated special main (Main=main), DIAG_MINIMAL_ENTRY | **OMS Setup → Search completed, NO crash** | entry-call path CLOSED (evidence-ledger P10) |

## T5. GT experiments (source: session-memory)

- **stale-absolute-address GT (INVALID):** GT at absolute 0x01936C30
  (a `main` final `blr` address captured in an earlier boot) — invalid
  because the fragment moves between boots; addresses from one session
  cannot be reused (PEF7). Falsifies nothing; retracted (F8).
- **valid relative-offset GT:** GT at main+offset (relative to the
  fragment base, computed in-session) showed that after the DebugStr
  break and G, execution did **NOT return to main+0x1A58** — i.e. the
  post-DebugStr continuation diverged from the intended path,
  demonstrating that DebugStr-based instrumentation is not transparent
  across the PPC↔68K boundary (evidence-ledger P9).
- The low-level trap (0x7F800008) replaces both: stops at the
  instruction after the trap with registers preserved, so the
  continuation is always the intended one (docs/g4-handoff.md).

## T6. The low-level-trap trace build (committed 67d7aed; Gate A PASS 2026-08-18)

- mechanism: kPowerPCLowLevelDebuggerTrap = 0x7F800008 at E0/I0–I6/IR/
  T0–T5 after each breadcrumb write; DX ON; G between checkpoints;
  record PPCC-relative offset per stop; identify checkpoint from the
  tag table (docs/g4-handoff.md). Registers preserved — the stop before
  a between-checkpoint crash is trustworthy.
- **Gate A PASS (host, on the G4-built PEF 4407a20e…, 10018 B):**
  `pefcheck --trapcheck --expect=15` → VERDICT PASS, exactly 15 ×
  `7F 80 00 08` in the Code section, all `tw 0x1c,r0,r0`, all 15 tags
  identified, no `tag ?`/packed warning. No DebugStr / `oms_tr_*` /
  trace-buffer code remains. MacsBug 6.5.3 stops **on the instruction
  after** the trap, so the displayed offset is **trap+4** (the `next`
  column of pefcheck). Full checkpoint|code|trap PPCC-rel|stop
  PPCC-rel|next-instruction table + runtime lookup in
  docs/g4-trap-one-pass.md.
- **E0 stop = PPCC+0x190C** (`addi r3,r29,0`); trap opcode at +0x1908.
- **Corrected runtime expectation:** I0 is NOT inherently the next
  checkpoint after E0. E0 instruments every `main()` invocation; I0
  fires only on `omdvInit`. If the first invocation is again msg 0x00FF,
  `oms_handle_message` takes the default path (see T7) and returns
  without any I checkpoint.

## T7. Clean native-trap run (artifact 4407a20e…, low-level trap, no DebugStr) — source: session-memory

- artifact sha: `4407a20eb774eec78ee2b5dc0802361d82b254b63a05ee924e49808b375e265e`
  (G4-built `USBMIDI9_OMS_TRACE_SEARCH` PEF, 10018 B, `USBMIDI9/USBMIDI9_OMS`).
- build config: USBMIDI9_OMS_TRACE_SEARCH low-level-trap build, PPC
  Linker Main = `main`, trace prefix file `USBMIDI9_OMS_trace_prefix.h`.
- observed: first MacsBug native low-level stop = **PPCC +0x190C = E0**,
  next instruction `addi r3,r29,0`. Vadim issued **exactly one G**.
- result: **Address Error at FFFFFFF3 — "while fetching instructions from
  FFFFFFF1 and FFFFFFF3"**. No further native checkpoint occurred.
- interpretation: the persistent FFFFFFF3 is **reproducible without
  DebugStr and without any trace-induced Mixed Mode call** (this build
  removes DebugStr entirely; the trap is a pure native stop). This closes
  the DebugStr-perturbation hypothesis. The fault is in the
  return/transfer path after `main` (see next G4 S-procedure): if the
  first `main` call is msg 0x00FF, `oms_handle_message` returns 0 via the
  default path without any I0–IR checkpoint, and the crash follows on the
  way back to 68K — consistent with T3 (msg 0x00FF, no I0..IR fired).
- E0→main disassembly, register mapping, and the minimal S-based next
  procedure are in docs/g4-trap-one-pass.md (T8 design).

## T8/T9. Clean native-trap single-step transcript and mechanical root localization (2026-08-18)

- **Raw evidence:** `raw-t8-t9-breakthrough-2026-08-18.txt` (verbatim).
- E0 registers were `r29=000000FF`, `r30=000CFBB0`, `r31=00000001`.
  `oms_handle_message(0xFF)` returned normally to `main+0x191C`.
- PPC `main` restored `LR=FFCEC400`; its final `blr` crossed Mixed Mode
  successfully. The first active 68K continuation was `0180267E`.
- The live sequence was `MOVE.L (A7)+,D0; MOVEQ #0,D0; RTS`. Before the
  first instruction `A7=2EB1389E`, with `[A7]=00000000` and
  `[A7+4]=00000000`. The pop advanced A7 to `2EB138A2`; the RTS consumed
  that second zero as its return PC.
- Address zero contains word `68F1`, decoded by MacsBug as `BVC.S *-$000D`;
  from PC 2, displacement -15 yields `FFFFFFF3`.
- **Conclusion:** the primary bad transfer is `01802682 RTS -> 00000000`.
  `FFFFFFF3` is secondary execution from address zero, not a direct pointer,
  JSR, or JMP target.

Static correlation: the exact bytes occur in the hash-pinned OMS library
`PROC 1` (`omslib_proc1.bin`, sha256 `3655f74d…`) at resource offset
`0x98BE`, immediately after the indirect `JSR (A0)` in the routine at
`0x98A2`; the continuation pops the next longword and then RTSes. The raw
bytes at `0x98B4` are `20 6F 00 18`, so its displacement is `0x0018` and,
after A7 reaches `S-0x0E`, it reads `S+0x0A`. The raw bytes at `0x98B8`
are `20 68 00 52`, proving only the `0x0052` displacement.
The extracted PROC body has no `0x0600` resource/code bias: resource offset,
executable/code offset, and PROC-relative offset are all `0x000098BE`.
The correct load base is `0x017F8DC0`, because
`0x017F8DC0 + 0x000098BE = 0x0180267E`. The earlier `0x017F87C0` value was
an arithmetic/documentation error. This is a resource-offset mapping, not
a reusable runtime address. The
earlier claim that `0x98B4` reads `S+0x04` and that the two zero words were
already identified as result-slot/return-PC is corrected: at the observed
SP they are the two `CLR.L` values at `S-0x0E` and `S-0x0A`; the object
identity at `S+0x0A` remains unresolved.

## T10. Final PPC return and second zero-RTS capture (2026-08-18)

- **Raw evidence:** `raw-t10-zero-rts-2026-08-18.txt`.
- PPC `main` reached its final `blr` with `LR=FFCEC400`; Mixed Mode returned
  to 68K and stopped at `01867A9E`, the continuation after the indirect
  callback in the +98A2-shaped wrapper.
- Before `MOVE.L (A7)+,D0`, `A7=2DE935FE`, with zero longwords at `[A7]` and
  `[A7+4]`. The pop advanced A7 to `2DE93602`; immediately before `RTS`,
  `[A7]=00000000`. Stepping the RTS produced `PC=00000000`.
- **PROVEN:** the immediate fault is the malformed callback-return stack →
  `MOVE.L` consumes the first zero → `RTS` consumes the second zero.
  `FFFFFFF3` is downstream execution from address zero.

## T11. Loader call site for the +98A2 wrapper (2026-08-18)

- **Raw evidence:** `raw-t11-dc46-caller-2026-08-18.txt`.
- The live mapping was `PROC1 base=0185E1E0`, `+98A2=01867A82`,
  `+98BE=01867A9E`. With `A7=S-0x0E` at `+98BE`, `S=2DE9360C`.
- The entry frame contained `[S]=0186BE26`, `[S+0x08]=FFFF`, and
  `[S+0x0A]=017BA020`.
- `0186BE26-0185E1E0=0xDC46`. Static `PROC1+0xDC44` is `JSR (A1)`,
  proving that `+0xDC46` is the return address of that call.
- **PROVEN:** the loader/materialization routine invokes the wrapper through
  the indirect `A1` target immediately after constructing the frame; the
  selector is the pushed `FFFF` and the source is the pushed driver record.

## T8/T9-A0. Indirect target is a Mixed Mode RoutineDescriptor (2026-08-18)

- **Raw evidence:** `raw-t8-t9-a0-routinedescriptor-2026-08-18.txt`.
- Immediately before `PROC 1 +0x98BC JSR (A0)`, live `A0=0x017D7A26`.
- Memory at that address begins with `0xAAFE`, the Mixed Mode magic. The
  relevant RoutineRecord fields decode as `procInfo=0x00000000`,
  `ISA=0x01` (PowerPC), `routineFlags=0x0004`, and
  `procDescriptor=0x01AEDF58`.
- **Correction:** a 68K `JSR` to a UniversalProcPtr may enter Mixed Mode
  directly when the target is a RoutineDescriptor. `JSR (A0)` therefore does
  not establish that A0 is raw 68K code.
- With descriptor ProcInfo zero, Mixed Mode declares no parameters and no
  result/argument cleanup. The caller's `SUBQ.W #4,A7`,
  `MOVE.W #$FFFF,-(A7)`, and two `CLR.L -(A7)` instructions remain at
  `A7=S-0x0E` on return. `+0x98BE` then pops the first zero and `+0x98C2`
  RTS consumes the second zero. This explains the exact observed stack state.
- This proves a call-site/object contract mismatch, not that the descriptor
  itself is malformed. The remaining alternatives are a wrongly constructed
  descriptor for the intended callback, or a wrong object/field supplied to
  `+0x98A2` whose valid zero-argument UPP belongs elsewhere. The separate
  authentic OMS entry ProcInfo `0x0FB0` must not be applied here without
  provenance evidence.
- The PPC `msg=0x00FF`, pointer, and `1` values observed during the direct
  descriptor invocation are not protocol evidence: embedded ProcInfo zero
  marshals no arguments. Treat those registers as inherited/otherwise
  produced state unless an independent `CallUniversalProc(...,0x0FB0,...)`
  observation proves the tuple on that separate path.
- The descriptor's creator, the object supplying `0x0052`, and whether
  `0x01AEDF58` is the USBMIDI9 PPCC main/transition vector remain unresolved.

## T12. Fresh-boot ordinary-driver walk (2026-08-20)

- **Raw evidence:** `MacsBug-logs/USBMIDI0-MACSBUG.LOG`; SHA-256
  `3956d509bcccbe8ca6806404fe776f9534f43782046a89294f595d09c3e53a0f`.
- This session did not capture USBMIDI9 or the PPC `main(0x00FF,...)` stop. It
  reached `PROC1+0x98BC` at `01876FAC` with base `0186D6F0` while walking an
  ordinary SampleCell record, then later MWM and Studio 64XTC records.
- The `+0x52` target was `01617490`, MacsBug-labeled `OMdv 0080 05FE`, and
  branched to a real 68K handler at `01617934`. Its epilogue at `016179A0`
  writes the result, restores caller PC `0187B336`, removes `0x0A` bytes,
  and jumps to that PC. At the return stop `[A7]=00000000` and
  `[A7+4]=0187B336`; the outer wrapper then returns normally.
- **Correction:** the previous blanket interpretation that `+98A2` callbacks
  fail to clean the Pascal frame is falsified. Retain T8/T9-A0 only as a
  separate historical USB/PPCC zero-ProcInfo descriptor observation.
- Static byte audit corrected the native loader at `PROC1+0xDC38` to
  `MOVE.L (A2),-(A7)`. The method topology is `O0=[A2]`,
  `O1=[O0+0x0C]`, `method=[O1+0x0C]`, then `JSR (method)`; it is not
  `A0=A2; [A2+0x0C]`.

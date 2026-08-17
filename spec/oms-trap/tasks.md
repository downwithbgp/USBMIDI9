# OMS trace build: replace DebugStr with MacsBug low-level PPC trap

## Goal

Replace the DebugStr-based instrumentation of the `USBMIDI9_OMS_TRACE_SEARCH`
build with MacsBug's native low-level PPC debugger trap:

    kPowerPCLowLevelDebuggerTrap = 0x7F800008   // tw LT|GT|EQ, r0, r0

so a G4 OMS Search stops in MacsBug at each checkpoint with registers
preserved (no cross-TOC Mixed Mode DebugStr call), letting Vadim identify
the checkpoint from the 'PPCC ...'+offset shown on screen and continue
with G until the genuine FFFFFFF3 crash.

## Constraints (from the request)

- Keep the `USBMIDI9_OMS_TRACE_SEARCH` compile-time guard and the breadcrumb
  ring (`oms_crumbs`/`oms_crumb`, 32 slots, {tag,a,b,c}).
- Remove ALL DebugStr calls, the Pascal formatter (`oms_tr_reset`/
  `oms_tr_cat`/`oms_tr_hex`/`oms_tr_break`), the trace buffer, and the
  DebugStr declaration/import from the trace path.
- At E0, I0–I6, IR, T0–T5: write the breadcrumb FIRST, then emit exactly
  one native PPC instruction word 0x7F800008.
- The trap must be emitted inline at each checkpoint (MacsBug stops on the
  instruction AFTER the trap; that instruction must be the checkpoint's own
  continuation, not a helper's `blr`).
- Use real CW Pro 4 PPC inline-assembly syntax (`asm { tw 0x1C, r0, r0 }`),
  NOT a C compiler intrinsic. Encoding proof: unit test that the PPC
  X-form encoding (opcode 31, TO=28, rA=0, rB=0, XO=4) yields exactly
  0x7F800008.
- Keep commit b88c5a7 intact: the production path keeps
  `CallOMSDvrAdd1DevProc1` via the Mixed Mode trampoline.
- Guard OFF must remain production-equivalent (no codegen change).
- Do NOT claim DebugStr caused the production crash.
- Before requesting a G4 build: run all host/static gates and prove the
  emitted opcode in the generated assembly/object path.
- Produce a table: checkpoint | code offset | PPCC-relative offset | trap
  bytes | next instruction (from the G4-built PEF; mechanical tooling
  provided). No absolute runtime addresses.

## Tasks

1. **oms/oms_driver.c — rewrite the trace path.**
   - Keep: guard, `oms_crumb` ring + `oms_crumb_wr`, `oms_crumb()` calls at
     every checkpoint (E0/I0–I6/IR/T0–T5), the `added =` result store at T4.
   - Delete: `oms_tr_reset`/`oms_tr_cat`/`oms_tr_hex`/`oms_tr_break`,
     `oms_tr_buf`, the DebugStr declaration and the guarded
     `#include <OSUtils.h>` block (the unconditional `<OSUtils.h>` at
     line 47 STAYS — `SystemZone()` at oms_bind_dispatch needs it).
   - Add guarded macro `OMS_TRAP()`:
     * `__MWERKS__ && powerc` → `asm { tw 0x1C, r0, r0 }` (CW Pro 4 PPC
       inline asm; the trap is `tw TO,rA,rB` raw form with TO=0x1C;
       documented fallback if the CW assembler rejects the raw TO form:
       `tw 28, r0, r0` — identical encoding, or a `.long`/`dc.l` word
       directive; the G4 disassembly gate proves the emitted bytes).
     * `__GNUC__` (host gate only) → `__asm__ volatile (".byte 0x7F,
       0x80, 0x00, 0x08")` so the Linux object provably contains the
       literal trap byte run (never executed; test binaries don't define
       the guard). NOTE: `.byte` order is used deliberately — a
       `.long 0x7F800008` on the little-endian x86 gate host would
       assemble to 08 00 80 7F and the byte-run gate would find 0 hits.
     * any other compiler → `#error` via the `#if/#elif/#else` ladder at
       the definition site (a trace build on an unknown compiler must
       fail loudly, not silently emit a non-trap).
   - Replace each `oms_tr_*` sequence with `OMS_TRAP();` right after the
     `oms_crumb()` call. 15 traps total.
   - Rewrite the trace-block comment: document the trap, MacsBug behavior
     (stops on next instruction, registers preserved, DX-controlled),
     checkpoint/tag table (E0=0xE0, I0..I6=0x100..0x106, IR=0x1F0,
     T0..T5=0x200..0x205), and the "do not claim DebugStr caused the
     crash" note.
2. **pefcheck — `--trapcheck` mode** (new module `trapcheck.rs`):
   - Scan every Code/ExecutableData section for bytes 7F 80 00 08.
   - Per hit report: code-section offset, container offset (= PPCC-relative
     offset: PPCC resource data IS the raw PEF container, no length
     prefix), trap decode (must be `tw 0x1c,r0,r0`), checkpoint tag
     identified by scanning BACK a bounded window (64 instructions) for
     `li rX,<known tag>` (opcode 14, rA=0, SIMM in the tag set); an
     unidentified tag is reported as `tag ?` and counts as a WARNING in
     the verdict (never silently mislabeled), and the section scan warns
     when a Code/ExecutableData section has container_length !=
     unpacked_length (packed), which would otherwise silently yield zero
     hits.
   - Minimal PPC disassembler: `decode(word) -> String` covering the
     compiler-relevant subset (X-form opcode 31 incl. mflr/mtlr/mtspr/
     mfspr/lwz/stw/add/subf/and/or/xor/neg/rlwinm/srawi/tw; D-form
     addi/addis/li/lis/ori/nop/lwz/lbz/stw/stb/lhz/sth/lmw/stmw + FP
     loads/stores; I-form b/bl; B-form bc; XL-form opcode 19 bclr/blr/
     bcctr/mcrf/isync; M-form rlwimi/rlwinm/rlwnm; ori/oris/xori/xoris/
     andi./andis.; twi/addic/subfic/mulli/cmplwi/cmpwi; sc), fallback
     "opcode N".
   - `--expect N` flag: exit 1 on count mismatch. Verdict PASS only if
     every trap decodes as `tw 0x1c,r0,r0` and the count matches.
   - Unit tests: encode(tw 0x1c,r0,r0)==0x7F800008; decode of known words
     (mflr r0, mtlr r0, stwu r1,-16(r1), blr, nop, li r3,0, bl +4, bne);
     scan + tag identification on a synthetic code blob; prop-test
     round-trip for the tw family (TO/rA/rB).
3. **Makefile — `check-trace` gate**: compile `oms/oms_driver.c` with
   `-DUSBMIDI9_OMS_TRACE_SEARCH -Dmain=oms_driver_entry` (CLASSIC_CFLAGS,
   which already defines `-Dpowerc` — the I6 site is powerc-gated, the
   other 14 need only the guard; on host cc the CW arm is never selected
   since `__MWERKS__` is undefined, so the GCC `.byte` arm carries all
   15 static sites: E0, I0–I6, IR, T0–T5) into
   `build/oms_driver_trace.o`, then mechanically prove the object's
   `.text` section contains exactly 15 occurrences of the byte run
   7F 80 00 08 (extract `.text` via `objcopy --dump-section .text` or
   `readelf -x .text`, then od/tr/grep count), and print the `.text`
   hexdump region for the record.
4. **docs/g4-handoff.md — new section** "OMS Search trace build — MacsBug
   low-level trap": the mechanism, the G4 build step (Target A +
   `-DUSBMIDI9_OMS_TRACE_SEARCH` only; Main stays `main`), the DX ON /
   OMS Search once / record PPCC+offset / G procedure, the checkpoint →
   tag → crumb-values table, the post-build mechanical proof
   (`pefcheck --trapcheck --expect 15` on the G4-built PEF), the
   checkpoint table template (checkpoint | code offset | PPCC-relative
   offset | trap bytes | next instruction), and the caution that DebugStr
   did not cause the original crash.
5. **Gates**: `make test`, `make test-sanitize`, `make check-classic`,
   `make check-trace`, `cargo test` (pefcheck incl. new tests), `cargo
   fmt --check`, `cargo clippy -- -D warnings`; guard-off object md5
   comparison vs HEAD (stash) to prove production equivalence.

## Acceptance

- [ ] Guard OFF object md5-identical to HEAD guard-off object (host cc).
- [ ] Guard ON host object contains exactly 15 × 7F 80 00 08 in .text.
- [ ] `pefcheck --trapcheck --expect 15` decodes every trap as
      `tw 0x1c,r0,r0` and identifies the tag (on the G4-built PEF; on the
      host it is demonstrated on a synthetic fixture + documented).
- [ ] No DebugStr calls/declarations, `oms_tr_*` helpers, or trace buffer
      remains in the tree (the doc/comment note that "DebugStr did not
      cause the crash" necessarily names DebugStr — that is prose, not
      code).
- [ ] b88c5a7 production path untouched (CallOMSDvrAdd1DevProc1 intact).
- [ ] All host/static gates green.

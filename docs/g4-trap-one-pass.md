# G4 native-trap experiment — one-pass handoff (commit 67d7aed)

Single physical G4 run. The `USBMIDI9_OMS_TRACE_SEARCH` build stops at all
15 checkpoints with the **MacsBug low-level native PPC debugger trap**
`0x7F800008` (`tw 0x1C,r0,r0`), which preserves registers (no cross-TOC
Mixed Mode DebugStr call). Goal: localize the OMS Search-time Address
Error (PC=FFFFFFF3) to the segment between two checkpoints, or observe all
15 checkpoints and no crash. **No exploratory debugger work on the G4** —
everything that can be computed statically is done on the host (Jayne)
beforehand.

---

## 0. State gate — VERIFIED on the host (Jayne), 2026-08-18

- `HEAD = e7c01be` (`docs(re): durable OMS/PEF/Mixed Mode RE corpus`);
  branch `main` clean (working tree clean), 2 commits ahead of
  `origin/main` (`67d7aed`, `e7c01be`).
- Both required commits present:
  - `67d7aed` — native-trap instrumentation (15× `0x7F800008` at
    E0/I0–I6/IR/T0–T5; `OMS_TRAP()`; `pefcheck --trapcheck`; `make
    check-trace`).
  - `e7c01be` — RE corpus preservation.
- Host gates all green:
  - `make check-trace` → **PASS — exactly 15 × 7F 80 00 08** in
    `build/oms_driver_trace.o` `.text`.
  - `make check-classic` → PASS (trace build compiles against stub
    headers; `OMS_TRAP()` GCC `.byte` arm carries all 15 sites).
  - `make test` → all pass; `make test-sanitize` → all pass.
  - `cargo test` (pefcheck) → integration 17 + prop 9 all pass
    (incl. `tw_encode_decode_roundtrip`, `scan_and_identify_tags`).
  - `cargo fmt --check` → clean; `cargo clippy --all-targets --
    -D warnings` → clean.
  - `pefcheck --trapcheck fixtures/production_usbmidi9.pef` → **VERDICT
    PASS, 0 traps** (production fixture carries no instrumentation).
  - Existing live PEF on the share was production (11442 B, sha
    `248f8d46…`, 0 traps); **it has since been replaced by the G4 trace
    build** — see "Gate A — RESULT" below.

**No further source changes are needed.** Only change source if the G4
CW4 compiler rejects the inline asm `asm { tw 0x1C, r0, r0 }` — then use
`tw 28, r0, r0` (identical encoding) in `oms/oms_driver.c` `OMS_TRAP()`.

---

## Phase 1 — Vadim: build the trace PEF (Target A)

**Project / target:** open the CodeWarrior project
`USBMIDI9:USBMIDI9:USBMIDI9 OMS.µ` on the G4 and select the
**"USBMIDI9 OMS PEF"** target (this is "Target A" — the PPC PEF
"shared library" target that produces the `'PPCC'` 1 code resource; the
other target, "USBMIDI9 OMS Driver", is the MacOS Merge resource-file
target and is **not** built in this phase).

**Enable `USBMIDI9_OMS_TRACE_SEARCH` (existing CW4 prefix-file setup):**
1. **Project → Target Settings…** (or double-click the target) → open the
   **C/C++ Compiler** panel (PPC side).
2. Set **Prefix file** to **`USBMIDI9_OMS_trace_prefix.h`** — this file
   already exists in the project folder
   (`USBMIDI9:USBMIDI9:USBMIDI9_OMS_trace_prefix.h`) and contains exactly
   `#define USBMIDI9_OMS_TRACE_SEARCH 1`. It is **target-specific** (never
   a shared prefix), so it cannot leak into the production build.
3. **Confirm `USBMIDI9_OMS_diag_prefix.h` is NOT the prefix.** The trace
   build must take the **full entry path** (`main` → `oms_handle_message`)
   and NOT `USBMIDI9_OMS_DIAG_MINIMAL_ENTRY`. Only the trace guard may be
   active.

**PPC Linker → Entry points → Main = `main`** — **KEEP it set to `main`**.
Do not blank it. (This is the required permanent production setting;
blanking it reverts to the closed E2-era no-special-main defect.)

**Nothing else changes.** Same 5 sources (`oms_driver.c`, `oms_rx.c`,
`oms_tx.c`, `midi_stream.c`, `packets.c`), same libraries
(`OMSGluePPC.lib`, USBManagerLib), same exports
(`USBMIDI9_OMS.exp` = `main`), same access paths.

**Make** (⌘K / **Project → Make**) the "USBMIDI9 OMS PEF" target.

**Expected raw output filename / path on the AFP share:**
```
G4 path:  USBMIDI9:USBMIDI9:USBMIDI9_OMS
Linux:    /home/vadim/USBMIDI9/USBMIDI9/USBMIDI9_OMS
```
(the CW PEF target writes the raw PEF container into the project folder,
overwriting the current production `USBMIDI9_OMS`). Its data fork IS the
PEF container (`Joy!peffpwpc` at byte 0, no length prefix).

---

## Post-build — host (Jayne) inspection + mechanical proof

As soon as Vadim reports the Make finished, on the host run:

```
cd /home/vadim/USBMIDI9/pefcheck && ./target/debug/pefcheck --trapcheck --expect=15 \
  ../USBMIDI9/USBMIDI9_OMS
```

**Gate A — the G4-built PEF must report:**
- **VERDICT: PASS — 15 traps** (exit 0; `--expect=15` makes any count
  mismatch exit 1).
- Every trap `bytes 7F 80 00 08`, decodes `tw 0x1c,r0,r0`.
- Every checkpoint tag identified (`E0 I0 I1 I2 I3 I4 I5 I6 IR T0 T1 T2
  T3 T4 T5`), **no** `tag ?` warning, no "packed section" warning.
- **`-x` confirmation that the 15 traps are inside the code section and
  not the trailing `0x7F800008` at the end** — the scan reports each
  trap's code offset and container (= PPCC-relative) offset.

Also run, for the negative control (the production build must stay clean):
```
./target/debug/pefcheck --trapcheck ../USBMIDI9/USBMIDI9_OMS.prod   # 0 traps
```
(the current 11442-B production build is already proven 0-traps above).

**Fill the actual table** from the pefcheck output (do NOT substitute
host/synthetic offsets):

### Gate A — RESULT (2026-08-18, G4-built PEF): **PASS**

`USBMIDI9/USBMIDI9_OMS` — **10018 bytes**, sha256
`4407a20eb774eec78ee2b5dc0802361d82b254b63a05ee924e49808b375e265e`.
`pefcheck --trapcheck --expect=15` → **VERDICT: PASS — 15 traps (expected
15)**, exit 0, all decode `tw 0x1c,r0,r0`, every checkpoint tag identified
(`I0 I1 I2 I3 I4 I5 I6 IR T1 T2 T3 T4 T5 T0 E0`), no `tag ?`, no packed
section warning. Only the Code section (container 0x280) carries traps;
exactly 15 × `7F 80 00 08` in it. **No DebugStr / `oms_tr_*` / trace
buffer code remains** (the only `DebugStr` text in `oms_driver.c` is the
prose comment "DebugStr did NOT cause the original production crash").
The trap is emitted inline: every decoded next instruction is the
checkpoint's own continuation (li/lwz/bl/addi/or), never a helper `blr`.

**pefcheck column semantics (audited 2026-08-18):** pefcheck's `ppcc-rel`
is the container offset of the **trap opcode** itself (the `7F 80 00 08`
word); its `next 0x____` is `trap + 4` = the instruction after the trap.
Per the MacsBug 6.5.3 low-level-trap semantics, MacsBug stops **on the
instruction after the trap**, so the displayed PPCC-relative offset at a
stop is the **`next` offset (`trap + 4`)**, NOT the trap opcode offset.
The table below therefore carries both columns; the runtime lookup (Phase
2c) uses the **MacsBug stop** column.

| checkpoint | code offset | trap PPCC-rel | MacsBug stop PPCC-rel | next instruction |
|---|---|---|---|---|
| E0 | 0x1688 | 0x1908 | **0x190C** | `addi r3,r29,0` |
| I0 | 0x0d98 | 0x1018 | **0x101C** | `li r3,257` |
| I1 | 0x0db0 | 0x1030 | **0x1034** | `lbz r3,9(r31)` |
| I2 | 0x0e14 | 0x1094 | **0x1098** | `lwz r3,-32708(r2)` |
| I3 | 0x0e44 | 0x10c4 | **0x10C8** | `li r3,36` |
| I4 | 0x0ee8 | 0x1168 | **0x116C** | `bl -0x33c` |
| I5 | 0x0f04 | 0x1184 | **0x1188** | `li r3,262` |
| I6 | 0x0f1c | 0x119c | **0x11A0** | `li r3,0` |
| IR | 0x0f5c | 0x11dc | **0x11E0** | `li r3,0` |
| T0 | 0x15b0 | 0x1830 | **0x1834** | `or r31,r3,r31` |
| T1 | 0x1104 | 0x1384 | **0x1388** | `lwz r3,0(r30)` |
| T2 | 0x11f0 | 0x1470 | **0x1474** | `addi r3,r1,60` |
| T3 | 0x1278 | 0x14f8 | **0x14FC** | `addi r3,r29,0` |
| T4 | 0x12ac | 0x152c | **0x1530** | `addi r31,r31,1` |
| T5 | 0x12d4 | 0x1554 | **0x1558** | `li r3,0` |

**Runtime offset note:** the pefcheck scan lists traps in **code-layout**
offset order (I0…T5,T0,E0), but during a real OMS Search the checkpoints
fire in **execution** order: **E0 (main entry) first**, then
I0→I1→…→I6→IR (omdvInit), then T0→T1→…→T5 (omdvAddDevices). Match each
displayed `+offset` (the MacsBug **stop** offset = trap+4) to the name via
the Phase 2c lookup; do not expect the offsets in ascending order during
the run.

**Verify exactly 15 × 7F 80 00 08 and NO DebugStr / Mixed Mode trace
path** in the trace PEF: pefcheck counts the trap words (must be 15), and
the trace path now emits only the native trap (no `DebugStr`, no
`OMS_TRAP`-adjacent Mixed Mode call) — confirmed statically by commit
`67d7aed` (the Pascal formatter / DebugStr declaration are removed) and by
the G4 disassembly decode in the pefcheck output.

From this table produce the **runtime lookup** (see Phase 2) and give it
to Vadim — he should never need to inspect breadcrumbs or compute
addresses during the run.

---

## Phase 2 — Vadim: package + run

### 2a. Exact packaging/build step for the USBMIDI9 OMS Driver

Only after Phase 1's PEF passes Gate A. This is the already-proven
resource assembly (existing `'OMdi'` / `'PPCC'` gates), NOT a new
mechanism. **The companion USBMIDI9 class driver / ndrv is UNCHANGED by
this experiment and is NOT rebuilt or replaced** — it remains the
known-good dispatch-v0x0002 artifact on the share
(`USBMIDI9:USBMIDI9:USBMIDI9`, sha `87f39a22…`). Only the OMS driver
resource file is (re)built here:

1. In `USBMIDI9 OMS.µ`, select the **"USBMIDI9 OMS Driver"** target (the
   MacOS Merge / Project Type = **Resource File** target). It compiles
   `oms/oms_driver.r` (`'OMdi'` 128, `'SICN'` 128, `'vers'` 1) and
   `oms/ppcc.r` — the `read 'PPCC' (1) "::USBMIDI9_OMS";` embeds the
   **trace** Target-A PEF (the same `USBMIDI9_OMS` file) byte-for-byte as
   `'PPCC'` 1. Do NOT skip resource type `PPCC`/`OMdi`/`SICN`/`vers` in
   the MacOS Merge panel. File Name `USBMIDI9 OMS Driver`, Creator `USM9`,
   Type `OMdv`. (The `USBMIDI9:USBMIDI9 OMS Resources:` folder is the Rez
   working dir; `::USBMIDI9_OMS` resolves to the PEF — path already
   validated by prior gates.)
2. **Make** the resource target → `USBMIDI9 OMS Driver`.
3. **ResEdit inspection (REQUIRED before install):** exactly
   `OMdi` 128 / `PPCC` 1 / `SICN` 128 / `vers` 1, **no `OMdv`**;
   `OMdi` 128 = `7F 10 00 00 00 00 00 01 00 01 00 00 00 00 00 00` (word
   at +6 = `00 01`); `PPCC` 1 starts `Joy!peffpwpc` (no length prefix),
   size = the trace PEF size (10018 B).
4. **Install** the file into **System Folder:OMS Folder**.

### 2b. Runtime script (one run, MacsBug)

```
1. INSTALL the OMS driver (step 2a) — copy into System Folder:OMS Folder.
   REBOOT ONLY IF ACTUALLY REQUIRED: the class driver/ndrv is UNCHANGED
   (known-good v0x0002 artifact 87f39a22 stays in place), so a resource-
   only refresh of the OMS driver file normally needs NO reboot — just
   relaunch OMS Setup. Reboot only if MacsBug/OMS reports stale-driver
   symptoms after relaunch.
2. MacsBug:  DX ON          (toggle the low-level native trap flag ON)
3. Launch OMS Setup -> Search  (once)
4. Each native low-level trap stops MacsBug on the instruction AFTER
   0x7F800008. At every stop:
     a. READ the displayed 'PPCC ...'+offset  (PPCC-relative offset;
        do NOT use absolute 0x018C... runtime addresses)
     b. MATCH it against the runtime lookup table (Phase 2c) to name the
        checkpoint (E0, I0..I6, IR, T0..T5) — no breadcrumb inspection,
        no address arithmetic
     c. G  (continue)
5. Repeat step 4 until either:
     - all expected checkpoints have occurred, OR
     - a genuine FFFFFFF3 crash stops the run.
6. If a crash happens between two checkpoints, the PREVIOUS low-level
   trap stop preserved registers — at that point dump registers (R) and
   report them with the checkpoint name + offset.
```

### 2c. Runtime lookup — checkpoint by MacsBug stop PPCC-relative offset

Filled from the actual pefcheck output (Phase 1) so Vadim reads the
displayed offset and names the checkpoint without computing anything.
**Use the MacsBug stop offset (`trap + 4`):** MacsBug 6.5.3 stops on
the instruction after the `0x7F800008` trap, so it displays
`trap PPCC-rel + 4`, NOT the trap opcode address. Both columns are shown
for reference; the operative key is the **stop** column.

| MacsBug stop | (trap opcode) | checkpoint | crumb tag | next instruction |
|---|---|---|---|---|
| **+0x101C** | +0x1018 | I0 | 0x100 | `li r3,257` |
| **+0x1034** | +0x1030 | I1 | 0x101 | `lbz r3,9(r31)` |
| **+0x1098** | +0x1094 | I2 | 0x102 | `lwz r3,-32708(r2)` |
| **+0x10C8** | +0x10c4 | I3 | 0x103 | `li r3,36` |
| **+0x116C** | +0x1168 | I4 | 0x104 | `bl -0x33c` |
| **+0x1188** | +0x1184 | I5 | 0x105 | `li r3,262` |
| **+0x11A0** | +0x119c | I6 | 0x106 | `li r3,0` |
| **+0x11E0** | +0x11dc | IR | 0x1F0 | `li r3,0` |
| **+0x1388** | +0x1384 | T1 | 0x201 | `lwz r3,0(r30)` |
| **+0x1474** | +0x1470 | T2 | 0x202 | `addi r3,r1,60` |
| **+0x14FC** | +0x14f8 | T3 | 0x203 | `addi r3,r29,0` |
| **+0x1530** | +0x152c | T4 | 0x204 | `addi r31,r31,1` |
| **+0x1558** | +0x1554 | T5 | 0x205 | `li r3,0` |
| **+0x1834** | +0x1830 | T0 | 0x200 | `or r31,r3,r31` |
| **+0x190C** | +0x1908 | E0 | 0xE0 | `addi r3,r29,0` |

Compact one-line key (the **stop** offsets MacsBug will display, sorted):

```
0x101C=I0  0x1034=I1  0x1098=I2  0x10C8=I3  0x116C=I4  0x1188=I5
0x11A0=I6  0x11E0=IR  0x1388=T1  0x1474=T2  0x14FC=T3  0x1530=T4
0x1558=T5  0x1834=T0  0x190C=E0
```

**Execution order** (what a real Search will produce, in order):
`E0 (+0x190C)` → `I0 (+0x101C)` → `I1 (+0x1034)` → `I2 (+0x1098)` →
`I3 (+0x10C8)` → `I4 (+0x116C)` → `I5 (+0x1188)` → `I6 (+0x11A0)` →
`IR (+0x11E0)` → `T0 (+0x1834)` → `T1 (+0x1388)` → `T2 (+0x1474)` →
`T3 (+0x14FC)` → `T4 (+0x1530)` → `T5 (+0x1558)`. Match each displayed
`+offset` to the name; the offsets are NOT in ascending order at runtime
because E0/main is laid out at the end of the code section.

(If a checkpoint is identified by `tag ?` in pefcheck — i.e. the trap's
preceding `li` was not recognized — name it from the breadcrumb ring
`D <oms_crumbs>` as a fallback; but with the CW4 codegen the tag `li` is
a few instructions before the trap, so this should not happen.)

---

## Post-run — host (Jayne) transcription

1. **`docs/re/runtime-traces.md`** — add a T6 row (or extend it): the
   observed checkpoint order, each PPCC-relative offset, the stops, the
   outcome (all 15 checkpoints occurred / genuine FFFFFFF3 at which
   segment), the register dump if a between-checkpoint crash, and the
   interpretation (which segment contains the fault). Replace the current
   "NOT YET RUN" T6 note.
2. **`docs/re/artifacts.toml`** — add (or replace) the artifact entry with
   the **actual G4 trace-PEF sha256 + size** from `pefcheck --trapcheck`
   output (`sha256 <hash>` and byte count of
   `USBMIDI9/USBMIDI9_OMS`), status = built/run as observed. Preserve the
   existing "missing" entries; do not re-derive hashes.
3. Only after both are updated propose any new code change.

---

## Optimized for one physical G4 run

- Vadim only builds (Phase 1) and later only packages+runs (Phase 2) —
  no exploratory debugger work; the lookup table removes all
  breadcrumb/address inspection during the run.
- All structural verification (`pefcheck --trapcheck --expect=15`, the
  table, the 15-trap count, no-DebugStr proof) is done on the host from
  the G4-built PEF — nothing runtime is needed to validate the build.
- The packaging and runtime steps are the already-proven mechanisms
  (`'PPCC'` 1 Rez `read`, `'OMdi'` 128 raw-data bytes, MacsBug DX/G),
  not new experiments.

# PEF / CFM — container format and fragment mechanics

Byte-verified knowledge about PowerPC PEF containers and the Code
Fragment Manager, as encountered in the OMS driver work. The mechanical
checker is `pefcheck/` (Rust); `tools/re/pef_analyze.py` and
`tools/re/pef_loaderinfo.py` are ad-hoc inspectors with the same
verified layouts.

## PEF1. Container header (verified against CodeWarrior-built fixtures)

```
offset  size  field
0x00    12    magic "Joy!peffpwpc"
0x0C    4     containerVersion        (1 in all fixtures)
0x10    4     timestamp               (build time; e.g. 0xe6a77bcc = 1998-06-18)
0x14    4     currentVersion          (0 in fixtures)
0x18    4     oldDefVersion           (0)
0x1C    4     oldAppVersion           (0)
0x20    2     sectionCount
0x22    2     instSectionCount        (2 in fixtures)
0x24    4     reserved
0x28    0x1C*N  section headers
```

Section header (0x1C each): nameOffset u32 | defaultAddress u32 |
totalLength u32 | unpackedLength u32 | containerLength u32 |
containerOffset u32 | kind u8 | shareKind u8 | alignment u8 |
reservedA u8. Kinds: 0 Code, 1 UnpackedData, 2 PackedData, 3 Constant,
4 Loader, 5 Debug, 6 ExecutableData, 7 Exception.

**Code sections must be ≥16-byte aligned in the container** (rule
enforced by pefcheck; E2b violated it — see false-leads F1).

## PEF2. Loader info (14 x u32, fixed 56 bytes)

mainSection SInt32 (-1 = none) | mainOffset u32 | initSection SInt32 |
initOffset | termSection SInt32 | termOffset | importedLibraryCount |
totalImportedSymbolCount | relocSectionCount | relocInstrOffset |
loaderStringsOffset | exportHashOffset | exportHashTablePower |
exportedSymbolCount.

- There is NO SInt16 loader-header variant: the SInt16 sectionIndex
  belongs to the exported-symbol table entry, not the loader header
  (b08bf7c/a1a9bc6 corrections).
- The loader container continues: imported library table (libs × 24 B;
  first u32 = nameOffset → C-string in loader strings), imported symbol
  table (syms × 4 B; u32 = flags<<24 | nameOffset → C-string), then
  relocation headers (12 B each: sectionIndex u16 | reserved u16 |
  relocCount u32 | firstRelocOffset u32) and the relocation instruction
  stream (big-endian u16 chunks).

## PEF3. Special main vs exported main (PROVEN — evidence-ledger P2)

- **Special main** (loader-info mainSection/mainOffset): what
  GetDiskFragment reports as the fragment's main symbol; -1 = none.
  Populated by the CW linker when PPC Linker → Entry points → Main is
  set (evidence-ledger P1).
- **Exported `main`** (export hash table entry, class 2 = TOC-class):
  an ordinary symbol, does NOT establish the fragment main.
- A valid special main typically points at a **transition vector**
  `[code, TOC]` in the data section, materialized by a TVector8
  relocation at load time (TM: section 1 + 0x3C → [0x1000016C,
  0x10000470]; 601: [0x10000000, 0x100000D0]; E2a: [0x10000000,
  0x10000010]). Containers store PRE-relocation bytes (TM: `00 00 01 6c
  00 00 00 00` at 0x3C).

## PEF4. Packed data (PROVEN — evidence-ledger P5)

Opcodes 0..4 (5-7 reserved); per-byte: opcode = b>>5, value = b&0x1F
(value 0 → big-endian 7-bit varint, high bit = continuation):
0 Zero, 1 Block, 2 Repeat, 3 RepeatBlock (common, repeatCount × [common
+ FRESH custom], common), 4 RepeatZero (repeatCount × [zeros + FRESH
custom], zeros). COMPLETE decode = exact output length AND exact packed
consumption. `tools/re/pef_unpack.py` and `pefcheck/src/pef.rs`
implement the corrected semantics.

## PEF5. Relocations

- Instruction stream is big-endian u16 chunks; relocation headers
  precede it. The TVector8/LgSetOrBySection-style programs relocate
  section words (add section base) and external symbols (import
  fixups — unresolved in the container).
- `pefcheck/src/reloc.rs` replays a relocation program against
  deterministic synthetic section bases (base[0] = 0x10000000,
  contiguous, 16-aligned) and reconstructs transition vectors; VALID
  only when packed-data decode AND relocation replay are both COMPLETE.

## PEF6. The 'PPCC' resource (PROVEN — docs/g4-handoff.md)

- OMS 2.3.8 loads the native PPC driver code via `'PPCC' <codeResID>`
  (codeResID = `OMDriverParams.xxportNumB`, the word at +6 of the
  16-byte `'OMdi'` 128). The 68K fallback is `'PROC'`.
- The PPCC resource payload is the **raw PEF container** —
  `Joy!peffpwpc` at byte 0, NO 4-byte length prefix. (The
  `[be32 length][data]` framing seen in authentic forks is the Resource
  Manager's own record framing, stripped by Get1Resource; earlier
  readings of it as payload were wrong — false-leads F3.)
- The `'OMdv'` resource path is the 68K fallback and must NOT hold a
  PEF (PEF-in-OMdv packaging disproven).

## PEF7. CFM behavior

- Fragments are loaded by GetDiskFragment (trap AA5A); mainAddr = the
  fragment's special main (loader-info fields), NOT a FindSymbol result
  (the OMS loader does no FindSymbol — oms-2.3.8-map M1).
- **Fragments move between boots** — the same PEF loads at different
  absolute addresses. All RE offsets are container-relative; runtime
  addresses are only meaningful inside one MacsBug session
  (runtime-traces.md).
- GetDiskFragment result layout: mainSymbolClass, connID, mainAddr
  written through out-params; the OMS loader copies the 32-byte result
  block into the driver table at +0x66.

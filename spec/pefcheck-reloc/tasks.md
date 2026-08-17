# pefcheck — relocation simulator (spec-driven)

Status: host-side tooling milestone. No new PPCC, staging, OMS Search, or G4
reboot. Continues `spec/pefcheck/tasks.md` (structural checker) with a
relocation simulator that reconstructs instantiated internal pointers.

## Goal

A spec-driven PEF relocation simulator in the `pefcheck` crate that, for a
given container, decompresses the relocated section, applies the relocation
program against **deterministic synthetic section base addresses** (never
Ghidra addresses), and reports the bytes at the special-main location. When
those bytes form a valid PPC transition vector, resolve the entry and TOC
back to section+offset and validate alignment/bounds. Prove it first on the
authentic TM and OMSLib PPCC 601 fixtures, then compare production
USBMIDI9 / E1 / E2a / E2b. Do not infer transition-vector semantics from
symbol names.

This turns the "pre-relocation contents do not establish vector absence"
finding from `spec/pefcheck/tasks.md` into an actual reconstruction: the
simulator materializes the pointers that CFM relocation would write.

## PEF relocation format (from Ghidra `.../format/pef`, Apple `PEFBinaryFormat.h`)

Relocation instructions are stored as **big-endian 16-bit chunks**
(`PEFRelocChunk = UInt16`). The instruction area begins at
`loaderContainerBase + relocInstrOffset` and is bounded by `relocCount`
chunks (per `LoaderRelocationHeader`). The opcode is decoded from the HIGH
bits of the first chunk; instructions larger than 2 bytes read a second
chunk. All relocation writes affect 4-byte (word) locations and are
**additive** (`memory[addr] += addend`).

Relocation headers (one per `relocSectionCount`, after the loader info
header and the import tables):
```
struct PEFLoaderRelocationHeader {
    UInt16  sectionIndex;      // section to fix up
    UInt16  reservedA;         // 0
    UInt32  relocCount;        // number of 16-bit chunks
    UInt32  firstRelocOffset;  // per-section offset into the relocation area;
                               // all fixtures have one relocation section, so
                               // 0 — chunks are contiguous from relocInstrOffset
};
```

### Opcode map (dispatch in first-chunk high bits)

| match | name | fields | effect |
|---|---|---|---|
| `(v & 0xC000)==0` | `RelocBySectDWithSkip` | skip=((v&0x3FC0)>>6), n=(v&0x3F) | advance `skip*4`; write sectionD base into `n` words (each +4) |
| `(v & 0xE000)>>13 == 0x2` | `RelocValueGroup` | sub=(v&0x1E00)>>9, run=(v&0x1FF)+1 | see sub-opcodes below |
| `(v & 0xE000)>>13 == 0x3` | `RelocByIndexGroup` | sub=(v&0x1E00)>>9, idx=v&0x1FF | see sub-opcodes below |
| `(v & 0xF000)>>12 == 0x8` | `RelocIncrPosition` | off=(v&0xFFF)+1 | advance relocation address by `off` |
| `(v & 0xF000)>>12 == 0x9` | `RelocSmRepeat` | chunks=(v&0xF00)>>8, n=(v&0xFF)+1 | repeat the preceding `chunks+1` chunks `n` times (see Repeat note below) |
| `(v & 0xFC00)>>10 == 0x28` | `RelocSetPosition` | off=((v&0x3FF)<<16)\|chunk2 | set relocation address to `off` in the relocated section |
| `(v & 0xFC00)>>10 == 0x29` | `RelocLgByImport` | idx=((v&0x3FF)<<16)\|chunk2 | write imported-symbol `idx` address; importIndex=idx+1; +4 |
| `(v & 0xFC00)>>10 == 0x2C` | `RelocLgRepeat` | chunks=(v&0x3C0)>>6, n=((v&0x3F)<<16)\|chunk2 | repeat the preceding `chunks+1` chunks `n` times (see Repeat note below) |
| `(v & 0xFC00)>>10 == 0x2D` | `RelocLgSetOrBySection` | sub=(v&0x3C0)>>6, idx=((v&0x3F)<<16)\|chunk2 | see sub-opcodes below |
| other high-bit patterns | (undefined) | — | informational note (never INVALID) |

### RelocValueGroup sub-opcodes

| sub | name | effect (per run, repeated `run` times) |
|---|---|---|
| 0 | `BySectC` | write sectionC base into word at addr; +4 |
| 1 | `BySectD` | write sectionD base into word at addr; +4 |
| 2 | `TVector12` | write sectionC at addr, write sectionD at addr+4, then +4 (12-byte step; 8-byte vector + 4 gap) |
| 3 | `TVector8` | write sectionC at addr, write sectionD at addr+4, then +8 (8-byte step; adjacent 4-byte entry and TOC words, no gap) |
| 4 | `VTable8` | write sectionD base into word at addr; +8 |
| 5 | `ImportRun` | write `run` imported symbols (importIndex..importIndex+run); each +4; importIndex += run |

### RelocByIndexGroup sub-opcodes

| sub | name | effect |
|---|---|---|
| 0 | `SmByImport` | write imported symbol `idx` address; +4; importIndex=idx+1 |
| 1 | `SmSetSectC` | sectionC = base of section `idx` |
| 2 | `SmSetSectD` | sectionD = base of section `idx` |
| 3 | `SmBySection` | write base of section `idx` into word at addr; +4 |

### RelocLgSetOrBySection sub-opcodes

| sub | name | effect |
|---|---|---|
| 0 | `LgBySection` | write base of section `idx` into word at addr; +4 |
| 1 | `LgSetSectC` | sectionC = base of section `idx` |
| 2 | `LgSetSectD` | sectionD = base of section `idx` |

### Loader state

- `relocationAddress`: byte offset in the section being relocated (starts at 0).
- `sectionC`, `sectionD`: initialized to the base of section 0 and section 1
  (or 0 if that section is not instantiated); changeable via SetSectC/D.
- `importIndex`: starts 0.
- Section bases come from the deterministic synthetic scheme (below).

### Repeat note

`RelocSmRepeat` (0x9) and `RelocLgRepeat` (0x2C) repeat the **preceding**
`chunks+1` chunks `count` times (the repeated instructions precede the repeat
opcode, per Apple's PEF spec). **None of the project fixtures uses a repeat
opcode**, so the simulator treats a repeat as an informational note and a
chunk-consuming no-op (no replay) — sufficient for fixture reconstruction.
Replay would be a follow-up for general PEFs. This limitation is documented,
never an INVALID.

## Synthetic base addresses

`base[0] = 0x1000_0000`; `base[i] = align16(base[i-1] + sections[i-1].total_length)`.
Deterministic and independent of Ghidra. This mirrors CFM's contiguous,
aligned placement convention (for TM, alignment byte 4 → 2^4 = 16 and
defaultAddress 0, so the synthetic section-1 base coincides with the value
the authentic loader produced). The scheme is a deliberate deterministic
choice for internal-pointer reconstruction, not a claim about exact runtime
placement.

## Simulator behavior

1. Locate the section to relocate (`relocSectionCount` headers; each fixture
   has exactly one, section index 1 = the PackedData section).
2. Decompress kind-2 sections with the pinned packed-data scheme, returning
   the **longest decodable prefix** and a decode status (`ok`, or
   `partial N/M (reserved opcode k at stream byte j)`, or exhaustion).
   A reserved opcode stops decoding but keeps the bytes already produced.
3. Apply the chunk program to the decoded prefix with the additive semantics
   above. Writes whose 4-byte range falls outside the decoded prefix are
   skipped and noted (byte unknown). Reads (for resolution) require the
   prefix to cover them.
4. Imported-symbol fixups (ImportRun, SmByImport, LgByImport) target
   **external** addresses the simulator cannot resolve. Record
   `(offset, symbol_name)` as unresolved external pointers and leave the
   target word byte-for-byte unchanged (note it). They never make a
   fragment INVALID.
5. Produce `RelocResult { content, decoded_len, decode_status, import_fixups, notes }`.

## Special-main vector resolution

If `mainSection != -1` and points at the relocated section, read the 8 bytes
at `mainOffset` in the relocated content **when `mainOffset+8 <= decoded_len`**.
- If `word0 != 0 && word0 % 4 == 0 && word1 != 0 && word1 % 4 == 0`, it is a
  valid PPC transition vector: resolve each word to
  `(section, offset = word - base[section])` for the unique section whose
  `[base, base+total)` contains it; validate `offset < total_length` and word
  alignment; report both resolutions.
- Otherwise report the raw bytes and that it is not an identifiable vector.
- If the location is beyond the decoded prefix, report "cannot reconstruct
  (relocated section not fully decodable; location beyond decoded prefix)".

## Fixture expectations (from the verified prototype)

Synthetic bases: TM `[0x10000000,0x10000470,…]`, 601 `[0x10000000,0x100000D0,…]`,
E `[0x10000000,0x10000010,…]`, production `[0x10000000,0x10002190,…]`.

| fixture | relocated section | decode | special main | result |
|---|---|---|---|---|
| TM | 1 (kind 2) | partial 90/167 (reserved op 5) | 1 + 0x3C | VALID vector entry→0+0, toc→1+0 |
| 601 | 1 | ok 12/12 | 1 + 0x4 | VALID vector entry→0+0, toc→1+0 |
| E1 | 1 | ok 8/8 | none (−1) | no special main |
| E2a | 1 | ok 8/8 | 1 + 0x0 | VALID vector entry→0+0, toc→1+0 |
| E2b | 1 | ok 8/8 | 1 + 0x0 | VALID vector entry→0+0, toc→1+0 (still INVALID for 0xE2 code alignment) |
| production | 1 | partial 0/468 (reserved op 6) | none (−1) | no special main; data not reconstructible |

TM decodes 90 bytes; its vector offset 0x3C is within the prefix and its
pre-relocation bytes there are zeros, so the vector is reconstructible.
Production's data decodes to 0 bytes, so its export `main` → section 1 +
0x80 cannot be reconstructed.

## Deliverables

- `pefcheck/src/reloc.rs`: `synthetic_bases`, `relocate_section`,
  `ResolvedVector`, `special_main_vector` (and helpers). Std-only.
- Wire into `validate.rs` `Report` as informational fields/notes (never
  errors) and print in `main.rs` report.
- Unit tests (fixture-based, `tests/pefcheck_tests.rs`) + property tests
  (`tests/prop_tests.rs`) for the simulator.
- Update `spec/pefcheck/tasks.md` reference and `docs/pefcheck-report.md`
  with the reconstructed vectors.

## Gates / acceptance

- `cargo test`, `cargo fmt --check`, `cargo clippy -- -D warnings` all green.
- TM, 601, E2a, E2b report a valid special-main vector resolving to
  (section 0 + 0, section 1 + 0) with 4-byte alignment; E1/production report
  no special main.
- E2b remains INVALID (code containerOffset 0xE2 alignment) — the simulator
  must not mask the structural error.
- Existing fixtures' sha256 pins unchanged; existing PASS/INVALID verdicts
  unchanged.
- Simulator is informational: it never flips a PASS to INVALID.

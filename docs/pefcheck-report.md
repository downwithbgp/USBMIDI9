# pefcheck — mechanical PEF comparison report (2026-08-18)

Tool: `pefcheck` (std-only Rust CLI, `pefcheck/`). All addresses below are
derived mechanically from PEF container fields — no Ghidra symbol names.
Fixture sha256 pins are enforced by `pefcheck/tests/pefcheck_tests.rs`.

## Verdict table

| artifact | file | size | sha256 (prefix) | code containerOffset | 16-byte code rule | special main (mechanical) | verdict |
|---|---|---|---|---|---|---|---|
| OMS Time Manager PPCC 1 (authentic, works) | `tm_ppcc1.pef` | 1579 | `4a0978fe…` | 0x170 | ✓ | section 1 + 0x3C | **PASS** |
| OMSLib PPCC 601 (authentic) | `omslib_ppcc601.pef` | 481 | `e5c47142…` | 0x110 | ✓ | section 1 + 0x4 | **PASS** |
| USBMIDI9 production (old 9501 B) | `production_usbmidi9.pef` | 9501 | `d33f3d3d…` | 0x280 | ✓ | none (−1) | **PASS** |
| E1 (preserved minimal entry) | `e1_oms.pef` | 257 | `9b5f6182…` | 0xF0 | ✓ | none (−1) | **PASS** |
| E2a (special-main variant) | `e2a_oms.pef` | 257 | `fa86b26d…` | 0xF0 | ✓ | section 1 + 0x0 | **PASS** |
| E2b (INVALID) | `e2b_oms.pef` | 257 | `87d12ec0…` | **0xE2** | **✗ (0xE2 % 16 = 2)** | section 1 + 0x0 | **INVALID** |

## Per-artifact mechanical report (pefcheck output)

### tm_ppcc1.pef (authentic OMS Time Manager PPCC 1; works on the G4)
```
sections: Code @0x170 len=1132; PackedData @0x5e0 len=75 unpacked=167; Loader @0x80 len=240
loader: mainSection=1 mainOffset=0x3c init=-1 term=-1 libs=1 imports=7
        relocInstrOffset=0x78 stringsOffset=0x80 exportHashOffset=0xe8 power=1 exports=0
special main: section 1 (PackedData) + 0x3c
relocation stream: 4a 06 42 07 46 02 00 00 49 6e 74 65 72 66 61 63 ...
note: main target content not decodable: reserved packed-data opcode 5 at stream byte 16
```
The special main = (section 1 = PackedData, offset 0x3C). The container's
data stream decodes through packed-data ops 0/1/2/4 and hits a reserved
opcode 5 at stream byte 16 (CW stream semantics beyond ops 0-4 unresolved).
**The transition vector [0x1000016C, 0x10000470] is NOT stored in the
container — it is loader-materialized** (as in every fixture here). The
loader-info layout is the 14×u32 form (byte-derived: u32 mainSection=1,
u32 mainOffset=0x3C, init/term = −1; the official PEF spec's SInt16 layout
would misread this working fragment as mainSection=0/initSection=1).

### omslib_ppcc601.pef (authentic OMSLib PPCC 601)
```
sections: Code @0x110 len=204; PackedData @0x1e0 len=1 unpacked=12; Loader @0x80 len=140
loader: mainSection=1 mainOffset=0x4 init=-1 term=-1 libs=1 imports=1
        relocInstrOffset=0x60 stringsOffset=0x64 exportHashOffset=0x84 power=1 exports=0
special main: section 1 (PackedData) + 0x4
main target bytes: 00 00 00 00 00 00 00 00  (zero vector — relocation-materialized)
```

### production_usbmidi9.pef (USBMIDI9 production, recovered from the Ghidra project)
```
sections: Code @0x280 len=8592; PackedData @0x2410 len=269 unpacked=468 total=33492; Loader @0x80 len=506
loader: mainSection=-1 mainOffset=0 init=-1 term=-1 libs=3 imports=13
        relocInstrOffset=0xc0 stringsOffset=0xd0 exportHashOffset=0x1e4 power=1 exports=1
export: name="main" class=2 nameOffset=0x10f value=0x80 section=1
relocation stream: 4a 0c 42 0c 46 04 42 00 80 43 40 2b 80 0f 40 0e ...
note: loader mainSection = -1 (no main symbol)
```
The production fragment has **no loader-info main symbol** (mainSection=-1)
and one export, `main` (class 2 = TOC-class, value 0x80, section 1) — the
same export shape as the E-series. Its 16-byte reloc stream is a real
relocation program (the data section carries 468 unpacked bytes of content).

### e1_oms.pef (preserved minimal-entry diagnostic)
```
sections: Code @0xf0 len=8; PackedData @0x100 len=1 unpacked=8; Loader @0x80 len=98
loader: mainSection=-1 mainOffset=0 init=-1 term=-1 libs=0 imports=0 exports=1
export: name="main" class=2 nameOffset=0x0 value=0x0 section=1
relocation stream: 46 00 00 00 6d 61 69 6e ...
note: loader mainSection = -1 (no main symbol)
```

### e2a_oms.pef (special-main vector variant)
```
sections: Code @0xf0 len=8; PackedData @0x100 len=1 unpacked=8; Loader @0x80 len=98
loader: mainSection=1 mainOffset=0 init=-1 term=-1 exports=1
special main: section 1 (PackedData) + 0x0
main target bytes: 00 00 00 00 00 00 00 00  (zero vector — relocation-materialized)
```

### e2b_oms.pef (INVALID)
```
sections: Code @0xe2 len=20; PackedData @0x100 len=1 unpacked=8; Loader @0x80 len=98
ERROR: code section 0 containerOffset 0xe2 is not 16-byte aligned (0xe2 % 16 = 2)
ERROR: section 0 (Code) containerOffset 0xe2 not aligned to 2^4 = 16
VERDICT: INVALID
```
E2b violates the PEF code-section container-alignment rule (>= 16 bytes in
the container) by moving the code from 0xF0 to 0xE2. This is the structural
defect MacsBug observed as misaligned code at runtime; E2b carries no
evidence about OMS entry/init/dispose behavior.

## Structural facts established by the tool

1. All six artifacts parse as PEF v1 containers (magic `Joy!peffpwpc`,
   containerVersion 1) with 3 sections (Code / PackedData / Loader).
2. The CW-built loader info is the 14×u32 layout (56 bytes) — verified
   mechanically from the authentic TM and 601 bytes; the official spec's
   SInt16 layout is not what CodeWarrior emits.
3. All section containerOffsets are 16-aligned except E2b's code (0xE2);
   all sections carry alignment byte 4 (2^4 = 16).
4. **No fixture stores a transition vector in the container.** The special
   main targets (TM 1+0x3C, 601 1+0x4, E2a/E2b 1+0x0) all land in the
   PackedData section whose container content is zeros/stub — the vectors
   are materialized by the loader at load time. Container-based vector
   decoding is therefore not possible; pefcheck reports the raw target
   bytes + the relocation-stream presence instead.
5. The E-series' four-byte relocation stream (`46 00 00 00`) is identical
   to the production's first instructions' shape; the production carries a
   full 16+ byte relocation program.
6. Exports parse and resolve by name: production and the E-series export
   exactly one symbol, `main`, class 2, pointing into section 1.

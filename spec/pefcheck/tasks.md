# pefcheck — tasks

Status: process reset after E2b. No new PPCC diagnostics. Host-side tooling only.
The structural checker below is complemented by the relocation simulator in
`spec/pefcheck-reloc/`.

## Goal

A small std-only Rust CLI `pefcheck` that mechanically parses PowerPC PEF
containers relevant to this project and validates structure — including the
code-section container-alignment rule E2b violated and PPC instruction-address
alignment. Addresses must be derived mechanically from PEF fields, never from
Ghidra symbol names. E2b must be REJECTED (code containerOffset 0xE2).
Produce a comparison report for the authentic PPCC(s), production USBMIDI9,
E1, E2a, E2b. Separately produce a CodeWarrior panel/settings checklist for
the G4 capture from `SampleOMSApp.mcp` target "Sample OMS PPC".

## PEF layout (established by prior decoding; all big-endian)

- Header @0: magic `Joy!peffpwpc` (12B), containerVersion u32 @0x0C,
  timestamp u32 @0x10, oldDef u32 @0x14, oldImp u32 @0x18, currentVersion
  u32 @0x1C, sectionCount u16 @0x20, instSectionCount u16 @0x22.
- Section headers @0x28, 0x1C each: nameOffset u32, defaultAddress u32,
  totalLength u32, unpackedLength u32, containerLength u32, containerOffset
  u32, sectionKind u8, shareKind u8, alignment u8, reservedA u8.
  kinds: 0=Code 1=UnpackedData 2=PackedData 3=Constant 4=Loader 5=Debug
  6=ExecutableData 7=Exception.
- Loader section (kind 4) container = loader info (NOT packed).
  LoaderInfoHeader = fixed 56-byte structure of 14 four-byte fields
  (Mac OS Runtime Architectures). `mainSection`, `initSection` and
  `termSection` are SInt32 (a value of -1 = no such symbol); the
  remaining count/offset fields are UInt32. There is **no SInt16
  loader-header layout** to override; the SInt16 sectionIndex belongs
  to the PEFExportedSymbol table entry, not the loader header. The
  parser keeps the raw big-endian u32 decode internally and exposes
  the three signed fields as i32. Field order: mainSection SInt32,
  mainOffset u32, initSection SInt32, initOffset u32, termSection
  SInt32, termOffset u32, importedLibraryCount u32,
  totalImportedSymbolCount u32, relocSectionCount u32,
  relocInstrOffset u32, loaderStringsOffset u32, exportHashOffset
  u32, exportHashTablePower u32, exportedSymbolCount u32.
- Export hash @ loader.containerOffset + exportHashOffset: 2^power u32 slots
  (symbolCount = w>>18, firstKeyIndex = w & 0x3FFFF); then
  exportedSymbolCount u32 keys (nameLength = w>>16, hashValue = w&0xFFFF);
  then exportedSymbolCount × 10B entries (classAndName u32, symbolValue u32,
  sectionIndex u16); name = strings at loaderStringsOffset +
  (classAndName & 0xFFFFFF).
- Special main (mechanical): if mainSection != 0xFFFFFFFF:
  mainAddr = sections[mainSection].defaultAddress + mainOffset.
- Transition vector (structural): if the special main (or an export) points
  into a non-code section (kind 1/2/4), read 8 bytes at the unpacked content +
  offset (decompress kind-2 via the packed-data scheme: opcode = byte>>5,
  value = byte&0x1F (0 => 7-bit varint), ops 0=ZeroSkip 1=BlockLiteral
  2=Repeat 3=RepeatBlock 4=RepeatZero); identifiable only when word0 != 0
  and both words are 4-byte aligned. Raw zero/stub bytes at the special-main
  location are **pre-relocation contents**: they do NOT establish that the
  vector is absent from the PEF representation — CFM relocation materializes
  pointer values at preparation (load) time. No code-range test applies
  (defaultAddress is 0).

## Validation rules

1. Container: magic; header/section table within file bounds.
2. Section bounds: each section containerOffset + containerLength <= file
   size; alignment byte: if 2^alignment <= 8192 then containerOffset %
   2^alignment == 0 (alignment > 13 clamped: check skipped, per spec
   semantics — alignment byte 0xFF must not overflow).
3. **Code-section alignment (the E2b rule): kind==0 sections must have
   containerOffset % 16 == 0.** E2b fails (0xE2 % 16 = 2). This rule is
   checked BEFORE rule 2's generic alignment so E2b reports the code-offset
   error specifically.
4. PPC instruction-address alignment: special-main address % 4 == 0 when
   the main section is code; every identifiable vector code target % 4 == 0;
   vector address % 4 == 0.
5. Loader bounds: loader info fields within the loader container; export
   table (slots+keys+entries) within bounds; export names resolve within
   the loader container (nameOffset is relative to loaderStringsOffset:
   name at loader + loaderStringsOffset + (classAndName & 0xFFFFFF);
   verified on the fixtures: production's 'main' resolves to "main").
6. mainSection == -1 (raw 0xFFFFFFFF, no main) is structurally legal; reported.
7. Special-main (mechanical): if mainSection != 0xFFFFFFFF: report the
   pair (mainSection, mainOffset) and the section kind; when the section
   is non-code (kinds 1/2/4/...), attempt the transition-vector decode at
   the section content + mainOffset (kind 2 = decompressed via the
   packed-data scheme below; kinds 1/4 = raw container bytes); when
   kind 0, the main address = code + mainOffset (alignment-checked).
   A zero target (E-series: unpacked = 8 zero bytes) reports the raw
   target bytes as **pre-relocation contents** — zero/stub bytes do NOT
   establish that the vector is absent from the PEF representation, since
   CFM relocation materializes pointer values at preparation time — plus
   the relocation-stream presence (relocSectionCount/relocInstrOffset).
   No "code target range" message is emitted:
   with container defaultAddress = 0 the zero word0 sits inside every
   code range, so a range check would be meaningless. Flat bases are
   assigned at load; the report states section + offset + raw words.

## Packed-data expansion (pinned; ported from the Ghidra-verified python)

Per stream byte: opcode = b >> 5, value = b & 0x1F; if value == 0,
value = varint (7 bits/byte, high bit = continuation). Ops:
0 = Zero: emit `value` zero bytes; 1 = Block: copy `value` literal
bytes; 2 = Repeat: count = varint, copy the `value`-byte block
(count+1) times; 3 = RepeatBlock: len = value, count = varint,
gap = varint; per iteration copy the len-block then the gap-block
(`count` iterations), then one final len-block; 4 = RepeatZero:
len = value, count = varint, emit len*(count+1) zero bytes.
Output must not exceed unpackedLength (overrun = report, not
INVALID); stream exhaustion / reserved opcode (5-7) = report, not
INVALID — rule 7's decode is informational. Pinned fixture
expectations: E1/E2a/E2b data (packed 1 byte `0x08`) unpack to 8
zero bytes — pre-relocation stub contents (a vector, if any, is
materialized by CFM relocation, not stored as such);
TM data stream (0x5E0, 75 B) decodes through ops 0/1/2/4 until a
reserved opcode 5 at stream byte 16 — reported as "not decodable
with ops 0-4", still PASS (the TM's vector [0x1000016C,
0x10000470] would be materialized by relocation at preparation time);
production data stream = the known-undecodable old-PEF stream —
same report-only treatment. None of the six fixtures stores a
vector's pointer values in the container (their special-main target
bytes are pre-relocation contents); the report emits the raw target
bytes + the relocation-stream presence (relocSectionCount,
relocInstrOffset, raw stream bytes) for each.

## Deliverables

- `pefcheck/` crate (std-only): src/main.rs (CLI: `pefcheck <file>...`,
  exit 0 = PASS, 1 = INVALID, 2 = parse error), src/pef.rs (parser),
  src/validate.rs (rules + report), tests/ (fixture-based), fixtures/
  (tm_ppcc1.pef, omslib_ppcc601.pef, production_usbmidi9.pef, e1_oms.pef,
  e2a_oms.pef, e2b_oms.pef — byte copies, frozen hashes preserved).
- Gates: `cargo test`, `cargo fmt --check`, `cargo clippy -- -D warnings`.
- `docs/pefcheck-report.md`: mechanical comparison (sections, main fields,
  vectors, alignment verdicts, PASS/INVALID) for all six fixtures.
- `docs/g4-cw-checklist.md`: CodeWarrior panels/settings to capture from
  SampleOMSApp.mcp "Sample OMS PPC" on the G4; note that an app target's
  main semantics differ from an OMS PPCC driver entry.

## Acceptance

- E2b rejected with the code-offset-16 alignment error; E1/E2a/TM/601/
  production pass structural checks.
- No Ghidra involvement in pefcheck (fixtures only).
- `cargo test` green; fmt/clippy clean; docs committed.
- Fixture hashes pinned in tests (FULL 64-char sha256): E1
  `9b5f6182dafce541a6ca02fec243af245109974afb198cd9fd2beef790579916`,
  E2a `fa86b26d440fbefe4875b255a73df94985304e587a27f71e46b7396700b99907`,
  E2b `87d12ec09db1d411e261d648f99b93cd04eb96977053e6702d1abf50b5d2dd60`,
  TM `4a0978fe6ee557a31a75fa46c5941d0a249433c93f745efc022eef3635c5a295`,
  601 `e5c47142e5e844b654a09383425bdad7f28baa05b0c1bb29dfe2061db2fefce7`,
  production
  `d33f3d3d89a0e82fb6d759175c0936f8d5e850e34c5c19f265cf833fdd4720c2`
  (the last three also recorded in docs/pefcheck-report.md) so a
  fixture copy error fails the suite.
- Decompressor unit tests cover all five packed-data opcodes + the
  7-bit varint, with output bounds vs unpackedLength.
- CLI exit codes: 0 = all files PASS, 1 = any file INVALID (takes
  precedence), 2 = parse error only.

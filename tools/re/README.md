# tools/re — USBMIDI9 reverse-engineering toolkit

Host-side helper scripts promoted from /tmp (2026-08-15..17 OMS/PEF/Mixed
Mode RE sessions) so the knowledge does not depend on ephemeral scratch
files. Every script is documented here with input/output and its
provenance; corrections made during promotion are noted in each file's
header. The authoritative structural checker is `pefcheck/` (Rust) — these
tools are for ad-hoc inspection; where they overlap, pefcheck wins.

Run the smoke tests: `make check-re-tools` (or `tools/re/smoke.sh`).

## Scripts

| tool | purpose | usage |
|---|---|---|
| `dis68k.py` | 68K disassembly of a blob region (capstone) | `dis68k.py FILE OFFSET LEN` |
| `disppc.py` | PPC disassembly at a PEF container offset (capstone) | `disppc.py FILE CONTAINER_OFFSET LEN` |
| `pef_unpack.py` | PEF packed-data decompressor, CORRECTED op3/op4 | `pef_unpack.py FILE OFF PACKED_LEN UNPACKED_LEN` |
| `pef_loaderinfo.py` | PEF loader-info decode (verified 14xU32 layout) | `pef_loaderinfo.py FILE` |
| `pef_analyze.py` | PEF sections + imports + exports (verified layout) | `pef_analyze.py FILE` |
| `rsrc_list.py` | classic resource-fork list/extract | `rsrc_list.py FILE [TYPE [ID]]` |
| `appledouble.py` | AppleDouble parse / resource-fork extract | `appledouble.py FILE [OUT]` |
| `omsabi.py` | OMSDevice mac68k ABI audit (sizeof 0xB6 = 182) | `omsabi.py` |
| `procinfo_check.c` | mechanical ProcInfo decodes (0xFB0, 0x2F0, A1/D0) | `cc -o pi procinfo_check.c && ./pi` |
| `bdiff.py` | byte-diff two binaries, differing runs | `bdiff.py FILE_A FILE_B` |
| `oms_errors.py` | extract OMS error constants from SDK OMS.h | `oms_errors.py OMS_H [SPEC_TXT]` |
| `ghidra/ApplyLabels.java` | reapply `docs/re/ghidra-functions.csv` labels on a fresh Ghidra import | Ghidra script (headless or GUI) |
| `binhex_decode.py` | BinHex 4.0 / HQX decoder (validated on 7 real vendor hqx; RLE count includes the byte before 0x90) | `binhex_decode.py FILE.hqx OUTPREFIX` |
| `sit5.py` | StuffIt 5 parser + method-13 (fastest) LZH decompressor (validated by IBM CRC16 on 7 real forks); method 15 NOT supported (see known-good-driver-search-2026-08-21.md §7) | `sit5.py LIST ARCHIVE`, `sit5.py EXTRACT ARCHIVE DIR`, `sit5.py DECOMPRESS 13 IN OUTLEN OUT` |
| `omdi_scan.py` | scan resource forks for OMdi/PPCC/OMdv/PROC; hex-decode OMdi payloads and classify code resources | `omdi_scan.py FILE_or_DIR...` |
| `make_classic_fixtures.py` | regenerate the synthetic hqx/sit5/omdi smoke fixtures (no proprietary bytes) | `make_classic_fixtures.py` |

## Conventions

- **Offsets are container/file/resource-relative, never runtime
  absolute.** CFM fragments move between boots. Runtime addresses
  (0x018Cxxxx style) appear only in `docs/re/runtime-traces.md`, marked
  load-specific.
- 68K library offsets are into `omslib_proc1.bin` (the `PROC` 1 of
  `Open Music System.rsrc`); PPC offsets are into the PEF container's
  code section unless stated otherwise.
- Binary artifacts are NOT committed (proprietary Opcode/Apple); hash and
  describe them in `docs/re/artifacts.toml`.

## Provenance / corrections

- `pef_unpack.py` — /tmp version had PRE-b08bf7c op3/op4 semantics
  (wrong RepeatBlock/RepeatZero). This version matches the verified
  `pefcheck/src/pef.rs` semantics; both authentic fixtures decode
  COMPLETE (TM 75→167, production 269→468).
- `pef_analyze.py` — /tmp version used a wrong 0x2C-byte section-header
  stride and guessed loader-info offsets; rewritten to the byte-verified
  layout (header: sectionCount u16 @0x20, headers @0x28 0x1C each;
  loader info = 14xU32; library table 24B/entry after the 56-byte
  header; symbol table 4B/entry; names are C-strings in the loader
  strings area).
- `pef_loaderinfo.py` — mainSection/initSection/termSection now printed
- `pef_mainrd_gate.py` — pre-runtime gate for the isolated static
  RoutineDescriptor special-main experiment; compares candidate to production
  while allowing only the intended data-section change.
  as SInt32 (-1 = none).
- `procinfo_check.c` — extended with the stack-based decodes
  (uppOMSDriverProcInfo=0xFB0, uppOMSDvrAdd1DevProc1Info=0x2F0) on top
  of the original register-based OMSReceivedFromPort decode.
- `bdiff.py`, `oms_errors.py` — generalized from hardcoded paths to argv.
- `rsrc_list.py`, `appledouble.py`, `omsabi.py`, `dis68k.py`,
  `disppc.py` — cleaned with provenance headers; `rsrc_list.py`
  re-validated byte-exact against the real OMS driver resource fork
  (PPCC 1 extraction sha256 746dc2ce… == /tmp/ppcc1_extracted.bin).

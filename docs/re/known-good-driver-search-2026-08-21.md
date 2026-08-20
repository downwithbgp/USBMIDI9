# Known-good OMS driver search (2026-08-21)

Goal: find at least one intact, known-working **PPC OMS driver** containing
both `OMdi 128` and a `PPCC` resource, so the private OMS native-loader
adapter (`+0xDC44 -> +0x98A2`, see `ppcc-abi-diagnostic-2026-08-20.md`
"UNKNOWN") can be compared against a known-good artifact. **No such driver
was found in any accessible archive.** Every vendor OMS driver obtained and
verified in this session is a 68K `OMdi + OMdv` driver. The evidence for
"shipping OMS drivers did not use the PPCC resource" is now strong; the
remaining unexamined candidate (Emagic Unitor/Logic-Control family) is
locked inside StuffIt 5.1 "max" (method 15) compression, which is a
different format from the documented "Arsenic" `As`-signature BWT and is
not decompressed by any open-source tool (see §7).

## 1. Bottom line

| claim | status |
|---|---|
| `OMdi 128` + `PPCC` driver exists in the accessible archives | **NOT FOUND** |
| every authentic vendor OMS driver examined is 68K (`OMdi` + `OMdv` code resource) | **PROVEN** (19 drivers, §4) |
| the OMS 2.3.8 loader's PPCC lookup (`Get1Resource('PPCC', OMdi+6)`) misses on every authentic driver because `OMdi+6 == 0` | **PROVEN** (§5) |
| the Emagic Unitor-family driver (prime remaining PPCC candidate) is locked in SIT5 method-15 compression | **PROVEN** (format mismatch) |
| USBMIDI9 source change justified by a known-good artifact | **NO** — no artifact found; no change made |

## 2. Sources searched

| source | what was searched | result |
|---|---|---|
| local `~/research/oms/oms238c/` (OMS 2.3.8 media, VISE-extracted, 157 files) | all 15+ OMS driver forks | all 68K, see §4 |
| local `~/research/oms/sc8850*` (Roland SC-8850 USB OMS driver) | OMdi/OMdv | 68K |
| Internet Archive: `tucows_206415` "Opcode's OMS" hqx | OMS 2.3.8 installer (`Install OMS 2.3.8 .sit`) | same 2.3.8 components (68K) |
| Internet Archive: `unitor-family-driver-v3` | Unitor MIDI Driver dmg | OS X pkg (not OMS-era) |
| Macintosh Garden `emagic-unitor8-midi-interface-software` | u8omsdrv.sit / u8install.sit / u8ctrl30.sit | driver = method-15 locked (§7) |
| Macintosh Garden `midiman` (M-Audio drivers CD 07/2000, ISO) | omsdrv106/104/base, usbmac104, macms1, macms4v1 sea/hqx | 4 MidiSport OMS drivers extracted + verified, all 68K (§4) |
| Macintosh Garden `emagic-logic-updates` (489 MB zip) | lc_oms_driver.sit (Logic Control OMS driver) | method-15 locked |
| Macintosh Garden OMS page (`open-music-system-238-oms`) | OMS_MIDI_Manager_Driver.sit + OMS 2.2/2.3.2/2.3.4-2.3.7 installers | not needed (same era) |
| motu.com Wayback CDX (1996-2016) | MTP AV / OMS driver downloads | classic-era drivers not preserved |
| emagic.de Wayback CDX (3000 captures) | Unitor/AMT driver downloads | downloads not preserved |
| archive.org full-text + advanced search | Emagic/Unitor/MOTU/OMS driver | no classic driver images |
| Macintosh Garden search | 5LX, MOTU, MTP, midisport, emagic | 5LX: nothing; MOTU: apps only |
| Asimov FTP index (2.5 MB) | OMS/opcode/emagic/motu | nothing |
| mac.archive.info-mac.org | (DNS dead) | — |

## 3. Extraction chain (all verified by CRCs)

To get from vendor archives to driver resource forks, this session built
three tools (committed in `tools/re/`):

1. **`binhex_decode.py`** — BinHex 4.0 decoder. The RLE count **includes**
   the byte before `0x90` (`FF9004` = 0xFF ×4; Peter N Lewis "BinHex 4.0
   Definition"). Earlier "layout B" (extra header byte) was an artifact of
   a wrong RLE port (`range(b)` vs `range(b-1)`); all 7 real hqx files
   validate with the single standard layout and all three CRCs
   (CRC-16/CCITT, init 0).
2. **`sit5.py`** — StuffIt 5 archive parser + **method-13 ("fastest")
   decompressor**. The LZH format matches the Amiga xad `SIT_13` and
   MacPaw XADMaster `XADStuffIt13Handle` (canonical Huffman, LSB-first
   bits, 12-bit table lookups; the bit-by-bit lookup must match the code
   length, not just the value). Validated: every decoded fork's **IBM
   CRC16** (poly 0x8005 reflected, init 0 — `XADCRCHandle IBMCRC16Handle`)
   equals the CRC stored in the SIT5 entry header, on 7 forks.
3. **`omdi_scan.py`** — resource-fork scanner classifying OMdi/PPCC/OMdv/
   PROC and hex-decoding every OMdi payload.

Reference decoders consulted: macutils `hexbin/hqx.c` (alphabet table,
RLE), xad `StuffIt.c` (SIT5 entry layout, SIT_13/SIT_Arsenic), XADMaster
(XADStuffIt5Parser, XADStuffIt13Handle, XADChecksumHandle), stuffit-rs
(Rust SIT5). The xad/stuffit-rs **method-15 ("max")** decompressors both
require the `As` (0x41 0x73) arithmetic signature; no SIT5.0/5.1-era
"max" stream carries it (see §7).

## 4. Every verified vendor driver (all 68K)

`OMdi 128` payloads below are the 16-byte `OMSDriverParams` from
`OMSDriver.h` (id, xxisSmart, hasMenuOrWindows, xxportNumM(+6),
xxportNumB(+8), flags, driverCompatibilityLevel, reservedFlags[6]).

| driver (file) | source | OMdi 128 | code resource | verification |
|---|---|---|---|---|
| MIDISport OMS Driver106 | M-Audio CD 07/2000 `omsdrv106.hqx` → .sea → SIT5 | `20 22 00 00 00 00 00 00 00 01 00 00 00 00 00 00` | OMdv 128, 6574 B (fork 9138 B), 68K (`60 0A 'OMdv' 0080`) | fork IBM CRC16 `6e1c` ✓ |
| MIDISport OMS Driver104 | `omsdr104.hqx` | same | OMdv 128, 6626 B (fork 8654 B), 68K | CRC `addf` ✓ |
| MIDISport OMS Driver (base) | `omsdr.hqx` | same | OMdv 128, 6422 B (fork 7402 B), 68K | CRC `d60c` ✓ |
| MIDISport OMS Driver1.04 (2x2) | `usbmac104.hqx` | same | OMdv 128, 6626 B (fork 8654 B), 68K | CRC `7a38` ✓ |
| Studio 64X / 64XTC / 128X OMS Driver | OMS 2.3.8 media | `40 00 00 01 00 00 00 00 80 01 00 00 00 00 00 00` | OMdv 131/132/133, 68K (+PROC 400/401) | rsrc_list ✓ |
| MIDI Time Piece OMS Driver | OMS 2.3.8 media | `66 66 00 00 00 00 00 00 80 01 00 00 00 00 00 00` | OMdv 128, 68K | rsrc_list ✓ |
| Standard Interface, Studio 3/4/5, IAC, QuickTime Music, SampleCell, MacProteus, MWM, ProSync, MIDIPort 32/96, QSDriver, SC-8850 USB | OMS 2.3.8 media + sc8850 | various Opcode ids | OMdv 68K | rsrc_list ✓ |

Resource sizes in the table are the OMdv resource body; the parenthetical
is the whole decompressed driver fork (resource fork of the driver file).

 The 2x2 archive additionally contains the Mac OS 8.5+ native USB drivers
(`MIDISportDriverv1.0.0`, `MIDISportLoaderv1.0.0`, `USBMIDISportShim`;
type `ndrv`) — these are **not** OMS drivers and carry no `OMdi`.

## 5. Why the PPCC path is never taken by real drivers

The OMS 2.3.8 loader (`oms-2.3.8-map.md` M1, PROVEN):
`loadCode`: `Get1Resource('PPCC', codeResID)` with
`codeResID = OMdi+6` (`xxportNumM`); on NULL it falls back to
`Get1IndResource('OMdv', 1)` (the 68K driver code lives **in the OMdv
resource** — every authentic driver's OMdv starts with the 68K
`60 0A 'OMdv' 00xx` header).

Every authentic driver examined has **`OMdi+6 == 0`**, so the PPCC lookup
is `Get1Resource('PPCC', 0)` → NULL → the 68K OMdv path (via the generic
`_CallUniversalProc` A9E2 site with `0x0FB0`). **No shipped driver ever
exercises the private `+0xDC44 -> +0x98A2` native adapter.** USBMIDI9 is
the first known `OMdi+PPCC` driver, and it is the first to hit that path
— consistent with the zero-ProcInfo failure being a genuine, never-validated
corner of OMS 2.3.8.

## 6. Comparison with USBMIDI9

| field | authentic 68K drivers (MidiSport) | USBMIDI9 |
|---|---|---|
| OMdi id (+0) | `0x2022` (M-Audio) / `0x4000` (Opcode) / `0x6666` (MOTU) | `0x7F10` (Opcode-assigned range) |
| xxportNumM (+6) | `0x0000` → PPCC lookup misses | `0x0001` → PPCC 1 loaded |
| xxportNumB (+8) | `0x0001` (legacy field, unused by 2.3.8 loader) | `0x0001` |
| compat level (+11) | `0x00` | `0x00` |
| code resource | `OMdv 128` = 68K code | `PPCC 1` = PEF fragment |
| loader path | OMdv → 68K + A9E2/0x0FB0 (works) | PPCC → GetDiskFragment → `+0xDC44` adapter (zero-ProcInfo crash) |

The OMdi fields themselves are not the missing contract: the authentic
OMdi records deliberately point the code lookup at the 68K OMdv fallback.
The missing artifact is a driver whose OMdi+6 selects a PPCC resource in a
way that was proven to work — and none exists in the accessible corpus.

## 7. The SIT5 method-15 ("max") blocker

StuffIt 5.0/5.1-era "max" streams (verified on 6 blobs from 3 independent
archives: MidiSport installer rsrc, Emagic Unitor rsrc, Logic Control
driver/PDF/Change History) start with high-entropy bytes and **do not**
decode under the documented "Arsenic" arithmetic-coder format:

- xad `SIT_Arsenic` (2003 and v13) and stuffit-rs `SitArsenicDecoder`
  both require the first 16 arithmetic-decoded bits to equal `As`
  (0x41 0x73). A brute-force over 12 byte offsets × both bit orders ×
  model variants found no `As` anywhere in the streams.
- The SIT5 "max" is therefore a different (undocumented) BWT variant.
  The Unarchiver/XADMaster lists SIT5 but has no decompressor; ReStuff
  converts SIT5 by running StuffIt Expander in an in-browser Mac emulator.

Blobs verified as method 15 (first bytes):

| blob | first 8 bytes | source |
|---|---|---|
| Unitor Family OMS Driver rsrc (36,206 B) | `42 c1 d4 f1 04 92 5b 18` | u8omsdrv.sit (Garden) |
| Logic Control OMS Driver rsrc (23,984 B) | `42 c1 d5 06 04 bb f4 b8` | emagic_MAC.zip → lc_oms_driver.sit |
| Change History.txt data (337 B) | `42 c1 d5 06 04 bb f4 b8` | same |
| MIDISport1x1 Installer rsrc (160,462 B) | `06 6b c8 4d f4 a3 22 d1` | MIDIMAN_07_2000.iso |
| Logic Control OMS Driver.pdf data (365,465 B) | `42 c1 d5 10 23 94 36 ae` | emagic_MAC.zip |

## 8. Exact manual acquisition for the G4 (StuffIt Expander route)

The one remaining high-value artifact is the **Emagic Unitor Family OMS
Driver** (`u8omsdrv.sit`, 14,880 B, SHA-256
`5f167fdef6cebfaa1ba1f09b17ae36463c02bc560dd10621ed3cd9c233886447`)
and the **Logic Control OMS Driver** (inside `lc_oms_driver.sit`, SHA-256
`38733d3d6c53dea8cf9098c405091aabd2ead76177e7cd22bb5e089fe78f2fb8`). To unstuff on the G4:

1. Copy `u8omsdrv.sit` to the AFP share; on the G4 drag it onto
   **StuffIt Expander** (any 5.x-8.x; method 15 "max" is supported by all
   real StuffIt versions).
2. The archive contains one file: `Unitor Family OMS Driver`
   (type `OMdv`, creator `U8dr`; resource fork 36,206 B).
3. Copy the extracted driver back to the share, then run
   `python3 tools/re/omdi_scan.py <driver>.rsrc` and
   `python3 tools/re/rsrc_list.py <driver>.rsrc` on the host.
4. If it shows `OMdi 128` + `PPCC`, extract `PPCC` with
   `rsrc_list.py ... PPCC 1` and run `pefcheck` + `ppcc_abi_report.py` on
   it. Record the archive and resource hashes in `artifacts.toml`.

Do **not** ask for another MacsBug session; this is purely an extraction
step.

## 9. What was NOT done (deliberately)

- No USBMIDI9 source change: the task forbids implementing a fix before a
  known-good artifact establishes the missing contract; none was found.
- No new PEF variant was synthesized.
- No proprietary binaries were committed; only tools, hashes, and docs
  (proprietary artifacts are hash-only in `artifacts.toml`).

## 10. Files changed in this session

- `tools/re/binhex_decode.py` — new, BinHex 4.0 decoder (validated).
- `tools/re/sit5.py` — new, SIT5 parser + method-13 decompressor (validated).
- `tools/re/omdi_scan.py` — new, OMdi/PPCC scanner.
- `tools/re/make_classic_fixtures.py` — new, synthetic fixture generator.
- `tools/re/fixtures/{hqx_smoke.hqx,sit5_smoke.sit,omdi_smoke.rsrc}` — new.
- `tools/re/smoke.sh` — new smoke tests for the three tools.
- `docs/re/artifacts.toml` — new artifact hashes.
- `docs/re/known-good-driver-search-2026-08-21.md` — this report.

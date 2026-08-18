# OMS 2.3.8 base-independent map

All offsets are **file/resource-relative**, never runtime absolute.
Component: `omslib_proc1.bin` = `PROC` 1 of `Open Music System.rsrc`
(OMS library; sha256 3655f74d…, 86507 B; proprietary — hash-only in
artifacts.toml). "TM stub" = the authentic OMS Time Manager `PPCC` 1
PEF (`tm_ppcc1.pef` fixture, sha256 4a0978fe…, 1579 B). "OMS Setup" =
`oms_setup_proc1.bin` (sha256 277050e3…, 604 B — a PROC, not the main
code blob) and `oms_setup_code1.bin` (sha256 19662c84…, 140038 B).

Confidence: P = PROVEN (byte/disasm evidence in docs/re or
docs/oms-ppcc-entry-crash.md), SI = STRONG INFERENCE, H = HYPOTHESIS.

## Coordinate model for PROC 1 runtime mapping

For the extracted artifact used here, `rsrc_list.py` removes the Resource
Manager's four-byte resource-length prefix and writes the resource body to
`omslib_proc1.bin`. The body begins at file/resource offset `0x000000` with
the PROC entry bytes (`60 0A 00 00`) and has no additional `0x0600`-byte
executable-code prefix. Therefore, for this artifact:

```text
resource offset       = executable/code offset = PROC-relative offset
runtime address       = load base + executable/code offset
```

For the T8/T9 stub:

| field | value | basis |
|---|---:|---|
| resource offset | `0x000098BE` | byte sequence `20 1F 70 00 4E 75` in `omslib_proc1.bin` |
| executable/code offset | `0x000098BE` | no resource/code bias in extracted PROC body |
| PROC-relative offset | `0x000098BE` | same coordinate for this raw PROC artifact |
| runtime address | `0x0180267E` | MacsBug T8/T9 transcript |
| runtime load base | `0x017F8DC0` | `0x0180267E - 0x000098BE` |
| mapping bias | `0x00000000` | resource offset equals executable offset |

The previously recorded `0x017F87C0` base was an arithmetic/documentation
error: it is `0x600` below the correct base and would map `0x98BE` to
`0x0180207E`, not `0x0180267E`. There is no evidence for a `0x600` bias.

## M1. Driver load path (library, PROVEN — docs/oms-ppcc-entry-crash.md §A/G.1)

| component/resource | offset (file) | proposed name | calling convention | inputs/outputs | evidence | confidence |
|---|---|---|---|---|---|---|
| library PROC 1 | 0xDCE4 | `loadCode` | 68K A4-based | gets the driver's code resource | disasm: `Get1Resource` trap A81F at 0xDD08 | P |
| library PROC 1 | 0xDD08 | Get1Resource site | trap A81F | type `'PPCC'` (pref=2) / `'PROC'` (pref=1); id = `OMDriverParams.xxportNumB` = word at +6 of the 16-byte `'OMdi'` 128 | disasm | P |
| library PROC 1 | 0x0DAA2 | driver-load fn | 68K | loads PPCC/PROC, pref=2 continuation 0x0DB6E..0x0DC1E | disasm + loader trace | P |
| library PROC 1 | 0x0DBDC | **GetDiskFragment site** | trap AA5A | args: FSSpec (table+0x52), filename (table+0x62), options 5; out: mainSymbolClass, connID, **mainAddr**; 32 result bytes copied to driver table +0x66 (moveq #7 loop at 0xDBF6) | disasm | P |
| driver record | +0x52 | connID out | — | CFragConnectionID | G.1 | P |
| driver record | +0x62 | **mainAddr out** | — | CFM-returned fragment main handle/address; live path's `+0x66` copy is a RoutineDescriptor whose `procDescriptor` is the fragment's PPC transition vector | G.1 + live descriptor dump | P |
| driver record | +0x66 | CFM result / main descriptor area | — | eight longwords copied from the GetDiskFragment result area; live first pointer is the RoutineDescriptor; `lea.l $66(a2),a0` then aliases `+0x52` to this area | G.1 + live dump | P |

## M2. Driver-message dispatch (PROVEN — §G.2)

| component/resource | offset | proposed name | calling convention | inputs/outputs | evidence | confidence |
|---|---|---|---|---|---|---|
| library PROC 1 | 0x10A36 | `driver-message dispatcher` | 68K | (driverRec, msgSlot, byteOut, flags) | disasm | P |
| library PROC 1 | 0x1075E | driver-type check | 68K | picks the PPC path (0x10B1C) vs 68K | disasm | P |
| library PROC 1 | 0x10B1C | PPC driver call path | 68K | builds the 14-byte argument block | disasm | P |
| library PROC 1 | 0x10C62–0x10C9A | call block | 68K | HLock(handle); A0 = UPP (mainAddr); A1 = A6; **trap A9E2 at 0x10C86**; 16-bit result D0; HUnlock | disasm | P |
| library PROC 1 | 0x10C86 | **_CallUniversalProc site** (the ONLY Mixed Mode trap in the blob) | trap A9E2 | 14-byte block = `[msg 2B][par1 4B][par2 4B][procInfo 4B]`; procInfo = uppOMSDriverProcInfo 0xFB0 | grep (1 occurrence) + SDK OMSDrvUPPs.h match | P |
| library PROC 1 | 0x10B6C–0x10B76 | return handling | 68K | result → d3; `tst.w d3`; `bne` → 0x10BEE (error); fall-through = success | disasm | P |
| library PROC 1 | 0x10BEE | error path | 68K | flags & 4 → return 2 (silent); else reporter 0x10934 (at (0x894,A4)); selects return 3 or retry | disasm | P |
| library PROC 1 | 0x10934 | error reporter (lazily installed) | 68K | driver-context args (FSSpec vRefNum/parID…) | disasm | P |

## M3. Known hardcoded OMS driver messages and callers (PROVEN)

| msg | value | caller site | notes |
|---|---|---|---|
| omdvInit | 0 | first message per Spec | "The code resource has just been loaded" |
| omdvDispose | 1 | after nonzero init return ("driver will receive an omdvDispose message immediately after returning") | |
| omdvAddDevices | 2 | during Setup Search / Studio Setup creation | |
| 0x10–0x13 | 16–19 | various | |
| 0x23 | 35 | **0x9F4A** → dispatcher 0x99B6 | the only direct caller of 0x99B6 |
| 0x29 | 41 | | |
| 0x2B | 43 | | omdvTestDevice at 0x2B990 |
| 0x1000 | 4096 | **0x54FC** | sent to a TQna driver |
| **0x00FF** | 255 | **NO literal site** | arrives via a variable-message path (S2); E0 observed msg=00FF (runtime-traces T3) |

## M4. Variable-message dispatcher (PROVEN)

| component | offset | name | convention | inputs/outputs | evidence |
|---|---|---|---|---|---|
| library PROC 1 | 0x99B6 | generic list-dispatcher | 68K | reads msg from caller `$10(a7)`; only direct caller 0x9F4A (passes 0x23) | disasm |

## M4a. Zero-return continuation reached by the T8/T9 failure (byte-audited)

| component/resource | offset | proposed name | convention | inputs/outputs | evidence | confidence |
|---|---:|---|---|---|---|---|
| library PROC 1 | 0x98A2 | special/internal dispatch routine | 68K | tests word at `0x0008(A7)`; special path constructs a callback call and reaches the continuation below | raw bytes + disasm | P |
| library PROC 1 | 0x98BE | post-Mixed-Mode continuation | 68K | exact bytes `20 1F 70 00 4E 75`; pops a longword into D0, clears D0, RTS | byte match + T8/T9-A0 | P |
| library PROC 1 | 0x147D0 | suspected function-pointer table region | data | neighboring longwords include `0x0000A3E8`, `0x000098A2`, `0x0000E17C`; no code reference to the table address was found in the extracted PROC 1 | raw data scan | SI |

The runtime `0x0180267E` maps to resource, executable, and PROC-relative
offset `0x000098BE` with load base `0x017F8DC0`. At runtime the
continuation's first longword was
zero (the value popped into D0), and the next longword was also zero (the
RTS return PC). This proves the bad return stack state but does not yet
identify which higher-level constructor supplied the missing continuation.

The raw bytes are:

```text
0x98A2: 0C 6F FF FF 00 08   CMPI.W #$FFFF,0x0008(A7)
0x98A8: 66 16               BNE.S  0x98C0
0x98AA: 59 4F               SUBQ.W #4,A7
0x98AC: 3F 3C FF FF         MOVE.W #$FFFF,-(A7)
0x98B0: 42 A7               CLR.L  -(A7)
0x98B2: 42 A7               CLR.L  -(A7)
0x98B4: 20 6F 00 18         MOVEA.L 0x0018(A7),A0
0x98B8: 20 68 00 52         MOVEA.L 0x0052(A0),A0
0x98BC: 4E 90               JSR    (A0)
0x98BE: 20 1F               MOVE.L (A7)+,D0
0x98C0: 70 00               MOVEQ  #0,D0
0x98C2: 4E 75               RTS
```

The extension word is the unsigned bit pattern `0x0018`; as a signed
16-bit displacement it is `+0x0018` = `+24` decimal. It is not decimal
18. Let entry A7 be `S`. The stack deltas are `0x00`, `-0x04`, `-0x06`,
`-0x0A`, `-0x0E`, then `-0x12` during `JSR`; the ordinary callee return
restores A7 to `S-0x0E`. Therefore the effective address is mechanically
`S-0x0E + (+0x0018) = S+0x0A`. `+98B4` loads the longword at exactly
`S+0x0A`; it is not yet proven to be an object or named argument. The raw
bytes prove the separate field displacement is `0x0052`, but do
not prove that the value at `S+0x0A` is a first argument or that its object
is the loader record. Those earlier identities are retracted pending a
consumer/caller trace.

The byte-level entry/constructed layout is:

```text
entry S+0x00..0x03  caller return PC                         (not rewritten here)
      S+0x04..0x07  caller-owned/contract-dependent longword (not proven)
      S+0x08..0x09  word tested by CMPI.W = 0xFFFF
      S+0x0A..0x0D  longword loaded by +98B4                 (value unknown statically)
      S+0x0E..0x11  following incoming bytes                 (not consumed by +98B4)

after +98B2, A7=S-0x0E:
      S-0x0E..0x0B  00000000  written by CLR.L at +98B2
      S-0x0A..0x07  00000000  written by CLR.L at +98B0
      S-0x06..0x05  FFFF      written by MOVE.W at +98AC
      S-0x04..0x01  reserved by SUBQ.W #4 at +98AA

during +98BC JSR, A7=S-0x12:
      S-0x12..0x0F  68K return PC pushed by JSR
```

On an ordinary return from the Mixed Mode target, A7 is again `S-0x0E`.
The continuation therefore pops the `+98B2` zero, and its RTS consumes the
`+98B0` zero. The producer of `S+0x0A` and the writer of the resulting
value's `+0x52` remain unresolved; the `0x147D4` table word is only a raw
data occurrence and has no identified in-code consumer.

The data at `0x147D0` is a candidate table because it contains neighboring
function-like offsets, including `0x000098A2`; however, the extracted PROC
1 has no literal code reference to `0x147D0`/`0x147D4`. Its table consumer,
base, stride, and argument construction remain unresolved. No contradiction
to `uppOMSDriverProcInfo=0x0FB0` has been established. The later runtime
capture proves that the `0x0052` value used on this path can be a Mixed Mode
RoutineDescriptor, so the earlier raw-68K-callback interpretation is
retracted.

Static descriptor-consumer check: PROC1 `+0x1EE0` compares the word at
`4(A7)` with `0xAAFE` and returns a Boolean. Its caller at `+0x1EF0`
chooses among internal record tables at `object+0x18`, `object+0x20`, and
`object+0x28`; records are traversed with `0x0E`-byte stride. This proves
that this OMS code recognizes incoming RoutineDescriptors, but it does not
prove that it created the live descriptor at `0x017D7A26`, nor that this
record family is the object read at `+98B4`. A scan found no
`NewRoutineDescriptor` call in OMS PROC1. Descriptor creator, the
`S+0x0A` object identity, and the connection of that object to `+0x98A2`
therefore remain unresolved.

## M5. OMS Setup PPC driver-call sites (H)

`oms_setup_code1.bin` (140038 B, 68K code) contains the OMS Setup
application logic. A single Mixed Mode call pattern mirroring M2 is
expected; the mapping is incomplete. **TO PRESERVE:** the G4 Search
crash happens while Setup is running — a full call-site map of Setup's
driver-message path would pin the 0x00FF caller.

## M6. Authentic TM PPCC stub (PROVEN — §B, pefcheck-report)

| component | offset | name | convention | inputs/outputs | evidence |
|---|---|---|---|---|---|
| tm_ppcc1.pef code | 0x16C | `.main` export target | pascal PPC, args r3/r4/r5, TOC r2 | `mflr r0` prologue; msg==0 writes a word to `*par1` (`stw r0,0(r28)` at 0x1000026C) and returns 0; private −1/−2 bridge to 68K `PROC` 1 via CallUniversalProc(entry, 0x3A5) after Get1Resource('PROC',1)+DetachResource+HLock; everything else returns 0 | Ghidra + disasm |
| tm_ppcc1.pef code | 0x100001DC | `li r3,-1` internal failure return | | | §E2b design |
| tm_ppcc1.pef data | section 1 + 0x3C | special main target (vector) | | `[0x1000016C, 0x10000470]` after TVector8 relocation | pefcheck |

## M7. USBMIDI9 production PPC layout (PROVEN — pefcheck + Ghidra)

| component | offset | name | notes |
|---|---|---|---|
| PEF code section | container 0x280, 8592 B | Code | 16-aligned ✓ |
| PEF data section | container 0x2410, 269 packed → 468 | PackedData | COMPLETE decode |
| PEF loader | container 0x80, 506 B | Loader | mainSection=-1 (production build has NO special main — the pre-Main-fix artifact) |
| loader-info | 0x80 + 0x24 | relocInstrOffset 0xC0; strings 0xD0; exportHash 0x1E4 power 1 | |
| export | `main` class=2 value=0x80 section=1 | transition vector [0x10001484, 0x1000A190] | 8-byte data object, NOT code (pre-Main-fix) |
| code | 0x1484 (Ghidra 0x10001484) | real pascal `main` | `mflr r0; stw r0,8(r1); stwu r1,-64(r1); sth r3,0x5a(r1)…` |
| code | 0x1390 | `oms_handle_message` | `extsh` msg; `cmplwi r0,0x2b; bgt default` |
| code | 0x338 / 0x7C0 / 0xCD4 | address-taken callbacks (oms_rx_event / oms_tx_send / USB notify) | vectors in loader info |
| imports | — | DriverServicesLib{BlockMove}, InterfaceLib{DisposeRoutineDescriptor, GetZone, NewRoutineDescriptor, Gestalt, SystemZone, SetZone, FindSymbol, CallUniversalProc}, USBManagerLib{USBGetNextDeviceByClass, USBGetDriverConnectionID, USBRemoveDeviceNotification, USBInstallDeviceNotification} | verified byte-level (tools/re/pef_analyze.py) |

## M8. Named functions during RE (stable names)

| artifact | offset | name | source |
|---|---|---|---|
| library PROC 1 | 0xDCE4 / 0xDD08 / 0x0DAA2 / 0x0DBDC / 0x0DC16 | loadCode / Get1Resource site / driver-load / GetDiskFragment site / interior-ptr lea | docs/oms-ppcc-entry-crash.md |
| library PROC 1 | 0x10A36 / 0x10C86 / 0x10BEE / 0x10934 | dispatcher / A9E2 site / error path / reporter | §G.2 |
| library PROC 1 | 0x99B6 / 0x9F4A / 0x54FC / 0x2B990 | list-dispatcher / its caller / TQna-msg site / omdvTestDevice | session analysis (00FF) |
| tm_ppcc1.pef | 0x16C | TM stub main | §B |

See `ghidra-functions.csv` for the machine-readable offset→name export
and `tools/re/ghidra/ApplyLabels.java` to reapply labels in Ghidra.

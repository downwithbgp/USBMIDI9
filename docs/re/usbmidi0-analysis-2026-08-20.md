# USBMIDI0 MacsBug session: static/runtime correlation

This note analyzes the unmodified transcript
`MacsBug-logs/USBMIDI0-MACSBUG.LOG`. The raw file is preserved byte-for-byte;
its SHA-256 is `3956d509bcccbe8ca6806404fe776f9534f43782046a89294f595d09c3e53a0f`
and its size is 67,060 bytes. The transcript contains 986 CR-delimited
logical lines.

## Executive result

This session did not reproduce the USBMIDI9 failure. It walked ordinary OMS
68K driver records, including MWM OMS Driver 2.2 and Studio 64XTC, through the
same-looking internal `FFFF` callback machinery and returned cleanly.

The most important correction is that the live `+98A2` target in this session
was an ordinary 68K OMdv code-resource veneer. Its handler removed the exact
10-byte Pascal argument frame, copied its result, restored the real return PC,
and jumped back to the caller. The earlier general claim that `+98A2` leaves
the frame for `+98BE/+98C2` to consume is therefore **FALSIFIED**. The older
zero-ProcInfo RoutineDescriptor capture remains valid as a separate historical
USB/PPCC invocation, but it must not be generalized to every `+98A2` call.

The new evidence narrows the production problem to the USBMIDI9-specific
native/PPCC path or its metadata/dispatch selection. It does not justify a
source workaround for `0x00FF`, and it does not establish that OMS's ordinary
OMdv adapter is defective.

## Timeline of the transcript

The session contains several intentional MacsBug stops and manual debugger
entries. The NMI/Command-Power entries near the end are not crash evidence.
The operational sequence is:

1. OMS Setup Search enters the OMS library. The current PROC 1 heap block is
   identified as `0186D6F0` with length `000151EB+09`; this is the relocation
   base for the runtime addresses below.
2. A breakpoint reaches `PROC1+0x98BC = 01876FAC`, the indirect `JSR (A0)`
   continuation point in the special `FFFF` wrapper.
3. `A2=01618C60` is an ordinary driver record (SampleCell OMS Driver), and
   `A0=01617490` is labeled by MacsBug as `'OMdv 0080 05FE'`. The target is
   valid 68K OMdv code, not junk and not the previously captured `AAFE`
   descriptor.
4. The OMdv veneer branches to handler code beginning at `01617934`. For
   selector `FFFF`, the handler reaches `016179A0` and executes the cleanup
   epilogue shown below.
5. The callback returns with result zero and a valid caller return address
   `0187B336`. The outer `+98BE` pop consumes the result and `+98C2` RTS
   returns normally.
6. The caller at `0187B336` continues through later callbacks and list logic.
   The transcript then visibly reaches MWM OMS Driver 2.2 and Studio 64XTC;
   those callbacks also return. The attempted USB name search found a string
   image, not a live record address, and did not capture USBMIDI9.
7. The final NMI entries are manual Command-Power entries into MacsBug and
   are excluded from the driver-failure timeline.

There is no valid `E0`/PPC `main(0x00FF,...)` observation in this transcript.
Consequently it cannot prove the semantics of the production USB return value.

## The corrected live callback

With PROC 1 base `0186D6F0`:

```text
01876F92  0C6F FFFF 0008       CMPI.W #$FFFF,$0008(A7)
01876F98  6616                  BNE.S  ...
01876F9A  594F                  SUBQ.W #4,A7
01876F9C  3F3C FFFF             MOVE.W #$FFFF,-(A7)
01876FA0  42A7                  CLR.L -(A7)
01876FA2  42A7                  CLR.L -(A7)
01876FA4  206F 0018             MOVEA.L $0018(A7),A0
01876FA8  2068 0052             MOVEA.L $0052(A0),A0
01876FAC  4E90                  JSR (A0)
01876FAE  201F                  MOVE.L (A7)+,D0
01876FB0  7000                  MOVEQ #0,D0
01876FB2  4E75                  RTS
```

The target `01617490` begins with a short veneer and reaches the real handler
at `01617934`:

```text
01617934  4E56 FFFC             LINK A6,#-$0004
01617938  48E7 0030             MOVEM.L A2/A3,-(A7)
0161794E  302E 0010             MOVE.W $0010(A6),D0
01617952  0C40 0012             CMPI.W #$0012,D0
01617956  6200 ...              BHI.S ...
...
016179A0  285F                  MOVEA.L (A7)+,A4
016179A2  2D6E FFFC 0012        MOVE.L -$0004(A6),$0012(A6)
016179A8  4CDF 0C00             MOVEM.L (A7)+,A2/A3
016179AC  4E5E                  UNLK A6
016179AE  205F                  MOVEA.L (A7)+,A0
016179B0  4FEF 000A             LEA $000A(A7),A7
016179B4  4ED0                  JMP (A0)
```

The abbreviated middle line is intentionally not used as evidence; the
surrounding raw bytes and the complete screen transcript establish the
handler's selector test. The decisive epilogue is exact: it writes the saved
result to the caller's result slot, restores the saved return PC, removes
`0x0A` bytes, and jumps through that return PC.

At the epilogue stop the stack was:

```text
2DE93608  00000000 0187B336 00C6E0F0 FFFF0161 ...
```

Thus `[A7]=0` is the result and `[A7+4]=0187B336` is a valid return address.
The outer `MOVE.L (A7)+,D0` consumes only the result; the following RTS uses
the restored caller PC. This directly falsifies the old “callback leaves two
zero longs” explanation for this ordinary OMdv target.

## Driver-list state machine

For this boot, the observed loop is `PROC1+0xA0A2..+0xA13E`, runtime
`01877792..0187782E`. The important state is a linked internal record, not a
public `OMSDriverTableEntry` asserted by address alone:

```text
A2 = current internal driver record
[A2+0x04] = next record (the MWM record followed SampleCell)
[A2+0x56] = byte state tested before/after initialization
[A2+0x8A] = callback target used by later walker stages
```

The loop first tests a pending handle/state at its stack frame, invokes a
helper, then tests `record+0x56`. If the record is not initialized it calls
the `+0x94A2` family. Later branches invoke `record+0x8A` with stage-specific
arguments and continue through `[A2+4]`. The transcript shows ordinary records
surviving these stages.

The loader's native-fragment block is a distinct earlier state transition:

```text
+0xDBDC  GetDiskFragment
+0xDBE6  MOVE.B #1,$57(A2)
+0xDBEC  LEA $106(A7),A0
+0xDBF0  LEA $66(A2),A1
+0xDBF6  copy eight longwords from the CFM result area to record+$66
+0xDBFC  MOVEA.L (A2),A0
+0xDBFE  MOVE.L $8(A0),$72(A2)
+0xDC04  MOVE.B #1,$77(A2)
+0xDC0A  MOVE.L $52(A2),$7A(A2)
+0xDC10  MOVEA.L A2,A0
+0xDC12  LEA $66(A0),A0
+0xDC16  MOVE.L A0,$52(A2)
+0xDC30  CLR.L -(A7)
+0xDC32  MOVE.L A2,-(A7)
+0xDC34  MOVE.W #$FFFF,-(A7)
+0xDC38  MOVE.L (A2),-(A7)
+0xDC3A  MOVEA.L (A7),A0
+0xDC3C  MOVEA.L $0C(A0),A1
+0xDC40  MOVEA.L $0C(A1),A1
+0xDC44  JSR (A1)
+0xDC46  MOVE.W D0,D3
```

This is the corrected byte-level topology. `A2` is the driver record, but the
value pushed at `+DC38` and dereferenced at `+DC3A` is `O0=[A2]`. The selected
method is therefore:

```text
O0 = [record]
O1 = [O0+0x0C]
method = [O1+0x0C]
```

The earlier interpretation `A0=A2; A1=[A2+0x0C]` was a disassembly/dataflow
error and is corrected in `oms-2.3.8-map.md` and `evidence-ledger.md`.

The unload block analogously pushes `0xFFFE` and follows the same nested
method topology. This makes `FFFF`/`FFFE` strongly identifiable as internal
load/unload probes, not public OMdv messages 0–43. The exact semantic name of
the method object is not proven from the static blob alone.

## Resource and ABI comparison

The available corpus does not contain an installable known-good native PPC
OMS driver. The authentic `tm_ppcc1.pef` is the PPCC 1 resource from Opcode's
`OMS Time Manager.rsrc`, a component with PROC 1, not an OMS driver file and
not evidence that TM is installed on the G4. Other local authentic driver
files are ordinary 68K drivers:

| artifact | resources / entry | what is proven |
|---|---|---|
| USBMIDI9 production | OMdi 128, PPCC 1, SICN 128, vers 1; current Main=main build | native PPC fragment path is present; no local OMdv companion in the driver resource |
| MWM OMS Driver 2.2 | OMdi 128, OMdv 128 | ordinary 68K OMdv veneer; live `FFFF` callback cleans correctly |
| SampleCell | OMdi 128, OMdv 128 | ordinary 68K path; live record walked successfully |
| Studio 64XTC | OMdi 128, OMdv 128 | ordinary 68K path; reached later in Search |
| Opcode TM component | PPCC 1, PROC 1 | static component fixture only; not a driver control |
| OMS Name Manager component | PPCC 2, PROC 2 | component, not an OMS driver |
| OMS library | PPCC/PROC resources | host-side OMS implementation |

USBMIDI9's OMdi bytes are:

```text
7F 10 00 00 00 00 00 01 00 01 00 00 00 00 00 00
```

Under the local SDK template these decode as id `0x7F10`, smart/menu zero,
obsolete port fields `xxportNumM=0`, `xxportNumB=1`, flags zero, compatibility
one, and reserved zeros. Nothing in this byte sequence is demonstrably
invalid. The documented ordinary-driver contract says driver code is in an
OMdv 128 resource, but the 2.3.8 native PPCC loader is a separate path and
the minimal PPCC diagnostic proved that PPCC can be entered. Therefore “no
OMdv companion” is a **STRONG candidate**, not a proven defect: the corpus
has no native PPC driver fixture with which to establish the required resource
combination.

The source-level ABI audit currently classifies the known sites as follows:

| site | expected type / contract | actual mechanism | status |
|---|---|---|---|
| `main` | `OMSCALLBACK(long main(short,long,long))`, ProcInfo `0x0FB0` when passed through generic MM | PPC special main plus OMS `CallUniversalProc` path | **PROVEN correct** |
| `omdvAddDevices` | `OMSDvrAdd1DevProc1UPP` | fixed `CallOMSDvrAdd1DevProc1` in b88c5a7 | **real bug fixed; not this crash** |
| `NewOMSReadHook2(oms_tx_send)` | OMS read-hook UPP | NewRoutineDescriptor / SDK UPP path | **not reached before failure** |
| received-port callback | SDK callback UPP and its SDK ProcInfo | `CallUniversalProc` wrapper | **not reached before failure** |
| native internal `FFFF` method | OMS-selected method plus the pointer installed at record+$52 | direct 68K callback in OMS | **ordinary OMdv target proven valid; USB native variant unresolved** |

## Root-cause ranking

* **PROVEN:** the new log's ordinary OMdv `+98A2` callback is valid and cleans
  its Pascal frame. The old universal cleanup-failure theory is false.
* **PROVEN:** the native loader copies the CFM result into `record+$66`, aliases
  `record+$52` to that result, then emits an internal `FFFF` probe through the
  nested `[record] -> +0x0C -> +0x0C` method topology.
* **PROVEN:** the current USBMIDI9 production failure occurs before public
  initialization, add-device, USB, or receive callbacks; those cannot explain
  the first fault without new contradictory evidence.
* **STRONG:** USBMIDI9's native PPCC/CFM `record+$52` object or the method
  selected for it differs from the ordinary OMdv target shown in this log.
  The old USB capture of an `AAFE` descriptor with embedded ProcInfo zero is
  relevant to this candidate, but it is not evidence that the ordinary OMdv
  path is malformed.
* **STRONG/PLAUSIBLE:** the absence of an OMdv companion or a native-driver
  metadata combination causes OMS to select a method adapter that is not
  compatible with the CFM main pointer. This is the smallest structural
  difference currently visible, but no authentic native PPC driver resource
  is available to prove the expected artifact.
* **PLAUSIBLE:** production returns zero for internal selector `0x00FF` and
  OMS subsequently interprets that result incorrectly. The new log does not
  contain the USB invocation, and the earlier zero-ProcInfo descriptor means
  the observed PPC register tuple was not marshaled evidence. Do not patch
  this value.
* **WEAK:** the closed special-main, OMSDevice layout, or add-device UPP bugs
  are the present fault. Existing controlled evidence rules these out for the
  first failing call.

## Smallest justified next step

No source fix is justified by this transcript alone. The decisive missing
comparison is the native USB record at the moment after `GetDiskFragment` and
before the `FFFF` method call, contrasted with a genuine installable native
PPC OMS driver—not the TM component fixture. If such a control is unavailable,
the single highest-information capture is one stop at the USB record's
`+DC44` call before executing it, dumping:

```text
TD
DM A2 90
DM A1 40
DM A7 20
S
TD
DM A1 20
```

The required interpretation is `A2=record`, `O0=[A2]`, `O1=[O0+0x0C]`,
`method=[O1+0x0C]`, and `A2+0x52`/`A2+0x66`. This one stop separates “USB is
assigned the ordinary OMdv-cleaning method” from “USB is assigned a native
method whose pointer/descriptor contract differs.” It must be armed before
Search/loader execution, not from the later PPC `E0` stop.

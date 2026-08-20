# PPCC main ABI diagnostic (2026-08-20)

This is the static diagnostic artifact for the USBMIDI9 native-PPC entry
question.  It intentionally does not rewrite a PEF, synthesize a second
descriptor, or change the production handler.  The checker is
`tools/re/ppcc_abi_report.py`; its input artifacts and the exact topology it
reports are preserved below.

## Result

The PPCC main-entry ABI is **not a no-argument bootstrap**.  The strongest
local evidence converges on the ordinary OMS driver procedure:

```c
OMSCALLBACK(long) main(short msg, long par1, long par2);
```

with the generic Mixed Mode call contract:

```text
uppOMSDriverProcInfo = 0x00000FB0
pascal long result
stack parameters: short, long, long
```

This is PROVEN for the public/generic OMS driver-entry path and is directly
implemented by the current USBMIDI9 source.  It is also visible in the
authentic TM PPCC main's PPC code.  The private native loader adapter used by
`PROC1 +0xDC44` is a separate question; the local corpus does not contain a
complete installable `OMdi + PPCC` native OMS driver with which to establish
that adapter's intended record contract.

## PEF artifacts

| role | file | size | SHA-256 | status |
|---|---|---:|---|---|
| USBMIDI9 production control | `USBMIDI9/USBMIDI9_OMS.production-save` | 10018 | `4407a20eb774eec78ee2b5dc0802361d82b254b63a05ee924e49808b375e265e` | preserved byte-for-byte |
| authentic PPCC component control | `pefcheck/fixtures/tm_ppcc1.pef` | 1579 | `4a0978fe6ee557a31a75fa46c5941d0a249433c93f745efc022eef3635c5a295` | Opcode Time Manager PPCC 1 fixture; not an installable OMS driver |
| rejected RD2 experiment | `USBMIDI9/USBMIDI9_OMS_RD2` | 10035 | `2ee694afdabc1c6882aa946a8059ea4cb8e2cf4e25bc6a4f8a263c06be3a2931` | descriptor-main; rejected by runtime evidence |

The packaged USBMIDI9 resource contract remains:

```text
OMdi 128, 16 bytes:
7F 10 00 00 00 00 00 01 00 01 00 00 00 00 00 00

PPCC 1: raw PEF bytes, no synthetic length prefix
PPCC 1[0:12] = 4A 6F 79 21 70 65 66 66 70 77 70 63
```

The source-only call/return diagnostic already in the project is the
smallest ABI-following runtime diagnostic:
`USBMIDI9_OMS_DIAG_MINIMAL_ENTRY` keeps the exact three-argument `main`,
returns `-1` for `omdvInit`, and returns zero for the subsequent dispose or
other messages.  It uses the normal linker setting `Main = main`; it does not
use a data-symbol main or a descriptor-to-descriptor chain.  The successful
G4 raw PEF from that earlier run is not present locally, so no new binary hash
is invented here.

## Current USBMIDI9 main topology

`pefcheck`, the loader inspector, and the new report all agree:

```text
PEF loader mainSection = 1
PEF loader mainOffset  = 0x88
special-main raw bytes = 00 00 16 50 00 00 80 00
```

The words are pre-relocation section offsets:

```text
code = section 0 + 0x1650
TOC  = section 1 + 0x8000

pefcheck synthetic relocation:
entry = 0x10001650
TOC   = 0x1000A390
```

The code-section bytes at section 0 + `0x1650` begin:

```text
7C 08 02 A6 93 E1 FF FC 93 C1 FF F8 93 A1 FF F4
90 01 00 08 94 21 FF B0 3B A3 00 00 3B C4 00 00
```

That is a PPC `mflr`/stack-frame prologue.  The following instructions copy
the incoming `r3`, `r4`, and `r5` into nonvolatile registers before calling the
driver dispatcher.  This is code for `main(short,long,long)`, not a no-arg
bootstrap.

The exported `main` is also class 2, section 1, value `0x88`; it points at the
same transition-vector data object.  There are no `AA FE` bytes anywhere in
the production PEF.  The runtime OMS-record descriptor observed in RD1/RD2 is
therefore not a static object in this PEF; it is the object returned/materialized
at the CFM/OMS boundary.

## Authentic PPCC control

The TM PEF has the same ordinary special-main representation:

```text
mainSection = 1
mainOffset  = 0x3C
raw         = 00 00 01 6C 00 00 00 00
vector      = code section 0 + 0x16C, data section 1 + 0
synthetic   = entry 0x1000016C, TOC 0x10000470
entry bytes = 7C 08 02 A6 93 E1 FF FC 93 C1 FF F8 93 A1 FF F4 ...
```

The main code at section 0 + `0x16C` begins with a PPC function prologue,
executes `extsh. r5,r3`, branches on the message, uses `r4` as another
incoming value, and returns a scalar in `r3`.  Its first routine at section 0
+ `0x000` is a separate fragment-init routine; that is not the special main.
The main is therefore not a no-argument initializer which returns an OMS UPP.

TM does contain an unrelated `AA FE` object in its packed data at data offset
`0x54`, and its init routine copies that object.  It is not the special main:
the TM loader points at data + `0x3C`, and the TM main code is the function at
code + `0x16C`.  The presence of this separate descriptor does not establish
an embedded descriptor ABI for PPCC main.

## RD2 rejection

The descriptor-main build has:

```text
mainSection = 1
mainOffset  = 0x1DC
bytes       = AA FE 07 00 00 00 00 00
```

Its code section and loader imports are otherwise production-shaped, but its
special main points to an AAFE RoutineDescriptor.  The live RD2 transition
proved that OMS/Mixed Mode reads the OMS-record outer descriptor's
`procDescriptor` as a PPC transition vector.  It does not recursively parse a
nested AAFE descriptor:

```text
outer procDescriptor -> inner bytes AA FE 07 00 ...
Mixed Mode loads inner[0] = AA FE 07 00 as the PPC code word
```

That is why the descriptor-main experiment is not a valid ABI fix.  It is not
used by the diagnostic artifact.

The existing `pef_mainrd_gate.py` comparison passes the RD2 bytes against the
preserved production control with:

```text
raw procDescriptor=0x00000088
raw TVector=[code+0x1650, data+0x8000]
production main TVector raw=[code+0x1650, data+0x8000]
code differences: 30 bytes, all expected +0x20 data relocations
GATE: PASS (static descriptor/relocation-shape checks)
```

Thus the RD2 source experiment did not alter the handler algorithm or its
loader-facing imports/exports; it changed the special-main data representation
and the expected data relocation layout.  The rejection is specifically the
runtime descriptor-to-descriptor topology, not an accidental handler change.

## SDK and OMSGlue evidence

The exact SDK sources searched were:

```text
OMS 2.0 SDK 28-Jan-98/Headers/OMSDriver.h
OMS 2.0 SDK 28-Jan-98/Headers/OMSDrvUPPs.h
OMS 2.0 SDK 28-Jan-98/Headers/OMSTypes.h
OMS 2.0 SDK 28-Jan-98/Examples/SampleApp/SampleOMSApp.mcp
OMS 2.0 SDK 28-Jan-98/Libraries/README.OMSGlue
OMS 2.0 SDK 28-Jan-98/Libraries/OMSGluePPC.lib
```

`OMSDriver.h` declares the driver entry exactly as `main(short,long,long)`.
`OMSDrvUPPs.h` defines `NewOMSDriverProc` and `CallOMSDriverProc` with
`uppOMSDriverProcInfo`, and the checked-in `tools/re/procinfo_check.c`
recomputes `0x0FB0` from the Mixed Mode macros.

`OMSGluePPC.lib` is a 58508-byte `MWOBPPC ` CodeWarrior static object
library (SHA-256 `694615e235604fd75cd4e9b3bf8c658f4f3d5d1e5002dc7bfc94083d31e37afa`).
The SDK README identifies it as the full PPC OMS client glue; the companion
`OMSLibPPC.slb` is a shared library for other PPC development environments.
The library's strings and the public glue headers contain `LinkToOMSGlue`,
`OMSGetCallAddress`, `CallUniversalProc`, and Mixed Mode structure names, but
no `OMSDriverProc`, `uppOMSDriverProcInfo`, or `main` export.  The string
`GetBootstrapProc` is present in the object/debug material, but there is no
local symbol-table/source evidence connecting it to the PPCC fragment main or
to a driver-entry descriptor.  The README and Sample OMS PPC project describe
client glue, not a special PPCC driver bootstrap.

Conclusion: `OMSGluePPC.lib` is not evidence for a hidden no-argument PPCC
main convention.  Its exact MWOBPPC object implementation is not decoded
because the available host binutils do not recognize that proprietary object
format; the public headers/spec and the PPC fixtures settle the entry
signature without it.

## OMS loader and the zero-ProcInfo call

The relevant OMS 2.3.8 `PROC` 1 bytes are in
`/tmp/omsdiag/omslib_proc1.bin` (86507 bytes, SHA-256
`3655f74d202c29db3b45eb83a122642b5e989de08c84f4d52e010e449920cc07`).
After `GetDiskFragment` at `+0x0DBDC`, OMS copies eight longwords from the
CFM result block to record + `0x66`, aliases record + `0x52` to record +
`0x66`, and reaches the internal method at `+0xDC44`:

```asm
+0xDBF0  LEA.L  0x66(A2),A1
+0xDBF6  MOVE.L (A0)+,(A1)+       ; repeated eight longwords
+0xDC0A  MOVE.L 0x52(A2),0x7A(A2) ; save old field
+0xDC10  MOVEA.L A2,A0
+0xDC12  LEA.L  0x66(A0),A0
+0xDC16  MOVE.L A0,0x52(A2)

+0xDC30  CLR.L  -(A7)
+0xDC32  MOVE.L A2,-(A7)
+0xDC34  MOVE.W #$FFFF,-(A7)
+0xDC38  MOVE.L (A2),-(A7)
+0xDC3A  MOVEA.L (A7),A0
+0xDC3C  MOVEA.L 0x0C(A0),A1
+0xDC40  MOVEA.L 0x0C(A1),A1
+0xDC44  JSR    (A1)
+0xDC46  MOVE.W D0,D3
+0xDC48  TST.W  D3
+0xDC4A  LEA.L  0x0E(A7),A7
+0xDC52  BNE    +0xDC58
+0xDC54  MOVE.B D4,0x56(A2)
+0xDC58  TST.W  D3
+0xDC5A  BEQ    +0xDC70
```

For the captured USBMIDI9 record, the two `+0x0C` dereferences resolve to
`+0x98A2`.  The `+0xDC44` call is therefore a private OMS adapter call, not
the generic `_CallUniversalProc` site.

The `+0x98A2` wrapper constructs its own 14-byte Pascal frame:

```asm
+0x98A2  CMPI.W #$FFFF,0x08(A7)
+0x98AA  SUBQ.W #4,A7
+0x98AC  MOVE.W #$FFFF,-(A7)
+0x98B0  CLR.L  -(A7)
+0x98B2  CLR.L  -(A7)
+0x98B4  MOVEA.L 0x18(A7),A0
+0x98B8  MOVEA.L 0x52(A0),A0
+0x98BC  JSR    (A0)
+0x98BE  MOVE.L (A7)+,D0
+0x98C0  MOVEQ  #0,D0
+0x98C2  RTS
```

The corrected `0x18` displacement reads the source record at the wrapper
entry's `S+0x0A`.  The live source was the OMS record itself, and record +
`0x52` equaled record + `0x66`, the outer AAFE descriptor.

If the direct target's embedded ProcInfo is zero, Mixed Mode does not marshal
or clean those wrapper arguments.  Control returns to `+0x98BE` with the
frame still present.  The first `MOVE.L` pops the first zero result slot;
`+0x98C2 RTS` consumes the second zero as a return PC.  The subsequent
`PC=00000000` and address-zero `68F1`/`FFFFFFF3` path follow from that state.

If the direct target instead were a conforming Pascal
`long(short,long,long)` callback, the callback would consume the 10 argument
bytes, write the reserved result, and restore the wrapper return PC.  The
wrapper would then return normally and `+0xDC46` would inspect D0, clean the
outer 14-byte loader frame, and store the success/error byte.  This is the
mechanical meaning of the zero-ProcInfo failure; it does not make zero an
invalid descriptor for every possible procedure.

The generic driver dispatcher is a different path at `+0x10C86`:

```asm
+0x10C7C  MOVE.L (A7)+,D0
+0x10C7E  MOVEA.L 0x1A(A7),A0
+0x10C82  MOVEA.L (A0),A0
+0x10C84  MOVEA.L A6,A1
+0x10C86  TRAP   $A9E2             ; _CallUniversalProc
+0x10C88  MOVE.W D0,D3
+0x10C8A  MOVEA.L 0x1A(A7),A0
+0x10C8E  TRAP   $A023
+0x10C90  MOVE.W D3,D0
+0x10C92  LEA    0x9E(A7),A7
+0x10C9A  RTS
```

That site supplies `0x0FB0` externally.  The zero returned by that generic
call is consumed as a status value; the static path does not turn it into a
routine address, handle, ProcInfo, or table pointer.  The earlier live
`00FF,<pointer>,1` tuple is not proof of a marshaled public message under the
zero-ProcInfo direct call.

## What is and is not fixed by this artifact

**PROVEN:**

- PPCC main is a three-argument OMS driver procedure, not a no-argument
  bootstrap.
- The normal CodeWarrior special main is a direct PPC transition vector.
- `uppOMSDriverProcInfo` is `0x0FB0` for the generic OMS entry call.
- RD2's descriptor-main creates an unsupported descriptor-to-descriptor chain.
- OMS's post-`+DC44` status handling is the `D0/D3` block above.
- `OMSGluePPC.lib` supplies client glue; no PPCC-main bootstrap contract is
  present in the available public/materialized evidence.

**STRONG INFERENCE:**

- The captured zero-ProcInfo outer descriptor is CFM/GetDiskFragment result
  metadata, not an OMdi or USBMIDI9 source write; OMS PROC1 merely copies and
  aliases it.

**UNKNOWN:**

- Whether a successful native `OMdi+PPCC` OMS driver supplies a companion
  68K adapter, a different dispatch object, or some other loader artifact so
  that the private `+0xDC44` path never direct-calls the generic CFM result
  with the wrong embedded contract.
- Whether CFM's zero ProcInfo is a normal generic main-address materialization
  for this specific private path.  The public CFM documentation does not
  define an OMS signature for `mainAddr`.

No behavioral source fix is justified by this static evidence.  The report
and compile-time signature guard are committed as the diagnostic artifact;
the existing minimal-entry guard remains available for a future clean Search
test, but no new packaging variant is proposed here.

## Reproduction and validation

From the repository root:

```sh
python3 tools/re/ppcc_abi_report.py \
  USBMIDI9/USBMIDI9_OMS.production-save \
  --control pefcheck/fixtures/tm_ppcc1.pef \
  --rejected USBMIDI9/USBMIDI9_OMS_RD2

cargo run --manifest-path pefcheck/Cargo.toml -- \
  USBMIDI9/USBMIDI9_OMS.production-save

make test
make check-classic
make check-re-tools
```

The report must end with:

```text
DIAGNOSTIC: PASS (candidate is function-main/vector, not descriptor-main)
```

The one clean future G4 test justified by this result is the already-defined
source diagnostic: build with `USBMIDI9_OMS_DIAG_MINIMAL_ENTRY`, keep
PPC Linker → Entry points → Main = `main`, package that exact raw PEF as
PPCC 1, and press Search.  Success means the call/return diagnostic reaches
the documented nonzero-init/dispose path without a crash; failure would be
evidence about the private OMS adapter, not about a no-argument PPCC ABI.

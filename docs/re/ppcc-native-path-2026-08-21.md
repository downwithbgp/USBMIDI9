# USBMIDI9 PPCC/native path: zero-ProcInfo descriptor

This note narrows the investigation to the native PPCC path. The fresh
USBMIDI0 transcript is only a control: its ordinary OMdv target cleans the
Pascal frame correctly.

## Captured USBMIDI9 descriptor

The first native capture stopped immediately before `PROC1+0x98BC JSR (A0)`:

```text
A0 = 017D7A26
017D7A26 AAFE 0700 0000 0000 0000 0000 0000 0000
017D7A36 0001 0004 01AE DF58 0000 0000 0000 0000
```

The later same-object capture was:

```text
A2 = 01809EE0
[A7+0x18] = 01809EE0
[A2+0x52] = 01809F46
[A2+0x66] = 01809F46

01809F46 AAFE 0700 0000 0000 0000 0000 0000 0000
01809F56 0001 0004 018E 9B78 0000 0000 0000 0000
```

The second capture proves that the source loaded by corrected `+98B4` was
the OMS driver record itself, and that `record+0x52` was the CFM result area.

| descriptor offset | width | raw value | meaning |
|---:|---:|---|---|
| `0x00` | 2 | `AAFE` | `_MixedModeMagic` |
| `0x02` | 1 | `07` | `kRoutineDescriptorVersion` |
| `0x03` | 1 | `00` | non-dispatched descriptor flags |
| `0x04` | 4 | `00000000` | reserved |
| `0x08` | 1 | `00` | reserved |
| `0x09` | 1 | `00` | selector info |
| `0x0A` | 2 | `0000` | last record index; one record follows |
| `0x0C` | 4 | `00000000` | embedded `ProcInfoType` |
| `0x10` | 1 | `00` | record reserved |
| `0x11` | 1 | `01` | `kPowerPCISA` |
| `0x12` | 2 | `0004` | `kUseNativeISA` |
| `0x14` | 4 | `018E9B78` | PPC transition-vector pointer |
| `0x18` | 4 | `00000000` | record reserved |
| `0x1C` | 4 | `00000000` | non-dispatched selector |

The target dump was:

```text
018E9B78 018E8C80 018F1AF0 018E8304 018F1AF0
```

The first pair is the selected code/TOC vector; the second is adjacent data.
With PPCC runtime anchor `018E73B0`, the selected code is `PPCC+0x18D0`.
The earlier `0x1650` wording was a coordinate mix-up: `0x1650` is the
code-section-relative location when the code section starts at container
offset `0x280`; `0x18D0` is the container/runtime coordinate.

## Exact provenance established statically

The native loader is:

```text
OMdi +0x06 = 1
  -> Get1Resource('PPCC', 1)
  -> GetDiskFragment(..., kLoadNewCopy, ...)
  -> copy eight longwords from the CFM result area to record+0x66
  -> record+0x52 = record+0x66
  -> internal FFFF dispatch
  -> +98A2: target = [record+0x52]; JSR (target)
```

The relevant bytes are:

```text
+0xDBEC  41EF 0106       LEA  $0106(A7),A0
+0xDBF0  43EA 0066       LEA  $0066(A2),A1
+0xDBF6  22D8            MOVE.L (A0)+,(A1)+
+0xDBF8  51C8 FFFC       DBRA
+0xDC10  204A            MOVEA.L A2,A0
+0xDC12  41E8 0066       LEA  $0066(A0),A0
+0xDC16  2548 0052       MOVE.L A0,$0052(A2)
```

PROC1 contains no `NewRoutineDescriptor` call and no store of zero to the
copied RoutineRecord. USBMIDI9's OMdi is only:

```text
7F 10 00 00 00 00 00 01 00 01 00 00 00 00 00 00
```

The PPCC PEF also contains no AAFE descriptor or `ProcInfo` field; its loader
metadata contains the special main/vector information. Therefore:

| possible zero producer | classification |
|---|---|
| OMS constant or PROC1 store | **DISPROVEN** |
| OMdi/resource metadata | **DISPROVEN** |
| USBMIDI9 source initialization | **DISPROVEN for this field** |
| preexisting driver-record field | **DISPROVEN on this branch**; `+52` is overwritten |
| CFM/GetDiskFragment result materialization | **STRONG** |
| exact CFM/ROM instruction writing zero | **UNKNOWN**; not present in artifacts |

The most precise conclusion is: zero is proven to arrive in the CFM result
copied by OMS, and strongly attributed to the fragment-main/CFM machinery.
It is not proven that CFM's internal default is universally zero. Apple's CFM
API describes `mainAddr` as a fragment-specific main address and does not
attach the OMS driver signature to it; Mixed Mode requires ProcInfo when a
RoutineDescriptor is created. The available public contract therefore does
not prove that CFM should have supplied `0x0FB0`.

## Why this differs from `uppOMSDriverProcInfo`

The generic OMS entry call at `PROC1+0x10C86` explicitly supplies:

```text
pascal long main(short msg, long par1, long par2)
ProcInfo = 0x0FB0
```

The native `+98A2` branch instead directly `JSR`s the descriptor. Mixed Mode
then consults the embedded zero. Its frame is:

```text
reserved result long
short FFFF
long 0
long 0
```

That frame independently requires the same Pascal long-returning,
short/long/long contract represented by `0x0FB0`. The descriptor's zero is
valid for a zero-argument/zero-result routine, but incompatible with this
adapter. This is a call-site/descriptor contract mismatch, not proof that
the descriptor itself is malformed.

## PPCC topology and `0x00FF`

The proven native topology is:

```text
OMdi/PPCC -> GetDiskFragment -> record+0x66
          -> record+0x52 alias
          -> O0=[record]
          -> O1=[O0+0x0C]
          -> method=[O1+0x0C]
          -> +98A2
          -> [record+0x52] direct JSR
          -> PPC main through embedded descriptor
```

The only `_CallUniversalProc` trap in the extracted PROC1 is `+0x10C86`.
Its native branch receives a caller-created message block through `A3`:

```text
[A3+0] -> short msg
[A3+2] -> long par1
[A3+6] -> long par2 / pointer-shaped value
```

No literal `0x00FF` occurs at that call site or in the PROC1 literal search.
The producer of 255 is therefore an unresolved variable-message producer
upstream of the dispatcher. The PPC boundary values `00FF,<pointer>,1` are
proven observations, but their source is not.

The returned zero has a proven consumer: the generic dispatcher copies the
CallUniversalProc result into `D3`, tests it, and takes the zero/nonzero status
branch. It does not turn that zero into a routine address, handle, pointer, or
ProcInfo. The zero-ProcInfo direct call separately leaves PPC argument
registers unmarshal-ed; whether the observed tuple is inherited from an
earlier generic call or simply residual state is **UNKNOWN**.

## Minimal versus production artifact boundary

The preserved minimal PEF is the old invalid-special-main artifact: 8-byte
code, no imports, `mainSection=-1`. The successful `b46c7251…` diagnostic PEF
was not copied back. The current production/trace PEF is 9,100-byte code,
476-byte unpacked data, 13 imports, and `mainSection=1, mainOffset=0x88`.
Thus a literal descriptor-byte comparison against the successful minimal
build cannot be performed from the repository. Existing evidence only proves
that the successful diagnostic used a linker-generated special main and
returned without crashing.

## Classification

```text
PPCC resource selected                         PROVEN
CFM result copied to record+0x66               PROVEN
record+0x52 aliases that result                 PROVEN
captured result is USBMIDI9's PPC RD            PROVEN
embedded ProcInfo is zero                       PROVEN
zero written by OMS/source/OMdi                 DISPROVEN
zero supplied by CFM materialization            STRONG
+98A2 expects 0x0FB0-compatible frame           PROVEN
direct zero-ProcInfo call causes malformed SP   PROVEN
0x00FF producer                                UNKNOWN
returned 0x00FF value becomes structural data  DISPROVEN statically
```

No source fix is justified yet. The evidence points to the OMS native
adapter/CFM contract boundary, not a USBMIDI9 message handler. The one
decisive A/B, if needed, is to compare the generated record `+0x52/+0x66`
descriptor bytes for the missing successful minimal PEF against production,
with identical outer resources and valid special main. No guessed `0x00FF`
return value should be used as a workaround.


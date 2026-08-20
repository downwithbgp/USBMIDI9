# PPCC special-main RoutineDescriptor experiment

This note and `oms/oms_main_rd_experiment.c` prepare an isolated build. They
do not change the production target or handler.

## SDK and PEF conclusions

The preserved Universal Interfaces `MixedMode.h` defines each `RoutineRecord`
as `procInfo`, reserved byte, `ISA`, `routineFlags`, `procDescriptor`, reserved
long, and selector. The same header defines the static
`BUILD_ROUTINE_DESCRIPTOR(procInfo, procedure)` macro. The macro emits a
non-dispatched descriptor with the supplied ProcInfo, current architecture,
and a native/prepared/absolute routine pointer.

`OMSDrvUPPs.h` expands `uppOMSDriverProcInfo` to the Pascal
`long(short,long,long)` contract, `0x0FB0` on the OMS 68K/PPC build.

Therefore the intended experiment object is:

```text
+0x00 AA FE          _MixedModeMagic
+0x02 07             kRoutineDescriptorVersion
+0x03 00             kSelectorsAreNotIndexable
+0x04 00000000       reserved1
+0x08 00             reserved2
+0x09 00             selectorInfo
+0x0A 0000           routineCount field emitted by the macro
+0x0C 00000FB0       uppOMSDriverProcInfo
+0x10 00             record reserved1
+0x11 01             PPC ISA
+0x12 0004           absolute | prepared | native
+0x14 <main TVector> PPC code/TOC pointer
+0x18 00000000       record reserved2
+0x1C 00000000       selector
```

Apple's CFM API documents `GetDiskFragment`'s `mainAddr` as the address of
the fragment's main symbol. It does not promise to infer an OMS signature or
to manufacture `uppOMSDriverProcInfo`. The PEF special-main field is likewise
an address, not a typed C function declaration. The existing valid
Main=main PEF already demonstrates that this address can be a data transition
vector rather than PPC instructions.

This proves static RoutineDescriptors are SDK-supported and that CFM need not
add OMS ProcInfo. It does not yet prove that this CodeWarrior linker accepts a
source data symbol in its Main setting or emits the expected relocation. That
is deliberately left as a build acceptance check.

## Current versus experiment

```text
CURRENT:    Main=main -> linker-generated PPC transition vector
            CFM result -> record+0x66/+0x52, captured ProcInfo=0

EXPERIMENT: Main=USBMIDI9OMSMainRD -> static AAFE descriptor
            descriptor ProcInfo=0x0FB0
            routine record -> the handler's PPC transition vector
```

The experiment changes only the special-main symbol representation. It adds
no `0x00FF` case and no manual stack cleanup.

## Isolated CodeWarrior target

Duplicate the production PPC PEF target. Add only
`oms/oms_main_rd_experiment.c` to the duplicate. Keep the same source files,
imports, libraries, export list, code model, and optimization settings. Set
only:

```text
PPC Linker -> Entry points -> Main = USBMIDI9OMSMainRD
```

Keep production `Main = main` unchanged. Build the duplicate to a separate
PEF, package it as the same PPCC resource ID in a separate driver file, and do
not put both files in the OMS Folder at once.

## Static acceptance before G4

The experiment PEF must satisfy all of these:

1. `pef_loaderinfo.py` reports a non-negative special-main section and an
   offset in unpacked data, not the handler's code address.
2. After unpack/relocation replay, the main address begins `AA FE 07 00` and
   has `00 00 0F B0` at descriptor offset `0x0C`.
3. The record has ISA `01`, flags `0004`, and `procDescriptor` resolves to the
   same PPC code/TOC transition vector used by production `main`.
4. Reserved and selector fields are zero and the descriptor is non-dispatched.
5. The production PEF remains byte-for-byte unchanged.

The pre-relocation pointer may be a relocation placeholder; validate the
relocation result, not just the raw placeholder.

## Runtime A/B after acceptance

With irrelevant OMS drivers removed and one isolated driver installed, stop
at the known 68K site immediately before `JSR (A0)` and collect:

```text
TD
DM A0 20
```

Current should show the captured `AAFE ... 00000000` ProcInfo. Experiment
should show `00000FB0` at `A0+0x0C`. Then run the same Search operation.

```text
descriptor becomes 0x0FB0 and Search progresses -> causal fix, provisionally
descriptor changes but failure remains                 -> another mismatch
descriptor remains zero                                -> Main data symbol was
                                                         not used as intended
```

## Ordering correction

The failing wrapper's return PC was `PROC1+0xDC46`, immediately after the
loader's `+0xDC44 JSR (A1)` into `+0x98A2`. Its proven path is:

```text
GetDiskFragment -> record+0x66/+0x52 -> +DC44 -> +98A2
    -> direct JSR to CFM result -> PPC main -> final blr
    -> +98BE -> malformed return
```

The separate generic `_CallUniversalProc` trap at `+0x10C86` returns at
`+0x10C88`; it is not the return site in this failing capture. Thus
`generic 0x00FF -> returned zero -> +98A2` is not proven and is contradicted
by the captured return-PC topology. The observed PPC tuple
`00FF,<record>,1` remains unmarshaled/residual state unless a trace proves a
preceding generic invocation.

## First G4 build result (2026-08-20)

The existing PEF target was changed to `Main=USBMIDI9OMSMainRD` and rebuilt.
The output was snapshotted before further builds:

```text
USBMIDI9/USBMIDI9_OMS_RD
size   10018
sha256 db706a708d3450289b93b7e923f276606a536d6dd596c7df092a55c3264e763e
```

The production control was located at
`USBMIDI9/USBMIDI9_OMS.production-save`:

```text
size   10018
sha256 4407a20eb774eec78ee2b5dc0802361d82b254b63a05ee924e49808b375e265e
```

`cmp` found only the PEF timestamp bytes at offsets `0x11..0x13` changed.
The candidate loader still reports `mainSection=1, mainOffset=0x88`, and its
special-main bytes are exactly the old transition vector:

```text
00 00 16 50 00 00 80 00
```

The descriptor gate therefore failed before any packaging or runtime test:

```text
GATE: FAIL
- magic AAFE: got 0000
- ProcInfo 0x0FB0: got 00008000
- procDescriptor is not an in-section data offset
```

This build did not establish `USBMIDI9OMSMainRD` as the PEF special main. It
is consistent with the symbol being absent from the target, the linker Main
setting not being applied to the generated PEF, or CodeWarrior resolving the
setting to the existing `main` symbol. It is not evidence that a descriptor
main is invalid. Do not package or run this artifact.

## Classification

```text
Static RoutineDescriptor construction is SDK-supported       PROVEN
PEF Main can identify a data address                         PROVEN
CFM infers 0x0FB0 from a function main                       DISPROVEN
CodeWarrior accepts this data symbol as Main                 FAILED in first build
RD pointer resolves to handler TVector                        TO VERIFY
Current direct path uses embedded ProcInfo=0                  PROVEN
Changing Main to a static 0x0FB0 RD fixes the crash             HYPOTHESIS
```

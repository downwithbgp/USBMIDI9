# FreeMIDI 1.45 ABI evidence ledger

This is the offset/raw-byte ledger behind [the ABI report](freemidi-driver-abi.md).
It records the static observations completed against the external research
corpus through 2026-08-22. No proprietary binary or resource fork is copied
into the repository; short byte excerpts appear inline as evidence only.

## Artifact identity

| artifact | source SHA-256 | derived blob SHA-256 | use |
|---|---|---|---|
| FreeMIDI System Extension resource fork | `e04e8ae9fd9eaba60b41251f4a04e30112d8b9e2216fd8e08870b0683ac0169e` | `CODE1` `bbefde2c6f46e7c00969c7e522c7c7cae1111ed544194bd3d02e362a55effcc8` | outer caller and internal wrapper |
| FreeMIDI PowerPlug data fork | `2159e638b539e3bf57692418d31931134bbf5c88a8822ab5eb2b7a9d567fbe60` | PPC section 0 `c5fe2b72c7eb92fc1c1a9a1b5fb10eabccefc26f85c56d9f9923be6792da996b`; section 1 `aa251f060dece0621367538e74472d670fa7f7c484af66c2116cbe4737ab170e` | client/API layer only |
| MOTU USB driver resource fork | `09bec550f0e5a498aca4189d7345edfddbcd99302eb9796770e7fd7591566794` | DDef `b6f4664933c861fc531e3c3e2584fd960f6fd7fe9bca36f843ba78339efaa1ae`; Code `e15194750f793ad671e33e9f96ca19b3bf33544621de142e837d528a429fbdea` | USB DDef wrapper/PEF |
| Roland SC-8850 driver resource fork | `bb101bda0945fd2aaec0f3fa16ea7e44b989be8565f0b42b88270ff7df794248` | DDef `0d9b22708a15dd66b39d78c4071e1c7a81a38a6d5767380a570b0f4b583e2b7a`; IDvr `1f7226bdb12c804c6cc9fe56e865e7d84d6a55b05a2361c5490bfb02a5e6d19`; XCOF `8845de1b10e497db2a66e5ce5e1711a6b106376dee874fff9ad060fb407860e1` | DDef/IDvr corroboration |
| Standard Interface driver resource fork | `4deb07db9bdcdaa6cca7ca9325048ce5e366e4d0bbeb801c163a7ab7cb510286` | IDvr `81afcfb9deb1262dbb04e779d0aa029858078d0278056b69b0a8627fd14e03f1` | IDvr corroboration |
| SCC driver resource fork | `551213453b6a28ee72746663ae27fb5bd47d7f98cc155088babb2887fbec0238` | DDef `eb10322c169d2dfe9db316356bd2b18969d9b3be641eff0a5673af5988eb67a2` | selector-table corroboration |
| InterApplication driver resource fork | `ef98cef1ce2c2a4020aeb75c76326bcaa75fe70e21745237c6f5cde5bcfb641b` | DDef `5b5a65e1903128626c1dbbcb98cc6794752bc8dd88cff57ab1e630f870087932` | selector-table corroboration |

The external source paths and provenance rules are in
`/home/vadim/research/oms/PROVENANCE.md`.

## Outer DDef call contract

### FreeMIDI System Extension caller

The relevant CODE1 bytes are at resource-relative `0x1d0dc`:

```text
1d0dc  a029 42a7 3f3c 0001 42a7 206a 0118 2050
1d0ec  4e90 3d40 fea6 3d6e fea6 fea4 3e2e fea4
```

Decoded:

```text
1d0dc  dc.w $a029
1d0de  clr.l  -(a7)       ; par2 = 0
1d0e0  move.w #1,-(a7)   ; selector = 1
1d0e4  clr.l  -(a7)       ; par1 = 0
1d0e6  movea.l $118(a2),a0
1d0ea  movea.l (a0),a0
1d0ec  jsr    (a0)
1d0ee  move.w d0,-$15a(a6)
```

The same caller invokes selector 7 at `0x1d0fc` and selector 9 at `0x1d11c`:

```text
1d0fc  486e feec 3f3c 0007 42a7 206a 0118 2050
1d10c  4e90 2d4a fec4 41f9 0000 c1a2 2d48 fec8
1d11c  486e fec4 3f3c 0009 42a7 206a 0118 2050
1d12c  4e90 3d40 fea2 3d6e fea2 fea0
```

The selector-7 local is `-$114(a6)`. On success, the caller copies it at
`0x1d160`:

```text
1d160  lea.l  -$114(a6),a0
1d164  movea.l a2,a1
1d166  addq.l #8,a1
1d168  addq.l #4,a1
1d16a  moveq  #$42,d0
1d16c  move.l (a0)+,(a1)+
1d16e  dbra   d0,$1d16c
```

`d0=$42` means 67 iterations, or 268 bytes, copied to `a2+0xc`. This is the
corrected result. The 70-byte copy that caused the earlier confusion is a
different constructor at `0x1cfac`:

```text
1cfe2  lea.l  $122(a0),a0
1cfe6  movea.l -$148(a6),a1
1cfec  moveq  #$10,d0
1cfec  move.l (a1)+,(a0)+
1cfee  dbra   d0,$1cfec
1cff2  move.w (a1)+,(a0)+
```

That is 17 longwords plus one word from a separate descriptor pointer. It is
not evidence that the selector-7 temporary is 70 bytes.

The selector-9 block is constructed immediately after selector 7:

```text
1d10e  move.l a2,-$13c(a6)       ; context word 0
1d112  lea.l  $c1a2.l,a0
1d118  move.l a0,-$138(a6)       ; function-pointer-sized word 4
1d11c  pea.l  -$13c(a6)
1d120  move.w #9,-(a7)
1d124  clr.l  -(a7)
1d126  ...
1d12c  jsr    (a0)
1d12e  move.w d0,-$15e(a6)
```

The literal is exact (`41 f9 00 00 c1 a2`). Under raw CODE1 disassembly,
offset `0xc1a2` is `ori.b #$46,(a0)`, the middle of the coherent function that
starts at `0xc190`. This may be a loader/code-resource address-bias issue, but
static extraction alone cannot turn it into a valid callback declaration.

For completeness, the coherent routine at raw `0xc190` takes three long
arguments at `a6+8`, `a6+0xc`, and `a6+0x10`. It treats the first as a record
base, the second as an end pointer, and advances by `0x1e` bytes. For each
non-null record it pushes the third argument and the record pointer, calls
`CODE1+0x34614`, adopts the returned `a0`, and cleans 8 bytes. A secondary
loop releases records with `CODE1+0xa418`, then calls `0x5c5f0` and
`0x5c646`. Raw start bytes are:

```text
c190  4e56 ffe8 48e7 1e38 2c2e 0008 2a2e 000c
c1a0  282e 0010 2846 2d4f fffc 601a 260c 2643
c1b0  200b 670e 2f04 2f0b 4eb9 0003 4614 2648
c1c0  504f 701e d9c0 b9c5 66e2 6038
```

This routine is therefore fully disassembled as a likely record-management
helper, but its relationship to the selector-9 registration remains
unproven because the literal target address needs a loaded-code observation.

Teardown is at `0x1d328`:

```text
1d328  0118 584f 671a 42a7 3f3c 0008 42a7 206a
1d338  0118 2050 4e90 206a 0118 a023 4fef 000a
```

It pushes `(par2=0, selector=8, par1=0)`, calls the entry, then disposes the
resource pointer. The three outer calls use caller cleanup; the callee returns
with `rts`, not `rtd`.

### MOTU DDef wrapper

MOTU DDef body `0x5c` begins with `movem.l d3-d5,-(a7)` and reads:

```text
5c  48e7 1c00
60  382f 0014       ; selector word at saved-frame offset 0x14
64  4eba 0010       ; helper at 0x76
68  2a00             ; setup result in d5
6c  7000
70  0c44 0001       ; selector == 1
```

For the native call at `0xd0`, the wrapper pushes the original `$16(a7)` long
(`par2`), the selector word, and the original `$16(a7)` long after stack
movement (the original `par1`), calls the native entry, and executes
`lea.l $a(a7),a7`. Selector 8 invokes native teardown, releases the held Code
resource, disposes its pointer, and clears the state. The wrapper returns the
word in `d3`.

Thus the observed frame after a normal `link a6,#0` is:

```text
a6+8  par1       long
a6+c  selector   word
a6+e  par2       long
```

This is a stack observation, not a declaration of the original source
language or Mixed Mode `ProcInfo`.

### Native transition: MOTU Code 128

The MOTU Code resource embeds a Mixed Mode RoutineDescriptor before its PEF;
it is not a bare PEF. The resource-relative bytes are:

```text
+0x00  aa fe 07 00 00 00 00 00 00 00 00 00 00 00 0e e5
+0x10  00 01 00 07 00 00 00 20 00 00 00 00 00 00 00 00
+0x20  4a 6f 79 21 70 65 66 66 70 77 70 63 ...
```

The descriptor has ProcInfo `0x00000EE5`, PowerPC ISA `1`, routine flags
`0x0007`, and a proc-descriptor/container pointer of `0x20`. The full Code
resource hashes to
`e15194750f793ad671e33e9f96ca19b3bf33544621de142e837d528a429fbdea`; the
PEF beginning at `+0x20` hashes to
`107e76bb6c109fb8829b9b971ffa002375c459f045996072d9383edcd0a7eaeb`.

The checked-in Mixed Mode macros independently decode `0xEE5` as
`kThinkCStackBased` (`5`), a 4-byte result, and stack parameters of 4, 2,
and 4 bytes. That matches the direct DDef frame. The PPC register mapping
`r3/r4/r5` and return in `r3` is a strong normal-PPC-ABI inference, not a
source declaration.

At `0x98–0xc6`, the wrapper obtains `Code 128`, holds/locks the handle, passes
its first long through raw Toolbox boundary `dc.w $a05c`, and stores that long
in wrapper state `a4+0`. At `0xd8–0xde` it executes
`movea.l (a4),a0; jsr (a0)` after pushing the original three packed
arguments. The pointer producer is therefore the resource-supplied AAFE
descriptor; the wrapper does not construct a RoutineDescriptor. The PEF
loader independently reports three sections, a special main at section 1
`+0x2c0`, one relocation section, 28 imports including `CallUniversalProc`,
and zero exports. Those are PEF startup/import facts and do not replace the
proven outer call boundary.

Selector 8 at `0xec–0x10e` invokes the same descriptor with selector 8, then
passes the Code handle through the corresponding raw Toolbox disposal boundary
and clears both wrapper state words. This native bridge is not required by a
pure 68K DDef.

### Native transition: Roland XCOF

Roland uses a different bridge. Its DDef setup at `0x118` reads `par1` from
`a6+8`, `par2` from `a6+0xc`, requests `XCOF` ID `0x2710` with `_Get1Resource`
(`A81F`), and calls opaque CFM boundary `A9AF`. It then resolves `mixd`,
`cfrg`, and `sysa` with `_FindSymbol` (`A1AD`), rejecting missing symbols via
the observed status paths `1` and `2`; successful setup locks/holds the
resource and continues through `0x1b–0x2f0`.

The XCOF has no code section: section 0 is empty, section 1 is packed data
(unpacked 320 bytes), and section 2 is loader data. It imports ten
NameRegistryLib symbols and exports ten corresponding `...RD` UPP descriptors.
The unpacked section-1 hash is
`83cb647c00282391329ba0dfc8036331bc3fd84b8b8d5fbae87e33a6bb747ea2`; its
first descriptor is `AAFE 0700 ... 000000F1`, repeated at `+0x20` and other
record-aligned offsets. These are Name Registry callback UPPs, not evidence
that Roland shares MOTU's outer descriptor or ProcInfo.

After fragment setup, Roland makes repeated `dc.w $aa5a` calls with selector
words and output pointers at `a4+0x2a72..0x2a96`; the raw frames and status
tests occupy `0x1e6–0x2f2`. The callee's internal record is not present in the
authenticated 68K/XCOF inputs, so this is the exact static boundary rather
than a guessed shared bridge.

## DDef selector dispatch matrix

All offsets below are relative to the extracted resource body. `default` means
the dispatcher returns its saved/default word; it does not mean the selector
is part of the public ABI.

| resource | dispatcher/table | selector map |
|---|---|---|
| MOTU `DDef 128` | `0x5c`, compare 1 then special 8 | 1 loads/holds `Code 128` and calls native; 8 calls native teardown and disposes Code; other selectors fall through |
| Roland `DDef 0` | `0x794` | 1 setup; 2 `0x760`; 3 `0x50c`; 4 `0x580`; 5 `0x630`; 6 `0x740`; 7 descriptor path; 8 teardown; 0/9 default |
| SCC `DDef 128` | `0xec0`, table `0xeee`, compare `0xd` | 0/9/10/11/12 default; 1 `0xf0a`; 2 `0xf72`; 3 `0xf98`; 4 `0xfa6`; 5 `0xf80`; 6 `0xf8c`; 7 `0xf34`; 8 `0xf22`; 13 `0xfb2` |
| InterApplication `DDef 128` | `0x11e`, table `0x144`, compare `8` | 0/2/6/8 default; 1 `0x156`; 3 `0x1b8`; 4 `0x1c4`; 5 `0x1ac`; 7 `0x170` |

### Selector-7 descriptor corroboration

Roland DDef selector 7 writes an `RdSU` creator descriptor, loads `ICON 129`,
and writes `0x100` at `par2+4`; SCC writes `SCCd`, loads `ICON 128`, and the
same `0x100`; InterApplication writes `APPd`, loads `ICON 128`, and the same
`0x100`. These are independent executable implementations of a descriptor
role. The repeated fields do not authenticate the complete selector-7 record.

## IDvr dispatch and nested vtable evidence

Roland IDvr body `0x30a` reads `a6+8` as context, `a6+0xc` as selector, and
`a6+0xe` as long parameter; its table at `0x33a` accepts 0–12:

```text
0 default       1 0x166 CableHookProc
2 0x17e QueryPort               3 0x1da InstallPort
4 0x212 DisposePort             5–8 default
9 0x22c ComparePorts            10 0x272 CableProc
11 inline context+0xc byte=4   12 inline return=1
```

`QueryPort` clears `context+0x3c` and obtains FMS address/callback fields at
`context+0x10` and `context+0x40`. `ComparePorts` writes function pointer
`0x250` through its second pointer parameter. `CableProc` initializes the
record at its context pointer: word 2 at +0, zero longs at +2/+6, `0xffff` at
+a/+c/+18, words 6 at +e/+10, word `0x25e` at +1c, then copies Pascal/string
data into +0x22 and +0x122. These are callee field writes, not a public C
struct.

The System Extension wrapper at `CODE1+0x38e70` is separate. Its raw call
sequence is:

```text
38e70  4e56 0000 2f0a 246e 0008 2f0a 4eb9 0003 8ea4
38e80  2f2e 0010 3f2e 000e 3f2e 000c 2f2a 000e
38e90  206a 0004 2050 4e90 4fef 0010 245f 4e5e 4e75
```

It pushes a long from `+10`, words from `+e` and `+c`, and object field `+e`,
calls the first object entry, cleans 16 bytes, restores `a2`, and returns.
The words must remain words in any future trace; this does not prove another
DDef selector signature.

## Receive, output, and timing evidence

Roland DDef contains executable receive routines at `0x44c` and `0x510`
(`RECEIVEFROMDRIVER` is the preceding string boundary), plus a `TransmitData`
string. The receive path is traced in the dedicated section below to the
per-port callback indirect call. The output method names
`DriverGetBuffer`/`SendNextCableByte` are present in the authenticated System
Extension resource fork's C++ symbol/string data, but no direct call from
those methods to a hardware DDef selector is present in the extracted CODE1;
the output trace therefore stops at the PowerPlug service-table dispatch and
does not promote names to a buffer ABI.

The PowerPlug client layer provides the cleanest timestamp observation. Its
export descriptor at section-1 offset `0x544` points to code `0x1d5c`; that
wrapper calls `0x3800`, which calls `0x386c`:

```text
386c  lwz r6,8(r3); lwz r0,4(r3); compare queue indices
3898  lwz r6,0x14(r3); add r3,r6,r0
38a4  lwz r0,0(r3); stw r0,0(r4)
38ac  lfd f0,4(r3); stfd f0,0(r5)
38b4  addi r3,r3,0xc; blr
38bc  li r3,0; blr
```

`FMSSendMidiTimeStamped` points to code `0x1c44`, which stores PPC `f1` at
`0x68(r1)` and passes a pointer to that 8-byte slot through the internal
service thunk. `FMSGetCurrentTime` points to `0x2d5c`; `FMSTimeGetPollFunction`
points to `0x3550`. These prove a client timestamp API and an 8-byte queue
timestamp representation, not the DDef hardware record.

## System Extension PPC Code 200/201

The two PPC resources are support code inside the System Extension, not
hardware-driver entry points:

| resource | PEF SHA-256 | imports/exports | relevant static evidence |
|---|---|---|---|
| Code 200 | `a50bdbc7f3df6d042d9fb7ed0d9d05e4bb678edbead96a7ecaf0aba2e67025c5` | imports `NewRoutineDescriptor`, `Gestalt`, `CallUniversalProc`; two loader exports with adjacent-name rendering | code `0x1a4` builds/uses a time-service object and at `0x200` stores PPC `f1` through a pointer; `0x340` reads time-base state and computes a double; `0x380` writes a 64-bit time-base pair to a pointer; `0x3a4` converts that pair to a double |
| Code 201 | `094a0c0a7953023ea5c1127c12fa48143f3df34040343e54de9c909776efc968` | imports `LMGetTicks`, `NewRoutineDescriptor`, `CallUniversalProc`; no verified exports | code `0x15c` parses MIDI/status bytes into a small state record and writes state bytes at offsets 0/1/2/4/5; this is internal MIDI parsing, not a DDef record declaration |

Code 200's use of PPC floating values and the time-base pair corroborates the
PowerPlug/client observations, while Code 201's `LMGetTicks` import shows that
the System Extension has more than one internal clock/parser path. Neither
resource proves what timestamp or callback record a native DDef driver must
use.

## Adversarial conclusions

* The selector-7 temporary is 268 bytes at the observed caller. The 70-byte
  copy is real but belongs to another constructor path.
* The literal selector-9 callback target is not a valid raw CODE1 entry when
  disassembled at the extracted offset. It requires a loader/resource address
  observation; no callback declaration is promoted.
* PowerPlug `FMS*` exports are application glue. Their queue/timestamp ABI is
  not a native DDef receive ABI.
* MOTU's literal `TheMOTUShimInterface` is not a verified PEF export. The PEF
  loader has no authenticated export by that name in the analyzed Code
  resource.
* Selector numbers are not globally universal: SCC accepts 13, IAC accepts 8,
  Roland IDvr accepts 12, and MOTU's DDef wrapper visibly handles only 1 and 8.
* No static evidence authenticates `ProcInfo`, status enums, callback context,
  dynamic port removal, or receive/output legal execution level.

## Discovery and complete `object+0x118` coverage

The first constructor that creates the System Extension's driver object is at
CODE1 `0x1cfac` (CODE1 SHA-256
`bbefde2c6f46e7c00969c7e522c7c7cae1111ed544194bd3d02e362a55effcc8`). It
initializes the object through `0x1d6c4`, clears `+0x118`, `+0x11c`,
`+0x120`, and `+0x121`, and performs a separate 70-byte descriptor copy to
`+0x122`. The resource path at `0x1d09c` then executes this exact sequence:

```text
push long 'DDef'; push word 1; _Get1Resource
store returned handle at object+0x118
if non-null: _HLock(handle); dereference handle; _HLock/_HNoPurge state
call selector 1, selector 7, selector 9 through the first long in the handle
```

The trap identities are recorded only where the instruction is unambiguous;
the resource-manager call at `0x1d09c` is the producer of the ID-1 handle,
not proof that every driver file must use ID 1. The authenticated files
contradict a universal ID: MOTU uses `DDef 128`, Roland `DDef 0`, and SCC/IAC
use `DDef 128`.

The other static producers are:

| CODE1 offset | form | consequence |
|---:|---|---|
| `0x1da46` | `move.l 0xc(a6),0x118(a2)` | constructor receives an already selected DDef pointer/entry and stores it directly |
| `0x6a7fc` | `move.l 0xc(a6),0x118(a2)` | separate service/provider object family; later calls use the stored value directly, without the resource-handle dereference used by the main DDef path |
| `0x1cfc6` | clear `0x118(a2)` | constructor initialization, not a usable producer |

The generic raw-signature scan in `tools/re/scan_freemidi_calls.py` was run
against the extracted CODE1. It reproduced the CODE1 SHA above and found 21
`+0x118`/direct-pointer references plus six direct calls to the IDvr wrapper
at `0x38e70`. The relevant DDef call sites and exact stack arguments are:

| CODE1 call | selector | `par1` | `par2` | cleanup/return |
|---:|---:|---|---|---|
| `0x1b766` | 12 | `a3` | result of `(*a3+0x34)` | caller removes 10 bytes; low `d0` retained |
| `0x1bbd0` | 5 | `a3` | result of `(*a3+0x34)` | same |
| `0x1bc0e` | 6 | `a3` | zero | same |
| `0x1d0ec` | 1 | zero | zero | same |
| `0x1d10c` | 7 | zero | `&local[-0x114]` | same |
| `0x1d12c` | 9 | zero | `&local[-0x13c]` | same |
| `0x1d33c` | 8 | zero | zero | same |
| `0x1d6b6` | 10 | zero | zero | same |
| `0x200aa` | 11 | zero | zero | same |

The calls at `0x6a818`, `0x6a8b6`, and `0x6a8de` are not folded into this
table: they call the stored value directly, with setup words `0x1b58` and
`0x1b59`, rather than loading the first long from a resource handle. They are
a second pointer-call family and must not be mislabeled as proven outer DDef
selectors.

Backward discovery reaches `CODE1+0x20440`, where a candidate object is
passed to `0x27d12` with candidate fields `+0xc`, `+0x12`, and `+0x16` and an
output pointer at `+4`; it is then compared at `+0x20` with `'DDef'`. The
allocation path creates a `0x168`-byte object and calls `0x1cfac`. The exact
enumerator is the Toolbox/object-manager boundary at `0x27d36`:

```text
27d12  load global service pointer; store it in candidate
27d1e  push word candidate+0xc
27d22  push long candidate+0xe
27d26  push long candidate+0x12
27d2a  push &candidate+4
27d2e  moveq #1
27d30  dc.w $aa52
27d32  pop result word; return candidate
```

All statically reachable producers and nearby resource calls have therefore
been accounted for. The missing datum is the implementation of the `$aa52`
service call: its input/output record is not in CODE1 and no Finder Manager
enumerator call is exposed as a direct instruction. The exact dynamic
observation needed is a MacsBug stop on `CODE1+0x27d30` with the stack before
the trap and the candidate object at `a6+8`; this reveals the current resource
file, candidate type/creator, and rejection result. This is a precise boundary,
not a claim that “discovery is unknown.”

For coverage, the independent `r2` cross-reference scan finds ten calls to
`CODE1+0x27d12`, not just the lifecycle caller. Their resource-relative
offsets are `0x7422`, `0x20460`, `0x2f4d8`, `0x2fbd8`, `0x315be`, `0x49cd6`,
`0x4b074`, `0x6d3ce`, and `0x6f6ea`, plus the wrapper entry at `0x27d12`.
The first two call sites compare the returned candidate's `+0x20` against
`FMS_` and `DDef` respectively; the other eight pass object/file records
through the same service wrapper and continue into `0x27dca` or local object
initialization. None directly opens a FreeMIDI Folder or calls a Finder
enumeration API in CODE1. The nearby direct resource calls are
`_Get1Resource` at the authenticated DDef/Code/ICON sites; no raw
`HOpenResFile`, `OpenRFPerm`, `PBGetCatInfo`, or BNDL/FREF selector is reached
in the statically decoded candidate path. Finder/current-resource-file state
and the `$aa52` service implementation are therefore the precise external
discovery boundary, while all CODE1 callers of the shared wrapper are
accounted for.

## DDef handler bodies

### Roland `DDef 0`

Artifact SHA-256 is `0d9b22708a15dd66b39d78c4071e1c7a81a38a6d5767380a570b0f4b583e2b7a`.
The dispatcher at `0x794` selects handlers for 1–8. Selector 1 calls the
FreeMIDI service selector `0x38`, stores the returned service context at
`a4+0x17d8`, installs `ReceiveFreeMidi` at `a4+0x2a6e`, clears the nine-entry
port table at `a4+0x17b4`, and iterates setup helpers at `0xebe` and `0x100c`.
It returns the saved service result through the common epilogue.

Selectors 2–6 are executable handlers, not table placeholders:

| selector | body | proved accesses/calls | return |
|---:|---:|---|---|
| 2 | `0x760` | no parameter/record access beyond dispatcher state | zero |
| 3 | `0x50c` | indexes `a4+0x17dc` by `(par1+8)+1`, tests record `+0x202`, calls helpers `0x1992` and `0x19d8` with that word | zero |
| 4 | `0x580` | same indexed record, tests `+0`, calls `0x1992` and `0x1a18` with `+0x202` | zero |
| 5 | `0x630` | passes `par1` to switch helper `0xc24`; its cases use record `a4+0x17dc + index*0x210 +0x204` and call absolute helpers `0x1182`, `0x1240`, `0x12fe`, `0x13bc`, `0x147a`, or `0x1538`. On success the handler builds a local `0x100`-byte packet, optionally prefixes `0x20,0x23,0x31+port`, and calls service entry `0x1ad0` with length, buffer, and `par1+0x4e` | helper status; failure calls `0xd64`, which clears record `+0x206` through common `0xe20` |
| 6 | `0x740` | passes `par1` to helper `0xd64` (which branches to common `0xe20`) | zero after cleanup |
| 7 | `0x8f6` | writes creator `RdSU` at `par2+0`, `0x100` at `+4`, `ICON 129` handle at `+6`, copies name from `a4+0x5c` into `+0xc`, clears `+0xa/+0xb` | zero |
| 8 | teardown path | calls `TearDownPPCNativeCode`, releases driver-owned state | common teardown status |

The selector-3/4 record stride is `0x210`; selector 3 is the receive-from-
driver notification path and selector 4 is its related port-state/flush
operation. The status-dependent helpers at `0x1992`, `0x19d8`, `0x1a18`,
`0x1ad0`, and the helper called by selector 6 are the remaining exact internal
call boundaries; their argument frames are preserved by the byte-level table
above, while their final application callback is the separately proven
per-port callback at `0x414` below.

### SCC `DDef 128`

Artifact SHA-256 is `eb10322c169d2dfe9db316356bd2b18969d9b3be641eff0a5673af5988eb67a2`.
The dispatcher at `0xec0` has executable bodies for 1–8 and 13. Selector 1
calls setup helper `0x1e2`, then `0xcaa` with `a4+0x92`, and returns the saved
service result. Selector 8 calls `0xd4c` (dispose the handle at `a4+0x92+2`),
then `0xb3c`; it returns through the same epilogue. Selectors 2, 5, 6, 3, 4,
and 13 pass respectively:

```text
2:  par2 long, par1 long                 -> 0xd98
5:  par1 long                             -> 0xdc8
6:  par1 long                             -> 0xe00
3:  par1 long, par2 word                  -> 0xe30
4:  par1 long                             -> 0xe62
13: par1 long                             -> 0xe90
```

`0xd68` is the common table lookup: it reads candidate `+8`, bounds-checks it
against the count, and returns `handle[2 + index*0xb5a]`. The handlers then
invoke helpers `0x2be`, `0x444`, `0x57a`, `0x58c`, `0x81a`, and `0x4dc` with
the selected record. The selector-13 path reads record `+6`, dereferences
that pointer once, and passes the resulting long to `0x4dc`. This is complete
parameter/record evidence; names such as “port” are intentionally not
promoted to structure declarations.

Selector 7 at `0xf34` writes creator `SCCd`, `0x100` at `+4`, loads `ICON 128`
at `+6`, copies the Pascal string at `a4+0` to `+0xc`, sets byte `+0xa=1`,
clears `+0xb`, and returns zero.

### InterApplication `DDef 128`

Artifact SHA-256 is `5b5a65e1903128626c1dbbcb98cc6794752bc8dd88cff57ab1e630f870087932`.
The dispatcher at `0x11e` has handlers 1, 3, 4, 5, and 7. Selector 1 gets
the service selector `0x38` and stores its result at `a4+0x26`; selector 5,
3, and 4 pass `par1` to helpers `0x8a`, `0x104`, and `0x11a` respectively.
The selector-5 helper writes `par1+4` to local code pointer `0x3a`, clears
`par1+0x32`, allocates a name handle at `par1+0x4e`, loads `ICON/SICN 129`,
and copies the Pascal name. Selector 3 writes the service value at `par1+0x36`
and clears `+0x3a`; selector 4 returns zero.

Selector 7 at `0x170` writes `APPd`, `0x100`, `ICON 128`, copies the name to
`+0xc`, clears `+0xa/+0xb`, and returns zero. The helper at `0x70` is a
length-byte plus `_BlockMove` copy. This independently corroborates the
selector-7 descriptor prefix and the fact that selector 1/7 need not imply a
PPC bridge.

### MOTU `DDef 128`

Artifact DDef SHA-256 is
`b6f4664933c861fc531e3c3e2584fd960f6fd7fe9bca36f843ba78339efaa1ae` and the
Code PEF SHA-256 is
`e15194750f793ad671e33e9f96ca19b3bf33544621de142e837d528a429fbdea`.
The 68K wrapper tests selector 1 and selector 8 explicitly. Setup obtains
`Code 128`, holds/loads its PEF, and crosses the native call boundary; teardown
calls the native teardown, releases the held resource, disposes the pointer,
and clears wrapper state. Other selectors fall through the wrapper default.
The exact 68K boundary is the native call at wrapper body `0xd0`; its raw
stack operation cleans ten bytes with `lea.l $a(a7),a7`, while the Toolbox
resource-management calls around `0x76–0x11c` remain trap/CFM boundaries.

## Selector-7 producer/consumer map

The only complete byte-range fact is the System Extension copy from local
`a6-$114` to persistent `object+0xc`, length `0x10c` (268). The following
map contains all offsets presently supported by executable reads/writes:

| offset | width | producer/reader | observed value or source | confidence |
|---:|---:|---|---|---|
| `+0x00` | long | Roland/SCC/IAC write | creator `RdSU`/`SCCd`/`APPd` | verified per driver |
| `+0x04` | word | all three write | `0x0100` | verified |
| `+0x06` | long | all three write; caller copies | `ICON 128`/`ICON 129` handle | verified |
| `+0x0a` | byte | Roland/SCC/IAC clear or set | flags: SCC sets 1; Roland/IAC clear | verified bytes; meaning unresolved |
| `+0x0b` | byte | all three clear | zero | verified |
| `+0x0c` onward | Pascal/string helper and caller copy | driver name source | driver-specific name; exact in-record encoding beyond the first byte is not promoted | strong inference |
| `+0x10c..+0x117` | none | System Extension copy boundary | copied as opaque reserved/driver data | verified boundary |

The caller later stores the 268 bytes in the persistent object at `+0xc` and
uses that object in constructors and vtable paths. No executable producer or
consumer in the corpus proves a port count, capability enum, callback table,
or embedded-vs-referenced string convention in the remaining range. The
separate 70-byte constructor record begins at `object+0x122` and is excluded
from this map by construction.

## Selector-9 relocation boundary

CODE0 SHA-256 is `93a96a3fff0c34341529248ba18e3f6caa81abcf8534239f3dc3ddb284ea87ab`.
Its 24 raw bytes are a compact segment/jump-table header beginning
`00 01 68 e8 00 00 2d 60 00 00 00 08 00 00 00 20`; CODE1 has attributes
`0x28` and begins with a segment header before its code body. Unlike a PEF,
the extracted Classic CODE resources do not expose a separately relocatable
PEF section/relocation stream. The segment/A5 and absolute-address fixups are
loader state, not bytes represented in the raw CODE1 extraction.

There are exactly two executable CODE1 producers of the literal `41 f9 00 00
c1 a2`:

```text
CODE1+0x1d112  lea.l $c1a2.l,a0; store in selector-9 block +4
CODE1+0x79332  lea.l $c1a2.l,a0; store at CODE1 global +0x166c8
```

The second occurrence is used to load/hold `Code 201` and invoke its first
entry with `&CODE1+0x166c8`. Code 201 (PEF SHA-256
`094a0c0a7953023ea5c1127c12fa48143f3df34040343e54de9c909776efc968`)
parses MIDI status/running-state bytes into a small state record, but its
cross-fragment entry does not reveal the relocated address represented by
`0xc1a2`. Other absolute `jsr $34614.l` references are valid CODE1 internal
targets, so applying one global raw-offset bias would be unsound.

The exact static boundary is therefore: instruction `CODE1+0x1d112`, raw
bytes `41 f9 00 00 c1 a2`, with the pointer producer being `lea.l` itself and
the consumer being the selector-9 8-byte block passed at `0x1d12c`. Static
analysis cannot determine whether the loader interprets this as a segment
relative address, a relocated CODE1 address, or an intentionally cross-code
pointer. One future MacsBug stop at the loaded `lea` instruction, capturing
`PC`, `A5`, the six bytes at `PC`, and `A0` immediately after the instruction,
resolves all alternatives. No function signature is inferred before that
observation.

## IDvr callers and handlers

Roland IDvr SHA-256 is
`1f7226dbb12c804c6cc9fe56e865e7d84d6a55b05a2361c5490bfb02a5e6d19`; Standard
Interface IDvr SHA-256 is
`81afcfb9deb1262dbb04e779d0aa029858078d0278056b69b0a8627fd14e03f1`.
The wrapper at CODE1 `0x38e70` receives an object pointer at `a6+8`, ensures
`object+8` is initialized through `0x38ea4`, then pushes:

```text
long  a6+0x10
word  a6+0x0e
word  a6+0x0c
long  object+0x0e
jsr   (object+0x04)
```

It cleans `0x10` bytes and returns the low word. Thus the object has a
verified entry pointer at `+4`, an implementation/resource field at `+8`,
and a long argument at `+e`; the wrapper itself does not prove public field
names. Direct wrapper callers are CODE1 `0x38728`, `0x3894c`, `0x389bc`,
`0x389f8`, `0x38f8a`, and `0x4ce50`. Their selector/argument patterns are,
respectively, `(word 0x10000, zero)`, `(word 0x50000, zero)`, per-port
`(selector 2, word index, long value)`, per-port `(selector 3, word index,
&word)`, selector 6, and a long `0x40000` operation with a driver-dependent
long. The caller at `0x389f8` consumes both the boolean `d0` and the output
word written through its second argument.

Roland IDvr selector bodies are complete at the following byte level:

| selector | body | field/callback evidence |
|---:|---:|---|
| 1 | `0x354` | calls helper `0x166`; helper returns byte widened to word |
| 2 | `0x364` | passes context and long value to `0x17e`; clears context `+0x3c`, obtains FMS `0x40/0x44`, stores returned address at `+0x10`, installs pointer at `+0x40` |
| 3 | `0x372` | passes context to `0x1da`; reads `*context+0x40`, calls FMS `0x44` |
| 4 | `0x37c` | passes long and context to `0x212`; byte result widened |
| 9 | `0x38e` | passes long/context to `0x22c`; helper writes `CODE1+0x250` through its second pointer |
| 10 | `0x39c` | passes long/context to `0x272`; initializes cable record |
| 11 | `0x3a8` | writes byte `4` to context `+0xc`, returns 1 |
| 12 | `0x3b2` | returns 1 |

Roland `CableProc` at `0x272` writes the opaque cable record: word 2 at +0,
zero longs +2/+6, `0xffff` words +a/+c/+18, word 6 at +e/+10, zeros +12/+14/+16,
word zero +1a, word `0x25e` +1c, long zero +1e, then Pascal/string copies at
+0x22 and +0x122. The helper at `0x250` maps input `d1==1` to byte `0xf5` and
clears `d1` for input 2; it is installed by selector 9. This is the concrete
callback target for Roland IDvr, not the unresolved DDef selector-9 literal.

Standard IDvr dispatch at `0x29a` has handlers 1–4, 9, and 10. Handler 1
calls `0x7a` and its FMS selectors `0x68`, `0x74`, `0x50`, `0x54`, `0x2c`,
`0x64`, `0x30`, and `0x34`; it checks creator `SCCd`, writes callback/context
fields at `+0x40/+0x3c`, and obtains the implementation pointers. Handlers 2
and 3 call no-op helpers `0x1e4/0x1e8`; handler 4 calls `0x1ea` with context
and long value; selector 9 calls `0x1ee`, which clears its output long;
selector 10 calls `0x1f6` and initializes the same opaque cable-record shape
with type word 2, zero pointers, words 1 at +e/+10, `0xffff` at +18, `0x25e`
at +1c, and Pascal strings at +0x22/+0x122 before FMS selector `0x34`.

## Receive trace

The strongest end-to-end static trace is Roland:

```text
packed input par1
  -> ReceiveFreeMidi (DDef +0x44c)
  -> port nibble selection through a4+0x78/+0x88
  -> per-byte loop reading byte 1(a6,d3.w)
  -> helper +0x414
  -> port table a4+0x17b4[index]
  -> record callback pointer at record+0x6a
  -> jsr (a1)
```

At `+0x44c`, the low nibble of `par1` selects a port table entry, the high
nibble is compared/updated through `a4+0x76`, and the byte count is the word
at `a6+0xc` after increment. The loop reads packed input bytes from the
caller frame at `a6+1+d3`, so this handler consumes an in-frame packed MIDI
sequence rather than an owned heap buffer. It passes a byte in `d0`, the
channel/status-derived word in `d2`, and a port-derived long in `d1` to
`+0x414`. That helper saves `d0-d2`, indexes `a4+0x17b4` by `d1*4`, clears
record byte `+0x22`, stores `d2` at `+0x23`, loads record long `+0x6a`, and
invokes it with `jsr (a1)`. The callback pointer is installed by selector 1
at `a4+0x2a6e`; the per-port record is initialized by the setup helpers.

`RECEIVEFROMDRIVER` has its name/string boundary at `+0x508`; its first
instruction is `movea.l 8(a6),a2` at `+0x510`. It indexes
`a4+0x17dc + ((par1+8)+1)*0x210`, tests byte state `+0x202`, then passes the
state word and global callback pointer `a4+0x2a6e` to helpers `0x1992` and
`0x19d8`. This proves port identity, state storage, callback origin, and
record stride. It does not expose a separate SysEx allocator or timestamp.

SCC and InterApplication use the same executable pattern at their descriptor
and FMS callback boundaries but their final callback is not statically
connected to the known application queue. Code 201 only parses status and
running-state bytes at PEF code `0x15c`; it writes state offsets 0/1/2/4/5 and
imports `LMGetTicks`, `NewRoutineDescriptor`, and `CallUniversalProc`. The
PowerPlug `FMSReadInputQueue` path is a separate known queue consumer:
`section-1 export +0x544 -> section-0 0x1d5c -> 0x3800 -> 0x386c`.
`0x386c` compares read/write indices at queue `+4/+8`, advances by a `0xc`
byte record, copies a four-byte packed value to the caller's first pointer,
copies an eight-byte timestamp with `lfd/stfd` to the caller's second pointer,
and returns zero when empty. No producer statically connects that queue to a
DDef callback. The exact opaque boundary is the service dispatch at PowerPlug
`0x3914`, whose target is loaded from the per-fragment table at `r2+0x20`.

Therefore receive is complete to an exact indirect callback boundary for
Roland, and to an exact cross-fragment service boundary for the client queue;
no callback signature, running-status ownership, SysEx continuation protocol,
or legal interrupt/task context is promoted beyond those raw observations.

The requested source-by-source coverage is:

| source | exact boundary and pointer producer | result |
|---|---|---|
| Roland DDef | `+0x44c` → helper `+0x414`; selector 1 installs `a4+0x2a6e`, setup records install callback at record `+0x6a` | packed bytes reach an indirect per-port callback |
| InterApplication DDef | `+0x3a`; byte loop reads record `+1`, calls record `+0x6a`, then calls record `+0x36`; selector 5 writes the local callback at record `+0x4` and selector 3 writes service state at `+0x36` | exact callback chain, no queue producer in this DDef |
| SCC DDef | helper `+0x2be`; driver stream object at absolute global `+0xb18` is called with operation `0x13`/`0x11`, then status byte selects `a4+0x80`, `+0x7a`, or `+0x72` helper | exact serial/parser-to-helper boundary; global pointer's external installer is not in the DDef blob |
| Roland IDvr cable hook | `+0x38` decodes packed 4-byte input into an output array, then `+0x118` applies state and calls `context+0x40 -> *context+0x40` | exact cable-hook callback boundary; it is not the unresolved DDef selector-9 pointer |
| Standard Interface IDvr | dispatch and cable-record initializer are present, but no executable `CableHookProc` body or call target occurs in the extracted `IDvr 128`; the only authenticated `CableHookProc` string is Roland IDvr resource offset `+0x157` | precise missing cross-resource/service link, not a strings-only ABI claim |
| Code 201 | PEF code `+0x15c` parses status/running state and writes its private state bytes; no export or call site links it to a DDef callback in CODE1 | exact parser boundary; no queue producer found |
| PowerPlug queue | `+0x3914` dispatch-table target is loaded from `*(r2+0x20)`; queue consumer `+0x386c` reads the 0xc-byte records | exact cross-fragment boundary with pointer producer and frame documented above |

## Output trace

The PowerPlug side proves both ordinary and timestamped client call frames,
but it terminates at an authenticated service dispatch rather than at a
hardware DDef entry. `FMSSendMidi` export code `0x1b60` copies five input
arguments into `r5/r6/r7/r9`, obtains service object `*(r2+0x6e4)`, adds
`0x84`, and calls `0x3914` with service selector `0x0000bba5`. The timestamped
operation at `0x1c44` stores PPC `f1` as an eight-byte value at `r1+0x68`,
passes its pointer in `r9`, and calls service offset `+0x240` with selector
`0x0000fba5`. Both wrappers use a normal PPC 0x40-byte frame and return the
service result in `r3`; they do not allocate a MIDI buffer themselves.

The exact output-side static boundary is therefore `PowerPlug+0x3914`:
`lwz r12,0x20(r2); lwz r0,0(r12); lwz r2,4(r12); mtctr r0; bctr`.
The pointer producer is the fragment dispatch table, and the call frame is
fully recorded above. The target fragment is not among the authenticated
68K/PEF resources with a decoded export that leads to `DriverGetBuffer`.
The Roland DDef contains the names `TransmitData`, `DriverGetBuffer`, and
`SendNextCableByte` only as adjacent code/string navigation evidence; no
instruction path connects them to a concrete DDef selector in the available
resources. This exact fragment target is the one missing datum for a dynamic
output trace; it is not an ownership inference from a name.

The caller audit used both raw byte search and the independent CODE1 `r2`/Capstone
instruction listings. `DriverGetBuffer`, `SendNextCableByte`, and
`CallSendAddressImpl` do not occur as executable symbol references in the
extracted CODE1 (`bbefde2c6f46e7c00969c7e522c7c7cae1111ed544194bd3d02e362a55effcc8`);
their authenticated occurrences are resource-fork C++ symbol/string data.
The only executable output-side callers found in the available client PEF are
the 187 PPC `bl 0x3914` sites, including ordinary `FMSSendMidi` at `0x1ba4`
and timestamped `FMSSendMidiTimeStamped` at `0x1c88` in section 0. The service
target is always produced by `lwz r12,0x20(r2)` at `+0x3914`; no static input
contains the installed target fragment's driver callback body. This accounts
for every available caller without treating C++ names as call edges.

## Coverage and contradiction audits

### Raw instruction anchors used for independent byte checks

The following short raw-byte anchors were compared against the Capstone decode
and, for CODE1, against the independent `r2` listing. Artifact hashes are in
the identity table at the start of this ledger; offsets are resource-relative.

| artifact/resource | offset | raw bytes | decoded boundary/state |
|---|---:|---|---|
| CODE1 / `CODE 1` | `0x1d09c` | `2f3c444465663f3c0001a80e` | push `'DDef'`, ID 1, Resource Manager call |
| CODE1 / `CODE 1` | `0x1d0dc` | `a02942a73f3c000142a7206a01182050` | outer selector-1 frame and handle dereference |
| CODE1 / `CODE 1` | `0x1d112` | `41f90000c1a22d48fec8486e` | selector-9 literal producer and block setup |
| CODE1 / `CODE 1` | `0x1d160` | `41eefeec224a50895889704222d8` | 268-byte selector-7 copy loop |
| CODE1 / `CODE 1` | `0x27d12` | `4e5600002f0a246e000841edd7ee` | candidate/object service wrapper prologue |
| CODE1 / `CODE 1` | `0x27d30` | `486a00047001aa52301f204a` | candidate service trap boundary `$aa52` |
| CODE1 / `CODE 1` | `0x38e70` | `4e5600002f0a246e00082f0a4eb90003` | IDvr wrapper prologue and service init call |
| CODE1 / `CODE 1` | `0x79332` | `41f90000c1a223c8000166c841f90002` | second `0xc1a2` producer before Code 201 call |
| Roland / `DDef 0` | `0x414` | `2f002f012f02241f221f201f` | save `d0-d2` before per-port callback |
| Roland / `DDef 0` | `0x44c` | `4e56000048e71f003c2e000c` | ReceiveFreeMidi frame and byte-count read |
| Roland / `DDef 0` | `0x510` | `246e0008102a000848805240` | RECEIVEFROMDRIVER context/index |
| Roland / `DDef 0` | `0x636` | `2f0a2f03246e00082f0a4eba` | selector-5 output handler frame |
| Roland / `DDef 0` | `0x740` | `4e5600002f2e00084ebafece` | selector-6 call to helper `0xd64` |
| Roland / `DDef 0` | `0x8f6` | `204720bc52645355594f2f3c` | selector-7 writes `RdSU` and loads icon |
| MOTU / `Code 128` | `0x00` | `aafe0700000000000000000000000ee50001000700000020` | embedded AAFE descriptor, ProcInfo `0xEE5`, PPC ISA/flags, PEF pointer |
| MOTU / `DDef 128` | `0xd8` | `206c00004e9036004fef000a` | load descriptor first long and direct `jsr (a0)` native boundary |
| Roland / `DDef 0` | `0x118` | `4e56fefa48e71c30246e0008266e000c` | XCOF setup frame: `par1`/`par2` and large local frame |
| Roland / `XCOF 10000` | `0x00` | `aafe07000000000000000000000000f100010004` | embedded Name Registry UPP descriptor in unpacked data |
| SCC / `DDef 128` | `0xf34` | `244524bc53434364594f2f3c` | selector-7 writes `SCCd` and loads icon |
| InterApplication / `DDef 128` | `0x170` | `246f001a24bc41505064594f` | selector-7 writes `APPd` and loads icon |
| Roland / `IDvr 128` | `0x30a` | `4e56000048e71c20246e0008` | IDvr context/selector/long dispatch frame |
| Roland / `IDvr 128` | `0x38e` | `2f042f0a4ebafe987601504f` | selector-9 callback installation call frame |
| Roland / `IDvr 128` | `0x272` | `4e5600002f0a2f03246e000c` | CableProc record output pointer at `a6+0xc` |
| Standard / `IDvr 128` | `0x31e` | `2f062f044ebafed2504f2005` | selector-10 cable-record initialization |
| PowerPlug / PPC section 0 | `0x1b60` | `7c0802a6900100089421ffc039630000` | ordinary send wrapper prologue |
| PowerPlug / PPC section 0 | `0x1c44` | `7c0802a6900100089421ffc039230000` | timestamped send wrapper prologue |
| PowerPlug / PPC section 0 | `0x386c` | `80c30008800300047c06000041820044` | queue indices and empty branch |
| PowerPlug / PPC section 0 | `0x3914` | `8182002090410014800c0000804c0004` | indirect service dispatch |

Coverage pass completed against CODE1 SHA above:

* direct DDef call sites: `0x1b766`, `0x1bbd0`, `0x1bc0e`, `0x1d0ec`,
  `0x1d10c`, `0x1d12c`, `0x1d33c`, `0x1d6b6`, and `0x200aa`;
* direct-pointer family: `0x6a818`, `0x6a8b6`, `0x6a8de`;
* IDvr wrapper calls: `0x38728`, `0x3894c`, `0x389bc`, `0x389f8`, `0x38f8a`,
  and `0x4ce50`;
* dispatch branches accounted for: Roland DDef 1–8, SCC DDef 1–8/13,
  InterApplication DDef 1/3/4/5/7, Roland IDvr 1–4/9–12, and Standard IDvr
  1–4/9/10;
* untraced indirect boundaries: CODE1 `$aa52` candidate service,
  CODE1 selector-9 literal relocation, PowerPlug `$3914` service dispatch,
  and driver-specific helper calls explicitly listed above;
* unexamined ranges relevant to the ABI: only the opaque Toolbox/PEF target
  bodies named above; no executable dispatch branch was omitted.

Contradiction pass: the outer frame survives independent raw bytes and the
MOTU saved-frame offsets; caller cleanup and callee `rts` are consistent.
Selector 7 is 268 bytes in the caller and 70 bytes only in the separate
constructor. PowerPlug client calls are distinct from DDef/IDvr calls. Raw
`0xc1a2` and loaded/relocated addresses are kept separate. Strings-only
claims have been downgraded to navigation evidence. Repeated creator/icon/
`0x100` writes are promoted only as the common descriptor prefix; all other
fields remain opaque.

## Implementation readiness

| item | status | static basis |
|---|---|---|
| pure 68K inert DDef wrapper shape | safe only for a 68K probe | authentic 12-byte executable prefixes, direct outer frame, caller cleanup |
| selectors 1/7/8 returning inert status | safe only for a 68K probe | selector 7 common prefix and selector 8 lifecycle; selector 9 can safely reject registration with zero |
| Finder/resource metadata template | safe only for a 68K probe | BNDL/FREF/creator/type corpus, but accepted DDef ID remains unobserved |
| production DDef with ports/receive/output | requires dynamic trace | callback signature, selector-9 relocation, queue/service target, ownership/context |
| MOTU outer native bridge | safe now for binary transition analysis | AAFE descriptor and ProcInfo `0xEE5` are static; DDef handle lifetime and `jsr (a0)` are explicit |
| MOTU native handler effects / Roland CFM service target | requires dynamic trace | PEF special-main/import relocation and Roland `AA5A` service target are separate opaque boundaries |
| another version or SDK | not required for tonight's 1.45 static result; useful later | no current 1.45 conclusion depends on their absence |

The smallest future MacsBug session is: use FreeMIDI 1.45 and CODE1 hash
`bbefde2c6f46e7c00969c7e522c7c7cae1111ed544194bd3d02e362a55effcc8`; break at
the loaded address corresponding to `CODE1+0x1d112`; trigger one driver
discovery/setup; capture `PC`, `A5`, `A0` immediately after the six-byte
`lea`, `a6+8..+0x18`, the 8-byte selector-9 block, and the first long at
`object+0x118`. Alternatives are: A0 equals the relocated CODE1/segment
target, A0 points to another fragment transition, or discovery rejects before
selector 9. The observation resolves selector-9 relocation and callback
target/context in one stop. A second stop at `CODE1+0x27d30` with the
pre-trap stack and candidate object resolves the remaining `$aa52` discovery
predicate if a probe must be installed.

## Evidence boundary

The result is sufficient to write a private analysis harness and a safe inert
probe specification. It is not sufficient to ship a production FreeMIDI
adapter or to create a G4 binary. The next decisive observations are the SDK
declarations or a MacsBug trace of the loaded callback address and selector-7
record on a cloned OS 9 system.

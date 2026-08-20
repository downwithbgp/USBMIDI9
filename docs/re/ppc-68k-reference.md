# PowerPC and 68K reference for USBMIDI9 RE

Practical reference for the Classic Mac OS 9 OMS investigation. It covers the
registers, calling conventions, and Mixed Mode boundary used in this project;
it is not a complete ISA manual.

## 1. Execution environments

USBMIDI9 crosses two instruction sets:

```text
68K OMS PROC 1 / Mixed Mode glue
        |
        | RoutineDescriptor (AAFE) or CallUniversalProc
        v
PowerPC PPCC fragment / CFM code fragment
```

Both use 32-bit addresses, but their instruction encodings and register files
are unrelated. Always identify the current ISA before disassembling an
address.

## 2. 68K registers and instructions

The OMS-side code is Motorola 68000-family code, using big-endian words and
longwords.

| Registers | Function |
|---|---|
| `D0`–`D7` | 32-bit data registers; arithmetic, comparisons, temporaries, and scalar results. `D0` is the usual scalar result register. |
| `A0`–`A6` | 32-bit address registers; pointers, object bases, and indirect-call targets. |
| `A7` | Stack pointer. `-(A7)` pushes; `(A7)+` pops. |
| `PC` | Program counter. `JSR` saves a return address; `RTS` consumes one from the stack. |
| `SR` / `CCR` | Status and condition codes (`X`, `N`, `Z`, `V`, `C`), plus interrupt/trace state in `SR`. |

| Instruction | Meaning |
|---|---|
| `MOVE.L` / `MOVE.W` | Copy a 32-bit / 16-bit value. |
| `MOVEA.L` | Load an address register from a longword. |
| `MOVEQ #n,Dn` | Sign-extended small constant into a data register. |
| `LEA` | Compute an address without reading memory. |
| `LINK` / `UNLK` | Establish / tear down an address-register frame. |
| `MOVEM` | Save or restore multiple registers. |
| `JSR` | Subroutine call; pushes a 32-bit return PC. `JSR (A0)` is indirect. |
| `RTS` | Pop a 32-bit return PC from `(A7)` and branch to it. |
| `JMP` | Branch without creating a subroutine return frame. |
| `Bcc` | Conditional branch using the condition codes. |

The OMS driver entry contract is Pascal stack based:

```c
pascal long main(short msg, long par1, long par2);
```

For the investigated `PROC1 +0x98A2` helper, the raw instruction is
`MOVEA.L 0x0018(A7),A0`. With the observed `A7=S-0x0E`, it reads `S+0x0A`:

```text
EA = (S - 0x0E) + 0x18 = S + 0x0A
```

This was dynamically shown to contain the OMS driver record.

## 3. PowerPC registers

The PPCC code runs on the PowerPC G3/G4. Instructions are fixed 32-bit words;
the general registers are `r0`–`r31`.

| Registers | Function in this project |
|---|---|
| `r0` | Special operand in several instruction forms; not a normal base in all forms. |
| `r1` | Stack pointer under the PowerPC ABI. |
| `r2` | CodeWarrior/Classic Mac TOC and global-data pointer. |
| `r3`–`r10` | Argument and result registers; `r3` is the primary scalar result. |
| `r11`–`r12` | Volatile temporaries/linkage registers. |
| `r13`–`r31` | Normally nonvolatile/callee-saved registers; compiler use varies. |
| `LR` | Link register. `bl` writes the return address; `blr` branches to it. |
| `CTR` | Count register and indirect-branch target for `bcctr`-style calls. |
| `CR` | Condition register fields used by conditional branches. |
| `XER` | Carry, overflow, and fixed-point exception state. |
| `FPSCR` | Floating-point status/control state; not relevant to this crash. |

Typical wrapper setup observed in PPCC code is:

```asm
addi    r3,r29,0
addi    r4,r30,0
addi    r5,r31,0
bl      oms_handle_message
...
blr
```

The exact semantic values of `r3`–`r5` must still be established from the
surrounding instructions; they are ABI argument registers, not automatic proof
of source-level variable names.

## 4. PowerPC call and return convention

For ordinary CodeWarrior PPC calls, `r1` is the stack pointer, arguments begin
in `r3`, and a scalar result is returned in `r3`. `bl target` writes the next
instruction to `LR`; `blr` returns through `LR`. Nonvolatile registers and the
stack frame follow the compiler ABI.

Mixed Mode marshalling determines how the original 68K arguments become PPC
register values. Therefore PPC register captures cannot be interpreted as a
literal copy of the 68K stack without decoding the ProcInfo.

## 5. Mixed Mode descriptors and UPPs

A classic 68K caller may directly `JSR` a UniversalProcPtr. For a PPC target,
that pointer identifies a Mixed Mode `RoutineDescriptor`, whose initial A-line
dispatch transfers control according to its routine record.

The packed descriptor layout used by this RE is:

```text
RoutineDescriptor
  +0x00  UInt16       magic / goMixedModeTrap       (AAFE)
  +0x02  UInt8        version
  +0x03  UInt8        descriptor flags
  +0x04  UInt32       reserved
  +0x08  UInt8        reserved
  +0x09  UInt8        selectorInfo
  +0x0A  UInt16       routineCount
  +0x0C  RoutineRecord[0]

RoutineRecord
  +0x00  UInt32       procInfo
  +0x04  UInt8        reserved
  +0x05  UInt8        ISA (1 = PowerPC)
  +0x06  UInt16       routine flags
  +0x08  ProcPtr      procDescriptor
  +0x0C  UInt32       reserved
  +0x10  UInt32       selector
```

`CallUniversalProc(ptr, externalProcInfo, ...)` uses the ProcInfo supplied by
the caller. A direct 68K call through a RoutineDescriptor uses the embedded
routine-record ProcInfo.

The OMS driver-entry contract is:

```text
uppOMSDriverProcInfo = 0x00000FB0
pascal long main(short msg, long par1, long par2)
```

It encodes a 4-byte result and stack parameters of 2, 4, and 4 bytes. This
proves the generic OMS driver-entry contract; it does not prove that every
stored callable pointer has this embedded value.

The USBMIDI9 descriptor experiment captured this topology:

```text
record+0x52 = record+0x66 -> OMS-record outer AAFE descriptor, ProcInfo = 0
                              -> procDescriptor = nested AAFE bytes
                                 nested ProcInfo = 0x0FB0
                                 nested procDescriptor = PPC transition vector
```

The later transition capture proves that this is not recursive descriptor
parsing. Mixed Mode loads the outer `procDescriptor` as a PPC transition-vector
address; if that address is itself an AAFE object, its first longword is read
as a code address. The nested object is therefore an observed data layout, not
a second descriptor invocation. The outer descriptor's embedded ProcInfo
controls a direct 68K call through the outer pointer.

## 6. CFM and transition vectors

The Code Fragment Manager loads a PPC PEF and resolves its main symbol. A PPC
routine is represented for cross-mode linkage by a transition vector containing
the code entry and PPC TOC/data context. In the captured inner descriptor, the
routine record pointed to transition-vector location `0x01B5AC88`.

Keep these concepts separate:

- PEF main symbol: a CFM fragment symbol;
- PPC transition vector: PPC linkage state, including code and TOC context;
- RoutineDescriptor: Mixed Mode metadata plus a routine pointer;
- UPP: the SDK callable-pointer abstraction.

## 7. Established USBMIDI9 path

```text
68K OMS loader/dispatcher
  -> CFM-loaded PPCC fragment
  -> record+0x52 / record+0x66 callable object
  -> Mixed Mode entry
  -> PPC main(short,long,long)
  -> PPC handler returns through blr
  -> 68K OMS continuation
```

The generic OMS path supplies `0x0FB0` externally through
`CallUniversalProc`. A separate direct-call path may use the embedded ProcInfo
of the pointer stored in the driver record. Consequently, a successful generic
call does not prove that every stored callable pointer has the same embedded
contract.

## 8. Boundary-debugging checklist

Before interpreting a capture, record:

1. Current ISA and instruction address.
2. `TD`, including `PC`, `A7`/`r1`, `LR`, `r2`, and argument registers.
3. Exact raw instruction bytes, including extension words.
4. Descriptor bytes at the callable pointer.
5. For PPC records, transition-vector code and TOC words.
6. Stack before and after the call, expressed in byte offsets.

Never infer a 68K displacement from rendered decimal-looking syntax, and never
disassemble a PPC address as 68K or a 68K address as PPC.

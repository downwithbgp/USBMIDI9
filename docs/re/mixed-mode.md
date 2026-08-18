# Mixed Mode — UPPs, ProcInfo, and the OMS call boundary

How 68K OMS invokes PPC driver code, and the constants that govern it.
All ProcInfo decodes are mechanically reproduced by
`tools/re/procinfo_check.c` (compile + run; asserts the values).

## MM1. UniversalProcPtr / RoutineDescriptor

- A **UPP (UniversalProcPtr)** is a handle to a **RoutineDescriptor**
  (Mixed Mode Manager structure). Created with
  `NewRoutineDescriptor(proc, procInfo, architecture)`; disposed with
  `DisposeRoutineDescriptor`.
- Invoked with `CallUniversalProc(upp, procInfo, args...)`. The
  ProcInfo tells the Mixed Mode Manager the calling convention, the
  result size, and the argument layout (stack and/or registers) — it
  must match the actual routine.
- A 68K `JSR` to a UPP/RoutineDescriptor is a valid Mixed Mode entry: the
  `0xAAFE` descriptor trap dispatches according to its RoutineRecord. A PPC
  direct call to a 68K UPP is different and can execute descriptor data as
  code. The b88c5a7 evidence concerns the PPC-side add1Device UPP call and
  still requires `CallOMSDvrAdd1DevProc1`; it does not invalidate the 68K
  `JSR (A0)` behavior observed in T8/T9-A0.

## MM2. ProcInfo bit layout (authentic UI 3.3.2 MixedMode.h)

Field widths: callingConvention 4, resultSize 2, stackParameter 2,
registerResultLocation 5, registerParameter 5; phases:
- resultSize << 4 (kResultSizePhase = 4)
- stack parameter k (1-based): sizeCode << (6 + (k-1)*2)
- register parameter k: (sizeCode | whichReg<<2) << (11 + (k-1)*5)
- SIZE_CODE(size): 4→3, 2→2, 1→1, else 0
- conventions: kPascalStackBased = 0, kRegisterBased = 2

## MM3. The OMS driver entry UPP — uppOMSDriverProcInfo = 0xFB0 (PROVEN)

`CallOMSDriverProc(upperRoutine, msg, par1, par2)` (OMS 2.0 SDK
OMSDrvUPPs.h) uses:

    kPascalStackBased | RESULT_SIZE(kFourByteCode)
      | STACK_ROUTINE_PARAMETER(1, kTwoByteCode)   /* short msg   */
      | STACK_ROUTINE_PARAMETER(2, kFourByteCode)  /* long par1   */
      | STACK_ROUTINE_PARAMETER(3, kFourByteCode)  /* long par2   */

= 0 | 3<<4 (0x30) | 2<<6 (0x80) | 3<<8 (0x300) | 3<<10 (0xC00) = **0xFB0**.

Evidence: SDK header composition; the OMS library's single call site
(A9E2 at 0x10C86) pushes exactly `[msg 2B][par1 4B][par2 4B][procInfo
4B]` (oms-2.3.8-map.md M2); procinfo_check.c asserts 0xFB0.

## MM4. The add1device callback UPP — uppOMSDvrAdd1DevProc1Info = 0x2F0 (PROVEN)

`CallOMSDvrAdd1DevProc1(userRoutine, device, devSize)` uses:

    kPascalStackBased | RESULT_SIZE(kFourByteCode)   /* OMSDeviceH   */
      | STACK_ROUTINE_PARAMETER(1, kFourByteCode)    /* OMSDevice *  */
      | STACK_ROUTINE_PARAMETER(2, kTwoByteCode)     /* short devSize */

= 0 | 0x30 | 3<<6 (0xC0) | 2<<8 (0x200) = **0x2F0**.

Evidence: SDK OMSDrvUPPs.h (verbatim composition above); procinfo_check.c.

## MM5. The receive hook — register-based ProcInfo (PROVEN)

The 68K `OMSReceivedFromPort` routine is invoked (PPC side, oms_rx.c)
through CallUniversalProc with a register-based ProcInfo:

    kRegisterBased
      | REGISTER_ROUTINE_PARAMETER(1, kRegisterA1, SIZE_CODE(sizeof(OMSPacket *)))  /* A1 = pkt      */
      | REGISTER_ROUTINE_PARAMETER(2, kRegisterD0, SIZE_CODE(sizeof(short)))        /* D0 = ioRefNum */

= 2 | (3 | 5<<2)<<11 | (2 | 0<<2)<<16 = 2 | 0xB800 | 0x20000 = **0x2B802**
(procinfo_check.c prints it). The 68K convention itself is
`#pragma parameter OMSReceivedFromPort(__A1, __D0)` (SDK OMSDriver.h,
`#ifndef powerc`) — the Spec calls it a "68K assembly language routine",
interrupt-level legal.

## MM6. Other OMS UPPs (SDK OMSDrvUPPs.h, for reference)

- `uppOMSDvrAdd1DevProc0Info` = result(4B) | STACK_PARAM(1,4B) =
  0x30|0xC0 = 0xF0 (compat-0 add1device, no devSize).
- `uppOMSDriverEventHandlerInfo` = void result | 2×4B params.
- `uppOMSSerErrResponseProcInfo` = void result | 3×2B params.
- `NewOMSReadHook2`/`CallOMSReadHook2` (OMSUPPs.h) — the send-proc
  RoutineDescriptor family used by omdvGetPortSendProc (see
  docs/architecture.md).

## MM7. DebugStr / Debugger and the low-level trap

- `DebugStr`/`Debugger` are Mixed Mode cross-TOC calls into the high-level
  Debugger implementation. On the PPC↔68K boundary they are NOT
  transparent: the trace build's intentional DebugStr breaks perturbed
  the state being localized (evidence-ledger P9). DebugStr did NOT cause
  the production crash.
- The replacement instrumentation is the **MacsBug low-level native PPC
  debugger trap** `kPowerPCLowLevelDebuggerTrap = 0x7F800008`
  (`tw LT|GT|EQ,r0,r0`; MacsBug 6.5.3 Read Me): one native instruction,
  no Mixed Mode involvement, no register modification; MacsBug stops on
  the instruction after the trap; controlled by DX (docs/g4-handoff.md
  "OMS Search trace build").
- The authentic TM PPCC stub bridges to its 68K `PROC` 1 via
  `CallUniversalProc(entry, 0x3A5, ...)` — a register/stack mixed
  convention worth decoding if the TM's private messages are ever
  needed (0x3A5 decode not yet done).

# OMS PPC receive bridge — tasks (spec/oms-ppc-bridge)

The PPC `OMSReceivedFromPort` ABI is fully authenticated (Opcode + Apple
evidence below). Implement the 68K-bridge so the shim stops calling
`OMSReceivedFromPort` as a native PPC import.

## Authenticated evidence (all read from ~/research/oms + UI 3.3.2)

- `OMSDriver.h` (OMS 2.0 SDK, 28-Jan-98) declares `OMSReceivedFromPort`
  ONLY `#ifndef powerc`, with `#pragma parameter OMSReceivedFromPort(__A1, __D0)`
  (68K registers A1 = OMSPacket*, D0 = short destRefNum).
- OMS Spec "OMSReceivedFromPort (68K assembly language routine)": On
  entry A1 -> OMSPacket *pkt; D0 = short sourceIORefNum. May be called
  at interrupt level. "For efficiency, it is best to obtain the address
  of this routine using OMSGetCallAddress(callOMSReceivedFromPort)
  rather than calling it via glue."
- OMS Spec "OMSGetCallAddress": `OMSAPI(long) OMSGetCallAddress(short
  callNum)`; "PowerPC native code ... The returned address will always
  point to 68K code, therefore the routine will need to be called via
  CallUniversalProc."
- OMS Spec "LinkToOMSGlue": `OMSAPI(OMSErr) LinkToOMSGlue(void)`; "Call
  LinkToOMSGlue in a piece of code, such as an OMS driver, which needs
  to call OMS, and can assume that OMS is running, but does not need to
  sign in."
- `OMSGlueProcs.h` (generated, authentic): `#define callOMSReceivedFromPort 112`.
- `OMSTypes.h`: `typedef short OMSErr;`.
- SDK `Libraries/README.OMSGlue`: "OMSGluePPC.lib - for CodeWarrior
  PowerPC" (authentic PPC glue library).
- UI 3.3.2 `MixedMode.h`: `kRegisterBased = 2`; `kRegisterD0 = 0`,
  `kRegisterA1 = 5`; `ProcInfoType = unsigned long`; `UniversalProcPtr =
  struct RoutineDescriptor *`; `SIZE_CODE(size)` and
  `REGISTER_ROUTINE_PARAMETER(whichParam, whichReg, sizeCode)` macros
  verbatim; `EXTERN_API_C(long) CallUniversalProc(UniversalProcPtr,
  ProcInfoType, ...)`; phase constants: kCallingConventionWidth=4,
  kResultSizeWidth=2, kStackParameterWidth=2, kRegisterResultLocationWidth=5,
  kRegisterParameterWidth=5, kRegisterParameterPhase=11,
  kRegisterParameterSizePhase=0, kRegisterParameterSizeWidth=2,
  kRegisterParameterWhichPhase=2.

## Design

- `struct oms_state` gains `long rxRoutine;` — the cached 68K address
  returned by `OMSGetCallAddress(callOMSReceivedFromPort)`; 0 =
  unresolved → receive delivery disabled (drain continues, packets
  dropped). Type `long` = the exact `OMSGetCallAddress` return type; no
  invented typedefs.
- `oms/oms_driver.h` defines the ProcInfo macro (exact authenticated
  expression):
  `#define kOMSReceivedFromPortProcInfo (kRegisterBased | REGISTER_ROUTINE_PARAMETER(1, kRegisterA1, SIZE_CODE(sizeof(OMSPacket *))) | REGISTER_ROUTINE_PARAMETER(2, kRegisterD0, SIZE_CODE(sizeof(short))))`
- `oms_init()` (omdvInit) resolves once, under `#if defined(powerc)`,
  AFTER `oms_zero(&g_oms, ...)` (the re-entry guard wipes the cached
  address):
  `g_oms.rxRoutine = 0L; if (LinkToOMSGlue() == noErr) { g_oms.rxRoutine = OMSGetCallAddress(callOMSReceivedFromPort); }`
  Resolution failure (LinkToOMSGlue error — OMSGetCallAddress is NOT
  called — or address 0) leaves rxRoutine 0 → delivery disabled;
  omdvInit still succeeds (driver loads; hot-plug etc. unaffected).
- `oms_rx.c` gains a static `oms_rx_deliver(OMSPacket *pkt, short
  ioRefNum)`: `#if defined(powerc)` → `CallUniversalProc((UniversalProcPtr)g_oms.rxRoutine, kOMSReceivedFromPortProcInfo, pkt, ioRefNum)` when rxRoutine != 0; `#else` → direct `OMSReceivedFromPort(pkt, ioRefNum)` (68K; the authentic `#pragma parameter` handles registers). The direct call in `oms_rx_drain` is replaced by the wrapper.
- `oms_rx.c` includes `<MixedMode.h>` (the only real-target TU needing
  it). `oms_driver.c` includes `<OMSGlueProcs.h>` for
  callOMSReceivedFromPort (resolves from OMS SDK:Headers: on the G4).
- Host builds compile the shim with `-Dpowerc` (CLASSIC_CFLAGS) so the
  host tests exercise the actual PPC wrapper path; the host-check
  `CallUniversalProc` stub cannot validate real PPC↔68K mode switching
  (marked in the header); it only forwards the two arguments.

## Tasks

- [T1] host-check/MixedMode.h (new): ONLY the exact surface the wrapper
  needs — kRegisterBased/kRegisterD0/kRegisterA1, the phase enum with
  the authentic values verbatim (source: UI 3.3.2 mirrored at
  github.com/elliotnunn/UniversalInterfaces
  3.3.2/Universal/Interfaces/CIncludes/MixedMode.h, the same mirror
  docs/host-check-audit.md relies on), SIZE_CODE +
  REGISTER_ROUTINE_PARAMETER verbatim, ProcInfoType, UniversalProcPtr
  (struct RoutineDescriptor*), and a CallUniversalProc declaration;
  header comment marks that host tests cannot validate actual PPC↔68K
  mode switching.
- [T2] host-check/OMSGlueProcs.h (new): `#define callOMSReceivedFromPort 112`
  (+ authentic generated-header provenance comment).
- [T3] host-check/OMS.h: add `typedef short OMSErr;`,
  `extern OMSErr LinkToOMSGlue(void);`, `extern long OMSGetCallAddress(short callNum);`.
- [T4] host-check/OMSDriver.h: wrap the OMSReceivedFromPort declaration
  in `#ifndef powerc` (authentic shape).
- [T5] oms/oms_driver.h: `long rxRoutine;` in `struct oms_state` +
  `kOMSReceivedFromPortProcInfo` macro.
- [T6] oms/oms_driver.c: include `<OMSGlueProcs.h>`; resolve at
  omdvInit under `#if defined(powerc)` (LinkToOMSGlue →
  OMSGetCallAddress(callOMSReceivedFromPort) → cache; failure leaves 0).
- [T7] oms/oms_rx.c: include `<MixedMode.h>`; `oms_rx_deliver()` wrapper
  (CallUniversalProc on powerc, direct call otherwise); replace the
  direct call in oms_rx_drain.
- [T8] Makefile: `-Dpowerc` in CLASSIC_CFLAGS (models the G4 PPC target;
  no other powerc guards exist in the tree).
- [T9] tests/test_oms_driver.c: mock LinkToOMSGlue (counts, returns
  noErr or a test-driven error), mock OMSGetCallAddress (asserts
  callNum == callOMSReceivedFromPort; returns the capture-routine
  address or 0 on failure; counts), CallUniversalProc stub (records
  procPtr + procInfo, forwards pkt/refnum to the routine; reads the
  `short` argument via `va_arg(ap, int)` — default promotion). Rename
  the OMSReceivedFromPort capture mock to a plain function (its address
  is what OMSGetCallAddress returns). New tests:
  - resolved routine cached (after omdvInit: rxRoutine == routine
    address; LinkToOMSGlue once; OMSGetCallAddress once with 112);
  - wrapper delivers OMSPacket* + ioRefNum through CallUniversalProc
    with procInfo == the authenticated expression (the test writes the
    expression itself and asserts equality; do NOT pin a G4 numeric
    literal — host sizeof(OMSPacket*) is 8, so SIZE_CODE differs from
    the G4's 4-byte-pointer value; comment the G4 value, compile-verified
    as 0x2B802 via SIZE_CODE(4) substitution, and leave the hardware
    gate to confirm);
  - resolution failure disables delivery safely (two modes: LinkToOMSGlue
    error → OMSGetCallAddress NOT called; address 0 → delivery skipped.
    In both: drain still runs/rxMessages counted, nothing captured, no
    CallUniversalProc call);
  - no repeated OMSGetCallAddress per packet (drive several events;
    resolution count stays 1).
- [T10] docs/g4-handoff.md: Libraries paragraph → link
  `OMS SDK:Libraries:OMSGluePPC.lib` (authentic CW PPC glue) into the
  OMS PEF target; the shim imports LinkToOMSGlue + OMSGetCallAddress
  via the glue; OMSReceivedFromPort is NOT an import (68K-only; reached
  via the cached address + CallUniversalProc); `<MixedMode.h>` and
  `<OMSGlueProcs.h>` resolve from the Universal Interfaces access path
  and `OMS SDK:Headers:` respectively; receive-path description updated;
  Target B states oms/oms_driver.r is NOT part of the PEF target
  (Target A compiles C sources only).
- [T11] docs/host-check-audit.md: audit-table rows for the new/changed
  stub surface (MixedMode.h, OMSGlueProcs.h, OMS.h additions,
  OMSDriver.h guard) with authentic sources.

## Gates

make test / make test-sanitize / make check-classic all green; review
the diff; one focused commit; push (no force-push — policy-denied).

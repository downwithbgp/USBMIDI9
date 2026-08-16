/*
 * Minimal stub of the Apple Mixed Mode Manager surface used by the
 * USBMIDI9 OMS shim's PPC 68K bridge (oms/oms_rx.c oms_rx_deliver).
 * NOT the real header — see OMS.h in this directory for the policy.
 *
 * Every constant/macro below was copied VERBATIM from the authentic
 * Universal Interfaces 3.3.2 MixedMode.h (mirrored at
 * github.com/elliotnunn/UniversalInterfaces,
 * 3.3.2/Universal/Interfaces/CIncludes/MixedMode.h — the same mirror
 * docs/host-check-audit.md relies on). Only the surface the wrapper
 * needs is modeled: the register-based calling convention, the
 * 68K-register selector constants, the ProcInfo phase enum, SIZE_CODE
 * and REGISTER_ROUTINE_PARAMETER, ProcInfoType, UniversalProcPtr and
 * CallUniversalProc.
 *
 * HOST LIMITATION (deliberate): CallUniversalProc is declared here so
 * the wrapper compiles, but host tests CANNOT validate actual PPC<->68K
 * mode switching — on the G4 the Mixed Mode Manager dispatches the
 * cached 68K address with parameters in registers (A1/D0 per the OMS
 * Spec); on the host the test supplies a plain forwarding
 * implementation. The register dispatch itself is verified only at the
 * G4 hardware gate.
 *
 * `pascal` is defined empty for the Linux compile (see OMS.h).
 */

#ifndef USBMIDI9_HOST_CHECK_MIXEDMODE_H
#define USBMIDI9_HOST_CHECK_MIXEDMODE_H

#include "MacTypes.h"

/* Calling conventions (MixedMode.h). */
typedef unsigned short CallingConventionType;
enum {
    kPascalStackBased             = 0,
    kCStackBased                  = 1,
    kRegisterBased                = 2,
    kD0DispatchedPascalStackBased = 8,
    kD1DispatchedPascalStackBased = 12,
    kD0DispatchedCStackBased      = 9,
    kStackDispatchedPascalStackBased = 14,
    kThinkCStackBased             = 5
};

/* 68K register selectors (MixedMode.h). */
enum {
    kRegisterD0                   = 0,
    kRegisterD1                   = 1,
    kRegisterA0                   = 4,
    kRegisterA1                   = 5,
    kRegisterA2                   = 6,
    kRegisterA3                   = 7
};

/* ProcInfo layout phases (MixedMode.h, verbatim). */
enum {
    kCallingConventionWidth       = 4,
    kCallingConventionPhase       = 0,
    kCallingConventionMask        = 0x0F,
    kResultSizeWidth              = 2,
    kResultSizePhase              = kCallingConventionWidth,
    kResultSizeMask               = 0x30,
    kStackParameterWidth          = 2,
    kStackParameterPhase          = (kCallingConventionWidth + kResultSizeWidth),
    kStackParameterMask           = (long)0xFFFFFFC0,
    kRegisterResultLocationWidth  = 5,
    kRegisterResultLocationPhase  = (kCallingConventionWidth + kResultSizeWidth),
    kRegisterParameterWidth       = 5,
    kRegisterParameterPhase       = (kCallingConventionWidth + kResultSizeWidth + kRegisterResultLocationWidth),
    kRegisterParameterMask        = 0x7FFFF800,
    kRegisterParameterSizePhase   = 0,
    kRegisterParameterSizeWidth   = 2,
    kRegisterParameterWhichPhase  = kRegisterParameterSizeWidth,
    kRegisterParameterWhichWidth  = 3
};

/* Size codes (MixedMode.h): 4->four, 2->two, 1->one, else none. */
enum {
    kNoByteCode                   = 0,
    kOneByteCode                  = 1,
    kTwoByteCode                  = 2,
    kFourByteCode                 = 3
};

/* ISA/RTA (MixedMode.h, verbatim; the G4 PPC build selects the
 * TARGET_CPU_PPC branch: kPowerPCISA | kPowerPCRTA, so
 * GetCurrentArchitecture() evaluates to 1). */
typedef SInt8 ISAType;
enum {
    kM68kISA                    = 0,
    kPowerPCISA                 = 1
};
typedef SInt8 RTAType;
enum {
    kOld68kRTA                  = 0 << 4,
    kPowerPCRTA                 = 0 << 4,
    kCFM68kRTA                  = 1 << 4
};
#define GetCurrentISA()     ((ISAType) kPowerPCISA)
#define GetCurrentRTA()     ((RTAType) kPowerPCRTA)
#define GetCurrentArchitecture()    (GetCurrentISA() | GetCurrentRTA())

typedef unsigned long ProcInfoType;   /* MixedMode.h */

/* UniversalProcPtr: pointer to classic 68K code or a RoutineDescriptor
 * (MacTypes.h). The host never dereferences it. */
struct RoutineDescriptor;
typedef struct RoutineDescriptor *UniversalProcPtr;

/* SizeCode / register-parameter macros (MixedMode.h, verbatim). */
#define SIZE_CODE(size) \
    (((size) == 4) ? kFourByteCode : (((size) == 2) ? kTwoByteCode : (((size) == 1) ? kOneByteCode : 0)))

#define REGISTER_ROUTINE_PARAMETER(whichParam, whichReg, sizeCode) \
    ((((ProcInfoType)(sizeCode) << kRegisterParameterSizePhase) | ((ProcInfoType)(whichReg) << kRegisterParameterWhichPhase)) << \
            (kRegisterParameterPhase + (((whichParam) - 1) * kRegisterParameterWidth)))

/* MixedMode.h: CallUniversalProc dispatches theProcPtr (68K code or a
 * RoutineDescriptor) according to procInfo. On the host the test
 * supplies the implementation (plain argument forwarding); it cannot
 * model register-based dispatch. */
extern long CallUniversalProc(UniversalProcPtr theProcPtr,
                              ProcInfoType procInfo, ...);

/* ProcInfo builders for stack-based (Pascal) routines (MixedMode.h,
 * verbatim) — used by the OMSUPPs.h generated UPP macros. */
#define RESULT_SIZE(sizeCode) \
    ((ProcInfoType)(sizeCode) << kResultSizePhase)

#define STACK_ROUTINE_PARAMETER(whichParam, sizeCode) \
    ((ProcInfoType)(sizeCode) << (kStackParameterPhase + (((whichParam) - 1) * kStackParameterWidth)))

/* RoutineDescriptor constructors (MixedMode.h, CFM branch — the G4
 * build). NewOMSReadHook2 (OMSUPPs.h) creates the send-proc descriptor
 * with NewRoutineDescriptor; the driver releases it with
 * DisposeRoutineDescriptor. Host tests supply forwarding
 * implementations; the real constructors run on the G4. */
extern UniversalProcPtr NewRoutineDescriptor(ProcPtr theProc,
                                             ProcInfoType theProcInfo,
                                             ISAType theISA);
extern void DisposeRoutineDescriptor(UniversalProcPtr theProcPtr);

#endif /* USBMIDI9_HOST_CHECK_MIXEDMODE_H */

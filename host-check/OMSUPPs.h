/*
 * Minimal stub of the Opcode OMS 2.0 SDK (28-Jan-98)
 * Headers/OMSUPPs.h — a generated UPP header ("*** THIS FILE GENERATED
 * AUTOMATICALLY -- DO NOT MODIFY ***"). Only the OMSReadHook2 UPP
 * surface the OMS shim's send path uses is modeled; the macro bodies
 * are copied VERBATIM from the authentic file
 * (~/research/oms/sdk/OMS 2.0 SDK 28-Jan-98/Headers/OMSUPPs.h).
 *
 * OMS_MAC_CFM selects the branch (authentic OMSTypes.h derives it from
 * USESROUTINEDESCRIPTORS || TARGET_RT_MAC_CFM); the G4 PPC CFM build
 * has it 1 — see OMS.h. The authentic header provides NO Dispose macro
 * for OMSReadHook2UPP (the driver pairs NewOMSReadHook2 with the
 * Mixed Mode Manager's DisposeRoutineDescriptor directly).
 */

#ifndef USBMIDI9_HOST_CHECK_OMSUPPS_H
#define USBMIDI9_HOST_CHECK_OMSUPPS_H

/* On the G4 the Universal Headers make UniversalProcPtr,
 * NewRoutineDescriptor and GetCurrentArchitecture visible before OMS.h
 * (MacTypes.h is included first by the driver sources). The stub keeps
 * that surface in MixedMode.h, so OMSUPPs.h pulls it in itself. */
#include "MixedMode.h"

#if OMS_MAC_CFM
enum {
    uppOMSReadHook2Info = kPascalStackBased
                    | RESULT_SIZE(0)                            /* pascal void          */
                    | STACK_ROUTINE_PARAMETER(1, kFourByteCode) /* OMSMIDIPacket *packet */
                    | STACK_ROUTINE_PARAMETER(2, kFourByteCode) /* long readHookRefCon  */
};
typedef UniversalProcPtr OMSReadHook2UPP;
#define NewOMSReadHook2(userRoutine) \
        (OMSReadHook2UPP)NewRoutineDescriptor((ProcPtr)(userRoutine), uppOMSReadHook2Info, GetCurrentArchitecture())
#define CallOMSReadHook2(userRoutine, packet, readHookRefCon) \
        CallUniversalProc((UniversalProcPtr)(userRoutine), uppOMSReadHook2Info, (packet), (readHookRefCon))
#else
typedef OMSReadHook2 OMSReadHook2UPP;
#define NewOMSReadHook2(userRoutine) \
        (OMSReadHook2UPP)(userRoutine)
#define CallOMSReadHook2(userRoutine, packet, readHookRefCon) \
        (*(userRoutine))((packet), (readHookRefCon))
#endif

#endif /* USBMIDI9_HOST_CHECK_OMSUPPS_H */

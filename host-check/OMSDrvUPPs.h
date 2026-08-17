/*
 * Minimal stub of the Opcode OMS 2.0 SDK (28-Jan-98)
 * Headers/OMSDrvUPPs.h — a generated UPP header ("*** THIS FILE
 * GENERATED AUTOMATICALLY -- DO NOT MODIFY ***"). Only the
 * OMSDvrAdd1DevProc0/1 UPP surface the OMS shim's omdvAddDevices path
 * uses is modeled; the macro bodies are copied VERBATIM from the
 * authentic file (~/research/oms/sdk/OMS 2.0 SDK 28-Jan-98/Headers/
 * OMSDrvUPPs.h). OMSDriver.h includes this file, exactly like the
 * authentic header.
 *
 * OMS_MAC_CFM selects the branch (authentic OMSTypes.h derives it from
 * USESROUTINEDESCRIPTORS || TARGET_RT_MAC_CFM); the G4 PPC CFM build
 * has it 1 — see OMS.h. On CFM the driver MUST invoke the callback
 * through CallOMSDvrAdd1DevProc1 (= CallUniversalProc with
 * uppOMSDvrAdd1DevProc1Info), NOT by calling the UPP directly: the UPP
 * is a UniversalProcPtr (routine descriptor), and a direct call
 * executes non-code bytes as PPC → Address Error. This is the
 * production integration crash (spec/add1device-upp-fix).
 */

#ifndef USBMIDI9_HOST_CHECK_OMSDRVUPPS_H
#define USBMIDI9_HOST_CHECK_OMSDRVUPPS_H

#include "MixedMode.h"

#if OMS_MAC_CFM
enum {
    uppOMSDvrAdd1DevProc0Info = kPascalStackBased
                    | RESULT_SIZE(kFourByteCode)            /* pascal OMSDeviceH    */
                    | STACK_ROUTINE_PARAMETER(1, kFourByteCode) /* OMSDevice *device */
};
typedef UniversalProcPtr OMSDvrAdd1DevProc0UPP;
#define NewOMSDvrAdd1DevProc0(userRoutine) \
        (OMSDvrAdd1DevProc0UPP)NewRoutineDescriptor((ProcPtr)(userRoutine), uppOMSDvrAdd1DevProc0Info, GetCurrentArchitecture())
#define CallOMSDvrAdd1DevProc0(userRoutine, device) \
        (OMSDeviceH)CallUniversalProc((UniversalProcPtr)(userRoutine), uppOMSDvrAdd1DevProc0Info, (device))
enum {
    uppOMSDvrAdd1DevProc1Info = kPascalStackBased
                    | RESULT_SIZE(kFourByteCode)            /* pascal OMSDeviceH    */
                    | STACK_ROUTINE_PARAMETER(1, kFourByteCode) /* OMSDevice *device */
                    | STACK_ROUTINE_PARAMETER(2, kTwoByteCode)  /* short devSize     */
};
typedef UniversalProcPtr OMSDvrAdd1DevProc1UPP;
#define NewOMSDvrAdd1DevProc1(userRoutine) \
        (OMSDvrAdd1DevProc1UPP)NewRoutineDescriptor((ProcPtr)(userRoutine), uppOMSDvrAdd1DevProc1Info, GetCurrentArchitecture())
#define CallOMSDvrAdd1DevProc1(userRoutine, device, devSize) \
        (OMSDeviceH)CallUniversalProc((UniversalProcPtr)(userRoutine), uppOMSDvrAdd1DevProc1Info, (device), (devSize))
#else
typedef OMSDvrAdd1DevProc0 OMSDvrAdd1DevProc0UPP;
#define NewOMSDvrAdd1DevProc0(userRoutine) \
        (OMSDvrAdd1DevProc0UPP)(userRoutine)
#define CallOMSDvrAdd1DevProc0(userRoutine, device) \
        (*(userRoutine))((device))
typedef OMSDvrAdd1DevProc1 OMSDvrAdd1DevProc1UPP;
#define NewOMSDvrAdd1DevProc1(userRoutine) \
        (OMSDvrAdd1DevProc1UPP)(userRoutine)
#define CallOMSDvrAdd1DevProc1(userRoutine, device, devSize) \
        (*(userRoutine))((device), (devSize))
#endif

#endif /* USBMIDI9_HOST_CHECK_OMSDRVUPPS_H */

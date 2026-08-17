/* procinfo_check.c — mechanical ProcInfo decodes for the OMS/Mixed Mode
 * UPPs used by USBMIDI9.
 *
 * Compile:  cc -o procinfo_check procinfo_check.c
 * Run:      ./procinfo_check
 *
 * The macros below are the authentic UI 3.3.2 MixedMode.h definitions
 * (verbatim from the mirror; see docs/re/mixed-mode.md). The program
 * recomputes the ProcInfo constants from their compositions and asserts
 * the known values, so a future reader can trust the decodes without
 * re-deriving them:
 *
 *   uppOMSDriverProcInfo      = 0xFB0  (OMSDrvUPPs.h: pascal long
 *     result(4B) | stack params msg(2B), par1(4B), par2(4B))
 *   uppOMSDvrAdd1DevProc1Info = 0x2F0  (OMSDrvUPPs.h: pascal OMSDeviceH
 *     result(4B) | stack params device(4B), devSize(2B))
 *   OMSReceivedFromPort call  = register-based A1(4B) + D0(2B), i.e.
 *     the ProcInfo passed to CallUniversalProc by oms_rx_deliver
 *     (docs/re/mixed-mode.md).
 *
 * Promoted from /tmp/procinfo_check.c (2026-08-16 RE session); extended
 * with the two stack-based decodes.
 */
#include <stdio.h>

typedef unsigned long ProcInfoType;

enum {
    kCallingConventionWidth = 4,
    kResultSizeWidth = 2,
    kStackParameterWidth = 2,
    kRegisterResultLocationWidth = 5,
    kRegisterParameterWidth = 5,
    kRegisterParameterPhase = (kCallingConventionWidth + kResultSizeWidth + kRegisterResultLocationWidth),
    kRegisterParameterSizePhase = 0,
    kRegisterParameterSizeWidth = 2,
    kRegisterParameterWhichPhase = kRegisterParameterSizeWidth,
    kStackParameterPhase = (kCallingConventionWidth + kResultSizeWidth),
    kResultSizePhase = kCallingConventionWidth
};

enum { kPascalStackBased = 0, kRegisterBased = 2, kRegisterD0 = 0, kRegisterA1 = 5 };

#define SIZE_CODE(size) \
    (((size) == 4) ? 3 : (((size) == 2) ? 2 : (((size) == 1) ? 1 : 0)))
#define RESULT_SIZE(size) \
    ((ProcInfoType)SIZE_CODE(size) << kResultSizePhase)
#define STACK_ROUTINE_PARAMETER(whichParam, size) \
    ((ProcInfoType)SIZE_CODE(size) << (kStackParameterPhase + (((whichParam) - 1) * kStackParameterWidth)))
#define REGISTER_ROUTINE_PARAMETER(whichParam, whichReg, sizeCode) \
    ((((ProcInfoType)(sizeCode) << kRegisterParameterSizePhase) | ((ProcInfoType)(whichReg) << kRegisterParameterWhichPhase)) << \
            (kRegisterParameterPhase + (((whichParam) - 1) * kRegisterParameterWidth)))

#define UPP_OMS_DRIVER_PROC \
    (kPascalStackBased \
     | RESULT_SIZE(4) \
     | STACK_ROUTINE_PARAMETER(1, 2) \
     | STACK_ROUTINE_PARAMETER(2, 4) \
     | STACK_ROUTINE_PARAMETER(3, 4))

#define UPP_OMS_DVR_ADD1_DEV_PROC1 \
    (kPascalStackBased \
     | RESULT_SIZE(4) \
     | STACK_ROUTINE_PARAMETER(1, 4) \
     | STACK_ROUTINE_PARAMETER(2, 2))

#define OMS_RECEIVED_FROM_PORT_PROCINFO \
    (kRegisterBased \
     | REGISTER_ROUTINE_PARAMETER(1, kRegisterA1, SIZE_CODE(4)) \
     | REGISTER_ROUTINE_PARAMETER(2, kRegisterD0, SIZE_CODE(2)))

int main(void)
{
    unsigned long upp_driver = (unsigned long)UPP_OMS_DRIVER_PROC;
    unsigned long upp_add1 = (unsigned long)UPP_OMS_DVR_ADD1_DEV_PROC1;
    unsigned long rx = (unsigned long)OMS_RECEIVED_FROM_PORT_PROCINFO;
    printf("uppOMSDriverProcInfo      = 0x%lX (expect 0xFB0)\n", upp_driver);
    printf("uppOMSDvrAdd1DevProc1Info = 0x%lX (expect 0x2F0)\n", upp_add1);
    printf("OMSReceivedFromPort call  = 0x%lX (register-based A1+D0)\n", rx);
    if (upp_driver != 0xFB0 || upp_add1 != 0x2F0) {
        printf("MISMATCH against the SDK-documented values!\n");
        return 1;
    }
    printf("OK\n");
    return 0;
}

/*
 * Experimental PPCC entry representation.  Add this file only to an
 * isolated CodeWarrior target whose PPC Linker Entry points/Main is
 * USBMIDI9OMSMainRD.  It is intentionally not part of production.
 */
#include <MacTypes.h>
#include <MixedMode.h>
#include <OMSDriver.h>

extern OMSCALLBACK(long) main(short msg, long par1, long par2);

RoutineDescriptor USBMIDI9OMSMainRD =
    BUILD_ROUTINE_DESCRIPTOR(uppOMSDriverProcInfo, (ProcPtr)main);


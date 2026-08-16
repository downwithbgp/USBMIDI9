/*
 * Minimal stub of OSUtils.h (Universal Interfaces). NOT the real header —
 * see MacTypes.h in this directory. Only the OS utility calls used by
 * the USBMIDI9 Probe are declared.
 */

#ifndef USBMIDI9_HOST_CHECK_OSUTILS_H
#define USBMIDI9_HOST_CHECK_OSUTILS_H

#include "MacTypes.h"

/* Queue element (OSUtils.h): the NMRec's qLink and QELEM.qType. */
struct QElem {
    struct QElem *qLink;
    short qType;
};
typedef struct QElem QElem;
typedef QElem *QElemPtr;

THz SystemZone(void);
void Delay(UInt32 ticks, UInt32 *finalTicks);
UInt32 Ticks(void);   /* ticks since startup (OSUtils.h) */

#endif /* USBMIDI9_HOST_CHECK_OSUTILS_H */

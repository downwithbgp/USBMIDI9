/*
 * Minimal stub of Memory.h (Universal Interfaces). NOT the real header —
 * see MacTypes.h in this directory. Only the Memory Manager zone calls
 * used by the USBMIDI9 Probe are declared.
 */

#ifndef USBMIDI9_HOST_CHECK_MEMORY_H
#define USBMIDI9_HOST_CHECK_MEMORY_H

#include "MacTypes.h"

THz GetZone(void);
void SetZone(THz zone);

#endif /* USBMIDI9_HOST_CHECK_MEMORY_H */

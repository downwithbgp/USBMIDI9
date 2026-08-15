/*
 * Minimal stub of Events.h (Universal Interfaces). NOT the real header —
 * see MacTypes.h in this directory. Only the keyboard-state call used
 * by the USBMIDI9 Probe is declared.
 */

#ifndef USBMIDI9_HOST_CHECK_EVENTS_H
#define USBMIDI9_HOST_CHECK_EVENTS_H

#include "MacTypes.h"

/* GetKeys: bit N of the 128-bit KeyMap = keycode N. */
typedef UInt8 KeyMap[16];
void GetKeys(KeyMap theKeys);

#endif /* USBMIDI9_HOST_CHECK_EVENTS_H */

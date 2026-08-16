/*
 * Minimal stub of Events.h (Universal Interfaces). NOT the real header —
 * see MacTypes.h in this directory. Only the keyboard-state call used
 * by the USBMIDI9 Probe is declared.
 */

#ifndef USBMIDI9_HOST_CHECK_EVENTS_H
#define USBMIDI9_HOST_CHECK_EVENTS_H

#include "MacTypes.h"

/* GetKeys: the KeyMap is the 128-bit keyboard state as four UInt32s
 * (authentic Events.h). KeyMapByteArray is the authentic byte view of
 * the same 128 bits; for keycode N the real Mac OS 9 GetKeys byte
 * representation is byte N/8 with mask 1 << (N % 8) (verified on the
 * G4). Modeled exactly on the real header (NOT a UInt8[16] KeyMap —
 * that simplification hid a real bit-indexing bug in the probe). */
typedef UInt32 KeyMap[4];
typedef UInt8 KeyMapByteArray[16];
void GetKeys(KeyMap theKeys);

#endif /* USBMIDI9_HOST_CHECK_EVENTS_H */

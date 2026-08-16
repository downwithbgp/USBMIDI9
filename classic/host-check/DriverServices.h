/*
 * Minimal stub of DriverServices.h (Universal Interfaces). NOT the real
 * header — see MacTypes.h in this directory. Only the execution-level
 * surface used by the USBMIDI9 safe bulk-read helper is declared.
 * Names/signatures verified against the authentic DDK 1.4.1 kit:
 *   - Rev 26 "Asynchronous Call Support" (p. 102): the driver services
 *     call CurrentExecutionLevel returns kHardwareInterruptLevel /
 *     kSecondaryInterruptLevel / kTaskLevel; CallSecondaryInterruptHandler2
 *     continues execution at secondary interrupt level.
 *   - PrinterClassDriver.c and USBModem/ModemDriver.c (both include
 *     <DriverServices.h>) call
 *     CallSecondaryInterruptHandler2(handler, nil, pb, &result) with a
 *     handler of the exact shape below; the USL result comes back in
 *     *result and the trampoline's own return is ignored.
 *   - USBModem/ShimSerialHAL.c: err = CurrentExecutionLevel();
 *     if (err) -> not task time (kTaskLevel == 0).
 * Only kTaskLevel == 0 is value-verified from the kit; the other two
 * values are the standard Driver Services constants (the driver only
 * compares against kTaskLevel, so they are not load-bearing here).
 * The real header types the handler via a proc-ptr typedef whose name is
 * not verifiable from the kit, so the stub uses the sample-verified raw
 * function-pointer shape.
 */

#ifndef USBMIDI9_HOST_CHECK_DRIVERSERVICES_H
#define USBMIDI9_HOST_CHECK_DRIVERSERVICES_H

#include "MacTypes.h"

enum {
    kTaskLevel               = 0,  /* task level (ShimSerialHAL.c usage) */
    kHardwareInterruptLevel  = 1,
    kSecondaryInterruptLevel = 2
};

OSStatus CurrentExecutionLevel(void);
OSStatus CallSecondaryInterruptHandler2(
    OSStatus (*theHandler)(void *callerRefCon, void *result),
    void *refCon, void *callerRefCon, void *result);

#endif /* USBMIDI9_HOST_CHECK_DRIVERSERVICES_H */

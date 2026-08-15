/*
 * Minimal stub of MacErrors.h (Universal Interfaces). NOT the real
 * header — see MacTypes.h in this directory. Error values verified
 * against the MacErrors.h snapshot at ~/research/usbddk/media/ (USB
 * error block unchanged since the DDK era) and docs/classic-usb-driver.md
 * §8.1.
 */

#ifndef USBMIDI9_HOST_CHECK_MACERRORS_H
#define USBMIDI9_HOST_CHECK_MACERRORS_H

#include "MacTypes.h"

#define noErr 0
#define kUSBNoErr 0
#define paramErr (-50)

/* USB errors (MacErrors.h): */
#define kUSBPending              1      /* call in progress */
#define kUSBOverRunErr          (-6908) /* packet too large / more data than buffer */
#define kUSBNotRespondingErr    (-6911) /* pipe stall, no device, device hung */
#define kUSBDeviceDisconnected  (-6972) /* disconnected during suspend or reset */
#define kUSBDeviceBusy          (-6977) /* device is already being configured */
#define kUSBPipeStalledError    (-6979) /* pipe has stalled */
#define kUSBAbortedError        (-6982) /* pipe aborted */
#define kUSBNotFound            (-6987) /* not found */
#define kUSBInternalErr         (-6999) /* internal error */

#endif /* USBMIDI9_HOST_CHECK_MACERRORS_H */

/*
 * USBMIDI9 Classic Mac OS USB transport layer.
 *
 * This layer will bridge the portable USB-MIDI core (core/) to the Classic
 * Mac OS 9 USB stack: device/interface discovery and matching, descriptor
 * acquisition, bulk endpoint I/O, completion callbacks, hotplug/device
 * removal, and the memory/lifetime constraints of the Classic environment.
 *
 * STATUS: scaffolding only. No Classic Mac OS USB API has been verified yet,
 * and nothing here may use unverified APIs. In particular, Mac OS X/macOS
 * APIs (IOKit, IOUSBDeviceInterface, IOUSBInterfaceInterface, DriverKit,
 * ...) are presumptively wrong for this target.
 *
 * TODO(classic-usb-api):
 * Determine the verified Classic Mac OS 9 USB API from Apple USB DDK/SDK
 * documentation before implementing anything. See docs/research.md for the
 * open research checklist.
 */

#ifndef USBMIDI9_CLASSIC_USB_DRIVER_H
#define USBMIDI9_CLASSIC_USB_DRIVER_H

#endif /* USBMIDI9_CLASSIC_USB_DRIVER_H */

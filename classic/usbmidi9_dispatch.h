/*
 * USBMIDI9 driver dispatch table ABI.
 *
 * The USBMIDI9 interface class driver exports one driver-specific symbol
 * beyond the two Apple-required ones: `USBMIDI9DispatchTable`, a versioned
 * table of function pointers through which client code (the USBMIDI9
 * Probe, and later OMS/FreeMIDI shims) talks to the driver. This is the
 * documented Apple mechanism for class-driver <-> client communication
 * (Mac OS USB DDK API Reference, Rev. 26, Ch 4 "Communicating With Client
 * Processes", p. 72-75; HID dispatch-table precedent, p. 76).
 *
 * The table provides ONLY:
 *   - a version field
 *   - enumeration of the attached USBMIDI9 interfaces
 *   - basic interface/endpoint information per interface
 *   - dequeue of raw received bytes (polled by the client)
 *
 * The exported symbol is declared `struct USBMIDI9DispatchTable
 * USBMIDI9DispatchTable` (a variable in the tag namespace, since a
 * variable cannot share a name with a typedef in the same scope).
 *
 * All procs are called from the client at task time and run in the
 * driver's context. Byte counts are UInt32; indices are 0-based and
 * stable only until an interface is removed.
 */

#ifndef USBMIDI9_CLASSIC_USBMIDI9_DISPATCH_H
#define USBMIDI9_CLASSIC_USBMIDI9_DISPATCH_H

#include <MacTypes.h>

/* Current table version. Clients must check `version` before calling
 * through the table and must not call procs that postdate the version
 * they understand. */
#define kUSBMIDI9DispatchTableVersion 0x0001u

/* Basic interface/endpoint information for one attached interface.
 * Filled by USBMIDI9EnumerateInterfaces / USBMIDI9GetInterfaceInfo.
 * Endpoint info is the bulk IN pipe's MaxPacketSize (the endpoint
 * address itself is not returned by the verified USL calls used by the
 * driver; see docs/classic-usb-driver.md §5.3). */
struct USBMIDI9InterfaceInfo {
    UInt32 index;              /* 0-based instance index */
    UInt16 vendorID;           /* idVendor of the device (host order) */
    UInt16 productID;          /* idProduct of the device (host order) */
    UInt32 interfaceNum;       /* bInterfaceNumber */
    UInt32 interfaceClass;     /* bInterfaceClass (0x01 = Audio) */
    UInt32 interfaceSubClass;  /* bInterfaceSubClass (0x03 = MIDIStreaming) */
    UInt32 interfaceProtocol;  /* bInterfaceProtocol */
    UInt32 maxPacketSize;      /* bulk IN endpoint MaxPacketSize, e.g. 64 */
    UInt32 availableBytes;     /* bytes currently queued, ready to dequeue */
};

struct USBMIDI9DispatchTable {
    UInt32 version;  /* kUSBMIDI9DispatchTableVersion */

    /* Fill outArray with up to maxCount interface records and return the
     * total number of attached interfaces in *outCount (not capped by
     * maxCount). Pass maxCount == 0 to query only the count. Returns
     * paramErr on a nil outCount, or when maxCount > 0 and outArray is
     * nil. */
    OSStatus (*enumerateInterfaces)(struct USBMIDI9InterfaceInfo *outArray,
                                    UInt32 maxCount, UInt32 *outCount);

    /* Fill outInfo for the interface at the given 0-based index. Returns
     * kUSBNotFound if the index is out of range, paramErr on nil outInfo. */
    OSStatus (*getInterfaceInfo)(UInt32 index,
                                 struct USBMIDI9InterfaceInfo *outInfo);

    /* Copy up to maxBytes of raw received bytes out of the interface's
     * resident ring buffer into buffer; returns the number of bytes
     * copied (0 when empty or the index is out of range). Non-blocking:
     * the client polls. */
    UInt32 (*dequeueBytes)(UInt32 index, void *buffer, UInt32 maxBytes);
};

#endif /* USBMIDI9_CLASSIC_USBMIDI9_DISPATCH_H */

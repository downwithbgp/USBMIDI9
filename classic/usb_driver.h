/*
 * USBMIDI9 Classic Mac OS USB interface class driver.
 *
 * A generic USB-MIDI 1.0 interface driver: matched as an interface driver
 * for bInterfaceClass 0x01 (Audio) / bInterfaceSubClass 0x03
 * (MIDIStreaming) with no vendor/product restriction. On initialization it
 * configures the interface, finds the bulk IN pipe, determines
 * MaxPacketSize, allocates resident buffers, and starts an asynchronous
 * bulk-read loop. Received bytes are copied into a fixed resident ring
 * buffer; client code (the Probe) dequeues them through the exported
 * USBMIDI9DispatchTable.
 *
 * The driver model, lifecycle, and every API used here are verified
 * against primary Apple documentation (Mac OS USB DDK API Reference
 * Rev. 26, and the authentic Mac OS USB DDK 1.4.1 kit); see
 * docs/classic-usb-driver.md for citations.
 *
 * This file is compiled on the Power Mac G4 (CodeWarrior) against the
 * Universal Headers + DDK USB.h. On Linux it is compile-checked against
 * the minimal stub headers in host-check/ (make check-classic).
 */

#ifndef USBMIDI9_CLASSIC_USB_DRIVER_H
#define USBMIDI9_CLASSIC_USB_DRIVER_H

#include <MacTypes.h>
#include <MacErrors.h>
#include <USB.h>

#include "usbmidi9_dispatch.h"
#include "ring.h"

/* Generic USB-MIDI interface match (USB Audio 1.0 / USB-MIDI 1.0). */
#define kUSBMIDI9InterfaceClass    0x01u
#define kUSBMIDI9InterfaceSubClass 0x03u

/* Driver name for the Name Registry. CodeWarrior supports Pascal string
 * literals ("\p..."); other compilers (the Linux host check) get a plain
 * C string. MPW (SC) builds would need the same treatment as MWERKS. */
#if defined(__MWERKS__)
#define kUSBMIDI9DriverName "\pUSBMIDI9"
#else
#define kUSBMIDI9DriverName "USBMIDI9"
#endif

/* Fixed limits (documented design choices, not device requirements). */
#define kUSBMIDI9MaxInterfaces 8u     /* max simultaneously handled interfaces */
#define kUSBMIDI9RingSize       4096u /* resident receive ring per interface */
#define kUSBMIDI9ReadAlignment 64u    /* MaxPacketSize alignment (performance) */
#define kUSBMIDI9MaxSyncDepth 16u     /* re-entrancy bound on synchronous USL returns */

/* Asynchronous state machine stages (pb.usbRefcon selector) and flags,
 * following the verified DDK sample pattern (KeyboardModule.h): one
 * USBPB per instance, refcon = stage + kCompletionPending while a USL
 * call is in flight; the completion routine advances the stage and
 * re-enters the machine. */
enum {
    kAllocRingState = 0,        /* USBAllocMem: resident ring buffer */
    kConfigureInterfaceState,   /* USBConfigureInterface: open pipes */
    kFindBulkInPipeState,       /* USBFindNextPipe: bulk IN pipe + MaxPacketSize */
    kAllocReadBufferState,      /* USBAllocMem: aligned receive buffer */
    kReadBulkInPipeState,       /* USBBulkRead loop */
    kStageMask         = 0x00ff,
    kReturnFromDriver  = 0x1000,
    kCompletionPending = 0x8000
};

/* One driver instance per matched interface (allocated from a fixed
 * static registry; no heap allocation at interrupt time). The USBPB must
 * be the first member: completion routines cast the PB back to the
 * instance, and pb.pbLength is set to sizeof(usbmidi9_instance) per the
 * DDK sample pattern. */
typedef struct usbmidi9_instance {
    USBPB  pb;                       /* must be first; pbLength = sizeof(instance) */
    USBInterfaceRef interfaceRef;    /* from initializeInterfaceProc */
    USBPipeRef bulkInPipe;           /* from USBFindNextPipe (0 until found) */
    UInt32 interfaceNum;             /* bInterfaceNumber */
    UInt32 interfaceClass;           /* bInterfaceClass */
    UInt32 interfaceSubClass;        /* bInterfaceSubClass */
    UInt32 interfaceProtocol;        /* bInterfaceProtocol */
    UInt16 vendorID;                 /* idVendor, host order (USBToHostWord) */
    UInt16 productID;                /* idProduct, host order */
    UInt16 maxPacketSize;            /* bulk IN MaxPacketSize (from USBFindNextPipe) */
    Ptr   readBufferBase;            /* raw USBAllocMem block (for dealloc) */
    Ptr   readBuffer;                /* 64-byte aligned receive buffer */
    Ptr   ringBuffer;                /* resident ring storage (USBAllocMem) */
    unsigned volatile ringHead;      /* dequeue index (client side) */
    unsigned volatile ringTail;      /* enqueue index (completion side) */
    OSStatus lastReadStatus;         /* last read completion status (debug) */
    UInt32 lastReadCount;            /* last read completion byte count (debug) */
    UInt32 callToken;                /* incremented before every USL call */
    UInt32 callInFlight;             /* token of the call not yet completed (0 = none) */
    UInt32 transDepth;               /* InitiateTransaction re-entrancy depth */
    Boolean removing;                /* set at removal: stop resubmission */
    USBMIDI9EventCallbackProcPtr eventCallback; /* optional client push hook */
    UInt32 eventRefcon;              /* refcon passed to the event callback */
} usbmidi9_instance;

#endif /* USBMIDI9_CLASSIC_USB_DRIVER_H */

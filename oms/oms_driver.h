/*
 * USBMIDI9 OMS driver shim — shared declarations.
 *
 * This component is an OMS hardware driver for the USBMIDI9 transport:
 * a file of type 'OMdv' (creator 'USM9') living in System Folder:OMS
 * Folder, whose 'OMdv' 128 code resource (a PEF container) implements the
 * OMS driver contract (OMSDriver.h / the OMS Programming Interface spec,
 * Mar 1995, "OMS Drivers" chapter — see docs/research.md "OMS" and
 * ~/research/oms/PROVENANCE.md).
 *
 * The shim contains NO USB data-path code: it consumes the
 * USBMIDI9DispatchTable (enumeration + dequeue) exactly like the Probe.
 * The only USB-adjacent call is the driver-lookup pattern
 * USBGetNextDeviceByClass + FindSymbol, which is precisely what Opcode's
 * own OMS 2.3.8 "OMS USB Manager" does (verified PEF import list).
 *
 * Driver identity:
 *   - signature/creator 'USM9'
 *   - 'OMdi' 128: id 0x7F10 (unassigned in the inspected OMS 2.3.8 driver
 *     set; load order only), flags 0, driverCompatibilityLevel 1
 *     (OMS 2.0+ driver API)
 *   - one OMSDevice per attached USBMIDI9 interface (whichOut =
 *     interface index + 1); OMSPortID.whichPort = cable number
 */

#ifndef USBMIDI9_OMS_OMS_DRIVER_H
#define USBMIDI9_OMS_OMS_DRIVER_H

#include <MacTypes.h>
#include <MacErrors.h>
#include <OMS.h>
#include <OMSDriver.h>
#include <Notifications.h>

#include "usbmidi9_dispatch.h"
#include "core/midi_stream.h"

/* Driver signature: file creator AND OMSPortID.driverID.
 * FOUR_CHAR_CODE('USM9') written numerically (the host check has no
 * multichar constants; same convention as the USB stub headers). */
#define kUSBMIDI9OMSDriverSignature 0x55534D39u

/* 'OMdi' 128 values (OMSDriverParams). */
#define kUSBMIDI9OMSDriverId        0x7F10
#define kUSBMIDI9OMSCompatLevel     1u
#define kUSBMIDI9OMSFlags           0x00u

/* Limits (mirror the USB driver registry + the USB-MIDI cable field). */
#define kUSBMIDI9OMSMaxInterfaces   8u
#define kUSBMIDI9OMSCables          16u

/* Poll period in ticks (1 tick ~ 16.7 ms). The Notification Manager is
 * the verified period precedent: Opcode's OMS USB Manager imports
 * NMInstall/NMRemove. */
#define kUSBMIDI9OMSPollTicks       1u

/* Bytes dequeued per interface per poll (16 Event Packets). */
#define kUSBMIDI9OMSDequeueChunk    64u

/* One cable's port state: OMS receive refnum + the neutral rx stream. */
struct oms_port {
    short ioRefNum;                  /* omdvSetPortReceiveRefNum; <0 = off */
    unsigned char valid;
    struct um9_rx_stream rx;         /* per-cable receive stream */
    struct um9_tx_stream tx;         /* per-cable transmit stream */
    unsigned char txCarry[3];        /* SysEx chunk re-chunking carry */
    unsigned char txCarryLen;        /* 0..2 */
};

/* One attached USBMIDI9 interface = one OMS device. */
struct oms_iface {
    unsigned char valid;
    struct USBMIDI9InterfaceInfo info;
    struct oms_port ports[kUSBMIDI9OMSCables];
};

/* The driver's global state (single instance; a CFM fragment owns its
 * data section). */
struct oms_state {
    struct USBMIDI9DispatchTable *table;   /* cached; re-located per poll */
    unsigned char midiStarted;
    unsigned char timerRunning;
    unsigned short nInterfaces;
    void *interfaceList;                   /* omdvSetInterfaceList handle;
                                              remembered, never touched */
    struct oms_iface ifaces[kUSBMIDI9OMSMaxInterfaces];
    struct NMRec nmRec;                    /* poll timer record */
    unsigned long rxPackets, rxMessages, rxDropped;
    unsigned long txConverted, txDropped, txMalformed;
    unsigned long relocates;               /* dispatch re-locations */
    unsigned long locateFailures;
};

extern struct oms_state g_oms;

/* Locate the USBMIDI9 dispatch table (USBGetNextDeviceByClass +
 * FindSymbol, the Probe's verified pattern). Returns noErr and caches
 * the table, or an error when no USBMIDI9 driver is attached. */
OSErr oms_locate_dispatch(void);

/* The omdv* message switch (called by `main`). */
long oms_handle_message(short msg, long par1, long par2);

/* Drain one interface's ring and deliver messages to OMS. Called from
 * the poll task. */
void oms_rx_drain(unsigned ifaceIndex);

/* The Notification Manager poll task (periodic; drains all started
 * interfaces and resubmits itself while MIDI is running; pascal on the
 * G4, per the Notifications.h NMProcPtr convention). */
OMSCALLBACK(void) oms_poll_task(UInt32 nMessage, UInt32 nRefCon);

/* The OMSReadHook2 send proc returned by omdvGetPortSendProc (pascal on
 * the G4, per the OMSDrvUPPs.h convention). */
OMSCALLBACK(void) oms_tx_send(OMSMIDIPacket *pkt, long refCon);

/* Transport seam for converted 4-byte USB-MIDI Event Packets.
 * v0.1 default: no bulk-OUT path exists in the dispatch table (no
 * enqueueBytes); the default implementation drops and counts.
 * TODO(oms-output): enqueueBytes + USB bulk-OUT, UNVERIFIED until the
 * G4 gate. */
void oms_tx_transport_send(unsigned portCode, const unsigned char pkt[4]);

#endif /* USBMIDI9_OMS_OMS_DRIVER_H */

/*
 * USBMIDI9 OMS driver shim — shared declarations.
 *
 * This component is an OMS hardware driver for the USBMIDI9 transport:
 * a file of type 'OMdv' (creator 'USM9') living in System Folder:OMS
 * Folder, whose 'PPCC' 1 code resource (the raw Target-A PEF container;
 * see oms/ppcc.r + docs/g4-handoff.md) implements the OMS driver
 * contract (OMSDriver.h / the OMS Programming Interface spec, Mar 1995,
 * "OMS Drivers" chapter — see docs/research.md "OMS" and
 * ~/research/oms/PROVENANCE.md).
 *
 * The shim contains NO USB data-path code: it consumes the
 * USBMIDI9DispatchTable (enumeration + dequeue) exactly like the Probe.
 * The USB-adjacent calls are the driver-lookup pattern
 * USBGetNextDeviceByClass + FindSymbol at lifecycle transitions and the
 * USB Manager device-notification API (USBInstallDeviceNotification /
 * USBRemoveDeviceNotification) — precisely what Opcode's own OMS 2.3.8
 * "OMS USB Manager" does (verified PEF import list; see
 * docs/research.md "OMS" and docs/host-check-audit.md).
 *
 * Receive scheduling (v0.1 audit correction): there is NO periodic poll
 * task. The class driver's read completion invokes the shim's event
 * callback (dispatch table v0x0002 setEventCallback) at interrupt level
 * right after bytes land in the ring; the callback drains the ring
 * through dequeueBytes and delivers via the cached 68K
 * OMSReceivedFromPort address (interrupt-level legal per the OMS Spec;
 * PPC bridge: OMSGetCallAddress + CallUniversalProc, see oms_rx.c). The Notification Manager is NOT used (its
 * NMInstall/NMRemove are an alert API, not a timer — verified by
 * disassembly of Opcode's own USB components).
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
#include <USB.h>

#include "usbmidi9_dispatch.h"
#include "midi_stream.h"   /* via {Project}::core: on the G4; -Icore here */

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

/* Bytes dequeued per interface per event (16 Event Packets). */
#define kUSBMIDI9OMSDequeueChunk    64u

/* ProcInfo for the cached 68K OMSReceivedFromPort routine, per the
 * authenticated ABI: kRegisterBased calling convention, param 1 =
 * OMSPacket * in A1, param 2 = short destRefNum in D0 (OMS Spec "68K
 * assembly language routine": A1 -> pkt, D0 = sourceIORefNum; Apple
 * MixedMode.h: kRegisterD0 = 0, kRegisterA1 = 5; SIZE_CODE/
 * REGISTER_ROUTINE_PARAMETER verbatim from UI 3.3.2). Evaluates to
 * 0x2B802 on the G4 (4-byte pointers, sizeof(short)=2); the value on
 * the Linux host differs (8-byte pointers) — the host cannot validate
 * real register dispatch (see host-check/MixedMode.h). */
#define kOMSReceivedFromPortProcInfo \
    (kRegisterBased \
     | REGISTER_ROUTINE_PARAMETER( \
           1, kRegisterA1, SIZE_CODE(sizeof(OMSPacket *))) \
     | REGISTER_ROUTINE_PARAMETER( \
           2, kRegisterD0, SIZE_CODE(sizeof(short))))

/* Minimum dispatch table version the shim accepts: it must provide the
 * v0x0002 setEventCallback entry (the receive push hook). A v1 driver
 * is rejected but the shim stays alive for replug. */
#define kUSBMIDI9OMSDispatchMinVersion 0x0002u

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
    struct USBMIDI9DispatchTable *table;  /* cached; valid only while the
                                             class driver fragment is
                                             loaded (attach/detach) */
    USBDeviceRef deviceRef;               /* the located driver's device */
    unsigned char midiStarted;
    unsigned char notifierInstalled;      /* USB notification installed */
    unsigned char callbackRegistered;     /* event callback in the table */
    unsigned short nInterfaces;
    void *interfaceList;                  /* omdvSetInterfaceList handle;
                                              remembered, never touched */
    struct oms_iface ifaces[kUSBMIDI9OMSMaxInterfaces];
    struct USBDeviceNotificationParameterBlock notifPb;
    OMSReadHook2UPP sendUpp;          /* send-proc RoutineDescriptor
                                         (NewOMSReadHook2 at omdvInit;
                                         disposed at re-init and
                                         omdvDispose; handed out by
                                         omdvGetPortSendProc) */
    long rxRoutine;                       /* cached 68K OMSReceivedFromPort
                                             address (OMSGetCallAddress
                                             return type, long); 0 =
                                             unresolved -> receive delivery
                                             disabled (drain continues) */
    unsigned long rxPackets, rxMessages, rxDropped;
    unsigned long txConverted, txDropped, txMalformed;
    unsigned long relocates;              /* dispatch re-locations */
    unsigned long locateFailures;
    unsigned long events;                 /* event-callback invocations */
};

extern struct oms_state g_oms;

/* Locate the USBMIDI9 dispatch table (USBGetNextDeviceByClass +
 * FindSymbol, the Probe's verified pattern) and register the receive
 * event callback. Called ONLY at lifecycle transitions: omdvInit,
 * omdvAddDevices, omdvStartMIDI2, and device-add notifications. Returns
 * noErr and caches the table, or an error when no USBMIDI9 driver is
 * attached or its table is too old. */
OSErr oms_locate_dispatch(void);

/* The omdv* message switch (called by `main`). */
long oms_handle_message(short msg, long par1, long par2);

/* The class driver's event callback (v0x0002 push hook): runs inside
 * the driver's read completion at (secondary) interrupt level. Drains
 * the interface's ring and delivers messages to OMS. Must not call any
 * task-time API and must not re-enter the dispatch table except via
 * dequeueBytes. */
void oms_rx_event(UInt32 ifaceIndex, UInt32 refcon);

/* Drain one interface's ring until empty. `deliver` = 1 hands decoded
 * messages to OMS; 0 drops them (stale backlog). Called from the event
 * callback and from attach/startMIDI2. */
void oms_rx_drain(unsigned ifaceIndex, unsigned deliver);

/* Drain every valid interface until its ring is empty. */
void oms_rx_drain_all(unsigned deliver);

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

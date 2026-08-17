/*
 * USBMIDI9 OMS driver shim — omdv* message dispatch and device
 * registration.
 *
 * Implements the OMS hardware-driver contract (OMSDriver.h; OMS
 * Programming Interface spec, Mar 1995, "OMS Drivers"):
 *
 *   OMSCALLBACK(long) main(short msg, long par1, long par2);
 *
 * with driverCompatibilityLevel 1 (OMS 2.0+). All message numbers,
 * parameter meanings, and return conventions below are cited to the SDK
 * headers / the Spec; see docs/research.md "OMS" for the provenance.
 *
 * The driver returns zero for every message unless a specific value is
 * appropriate (Spec: "The driver must return zero in response to any
 * message, except when the driver is returning a specific value which is
 * appropriate for the message it received.").
 *
 * Lifecycle (v0.1 audit correction — no per-tick USB activity):
 *   - the dispatch table is located ONLY at lifecycle transitions
 *     (omdvInit / omdvAddDevices / omdvStartMIDI2) and on device-add
 *     notifications. There is NO periodic poll task.
 *   - attach/detach of the USBMIDI9 class driver is learned from the
 *     USB Manager's device notification (USBInstallDeviceNotification;
 *     Rev 26 Ch 4: "Use the USBInstallDeviceNotification mechanism to be
 *     alerted when a device or interface is added or removed" — the
 *     same API the real Opcode OMS USB Manager imports). A matching
 *     kNotifyRemove* drops the cached dispatch pointer before the class
 *     driver fragment unloads; kNotifyAdd* re-locates through the
 *     notification's deviceRef (USBGetDriverConnectionID + FindSymbol,
 *     the Opcode OMS USB Manager's own pattern), with a
 *     USBGetNextDeviceByClass walk only as the init fallback.
 *   - receive is push-based: the class driver's read completion invokes
 *     oms_rx_event (dispatch table v0x0002 setEventCallback), which
 *     drains the ring and delivers via the cached 68K
 *     OMSReceivedFromPort address (PPC bridge: resolved once at
 *     omdvInit via LinkToOMSGlue + OMSGetCallAddress, invoked through
 *     CallUniversalProc; interrupt-level legal per the Spec).
 */

#include <MacTypes.h>
#include <MacErrors.h>
#include <stddef.h>
#include <USB.h>
#include <CodeFragments.h>
#include <Memory.h>
#include <OSUtils.h>
#include <OMS.h>
#include <OMSDriver.h>
#include <OMSGlueProcs.h>       /* callOMSReceivedFromPort (authentic
                                   generated header; SDK Headers/) */

#include "oms_driver.h"

/* Exported symbol name of the USBMIDI9 dispatch table. CodeWarrior
 * supports Pascal string literals; other compilers (Linux host check)
 * get a plain C string. */
#if defined(__MWERKS__)
#define kOmsDispatchSymbolName "\pUSBMIDI9DispatchTable"
#else
#define kOmsDispatchSymbolName "USBMIDI9DispatchTable"
#endif

/* Generic USB-MIDI interface match (mirrors the driver description and
 * the Probe; also the device-notification class/subclass filter). */
#define kOmsInterfaceClass    0x01u
#define kOmsInterfaceSubClass 0x03u

struct oms_state g_oms;

/* Zero `n` bytes (portable memset replacement). */
static void oms_zero(void *p, unsigned n)
{
    unsigned char *b = (unsigned char *)p;

    while (n-- > 0u) {
        *b++ = 0u;
    }
}

/* Reset per-port stream state. Called when the transport (re)appears so
 * a mid-SysEx unplug cannot leave stale continuation state behind. */
static void oms_reset_ports(void)
{
    unsigned i;
    unsigned c;

    for (i = 0u; i < kUSBMIDI9OMSMaxInterfaces; i++) {
        for (c = 0u; c < kUSBMIDI9OMSCables; c++) {
            struct oms_port *port = &g_oms.ifaces[i].ports[c];
            if (port->valid) {
                um9_rx_init(&port->rx, c);
                um9_tx_init(&port->tx, c);
                port->txCarryLen = 0u;
            }
        }
    }
}

/* Build the Pascal-string device name "USBMIDI9 Port N" (N = 1-based
 * whichOut; max 31 chars per OMSString). No toolbox dependencies. */
static void oms_make_port_name(OMSString name, unsigned n)
{
    static const char prefix[] = "USBMIDI9 Port ";
    unsigned len = 0u;

    while (prefix[len] != '\0' && len < OMS_STRING_LEN) {
        name[len + 1u] = (unsigned char)prefix[len];
        len++;
    }
    name[len + 1u] = (unsigned char)('0' + (n % 10u));
    len++;
    name[0] = (unsigned char)len;
}

/* Copy a C literal into a Pascal string field, clamped to `max` chars
 * (the field's length limit: OMS_STRING_LEN for devName/locationName,
 * OD_MAX_MANUF_LEN / OD_MAX_MODEL_LEN for manuf/model). The target is a
 * plain pointer: the write bound is max + 1 (length byte), which each
 * caller's array satisfies. */
static void oms_pstr_set_n(unsigned char *name, const char *lit, unsigned max)
{
    unsigned len = 0u;

    while (lit[len] != '\0' && len < max) {
        name[len + 1u] = (unsigned char)lit[len];
        len++;
    }
    name[0] = (unsigned char)len;
}

/* Register the push hook for every interface index the driver reports.
 * Interfaces that appear later are registered at the next omdvAddDevices
 * (M1B single-device scope). */
static void oms_register_callbacks(struct USBMIDI9DispatchTable *table)
{
    UInt32 i;

    g_oms.callbackRegistered = 0u;
    for (i = 0u; i < kUSBMIDI9OMSMaxInterfaces; i++) {
        if (table->setEventCallback(i, oms_rx_event, 0u) == noErr) {
            g_oms.callbackRegistered = 1u;
        }
    }
}

/* Bind the shim to a located class driver: version-gate the table,
 * cache it, reset stream state (a bind only ever happens from
 * no-table), register the push hook, and drain the backlog (delivered
 * only if MIDI is running). */
static OSErr oms_bind_dispatch(USBDeviceRef deviceRef,
                               CFragConnectionID connID)
{
    THz currentZone;
    CFragSymbolClass symClass;
    struct USBMIDI9DispatchTable *table;
    OSErr err;

    table = NULL;
    /* Class drivers load in the System Zone; look up the symbol
     * there (Rev 26 Ch 4 p. 83-85; HIDReader.c pattern). */
    currentZone = GetZone();
    SetZone(SystemZone());
    err = FindSymbol(connID, kOmsDispatchSymbolName, (Ptr *)&table,
                     &symClass);
    SetZone(currentZone);
    if (err != noErr || table == NULL) {
        g_oms.locateFailures++;
        return (err != noErr) ? err : kUSBBadDispatchTable;
    }
    /* The shim needs the v0x0002 push hook; a v1 driver is rejected
     * but the shim stays alive for a later replug. */
    if (table->version < kUSBMIDI9OMSDispatchMinVersion
        || table->setEventCallback == NULL) {
        g_oms.locateFailures++;
        return kUSBBadDispatchTable;
    }
    g_oms.table = table;
    g_oms.deviceRef = deviceRef;
    g_oms.relocates++;
    oms_reset_ports();
    oms_register_callbacks(table);
    /* Backlog that arrived before the hook was registered: deliver if
     * MIDI is running, otherwise discard it below. */
    oms_rx_drain_all(g_oms.midiStarted);
    return noErr;
}

/* Attach via the device notification's deviceRef: no device-list walk
 * (the Opcode OMS USB Manager's AddDeviceOrInterface pattern). */
static OSErr oms_attach_device(USBDeviceRef deviceRef)
{
    CFragConnectionID connID;

    if (g_oms.table != NULL) {
        return noErr;               /* already attached */
    }
    if (USBGetDriverConnectionID(&deviceRef, &connID) != noErr) {
        return kUSBNotFound;
    }
    return oms_bind_dispatch(deviceRef, connID);
}

/* Locate the USBMIDI9 dispatch table via USBGetNextDeviceByClass +
 * FindSymbol (the Probe's verified pattern; Rev 26 Ch 4 "to determine
 * the presence of a device"). Called ONLY at lifecycle transitions:
 * omdvInit, omdvAddDevices, omdvStartMIDI2. Never per tick. */
OSErr oms_locate_dispatch(void)
{
    USBDeviceRef deviceRef;
    CFragConnectionID connID;

    if (g_oms.table != NULL) {
        return noErr;
    }
    deviceRef = kNoDeviceRef;
    for (;;) {
        if (USBGetNextDeviceByClass(&deviceRef, &connID,
                                    kOmsInterfaceClass,
                                    kOmsInterfaceSubClass,
                                    kUSBAnyProtocol) != noErr) {
            g_oms.locateFailures++;
            return kUSBNotFound;    /* no (more) matching driver */
        }
        if (oms_bind_dispatch(deviceRef, connID) == noErr) {
            return noErr;
        }
        /* Not our driver (no USBMIDI9DispatchTable symbol) or a too-old
         * table: try the next USB-MIDI device. */
    }
}

/* Drop the cached dispatch pointer: the class driver fragment is about
 * to unload (kNotifyRemove* precedes the driver's finalize), so any
 * cached pointer would dangle. OMS re-scans via omdvAddDevices when the
 * user re-examines the studio setup. */
static void oms_detach(void)
{
    g_oms.table = NULL;
    g_oms.deviceRef = kNoDeviceRef;
    g_oms.callbackRegistered = 0u;
    g_oms.nInterfaces = 0u;
    oms_zero(g_oms.ifaces, sizeof(g_oms.ifaces));
}

/* Refresh the cached dispatch table; returns noErr when usable. */
static OSErr oms_ensure_dispatch(void)
{
    if (g_oms.table == NULL) {
        return oms_locate_dispatch();
    }
    return noErr;
}

/* USB Manager device notification (task time; Rev 26 Ch 4/6, and the
 * same event values the Opcode OMS USB Manager switches on). Installed
 * at omdvInit; the USB Manager may call it synchronously during
 * installation for an already-attached device. The parameter is void *
 * exactly as the authentic DDK declares
 * USBDeviceNotificationCallbackProcPtr (USB.h 1.4.1); the pb type is
 * recovered by cast. */
static void oms_usb_notify(void *pbv)
{
    USBDeviceNotificationParameterBlock *pb =
        (USBDeviceNotificationParameterBlock *)pbv;

    if (pb == NULL) {
        return;
    }
    switch (pb->usbDeviceNotification) {
    case kNotifyAddDevice:
    case kNotifyAddInterface:
        (void)oms_attach_device(pb->usbDeviceRef);
        break;
    case kNotifyRemoveDevice:
    case kNotifyRemoveInterface:
        if (g_oms.table != NULL && pb->usbDeviceRef == g_oms.deviceRef) {
            oms_detach();
        }
        break;
    default:
        break;
    }
}

/* --- omdvInit / omdvDispose ------------------------------------------ */

static OSErr oms_init(OMSFile *file)
{
    (void)file;                         /* driver location; not needed */

    /* Re-entry guard: a second init must remove the first notification
     * (the token lives in notifPb; zeroing first would leak it) and
     * release the send-proc RoutineDescriptor (oms_zero would drop the
     * pointer; dispose it before it is lost). */
    if (g_oms.notifierInstalled) {
        (void)USBRemoveDeviceNotification(g_oms.notifPb.token);
        g_oms.notifierInstalled = 0u;
    }
    if (g_oms.sendUpp != NULL) {
        DisposeRoutineDescriptor(g_oms.sendUpp);
        g_oms.sendUpp = NULL;
    }
    oms_zero(&g_oms, sizeof(g_oms));

    /* PPC: build the send-proc RoutineDescriptor once per init
     * (NewOMSReadHook2 -> NewRoutineDescriptor, the authentic OMSUPPs.h
     * constructor; it may allocate, so this is task time, never
     * omdvGetPortSendProc). The generated OMSUPPs.h ships no Dispose
     * wrapper; the re-entry guard above and omdvDispose release the
     * descriptor with the underlying Mixed Mode Manager call
     * (DisposeRoutineDescriptor, UI 3.3.2) so repeated omdvInit never
     * leaks. omdvGetPortSendProc hands this UPP out and OMS invokes it
     * through CallOMSReadHook2. */
    g_oms.sendUpp = NewOMSReadHook2(oms_tx_send);

    /* Install the device notification first: the USB Manager may call
     * back synchronously for an already-attached device, and the
     * zeroed state makes that safe. Then locate any device that was
     * already attached before this driver loaded (one walk). */
    g_oms.notifPb.pbLength = (UInt16)sizeof(g_oms.notifPb);
    g_oms.notifPb.pbVersion = 0u;       /* Rev 26 Ch 6 does not define a
                                           version constant; Apple's
                                           StorageClassShim.c sample
                                           leaves it unset */
    g_oms.notifPb.usbDeviceNotification = kNotifyAnyEvent;
    /* reserved1 is UInt8[1] (authentic USB.h 1.4.1) — an array, not a
     * scalar: it cannot be assigned, needs no explicit initialization
     * (oms_zero above already cleared it), and Apple's
     * StorageClassShim.c sample does not touch it either. */
    g_oms.notifPb.usbDeviceRef = kNoDeviceRef;
    g_oms.notifPb.usbClass = kOmsInterfaceClass;
    g_oms.notifPb.usbSubClass = kOmsInterfaceSubClass;
    g_oms.notifPb.usbProtocol = kUSBAnyProtocol;
    g_oms.notifPb.usbVendor = kUSBAnyVendor;
    g_oms.notifPb.usbProduct = kUSBAnyProduct;
    g_oms.notifPb.result = noErr;
    g_oms.notifPb.callback = oms_usb_notify;
    g_oms.notifPb.refcon = 0u;
    USBInstallDeviceNotification(&g_oms.notifPb);
    if (g_oms.notifPb.result == noErr) {
        g_oms.notifierInstalled = 1u;
    }
    /* No hard failure when no device is attached: the driver must
     * survive device absence to support hot-plug (SampleCell's omdvInit
     * succeeds without hardware; the Spec's only mandated compat-1
     * error is an OMSVersion() < 2.0 check, not hardware presence). */
    (void)oms_locate_dispatch();

    /* PPC: resolve the 68K OMSReceivedFromPort routine at every
     * omdvInit (each init zeroes g_oms and re-resolves; the glue
     * tolerates repeated LinkToOMSGlue). The Spec:
     * drivers call LinkToOMSGlue (glue init without sign-in), then
     * obtain the routine's address with
     * OMSGetCallAddress(callOMSReceivedFromPort) — "best to obtain the
     * address ... rather than calling it via glue"; the returned 68K
     * address is invoked through CallUniversalProc (oms_rx.c). A
     * failure (glue error — OMSGetCallAddress is not called — or
     * address 0) disables receive delivery: the deliver path drops
     * when rxRoutine is 0 while the ring drain keeps running. On 68K
     * the direct OMSReceivedFromPort call is used instead and no
     * resolution is needed. */
#if defined(powerc)
    g_oms.rxRoutine = 0L;
    if (LinkToOMSGlue() == noErr) {
        g_oms.rxRoutine = OMSGetCallAddress(callOMSReceivedFromPort);
    }
#endif
    return 0;
}

static OSErr oms_dispose(void)
{
    /* Best effort: unregister the push hook so the class driver never
     * calls into a fragment that is going away. The class driver also
     * clears its own copy at kNotifyDriverBeingRemoved. */
    if (g_oms.table != NULL && g_oms.table->setEventCallback != NULL) {
        UInt32 i;

        for (i = 0u; i < kUSBMIDI9OMSMaxInterfaces; i++) {
            (void)g_oms.table->setEventCallback(i, NULL, 0u);
        }
    }
    if (g_oms.notifierInstalled) {
        /* Rev 26 Ch 6: a notification must be removed before the code
         * fragment unloads. */
        (void)USBRemoveDeviceNotification(g_oms.notifPb.token);
        g_oms.notifierInstalled = 0u;
    }
    if (g_oms.sendUpp != NULL) {
        /* Release the send-proc RoutineDescriptor (the counterpart of
         * NewRoutineDescriptor; the fragment is going away). */
        DisposeRoutineDescriptor(g_oms.sendUpp);
        g_oms.sendUpp = NULL;
    }
    oms_zero(&g_oms, sizeof(g_oms));
    return 0;
}

/* --- omdvAddDevices --------------------------------------------------- */

/* Register one OMS device per attached USBMIDI9 interface via the
 * compat-level-1 add1device callback (Spec: zero the OMSDevice, fill
 * whichOut/ownerDriver/flags/midiChannels/name/icon/driverSpecific,
 * call CallOMSDvrAdd1DevProc1(add1Device, &dev, sizeof(dev))). */
static OSErr oms_add_devices(OMSDvrAdd1DevProc1 add1Device)
{
    struct USBMIDI9InterfaceInfo info;
    OMSDevice dev;
    UInt32 count;
    UInt32 i;
    OSErr err;

    if (add1Device == NULL) {
        return 0;                       /* defensive; OMS always passes one */
    }
    err = oms_ensure_dispatch();
    if (err != noErr) {
        return 0;                       /* no interfaces to add */
    }
    /* Re-register the push hook: interfaces may have appeared since the
     * last registration (each new interface starts with a NULL hook). */
    oms_register_callbacks(g_oms.table);
    if (g_oms.table->enumerateInterfaces(NULL, 0u, &count) != noErr) {
        return 0;
    }
    if (count > kUSBMIDI9OMSMaxInterfaces) {
        count = kUSBMIDI9OMSMaxInterfaces;
    }
    g_oms.nInterfaces = (unsigned short)count;

    for (i = 0u; i < count; i++) {
        if (g_oms.table->getInterfaceInfo(i, &info) != noErr) {
            continue;
        }
        g_oms.ifaces[i].valid = 1u;
        g_oms.ifaces[i].info = info;

        oms_zero(&dev, sizeof(dev));
        dev.whichOut = (short)(i + 1u);         /* 1-based, unique */
        dev.ownerDriver = kUSBMIDI9OMSDriverSignature;
        dev.flags1 = kInConnected | kIsReceiver | kIsMultitimbral;
        dev.flags2 = kIsMIDIInterface | kNoChildDevices;
        dev.midiChannels = 0xFFFF;              /* 16 channels */
        dev.iconID = 0;                         /* index into SICN 128 */
        /* nOutputPorts stays 0: the dispatch table does not expose the
         * interface's cable count, so advertising a port count would be
         * guesswork. Devices attach to ports via OMSPortID.whichPort
         * (the cable number) regardless.
         * TODO(oms-ports): advertise nOutputPorts once the dispatch
         * table reports the cable count. */
        /* driverSpecific[0..1]: user preferences (none); [2..3]:
         * interface index, used to re-match when OMS Setup re-examines
         * the world (Spec, omdvAddDevices). */
        dev.driverSpecific[2] = (unsigned char)i;
        oms_make_port_name(dev.devName, i + 1u);
        oms_pstr_set_n(dev.manuf, "USBMIDI9", OD_MAX_MANUF_LEN);
        oms_pstr_set_n(dev.model, "USB-MIDI Interface", OD_MAX_MODEL_LEN);

        (void)add1Device(&dev, (short)sizeof(dev));
    }
    return 0;
}

/* --- other messages --------------------------------------------------- */

static OSErr oms_set_interface_list(OMSDeviceListH interfaceList)
{
    /* Remember the handle without ever altering or disposing it
     * (Spec, omdvSetInterfaceList). */
    g_oms.interfaceList = (void *)interfaceList;
    return 0;
}

static OSErr oms_start_midi(void)
{
    /* OMS 2.0: ignore par1 (port availability) and use the Serial
     * Hardware Manager for serial ports; we have none. MIDI actually
     * starts at omdvStartMIDI2, when the system is consistent. */
    return 0;
}

static OSErr oms_start_midi2(void)
{
    unsigned i;

    /* Attach first (while midiStarted is still 0, so the attach drain
     * DISCARDS the stale backlog instead of delivering it), then drop
     * anything that arrived while MIDI was off, then reset the per-port
     * streams so no mid-SysEx continuation leaks across a stop/start,
     * and only then start delivering. */
    (void)oms_ensure_dispatch();
    oms_rx_drain_all(0u);               /* discard stale backlog */
    for (i = 0u; i < kUSBMIDI9OMSMaxInterfaces; i++) {
        if (g_oms.ifaces[i].valid) {
            unsigned c;
            for (c = 0u; c < kUSBMIDI9OMSCables; c++) {
                struct oms_port *port = &g_oms.ifaces[i].ports[c];
                if (port->valid) {
                    um9_rx_init(&port->rx, c);
                    um9_tx_init(&port->tx, c);
                    port->txCarryLen = 0u;
                }
            }
        }
    }
    g_oms.midiStarted = 1u;
    return 0;
}

static OSErr oms_stop_midi(void)
{
    g_oms.midiStarted = 0u;
    return 0;
}

static OSErr oms_get_port_send_proc(OMSPortID *portID, OMSSendParams *sendPars)
{
    unsigned ifaceNo;
    unsigned cable;

    if (portID == NULL || sendPars == NULL) {
        return 0;
    }
    ifaceNo = (unsigned)portID->whichInterface;
    cable = (unsigned)portID->whichPort;
    if (ifaceNo < 1u || ifaceNo > kUSBMIDI9OMSMaxInterfaces
        || cable >= kUSBMIDI9OMSCables) {
        return 0;
    }
    /* Spec: return a proc which will send an OMSPacket to the port;
     * compat >= 1: an OMSReadHook2. proc is the RoutineDescriptor built
     * at omdvInit (NewOMSReadHook2; the authentic OMSSendParams field
     * type is OMSReadHook2UPP) — OMS invokes it via CallOMSReadHook2.
     * paramD0 is passed as the readHookRefCon; the low word of paramD1
     * is passed in the packet's appConnRefCon. The proc may be called
     * at interrupt level. */
    sendPars->proc = g_oms.sendUpp;
    sendPars->paramD0 = (long)((ifaceNo << 8) | cable);
    sendPars->paramD1 = 0L;
    return 0;
}

static OSErr oms_set_port_receive_refnum(OMSPortID *portID, short ioRefNum)
{
    unsigned ifaceNo;
    unsigned cable;
    struct oms_port *port;

    if (portID == NULL) {
        return 0;
    }
    ifaceNo = (unsigned)portID->whichInterface;
    cable = (unsigned)portID->whichPort;
    if (ifaceNo < 1u || ifaceNo > kUSBMIDI9OMSMaxInterfaces
        || cable >= kUSBMIDI9OMSCables) {
        return 0;
    }
    /* Spec: ioRefNum identifies this port when calling OMSReceivedFromPort;
     * -1 means do not pass received data from the port to OMS. The driver
     * should initially assume -1 for all possible sources. */
    port = &g_oms.ifaces[ifaceNo - 1u].ports[cable];
    port->valid = 1u;
    port->ioRefNum = ioRefNum;
    um9_rx_init(&port->rx, cable);
    um9_tx_init(&port->tx, cable);
    return 0;
}

long oms_handle_message(short msg, long par1, long par2)
{
    switch (msg) {
    case omdvInit:
        return (long)oms_init((OMSFile *)par1);
    case omdvDispose:
        return (long)oms_dispose();
    case omdvAddDevices:
        /* compat level 1: par1 = OMSDvrAdd1DevProc1UPP, par2 =
         * OMSAddDevParams * (portsUsed/baudRatePerPort are serial-port
         * concepts; we have no serial ports). */
        return (long)oms_add_devices((OMSDvrAdd1DevProc1)par1);
    case omdvConfigure:
    case omdvGetMenu:                 /* return null: no menus */
    case omdvDoMenu:
    case omdvCloseUserInterface:
        return 0L;
    case omdvSetInterfaceList:
        return (long)oms_set_interface_list((OMSDeviceListH)par1);
    case omdvStartMIDI:
        return (long)oms_start_midi();
    case omdvStartMIDI2:
        return (long)oms_start_midi2();
    case omdvStopMIDI:
        return (long)oms_stop_midi();
    case omdvGetPortSendProc:
        return (long)oms_get_port_send_proc((OMSPortID *)par1,
                                            (OMSSendParams *)par2);
    case omdvSetPortReceiveRefNum:
        return (long)oms_set_port_receive_refnum((OMSPortID *)par1,
                                                 (short)par2);
    case omdvTestDevice:
    case omdvDifferentStudioSetup:
    case omdvConnectsChanged:
    case omdvRemoveOutput:
    case omdvGetCommSpeed:
    case omdvStartupComplete:
    case omdvClosedMIDISetup:
    case omdvStudioSetupChanged:
        return 0L;
    default:
        /* Unknown messages (and driver-private numbers above 4095) are
         * ignored per the Spec: return zero. */
        return 0L;
    }
}

/* Driver entry point (OMSDriver.h): called by OMS through the loaded
 * 'PPCC' fragment's main symbol with the omdv* messages. Exported from
 * the PEF as `main` (codewarrior/USBMIDI9_OMS.exp).
 *
 * Diagnostic build: define USBMIDI9_OMS_DIAG_MINIMAL_ENTRY in the G4
 * OMS PEF target's preprocessor settings ONLY (never in a shared
 * prefix file — it must not leak into the production build) to replace
 * the entry with a trivial `return 0` for every message — NO
 * LinkToOMSGlue, OMSGetCallAddress, NewRoutineDescriptor, USB Manager
 * calls, FindSymbol, notifications, dispatch lookup or RX/TX code.
 * This is the stage-0 diagnostic for the OMS Setup type-2/type-3
 * crash gate (see docs/g4-handoff.md): returning 0 is safe for every
 * driver message (OMS spec: omdvInit returns OMSErr; a driver that
 * provides no devices/ports is legal). It is a TEMPORARY diagnostic —
 * the production entry must remain oms_handle_message. */
#ifdef USBMIDI9_OMS_DIAG_MINIMAL_ENTRY
OMSCALLBACK(long) main(short msg, long par1, long par2)
{
    (void)msg;
    (void)par1;
    (void)par2;
    return 0L;
}
#else
OMSCALLBACK(long) main(short msg, long par1, long par2)
{
    return oms_handle_message(msg, par1, par2);
}
#endif

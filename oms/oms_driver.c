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
#include <Notifications.h>

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
 * the Probe). */
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

OSErr oms_locate_dispatch(void)
{
    USBDeviceRef deviceRef;
    CFragConnectionID connID;
    CFragSymbolClass symClass;
    THz currentZone;
    struct USBMIDI9DispatchTable *table;
    unsigned char hadTable;
    OSErr err;

    hadTable = (g_oms.table != NULL);
    g_oms.table = NULL;
    deviceRef = kNoDeviceRef;
    for (;;) {
        err = USBGetNextDeviceByClass(&deviceRef, &connID,
                                      kOmsInterfaceClass,
                                      kOmsInterfaceSubClass,
                                      kUSBAnyProtocol);
        if (err != noErr) {
            g_oms.locateFailures++;
            return err;                 /* no (more) matching driver */
        }
        table = NULL;
        /* Class drivers load in the System Zone; look up the symbol
         * there (Rev 26 Ch 4 p. 83-85; HIDReader.c pattern). */
        currentZone = GetZone();
        SetZone(SystemZone());
        err = FindSymbol(connID, kOmsDispatchSymbolName,
                         (Ptr *)&table, &symClass);
        SetZone(currentZone);
        if (err == noErr && table != NULL) {
            /* Version gate (usbmidi9_dispatch.h): clients must not call
             * procs from a table older than they understand. */
            if (table->version < kUSBMIDI9DispatchTableVersion) {
                g_oms.locateFailures++;
                return kUSBBadDispatchTable;
            }
            if (!hadTable) {
                /* The driver (re)appeared: reset per-port stream state
                 * so a mid-SysEx unplug cannot leave stale continuation
                 * state behind. Only on a transition: the table was NOT
                 * found on the previous poll. */
                oms_reset_ports();
            }
            g_oms.table = table;
            g_oms.relocates++;
            return noErr;
        }
    }
}

/* Refresh the cached dispatch table; returns noErr when usable. */
static OSErr oms_ensure_dispatch(void)
{
    if (g_oms.table == NULL) {
        return oms_locate_dispatch();
    }
    return noErr;
}

/* --- omdvInit / omdvDispose ------------------------------------------ */

static OSErr oms_init(OMSFile *file)
{
    (void)file;                         /* driver location; not needed */

    oms_zero(&g_oms, sizeof(g_oms));
    return oms_locate_dispatch();       /* error -> OMS sends omdvDispose */
}

static OSErr oms_dispose(void)
{
    if (g_oms.timerRunning) {
        NMRemove(&g_oms.nmRec);
        g_oms.timerRunning = 0u;
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

    g_oms.midiStarted = 1u;
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
    if (!g_oms.timerRunning) {
        g_oms.nmRec.nMsg = (UInt32)(unsigned long)oms_poll_task;
        g_oms.nmRec.nRefCon = 0u;
        /* Notification Manager eventTime is absolute ticks (since
         * startup); a relative 1 would have already passed and fire the
         * task back-to-back. Absolute base + fixed period: the task
         * re-arms with eventTime += period, so the rate does not drift. */
        g_oms.nmRec.eventTime = Ticks() + kUSBMIDI9OMSPollTicks;
        if (NMInstall(&g_oms.nmRec) == noErr) {
            g_oms.timerRunning = 1u;
        }
    }
    return 0;
}

static OSErr oms_stop_midi(void)
{
    if (g_oms.timerRunning) {
        NMRemove(&g_oms.nmRec);
        g_oms.timerRunning = 0u;
    }
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
     * compat >= 1: an OMSReadHook2; paramD0 is passed as the
     * readHookRefCon; the low word of paramD1 is passed in the
     * packet's appConnRefCon. The proc may be called at interrupt
     * level. */
    sendPars->proc = (OMSReadHook2)oms_tx_send;
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
 * 'OMdv' code resource with the omdv* messages. Exported from the PEF
 * as `main` (codewarrior/USBMIDI9_OMS.exp). */
OMSCALLBACK(long) main(short msg, long par1, long par2)
{
    return oms_handle_message(msg, par1, par2);
}

/*
 * Host tests for the USBMIDI9 OMS driver shim (oms/oms_driver.c,
 * oms/oms_rx.c, oms/oms_tx.c) driven through a scripted mock OMS/USB
 * environment.
 *
 * The shim sources are compiled into this translation unit against the
 * stub headers in host-check/ (with -Dpowerc, modeling the G4 PPC
 * target), and every external call the shim makes is intercepted by the
 * mocks below: USBGetNextDeviceByClass, USBGetDriverConnectionID,
 * FindSymbol, GetZone/SetZone/SystemZone (dispatch-table lookup),
 * USBInstallDeviceNotification/USBRemoveDeviceNotification (device
 * lifecycle), LinkToOMSGlue + OMSGetCallAddress (PPC resolution of the
 * 68K OMSReceivedFromPort routine), CallUniversalProc (delivery through
 * the cached routine — the host stub only forwards the two arguments;
 * real PPC<->68K register dispatch cannot be modeled on the host),
 * OMSOpenDriverResFile/OMSCloseDriverResFile.
 *
 * Receive is push-based: data delivery is driven by invoking the event
 * callback the shim registered through the fake table's
 * setEventCallback (exactly what the real class driver's read
 * completion does), NOT by a poll loop. A core regression assertion in
 * test_receive_path: receiving data performs ZERO
 * USBGetNextDeviceByClass calls — the per-tick USB walk is gone.
 *
 * The test does NOT define any USL transfer/pipe function (USBBulkRead,
 * USBBulkWrite, USBIntRead, USBConfigureInterface, USBAllocMem, ...):
 * the link succeeds only because the shim contains no USB data-path
 * code — that is the no-USL-transfer hygiene gate.
 *
 * Assertions go through the real public surface: the driver entry
 * `main` (omdv* messages) and the state in g_oms.
 */

#include <stdio.h>
#include <stdarg.h>

#include <MacTypes.h>
#include <MacErrors.h>
#include <Memory.h>
#include <CodeFragments.h>
#include <USB.h>
#include <OMS.h>
#include <OMSDriver.h>
#include <OMSGlueProcs.h>       /* callOMSReceivedFromPort = 112 */
#include <MixedMode.h>          /* CallUniversalProc / ProcInfo */

#include "usbmidi9_dispatch.h"
#include "core/midi_stream.h"
#include "oms/oms_driver.h"   /* constants used by the mocks below */

/* ---- test helpers ---------------------------------------------------- */

static int g_failures;

static void fail(const char *expr, const char *file, int line)
{
    printf("FAIL %s:%d: %s\n", file, line, expr);
    g_failures++;
}

#define CHECK(cond) do { \
    if (!(cond)) { \
        fail(#cond, __FILE__, __LINE__); \
    } \
} while (0)

/* ---- Mock environment ------------------------------------------------ */


#define MOCK_MAX_IFACES 8u
#define MOCK_QUEUE_MAX 256u

static struct USBMIDI9InterfaceInfo gMockInfo[MOCK_MAX_IFACES];
static unsigned char gMockQueue[MOCK_MAX_IFACES][MOCK_QUEUE_MAX];
static unsigned gMockQueueLen[MOCK_MAX_IFACES];
static unsigned gMockNInterfaces;

static int gDriverPresent;              /* USBGetNextDeviceByClass result */
static int gFindCalls;
static int gNextDeviceCalls;
static int gConnIDCalls;                /* USBGetDriverConnectionID */
static USBDeviceRef gMockDeviceRef;     /* the located device's ref */
static CFragConnectionID gMockConnID;

/* USB Manager device-notification mock state. */
static int gNotifInstallCalls;
static int gNotifRemoveCalls;
static unsigned char gNotifInstalled;
static UInt32 gNotifToken;
static USBDeviceNotificationCallbackProcPtr gInstalledNotifProc;
static struct USBDeviceNotificationParameterBlock gInstalledPb;
static UInt16 gInstalledClass;
static UInt16 gInstalledSubClass;
static UInt8 gInstalledFilter;

/* The event callback the shim registered via fake_set_event_callback
 * (the class driver would invoke it from its read completion). */
static USBMIDI9EventCallbackProcPtr gRegisteredEventCallback;
static UInt32 gRegisteredEventRefcon;
static int gSetEventCallbackCalls;

static OMSPacket gCaptured[64];         /* OMSReceivedFromPort captures */
static short gCapturedRefNum[64];
static unsigned gCapturedCount;

static struct {
    unsigned portCode;
    unsigned char pkt[4];
} gTxCaptured[64];
static unsigned gTxCapturedCount;

static OMSDevice gAddedDevices[8];
static short gAddedSizes[8];
static unsigned gAddedCount;

static struct USBMIDI9DispatchTable gFakeTable;

/* --- mock USL / CFM / USB Manager / OMS ------------------------------ */

OSStatus oms_mock_USBGetNextDeviceByClass(USBDeviceRef *deviceRef,
                                 CFragConnectionID *connID,
                                 UInt16 theClass, UInt16 theSubClass,
                                 UInt16 theProtocol)
{
    (void)theClass;
    (void)theSubClass;
    (void)theProtocol;
    gNextDeviceCalls++;
    if (!gDriverPresent) {
        return kUSBNotFound;
    }
    if (*deviceRef == kNoDeviceRef) {
        *deviceRef = gMockDeviceRef;
        *connID = gMockConnID;
        return noErr;
    }
    return kUSBNotFound;
}

OSStatus oms_mock_USBGetDriverConnectionID(USBDeviceRef *deviceRef,
                                           CFragConnectionID *connID)
{
    gConnIDCalls++;
    if (!gDriverPresent || *deviceRef != gMockDeviceRef) {
        return kUSBNotFound;
    }
    *connID = gMockConnID;
    return noErr;
}

/* Compile-time regression guard: the authentic DDK declares
 * USBGetDriverConnectionID(USBDeviceRef *deviceRef, CFragConnectionID
 * *connID) — the first parameter is a POINTER to the ref (USB.h 1.4.1;
 * Rev 26 Ch 6 p. 181; Apple's SampleShim.c calls it as
 * USBGetDriverConnectionID(&pb->usbDeviceRef, &connID)). The real G4
 * build (DDK 1.5.1f1) failed with "cannot convert 'long' to 'long *'"
 * when the host-check stub had drifted to pass-by-value. If the mock
 * ever regresses to pass-by-value, this binding no longer type-checks
 * and the -Werror build stops. */
static OSStatus (*const kConnIDSigGuard)(USBDeviceRef *,
                                         CFragConnectionID *) =
    oms_mock_USBGetDriverConnectionID;

OSStatus oms_mock_FindSymbol(CFragConnectionID connID, const char *symName,
                    Ptr *symAddr, CFragSymbolClass *symClass)
{
    (void)connID;
    (void)symName;
    gFindCalls++;
    if (!gDriverPresent) {
        return kUSBNotFound;
    }
    *symAddr = (Ptr)&gFakeTable;
    *symClass = kDataCFragSymbol;
    return noErr;
}

THz oms_mock_GetZone(void)
{
    return (THz)0;
}

void oms_mock_SetZone(THz zone)
{
    (void)zone;
}

THz oms_mock_SystemZone(void)
{
    return (THz)0;
}

/* The USB Manager's notification install (Rev 26 Ch 6): the pb is
 * filled by the caller; result/token come back in the pb; the callback
 * may fire synchronously during installation. */
void USBInstallDeviceNotification(USBDeviceNotificationParameterBlock *pb)
{
    gNotifInstallCalls++;
    gInstalledPb = *pb;
    gInstalledClass = pb->usbClass;
    gInstalledSubClass = pb->usbSubClass;
    gInstalledFilter = pb->usbDeviceNotification;
    gNotifToken++;
    pb->token = gNotifToken;
    pb->result = noErr;
    gInstalledNotifProc = pb->callback;
    gNotifInstalled = 1u;
}

OSStatus USBRemoveDeviceNotification(UInt32 token)
{
    (void)token;
    gNotifRemoveCalls++;
    gInstalledNotifProc = NULL;
    gNotifInstalled = 0u;
    return noErr;
}

/* Mock of the 68K OMSReceivedFromPort routine — the address the mock
 * OMSGetCallAddress returns for callOMSReceivedFromPort. The real
 * routine runs in 68K mode with pkt in A1 and destRefNum in D0 (the
 * host cannot model that); the wrapper's CallUniversalProc stub just
 * forwards the two arguments to this function. */
static void mock_oms_received_from_port(OMSPacket *pkt, short destRefNum)
{
    if (gCapturedCount < 64u && pkt != NULL) {
        gCaptured[gCapturedCount] = *pkt;
        gCapturedRefNum[gCapturedCount] = destRefNum;
        gCapturedCount++;
    }
}

/* PPC resolution mocks (OMS.h). gLinkFail makes LinkToOMSGlue return an
 * error; gCallAddrFail makes OMSGetCallAddress return 0 (no routine). */
static int gLinkCalls;
static int gLinkFail;
static int gCallAddrCalls;
static short gLastCallNum;
static int gCallAddrFail;

OMSErr LinkToOMSGlue(void)
{
    gLinkCalls++;
    return (OMSErr)(gLinkFail ? 1 : 0);
}

long OMSGetCallAddress(short callNum)
{
    gCallAddrCalls++;
    gLastCallNum = callNum;
    if (gCallAddrFail) {
        return 0L;
    }
    return (long)(Ptr)mock_oms_received_from_port;
}

/* Host stand-in for the Mixed Mode Manager (MixedMode.h). It cannot
 * dispatch 68K code with register-based parameters; it records the
 * arguments and forwards the two parameters to the routine pointer, so
 * the host tests cover resolution/caching/argument flow but NOT real
 * PPC<->68K mode switching (that is the G4 hardware gate). */
static UniversalProcPtr gLastCallProc;
static ProcInfoType gLastProcInfo;
static int gCallUniversalCalls;

long CallUniversalProc(UniversalProcPtr theProcPtr, ProcInfoType procInfo, ...)
{
    OMSPacket *pkt;
    short destRefNum;
    va_list ap;

    gCallUniversalCalls++;
    gLastCallProc = theProcPtr;
    gLastProcInfo = procInfo;
    va_start(ap, procInfo);
    pkt = va_arg(ap, OMSPacket *);
    destRefNum = (short)va_arg(ap, int);    /* default promotion */
    va_end(ap);
    ((void (*)(OMSPacket *, short))theProcPtr)(pkt, destRefNum);
    return 0L;
}

/* Mixed Mode Manager RoutineDescriptor mocks (MixedMode.h declares
 * these; the shim reaches them through the OMSUPPs.h UPP macros). The
 * host has no RoutineDescriptor machinery: the "descriptor" is the
 * routine itself, so the CallUniversalProc forwarding mock above can
 * invoke it. */
static int gNewRDCalls;
static int gDisposeRDCalls;
static ProcPtr gLastRDProc;
static ProcInfoType gLastRDProcInfo;
static ISAType gLastRDISA;

UniversalProcPtr mock_NewRoutineDescriptor(ProcPtr theProc,
                                           ProcInfoType theProcInfo,
                                           ISAType theISA)
{
    gNewRDCalls++;
    gLastRDProc = theProc;
    gLastRDProcInfo = theProcInfo;
    gLastRDISA = theISA;
    return (UniversalProcPtr)theProc;
}

void mock_DisposeRoutineDescriptor(UniversalProcPtr theProcPtr)
{
    (void)theProcPtr;
    gDisposeRDCalls++;
}

short OMSOpenDriverResFile(OMSSignature driverID)
{
    (void)driverID;
    return -1;
}

void OMSCloseDriverResFile(OMSSignature driverID)
{
    (void)driverID;
}

/* --- fake dispatch table ---------------------------------------------- */

static OSStatus fake_enumerate(struct USBMIDI9InterfaceInfo *outArray,
                               UInt32 maxCount, UInt32 *outCount)
{
    UInt32 i;

    if (outCount == NULL) {
        return paramErr;
    }
    *outCount = (UInt32)gMockNInterfaces;
    for (i = 0u; i < gMockNInterfaces && i < maxCount; i++) {
        outArray[i] = gMockInfo[i];
    }
    return noErr;
}

static OSStatus fake_get_info(UInt32 index, struct USBMIDI9InterfaceInfo *out)
{
    if (out == NULL || index >= gMockNInterfaces) {
        return kUSBNotFound;
    }
    *out = gMockInfo[index];
    return noErr;
}

static UInt32 fake_dequeue(UInt32 index, void *buffer, UInt32 maxBytes)
{
    UInt32 n;

    if (buffer == NULL || index >= gMockNInterfaces) {
        return 0u;
    }
    n = (UInt32)gMockQueueLen[index];
    if (n > maxBytes) {
        n = maxBytes;
    }
    {
        UInt32 i;
        unsigned char *dst = (unsigned char *)buffer;
        for (i = 0u; i < n; i++) {
            dst[i] = gMockQueue[index][i];
        }
    }
    gMockQueueLen[index] = 0u;
    return n;
}

static OSStatus fake_set_event_callback(UInt32 index,
                                        USBMIDI9EventCallbackProcPtr callback,
                                        UInt32 refcon)
{
    if (index >= gMockNInterfaces) {
        return kUSBNotFound;
    }
    gSetEventCallbackCalls++;
    gRegisteredEventCallback = callback;    /* last registration wins */
    gRegisteredEventRefcon = refcon;
    return noErr;
}

/* --- mock add1device callback ----------------------------------------- */

static OMSDeviceH mock_add1(OMSDevice *device, short devSize)
{
    if (gAddedCount < 8u && device != NULL) {
        gAddedDevices[gAddedCount] = *device;
        gAddedSizes[gAddedCount] = devSize;
        gAddedCount++;
    }
    return NULL;
}

/* --- the shim under test ---------------------------------------------- */

/* The probe test (test_probe.c, linked into the same binary) defines the
 * real GetZone/SetZone/SystemZone/USBGetNextDeviceByClass/FindSymbol
 * mocks, so the shim's references to those five are renamed here and
 * satisfied by the oms_mock_* definitions above (same technique as the
 * `main` rename below). */
#define GetZone oms_mock_GetZone
#define SetZone oms_mock_SetZone
#define SystemZone oms_mock_SystemZone
#define USBGetNextDeviceByClass oms_mock_USBGetNextDeviceByClass
#define USBGetDriverConnectionID oms_mock_USBGetDriverConnectionID
#define FindSymbol oms_mock_FindSymbol
#define NewRoutineDescriptor mock_NewRoutineDescriptor
#define DisposeRoutineDescriptor mock_DisposeRoutineDescriptor

#define main oms_driver_main
#include "oms/oms_driver.c"
#include "oms/oms_rx.c"
#include "oms/oms_tx.c"
#undef main
#undef NewRoutineDescriptor
#undef DisposeRoutineDescriptor

/* Compile-time guards (the G4 gate: host-check types must match the
 * authentic headers exactly).
 *
 * 1. OMSSendParams.proc is OMSReadHook2UPP — a RoutineDescriptor
 *    pointer, not a raw OMSReadHook2 function: the real G4 build
 *    rejected the raw assignment with "cannot convert 'pascal void
 *    (*)...'". If proc regresses to the raw function type, &proc is no
 *    longer OMSReadHook2UPP * and this array type becomes negative.
 * 2. USBDeviceNotificationParameterBlock.reserved1 is UInt8[1] (USB.h
 *    1.4.1) — an ARRAY: the real G4 build rejected `reserved1 = 0u`
 *    with "not an lvalue". A scalar regression makes &reserved1 a
 *    UInt8 * and this array type becomes negative. */
typedef char oms_send_proc_is_upp_guard[
    __builtin_types_compatible_p(
        __typeof__(&((struct OMSSendParams *)0)->proc),
        OMSReadHook2UPP *) ? 1 : -1];
typedef char usb_notif_reserved1_is_array_guard[
    __builtin_types_compatible_p(
        __typeof__(&((struct USBDeviceNotificationParameterBlock *)0)->reserved1),
        UInt8 (*)[1]) ? 1 : -1];

/* Install the fake environment with `n` interfaces and a driver present. */
static void mock_setup(unsigned n)
{
    unsigned i;

    gMockNInterfaces = n;
    gDriverPresent = 1;
    gMockDeviceRef = 0x1000u;
    gMockConnID = 1;
    gFindCalls = 0;
    gNextDeviceCalls = 0;
    gConnIDCalls = 0;
    gNotifInstallCalls = 0;
    gNotifRemoveCalls = 0;
    gNotifInstalled = 0u;
    gNotifToken = 0u;
    gInstalledNotifProc = NULL;
    gRegisteredEventCallback = NULL;
    gRegisteredEventRefcon = 0u;
    gSetEventCallbackCalls = 0;
    gCapturedCount = 0u;
    gLinkCalls = 0;
    gLinkFail = 0;
    gCallAddrCalls = 0;
    gLastCallNum = 0;
    gCallAddrFail = 0;
    gCallUniversalCalls = 0;
    gLastCallProc = NULL;
    gLastProcInfo = 0u;
    gNewRDCalls = 0;
    gDisposeRDCalls = 0;
    gLastRDProc = NULL;
    gLastRDProcInfo = 0u;
    gLastRDISA = 0;
    gTxCapturedCount = 0u;
    gAddedCount = 0u;
    gFakeTable.version = kUSBMIDI9DispatchTableVersion;
    gFakeTable.enumerateInterfaces = fake_enumerate;
    gFakeTable.getInterfaceInfo = fake_get_info;
    gFakeTable.dequeueBytes = fake_dequeue;
    gFakeTable.setEventCallback = fake_set_event_callback;
    for (i = 0u; i < MOCK_MAX_IFACES; i++) {
        gMockInfo[i].index = i;
        gMockInfo[i].vendorID = 0x0A4Du;
        gMockInfo[i].productID = 0x0090u;
        gMockInfo[i].interfaceNum = (UInt32)i;
        gMockInfo[i].interfaceClass = 1u;
        gMockInfo[i].interfaceSubClass = 3u;
        gMockInfo[i].interfaceProtocol = 0u;
        gMockInfo[i].maxPacketSize = 64u;
        gMockInfo[i].availableBytes = 0u;
        gMockQueueLen[i] = 0u;
    }
}

/* Queue raw bytes for interface 0 (as dequeueBytes would return them). */
static void mock_queue(unsigned iface, const unsigned char *bytes, unsigned n)
{
    unsigned i;

    for (i = 0u; i < n && i < MOCK_QUEUE_MAX; i++) {
        gMockQueue[iface][i] = bytes[i];
    }
    gMockQueueLen[iface] = n;
}

/* Fire the class driver's push hook for one interface, exactly as the
 * real driver's read completion does after enqueueing bytes. */
static void mock_data_arrives(unsigned iface)
{
    if (gRegisteredEventCallback != NULL) {
        gRegisteredEventCallback((UInt32)iface, gRegisteredEventRefcon);
    }
}

/* Fire a USB Manager device notification (kNotifyAddDevice etc.). */
static void mock_notify(UInt8 event, USBDeviceRef deviceRef)
{
    struct USBDeviceNotificationParameterBlock pb;

    if (gInstalledNotifProc == NULL) {
        return;
    }
    pb.pbLength = (UInt16)sizeof(pb);
    pb.pbVersion = 0u;
    pb.usbDeviceNotification = event;
    /* reserved1 is UInt8[1] (authentic USB.h 1.4.1) — an array, not
     * assignable; the shim never reads it. */
    pb.usbDeviceRef = deviceRef;
    pb.usbClass = 0x01u;
    pb.usbSubClass = 0x03u;
    pb.usbProtocol = 0u;
    pb.usbVendor = 0u;
    pb.usbProduct = 0u;
    pb.result = noErr;
    pb.token = gNotifToken;
    pb.callback = gInstalledNotifProc;
    pb.refcon = 0u;
    gInstalledNotifProc(&pb);
}

/* Capture seam for the send path. */
static void tx_capture(unsigned portCode, const unsigned char pkt[4])
{
    if (gTxCapturedCount < 64u) {
        gTxCaptured[gTxCapturedCount].portCode = portCode;
        gTxCaptured[gTxCapturedCount].pkt[0] = pkt[0];
        gTxCaptured[gTxCapturedCount].pkt[1] = pkt[1];
        gTxCaptured[gTxCapturedCount].pkt[2] = pkt[2];
        gTxCaptured[gTxCapturedCount].pkt[3] = pkt[3];
        gTxCapturedCount++;
    }
}

/* Standard bring-up: init + add devices + enable port 0 with a refnum. */
static void mock_start_midi(unsigned refnum)
{
    OMSPortID portID;

    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(oms_driver_main(omdvAddDevices, (long)(Ptr)mock_add1, 0L) == 0L);
    portID.driverID = kUSBMIDI9OMSDriverSignature;
    portID.whichInterface = 1;
    portID.whichPort = 0;
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID,
                          refnum) == 0L);
    CHECK(oms_driver_main(omdvStartMIDI, 0L, 0L) == 0L);
    CHECK(oms_driver_main(omdvStartMIDI2, 0L, 0L) == 0L);
    CHECK(g_oms.midiStarted == 1u);
}

/* ---- tests ----------------------------------------------------------- */

static void test_init_locate(void)
{
    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(g_oms.table == &gFakeTable);
    CHECK(g_oms.deviceRef == gMockDeviceRef);
    CHECK(g_oms.relocates == 1ul);
    CHECK(gFindCalls == 1);
    CHECK(gNextDeviceCalls == 1);

    /* The notification is installed with the USB-MIDI class filter. */
    CHECK(gNotifInstalled == 1u);
    CHECK(gNotifInstallCalls == 1);
    CHECK(gInstalledFilter == kNotifyAnyEvent);
    CHECK(gInstalledClass == 0x01u);
    CHECK(gInstalledSubClass == 0x03u);

    /* The push hook is registered with the class driver. */
    CHECK(gRegisteredEventCallback == oms_rx_event);
    CHECK(g_oms.callbackRegistered == 1u);

    /* OMS disposes us afterwards; state clears and the push hook is
     * unregistered from the class driver (best effort). */
    CHECK(oms_driver_main(omdvDispose, 0L, 0L) == 0L);
    CHECK(g_oms.table == NULL);
    CHECK(gNotifRemoveCalls == 1);
    CHECK(gNotifInstalled == 0u);
    CHECK(gRegisteredEventCallback == NULL);   /* dispose unregistered */
}

static void test_init_no_driver(void)
{
    mock_setup(0u);
    gDriverPresent = 0;
    /* omdvInit succeeds without hardware (SampleCell precedent): the
     * driver must survive device absence to support hot-plug. */
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(g_oms.table == NULL);
    CHECK(g_oms.locateFailures == 1ul);
    CHECK(gNotifInstalled == 1u);
    /* Clean up so later tests start from a disposed state. */
    CHECK(oms_driver_main(omdvDispose, 0L, 0L) == 0L);
}

static void test_init_twice(void)
{
    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    /* A second init must remove the first notification before
     * reinstalling (no leaked token, no double install). */
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(gNotifInstallCalls == 2);
    CHECK(gNotifRemoveCalls == 1);
    CHECK(gNotifInstalled == 1u);
}

static void test_init_v1_table_rejected(void)
{
    mock_setup(1u);
    /* A v0x0001 table has no setEventCallback entry: the shim must
     * reject it (it cannot receive push), but stay alive for replug. */
    gFakeTable.version = 0x0001u;
    gFakeTable.setEventCallback = NULL;
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(g_oms.table == NULL);
    CHECK(g_oms.locateFailures == 2ul); /* rejected v1, then no more */
    CHECK(gNotifInstalled == 1u);

    /* The driver is upgraded to v0x0002; the add notification attaches. */
    mock_setup(1u);
    gDriverPresent = 0;             /* no walk-based locate at init */
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    gDriverPresent = 1;
    mock_notify(kNotifyAddInterface, gMockDeviceRef);
    CHECK(g_oms.table == &gFakeTable);
    CHECK(gRegisteredEventCallback == oms_rx_event);
}

static void test_add_devices(void)
{
    mock_setup(2u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);

    /* compat level 1: par1 = add1device, par2 = OMSAddDevParams. */
    {
        OMSAddDevParams pars;
        pars.portsUsed = NULL;
        pars.baudRatePerPort = NULL;
        pars.addHardwareManually = 0;
        CHECK(oms_driver_main(omdvAddDevices, (long)(Ptr)mock_add1,
                   (long)(Ptr)&pars) == 0L);
    }
    CHECK(gAddedCount == 2u);
    CHECK(gAddedSizes[0] == (short)sizeof(OMSDevice));

    CHECK(gAddedDevices[0].whichOut == 1);
    CHECK(gAddedDevices[1].whichOut == 2);
    CHECK(gAddedDevices[0].ownerDriver == kUSBMIDI9OMSDriverSignature);
    CHECK(gAddedDevices[0].flags1 == (kInConnected | kIsReceiver
                                      | kIsMultitimbral));
    CHECK(gAddedDevices[0].flags2 == (kIsMIDIInterface | kNoChildDevices));
    CHECK(gAddedDevices[0].midiChannels == (short)0xFFFF);
    CHECK(gAddedDevices[0].iconID == 0);
    CHECK(gAddedDevices[0].driverSpecific[2] == 0u);
    CHECK(gAddedDevices[1].driverSpecific[2] == 1u);
    CHECK(gAddedDevices[0].devName[0] == 15u);      /* "USBMIDI9 Port 1" */
    CHECK(gAddedDevices[0].devName[15] == '1');
    CHECK(gAddedDevices[1].devName[15] == '2');
    CHECK(gAddedDevices[0].manuf[0] == 8u);         /* "USBMIDI9" */
    CHECK(gAddedDevices[0].model[0] == 18u);        /* "USB-MIDI Interface" */

    /* No interfaces attached: nothing to add (mock_setup reset the
     * counter). */
    mock_setup(0u);
    gDriverPresent = 0;
    CHECK(oms_driver_main(omdvAddDevices, (long)(Ptr)mock_add1, 0L) == 0L);
    CHECK(gAddedCount == 0u);
}

static void test_port_refnum_and_send_proc(void)
{
    OMSPortID portID;
    OMSSendParams sendPars;

    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);

    /* omdvSetPortReceiveRefNum: port (iface 1, cable 0) -> refnum 0x100;
     * cable 5 -> -1 (do not deliver). */
    portID.driverID = kUSBMIDI9OMSDriverSignature;
    portID.whichInterface = 1;
    portID.whichPort = 0;
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID, 0x100L) == 0L);
    CHECK(g_oms.ifaces[0].ports[0].valid == 1u);
    CHECK(g_oms.ifaces[0].ports[0].ioRefNum == 0x100);

    portID.whichPort = 5;
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID, -1L) == 0L);
    CHECK(g_oms.ifaces[0].ports[5].ioRefNum == -1);

    /* Out-of-range port is ignored. */
    portID.whichInterface = 0;
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID, 0x100L) == 0L);
    portID.whichInterface = 9;
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID, 0x100L) == 0L);

    /* omdvGetPortSendProc: proc + paramD0/paramD1 wiring (Spec:
     * paramD0 = readHookRefCon; low word of paramD1 in pkt->appConnRefCon;
     * OMS fills omsUniqueID). */
    portID.whichInterface = 1;
    portID.whichPort = 0;
    sendPars.proc = NULL;
    sendPars.paramD0 = 0L;
    sendPars.paramD1 = -1L;
    sendPars.omsUniqueID = 0;
    CHECK(oms_driver_main(omdvGetPortSendProc, (long)(Ptr)&portID,
               (long)(Ptr)&sendPars) == 0L);
    CHECK(sendPars.proc == g_oms.sendUpp);      /* the UPP, not a raw fn */
    CHECK(sendPars.paramD0 == (1L << 8));           /* (iface<<8)|cable */
    CHECK(sendPars.paramD1 == 0L);
}

static void test_receive_path(void)
{
    static const unsigned char g4packets[8] = {
        0x09u, 0x90u, 0x30u, 0x50u,   /* the real G4 Keystation packets */
        0x09u, 0x90u, 0x30u, 0x00u
    };

    mock_setup(1u);
    mock_start_midi(0x100);

    /* Feed the real G4 traffic through the push hook. */
    mock_queue(0u, g4packets, 8u);
    mock_data_arrives(0);

    CHECK(gCapturedCount == 2u);
    CHECK(gCapturedRefNum[0] == 0x100);
    CHECK(gCaptured[0].flags == omsNoCont);
    CHECK(gCaptured[0].len == 3u);
    CHECK(gCaptured[0].data[0] == 0x90u);
    CHECK(gCaptured[0].data[1] == 0x30u);
    CHECK(gCaptured[0].data[2] == 0x50u);
    CHECK(gCaptured[0].srcIORefNum == 0x100);
    CHECK(gCaptured[1].data[2] == 0x00u);

    /* REGRESSION (the audit's core demand): receiving data performs NO
     * USBGetNextDeviceByClass walk — the per-tick relocation is gone.
     * The only walk so far was the single init locate. */
    CHECK(gNextDeviceCalls == 1);

    CHECK(oms_driver_main(omdvStopMIDI, 0L, 0L) == 0L);
    CHECK(g_oms.midiStarted == 0u);
}

static void test_receive_sysex(void)
{
    static const unsigned char sysex[8] = {
        0x04u, 0xF0u, 0x41u, 0x10u,
        0x08u, 0x42u, 0x12u, 0xF7u     /* CIN 0x8: end with 3 bytes */
    };

    mock_setup(1u);
    mock_start_midi(0x200);

    mock_queue(0u, sysex, 8u);
    mock_data_arrives(0);

    CHECK(gCapturedCount == 2u);
    CHECK(gCaptured[0].flags == omsStartCont);
    CHECK(gCaptured[0].len == 3u);
    CHECK(gCaptured[0].data[0] == 0xF0u);
    CHECK(gCaptured[0].data[1] == 0x41u);
    CHECK(gCaptured[0].data[2] == 0x10u);
    CHECK(gCaptured[1].flags == omsEndCont);
    CHECK(gCaptured[1].len == 3u);
    CHECK(gCaptured[1].data[0] == 0x42u);
    CHECK(gCaptured[1].data[1] == 0x12u);
    CHECK(gCaptured[1].data[2] == 0xF7u);
}

static void test_receive_refnum_minus_one(void)
{
    static const unsigned char note[4] = { 0x0Au, 0x90u, 0x3Cu, 0x40u };
    OMSPortID portID;

    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(oms_driver_main(omdvAddDevices, (long)(Ptr)mock_add1, 0L) == 0L);
    portID.driverID = kUSBMIDI9OMSDriverSignature;
    portID.whichInterface = 1;
    portID.whichPort = 0;
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID, -1L) == 0L);
    CHECK(oms_driver_main(omdvStartMIDI2, 0L, 0L) == 0L);

    mock_queue(0u, note, 4u);
    mock_data_arrives(0);
    CHECK(gCapturedCount == 0u);            /* -1: do not deliver */

    /* Later OMS enables the port; the next data is delivered. */
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID, 0x300L) == 0L);
    mock_queue(0u, note, 4u);
    mock_data_arrives(0);
    CHECK(gCapturedCount == 1u);
    CHECK(gCapturedRefNum[0] == 0x300);
}

/* Data on a cable OMS never configured must be drained (so the stream
 * stays aligned) but NOT delivered: the Spec says the driver should
 * initially assume negative refnums for all possible sources. */
static void test_receive_unconfigured_cable(void)
{
    static const unsigned char note[4] = { 0x1Au, 0x90u, 0x3Cu, 0x40u };
    OMSPortID portID;

    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(oms_driver_main(omdvAddDevices, (long)(Ptr)mock_add1, 0L) == 0L);
    portID.driverID = kUSBMIDI9OMSDriverSignature;
    portID.whichInterface = 1;
    portID.whichPort = 0;
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID, 0x100L) == 0L);
    CHECK(oms_driver_main(omdvStartMIDI2, 0L, 0L) == 0L);

    /* Cable 1 (0x1A = cable 1, CIN 0xA) was never given a refnum. */
    mock_queue(0u, note, 4u);
    mock_data_arrives(0);
    CHECK(gCapturedCount == 0u);

    /* The stream on cable 1 must stay aligned: a later Note Off on the
     * same cable decodes correctly once OMS enables it. */
    {
        static const unsigned char noteoff[4] = { 0x19u, 0x80u, 0x3Cu, 0x00u };
        portID.whichPort = 1;
        CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID,
                              0x400L) == 0L);
        mock_queue(0u, noteoff, 4u);
        mock_data_arrives(0);
        CHECK(gCapturedCount == 1u);
        CHECK(gCapturedRefNum[0] == 0x400);
        CHECK(gCaptured[0].data[0] == 0x80u);
    }
}

/* Bytes that arrived while MIDI was off must be discarded at
 * omdvStartMIDI2, never delivered as fresh notes. */
static void test_backlog_discarded_on_start(void)
{
    static const unsigned char note[4] = { 0x0Au, 0x90u, 0x3Cu, 0x40u };
    OMSPortID portID;

    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(oms_driver_main(omdvAddDevices, (long)(Ptr)mock_add1, 0L) == 0L);
    portID.driverID = kUSBMIDI9OMSDriverSignature;
    portID.whichInterface = 1;
    portID.whichPort = 0;
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID,
                          0x100L) == 0L);

    /* MIDI is not running: the class driver pushes and the shim
     * drains-and-drops (the ring must not fill while MIDI is off). */
    mock_queue(0u, note, 4u);
    mock_data_arrives(0);
    CHECK(gCapturedCount == 0u);            /* not delivered */
    CHECK(gMockQueueLen[0] == 0u);          /* dropped, ring empty */

    /* Start: the stale backlog is discarded, not delivered. */
    CHECK(oms_driver_main(omdvStartMIDI2, 0L, 0L) == 0L);
    CHECK(gCapturedCount == 0u);
    CHECK(gMockQueueLen[0] == 0u);

    /* Live data now flows. */
    mock_queue(0u, note, 4u);
    mock_data_arrives(0);
    CHECK(gCapturedCount == 1u);
}

/* A device that attaches while MIDI is already running: the add
 * notification attaches through the deviceRef (NO device-list walk),
 * and once OMS re-adds the devices, live data flows. */
static void test_attach_while_midi_running(void)
{
    static const unsigned char note[4] = { 0x0Au, 0x90u, 0x3Cu, 0x40u };
    OMSPortID portID;

    mock_setup(1u);
    gDriverPresent = 0;             /* no device at init/start */
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    portID.driverID = kUSBMIDI9OMSDriverSignature;
    portID.whichInterface = 1;
    portID.whichPort = 0;
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID,
                          0x100L) == 0L);
    CHECK(oms_driver_main(omdvStartMIDI2, 0L, 0L) == 0L);
    CHECK(g_oms.table == NULL);

    /* The device appears. The add notification attaches WITHOUT a
     * device-list walk: gNextDeviceCalls counts only the init walk and
     * the omdvStartMIDI2 locate (each an OMS lifecycle transition). */
    gDriverPresent = 1;
    mock_notify(kNotifyAddInterface, gMockDeviceRef);

    CHECK(g_oms.table == &gFakeTable);
    CHECK(gRegisteredEventCallback == oms_rx_event);
    CHECK(gNextDeviceCalls == 2);           /* init + startMIDI2 walks */
    CHECK(gConnIDCalls == 1);               /* notification path */

    /* OMS re-examines the studio setup: devices are re-added and live
     * data flows through the push hook. */
    CHECK(oms_driver_main(omdvAddDevices, (long)(Ptr)mock_add1, 0L) == 0L);
    CHECK(gAddedCount == 1u);
    mock_queue(0u, note, 4u);
    mock_data_arrives(0);
    CHECK(gCapturedCount == 1u);
    CHECK(gCapturedRefNum[0] == 0x100);
    CHECK(gNextDeviceCalls == 2);           /* still no extra walk */
}

/* Unplug: the matching removal notification drops the cached dispatch
 * pointer (the class driver fragment is about to unload). Replug: the
 * add notification re-attaches and delivery resumes. */
static void test_hotplug_lifecycle(void)
{
    static const unsigned char note[4] = { 0x0Au, 0x90u, 0x3Cu, 0x40u };

    mock_setup(1u);
    mock_start_midi(0x100);

    /* Unplug: the driver fragment unloads; the shim must not touch the
     * dangling table. Data cannot arrive from a dead driver, but even
     * if the hook were somehow invoked the drain guard holds. */
    gDriverPresent = 0;
    mock_notify(kNotifyRemoveDevice, gMockDeviceRef);
    CHECK(g_oms.table == NULL);
    CHECK(g_oms.deviceRef == kNoDeviceRef);
    mock_queue(0u, note, 4u);
    mock_data_arrives(0);
    CHECK(gCapturedCount == 0u);
    CHECK(gMockQueueLen[0] == 4u);          /* ring untouched */

    /* Replug: the add notification re-locates through the deviceRef
     * (no walk) and re-registers the hook. The detached shim has no
     * valid interfaces (oms_detach cleared them), so nothing is
     * delivered until OMS re-adds the devices. */
    gDriverPresent = 1;
    mock_notify(kNotifyAddDevice, gMockDeviceRef);
    CHECK(g_oms.table == &gFakeTable);
    CHECK(g_oms.deviceRef == gMockDeviceRef);
    CHECK(gRegisteredEventCallback == oms_rx_event);
    CHECK(gCapturedCount == 0u);
    /* Still only the init walk: replug used the notification. */
    CHECK(gNextDeviceCalls == 1);
    CHECK(gConnIDCalls == 1);

    /* OMS re-scans: devices are re-added (the initial add from
     * mock_start_midi plus this one); the port refnum must be
     * re-established (oms_detach cleared port state), then the queued
     * bytes are delivered on the next push. */
    CHECK(oms_driver_main(omdvAddDevices, (long)(Ptr)mock_add1, 0L) == 0L);
    CHECK(gAddedCount == 2u);
    {
        OMSPortID portID;
        portID.driverID = kUSBMIDI9OMSDriverSignature;
        portID.whichInterface = 1;
        portID.whichPort = 0;
        CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID,
                              0x100L) == 0L);
    }
    mock_data_arrives(0);
    CHECK(gCapturedCount == 1u);
    CHECK(gNextDeviceCalls == 1);           /* still no extra walk */
}

/* An unrelated device's removal must not clear our state. */
static void test_unrelated_remove_keeps_table(void)
{
    mock_setup(1u);
    mock_start_midi(0x100);

    mock_notify(kNotifyRemoveDevice, 0x9999u);
    CHECK(g_oms.table == &gFakeTable);
    CHECK(g_oms.deviceRef == gMockDeviceRef);
    CHECK(gRegisteredEventCallback == oms_rx_event);
}

/* A removal notification for a device that was never located is a
 * no-op (no crash, no state change). */
static void test_remove_before_locate_noop(void)
{
    mock_setup(0u);
    gDriverPresent = 0;
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);

    mock_notify(kNotifyRemoveDevice, gMockDeviceRef);
    CHECK(g_oms.table == NULL);
    mock_notify(kNotifyRemoveInterface, gMockDeviceRef);
    CHECK(g_oms.table == NULL);
}

/* The authentic USBGetDriverConnectionID takes the device ref BY
 * POINTER (G4/DDK 1.5.1f1 gate: "cannot convert 'long' to 'long *'").
 * kConnIDSigGuard above is the compile-time contract; this run keeps it
 * referenced and documents the shape. */
static void test_connid_signature_guard(void)
{
    CHECK(kConnIDSigGuard == oms_mock_USBGetDriverConnectionID);
}

/* The push hook is gated on midiStarted: data that arrives while MIDI
 * is stopped stays in the ring (discarded at the next start). */
static void test_event_gated_when_midi_stopped(void)
{
    static const unsigned char note[4] = { 0x0Au, 0x90u, 0x3Cu, 0x40u };

    mock_setup(1u);
    mock_start_midi(0x100);
    CHECK(oms_driver_main(omdvStopMIDI, 0L, 0L) == 0L);

    mock_queue(0u, note, 4u);
    mock_data_arrives(0);
    CHECK(gCapturedCount == 0u);
    CHECK(gMockQueueLen[0] == 0u);          /* drained and dropped */
}

/* A SysEx message spanning two events must survive: the per-interface
 * stream carries continuation state between drains. */
static void test_sysex_across_events(void)
{
    static const unsigned char part1[4] = { 0x04u, 0xF0u, 0x41u, 0x10u };
    static const unsigned char part2[4] = { 0x08u, 0x42u, 0x12u, 0xF7u };

    mock_setup(1u);
    mock_start_midi(0x100);

    mock_queue(0u, part1, 4u);
    mock_data_arrives(0);
    mock_queue(0u, part2, 4u);
    mock_data_arrives(0);

    CHECK(gCapturedCount == 2u);
    CHECK(gCaptured[0].flags == omsStartCont);
    CHECK(gCaptured[1].flags == omsEndCont);
    CHECK(gCaptured[0].data[0] == 0xF0u);
    CHECK(gCaptured[1].data[2] == 0xF7u);
}

/* A mid-SysEx unplug/replug must not leak continuation state: the
 * attach path resets the per-port streams. */
static void test_replug_resets_stream(void)
{
    static const unsigned char mid[4] = { 0x04u, 0xF0u, 0x41u, 0x10u };
    static const unsigned char tail[4] = { 0x08u, 0x42u, 0x12u, 0xF7u };

    mock_setup(1u);
    mock_start_midi(0x100);

    /* A SysEx starts... */
    mock_queue(0u, mid, 4u);
    mock_data_arrives(0);
    CHECK(gCapturedCount == 1u);
    CHECK(gCaptured[0].flags == omsStartCont);

    /* ...and the device disappears mid-message. */
    gDriverPresent = 0;
    mock_notify(kNotifyRemoveDevice, gMockDeviceRef);
    CHECK(g_oms.table == NULL);

    /* Replug: the tail alone must NOT complete the old SysEx (the
     * streams were reset at attach; F7 alone is an unterminated
     * message and is dropped). */
    gDriverPresent = 1;
    mock_notify(kNotifyAddDevice, gMockDeviceRef);
    mock_queue(0u, tail, 4u);
    mock_data_arrives(0);
    CHECK(gCapturedCount == 1u);            /* no spurious completion */
}

/* RX: a whole SysEx arriving in one end packet is a COMPLETE message:
 * delivered with omsNoCont, not a dangling end. */
static void test_receive_whole_sysex_single_packet(void)
{
    static const unsigned char sysex[4] = { 0x07u, 0xF0u, 0xF7u, 0x00u };

    mock_setup(1u);
    mock_start_midi(0x100);

    mock_queue(0u, sysex, 4u);
    mock_data_arrives(0);

    CHECK(gCapturedCount == 1u);
    CHECK(gCaptured[0].flags == omsNoCont);
    CHECK(gCaptured[0].len == 2u);
    CHECK(gCaptured[0].data[0] == 0xF0u);
    CHECK(gCaptured[0].data[1] == 0xF7u);
}

/* --- PPC 68K bridge (oms_rx_deliver / g_oms.rxRoutine) --------------- */

/* The 68K OMSReceivedFromPort address is resolved exactly once at
 * omdvInit: LinkToOMSGlue() then
 * OMSGetCallAddress(callOMSReceivedFromPort), cached in g_oms.rxRoutine. */
static void test_ppc_rx_routine_resolved(void)
{
    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(gLinkCalls == 1);
    CHECK(gCallAddrCalls == 1);
    CHECK(gLastCallNum == callOMSReceivedFromPort);
    CHECK(g_oms.rxRoutine == (long)(Ptr)mock_oms_received_from_port);
    CHECK(gCallUniversalCalls == 0);    /* nothing delivered yet */
}

/* Delivery goes through CallUniversalProc with the cached routine and
 * the authenticated ProcInfo expression; the wrapper receives the
 * OMSPacket* and the ioRefNum. */
static void test_ppc_rx_delivers_via_wrapper(void)
{
    static const unsigned char pkt4[4] = { 0x09u, 0x90u, 0x3Cu, 0x40u };
    /* The authenticated ProcInfo, written out here (OMS Spec 68K entry:
     * A1 = pkt, D0 = destRefNum; MixedMode.h kRegisterD0 = 0,
     * kRegisterA1 = 5) so a change in the shim's macro fails this test.
     * On the G4 (4-byte pointers) this evaluates to 0x2B802; on this
     * host sizeof(OMSPacket*) is 8 so SIZE_CODE yields 0 for param 1 —
     * the host cannot validate the real register dispatch. */
    const ProcInfoType expected =
        kRegisterBased
        | REGISTER_ROUTINE_PARAMETER(
              1, kRegisterA1, SIZE_CODE(sizeof(OMSPacket *)))
        | REGISTER_ROUTINE_PARAMETER(
              2, kRegisterD0, SIZE_CODE(sizeof(short)));

    mock_setup(1u);
    mock_start_midi(0x5A5A);

    mock_queue(0u, pkt4, 4u);
    mock_data_arrives(0);

    CHECK(gCallUniversalCalls == 1);
    CHECK(gLastCallProc == (UniversalProcPtr)g_oms.rxRoutine);
    CHECK(gLastProcInfo == expected);
    CHECK(gCapturedCount == 1u);
    CHECK(gCapturedRefNum[0] == (short)0x5A5A);
    CHECK(gCaptured[0].data[0] == 0x90u);
    CHECK(gCaptured[0].data[1] == 0x3Cu);
    CHECK(gCaptured[0].data[2] == 0x40u);
}

/* Resolution failure disables delivery safely: the drain keeps running
 * (messages counted) but nothing is delivered and CallUniversalProc is
 * never called. Two failure modes: LinkToOMSGlue error (OMSGetCallAddress
 * must NOT be called) and OMSGetCallAddress returning 0. */
static void test_ppc_rx_resolution_failure_disables(void)
{
    static const unsigned char pkt4[4] = { 0x09u, 0x90u, 0x01u, 0x02u };

    /* Mode 1: glue fails. */
    mock_setup(1u);
    gLinkFail = 1;
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(gLinkCalls == 1);
    CHECK(gCallAddrCalls == 0);
    CHECK(g_oms.rxRoutine == 0L);
    mock_start_midi(0x100);
    mock_queue(0u, pkt4, 4u);
    mock_data_arrives(0);
    CHECK(g_oms.rxMessages == 1ul);         /* drain ran */
    CHECK(gCapturedCount == 0u);            /* delivery disabled */
    CHECK(gCallUniversalCalls == 0);

    /* Mode 2: no routine address. */
    mock_setup(1u);
    gCallAddrFail = 1;
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(gLinkCalls == 1);
    CHECK(gCallAddrCalls == 1);
    CHECK(g_oms.rxRoutine == 0L);
    mock_start_midi(0x100);
    mock_queue(0u, pkt4, 4u);
    mock_data_arrives(0);
    CHECK(g_oms.rxMessages == 1ul);
    CHECK(gCapturedCount == 0u);
    CHECK(gCallUniversalCalls == 0);
}

/* The routine address is resolved once at init, never per packet or per
 * event — the Spec's "best to obtain the address ... rather than
 * calling it via glue" efficiency contract. */
static void test_ppc_rx_no_repeat_resolution(void)
{
    static const unsigned char g4packets[8] = {
        0x09u, 0x90u, 0x30u, 0x50u,
        0x09u, 0x90u, 0x30u, 0x00u
    };

    mock_setup(1u);
    mock_start_midi(0x100);

    mock_queue(0u, g4packets, 8u);
    mock_data_arrives(0);
    mock_queue(0u, g4packets, 8u);
    mock_data_arrives(0);

    CHECK(gCapturedCount == 4u);
    CHECK(gLinkCalls == 1);
    CHECK(gCallAddrCalls == 1);             /* once total */
    CHECK(gCallUniversalCalls == 4);        /* once per packet */
}

static void test_send_hook(void)
{
    OMSPortID portID;
    OMSSendParams sendPars;
    OMSMIDIPacket pkt;

    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    portID.driverID = kUSBMIDI9OMSDriverSignature;
    portID.whichInterface = 1;
    portID.whichPort = 0;
    CHECK(oms_driver_main(omdvGetPortSendProc, (long)(Ptr)&portID,
               (long)(Ptr)&sendPars) == 0L);
    CHECK(sendPars.proc != NULL);
    CHECK(sendPars.paramD0 == (1L << 8));

    /* Default seam (v0.1): no USB bulk-OUT transport yet; the converted
     * packet is dropped and counted. */
    pkt.flags = omsNoCont;
    pkt.len = 1u;
    pkt.data[0] = 0xF8u;
    CallOMSReadHook2(sendPars.proc, &pkt, sendPars.paramD0);
    CHECK(g_oms.txConverted == 1ul);
    CHECK(g_oms.txDropped == 1ul);

    oms_tx_transport = tx_capture;

    /* A conventional Note On: one Event Packet, cable 0, CIN 0xA. */
    pkt.beatTimeStamp = 0L;
    pkt.smpteTimeStamp = 0L;
    pkt.flags = omsNoCont;
    pkt.len = 3u;
    pkt.srcIORefNum = 0u;
    pkt.appConnRefCon = 0u;
    pkt.data[0] = 0x90u;
    pkt.data[1] = 0x3Cu;
    pkt.data[2] = 0x40u;
    pkt.data[3] = 0u;
    CallOMSReadHook2(sendPars.proc, &pkt, sendPars.paramD0);

    CHECK(gTxCapturedCount == 1u);
    CHECK(gTxCaptured[0].portCode == (1u << 8));
    CHECK(gTxCaptured[0].pkt[0] == 0x0Au);      /* cable 0, CIN 0xA */
    CHECK(gTxCaptured[0].pkt[1] == 0x90u);
    CHECK(gTxCaptured[0].pkt[2] == 0x3Cu);
    CHECK(gTxCaptured[0].pkt[3] == 0x40u);

    /* A 2-byte message (Program Change) must not pad a third byte. */
    pkt.flags = omsNoCont;
    pkt.len = 2u;
    pkt.data[0] = 0xC0u;
    pkt.data[1] = 0x05u;
    CallOMSReadHook2(sendPars.proc, &pkt, sendPars.paramD0);
    CHECK(gTxCapturedCount == 2u);
    CHECK(gTxCaptured[1].pkt[0] == 0x0Du);
    CHECK(gTxCaptured[1].pkt[1] == 0xC0u);
    CHECK(gTxCaptured[1].pkt[2] == 0x05u);
    CHECK(gTxCaptured[1].pkt[3] == 0x00u);

    /* 4-byte non-SysEx packet is malformed: dropped, counted. */
    pkt.len = 4u;
    CallOMSReadHook2(sendPars.proc, &pkt, sendPars.paramD0);
    CHECK(gTxCapturedCount == 2u);
    CHECK(g_oms.txMalformed == 1ul);
}

static void test_send_hook_sysex_chunks(void)
{
    OMSPortID portID;
    OMSSendParams sendPars;
    OMSMIDIPacket pkt;

    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    portID.driverID = kUSBMIDI9OMSDriverSignature;
    portID.whichInterface = 1;
    portID.whichPort = 2;
    CHECK(oms_driver_main(omdvGetPortSendProc, (long)(Ptr)&portID,
               (long)(Ptr)&sendPars) == 0L);
    CHECK(sendPars.paramD0 == ((1L << 8) | 2L));

    oms_tx_transport = tx_capture;

    /* OMS delivers SysEx in <=4-byte continuation chunks; USB-MIDI wants
     * 3-byte CIN 0x4 packets. The carry re-chunks: */
    pkt.flags = omsStartCont;
    pkt.len = 4u;
    pkt.data[0] = 0xF0u;
    pkt.data[1] = 0x7Eu;
    pkt.data[2] = 0x7Fu;
    pkt.data[3] = 0x00u;
    CallOMSReadHook2(sendPars.proc, &pkt, sendPars.paramD0); /* -> 0x4 [F0 7E 7F] */

    pkt.flags = omsMidCont;
    pkt.len = 3u;
    pkt.data[0] = 0x01u;
    pkt.data[1] = 0x02u;
    pkt.data[2] = 0x03u;
    CallOMSReadHook2(sendPars.proc, &pkt, sendPars.paramD0); /* -> 0x4 [00 01 02] */

    pkt.flags = omsEndCont;
    pkt.len = 2u;
    pkt.data[0] = 0x04u;
    pkt.data[1] = 0xF7u;
    CallOMSReadHook2(sendPars.proc, &pkt, sendPars.paramD0); /* -> 0x8 [03 04 F7] */

    CHECK(gTxCapturedCount == 3u);
    CHECK(gTxCaptured[0].portCode == ((1u << 8) | 2u));
    CHECK(gTxCaptured[0].pkt[0] == 0x24u);      /* cable 2, CIN 0x4 */
    CHECK(gTxCaptured[0].pkt[1] == 0xF0u);
    CHECK(gTxCaptured[0].pkt[2] == 0x7Eu);
    CHECK(gTxCaptured[0].pkt[3] == 0x7Fu);
    CHECK(gTxCaptured[1].pkt[0] == 0x24u);
    CHECK(gTxCaptured[1].pkt[1] == 0x00u);
    CHECK(gTxCaptured[1].pkt[2] == 0x01u);
    CHECK(gTxCaptured[1].pkt[3] == 0x02u);
    CHECK(gTxCaptured[2].pkt[0] == 0x28u);      /* cable 2, CIN 0x8 */
    CHECK(gTxCaptured[2].pkt[1] == 0x03u);
    CHECK(gTxCaptured[2].pkt[2] == 0x04u);
    /* Whole SysEx in one noCont packet (the MIDI Manager / OMS
     * convention: a complete message carries no continuation flags):
     * F0 7E 7F F7 -> 0x4 [F0 7E 7F] + 0x6 [F7]. */
    pkt.flags = omsNoCont;
    pkt.len = 4u;
    pkt.data[0] = 0xF0u;
    pkt.data[1] = 0x7Eu;
    pkt.data[2] = 0x7Fu;
    pkt.data[3] = 0xF7u;
    CallOMSReadHook2(sendPars.proc, &pkt, sendPars.paramD0);
    CHECK(gTxCapturedCount == 5u);
    CHECK(gTxCaptured[3].pkt[0] == 0x24u);      /* cable 2, CIN 0x4 */
    CHECK(gTxCaptured[3].pkt[1] == 0xF0u);
    CHECK(gTxCaptured[3].pkt[2] == 0x7Eu);
    CHECK(gTxCaptured[3].pkt[3] == 0x7Fu);
    CHECK(gTxCaptured[4].pkt[0] == 0x26u);      /* cable 2, CIN 0x6 */
    CHECK(gTxCaptured[4].pkt[1] == 0xF7u);
    CHECK(g_oms.txConverted == 5ul);
}

/* The send proc is a RoutineDescriptor (UPP), not a raw function: built
 * once at omdvInit via NewOMSReadHook2 -> NewRoutineDescriptor (the
 * authentic OMSUPPs.h constructor), handed out unchanged by
 * omdvGetPortSendProc, released at omdvDispose. */
static void test_send_proc_is_upp(void)
{
    OMSPortID portID;
    OMSSendParams sendPars;

    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(gNewRDCalls == 1);
    CHECK(g_oms.sendUpp != NULL);
    CHECK(gLastRDProc == (ProcPtr)oms_tx_send);
    CHECK(gLastRDProcInfo == uppOMSReadHook2Info);
    CHECK(gLastRDISA == kPowerPCISA);    /* GetCurrentArchitecture on G4 */

    portID.driverID = kUSBMIDI9OMSDriverSignature;
    portID.whichInterface = 1;
    portID.whichPort = 0;
    CHECK(oms_driver_main(omdvGetPortSendProc, (long)(Ptr)&portID,
                          (long)(Ptr)&sendPars) == 0L);
    CHECK(sendPars.proc == g_oms.sendUpp);   /* the UPP, not a raw fn */
    CHECK(gNewRDCalls == 1);                 /* no per-call allocation */

    CHECK(oms_driver_main(omdvDispose, 0L, 0L) == 0L);
    CHECK(gDisposeRDCalls == 1);
    CHECK(g_oms.sendUpp == NULL);
}

/* Repeated omdvInit must not leak RoutineDescriptors: the old UPP is
 * disposed before the new one is created. */
static void test_upp_reinit_recreates(void)
{
    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(gNewRDCalls == 1);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);   /* re-init */
    CHECK(gDisposeRDCalls == 1);      /* old descriptor released */
    CHECK(gNewRDCalls == 2);          /* exactly one alive at a time */
    CHECK(oms_driver_main(omdvDispose, 0L, 0L) == 0L);
    CHECK(gDisposeRDCalls == 2);
}

/* ---- runner ---------------------------------------------------------- */

int test_oms_driver_run(void)
{
    g_failures = 0;
    test_connid_signature_guard();
    test_send_proc_is_upp();
    test_upp_reinit_recreates();
    test_init_locate();
    test_init_no_driver();
    test_init_twice();
    test_init_v1_table_rejected();
    test_add_devices();
    test_port_refnum_and_send_proc();
    test_receive_path();
    test_receive_sysex();
    test_receive_refnum_minus_one();
    test_receive_unconfigured_cable();
    test_backlog_discarded_on_start();
    test_attach_while_midi_running();
    test_hotplug_lifecycle();
    test_unrelated_remove_keeps_table();
    test_remove_before_locate_noop();
    test_event_gated_when_midi_stopped();
    test_sysex_across_events();
    test_replug_resets_stream();
    test_receive_whole_sysex_single_packet();
    test_ppc_rx_routine_resolved();
    test_ppc_rx_delivers_via_wrapper();
    test_ppc_rx_resolution_failure_disables();
    test_ppc_rx_no_repeat_resolution();
    test_send_hook();
    test_send_hook_sysex_chunks();
    printf("test_oms_driver: %s\n", g_failures ? "FAIL" : "OK");
    return g_failures;
}

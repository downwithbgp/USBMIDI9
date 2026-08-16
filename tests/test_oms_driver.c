/*
 * Host tests for the USBMIDI9 OMS driver shim (oms/oms_driver.c,
 * oms/oms_rx.c, oms/oms_tx.c) driven through a scripted mock OMS/USB
 * environment.
 *
 * The shim sources are compiled into this translation unit against the
 * stub headers in classic/host-check/, and every external call the shim
 * makes is intercepted by the mocks below: USBGetNextDeviceByClass,
 * FindSymbol, GetZone/SetZone/SystemZone (dispatch-table lookup),
 * NMInstall/NMRemove (poll timer), OMSReceivedFromPort (input delivery),
 * OMSOpenDriverResFile/OMSCloseDriverResFile.
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

#include <MacTypes.h>
#include <MacErrors.h>
#include <Memory.h>
#include <CodeFragments.h>
#include <USB.h>
#include <OMS.h>
#include <OMSDriver.h>
#include <Notifications.h>

#include "usbmidi9_dispatch.h"
#include "core/midi_stream.h"

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
static int gNMInstallCalls;
static int gNMRemoveCalls;
static NMProcPtr gInstalledNMProc;
static unsigned long gInstalledNMRefCon;

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

/* --- mock USL / CFM / OMS -------------------------------------------- */

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
        *deviceRef = 0x1000u;
        *connID = 1;
        return noErr;
    }
    return kUSBNotFound;
}

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

/* Ticks since startup (the shim's poll timer uses an absolute base). */
UInt32 Ticks(void)
{
    return 1000u;
}

/* The shim under test (included below) defines oms_poll_task; the mock
 * NMInstall captures it directly — the real NMRec.nMsg is 32-bit, which
 * cannot hold a host function pointer, but on the 32-bit G4 it does. */
void oms_poll_task(UInt32 nMessage, UInt32 nRefCon);

OSErr NMInstall(NMRecPtr nmRecPtr)
{
    gNMInstallCalls++;
    CHECK(nmRecPtr->nMsg != 0u);        /* the shim stored the proc */
    gInstalledNMProc = (NMProcPtr)oms_poll_task;
    gInstalledNMRefCon = nmRecPtr->nRefCon;
    return noErr;
}

OSErr NMRemove(NMRecPtr nmRecPtr)
{
    (void)nmRecPtr;
    gNMRemoveCalls++;
    gInstalledNMProc = NULL;
    return noErr;
}

void OMSReceivedFromPort(OMSPacket *pkt, short destRefNum)
{
    if (gCapturedCount < 64u && pkt != NULL) {
        gCaptured[gCapturedCount] = *pkt;
        gCapturedRefNum[gCapturedCount] = destRefNum;
        gCapturedCount++;
    }
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
#define FindSymbol oms_mock_FindSymbol

#define main oms_driver_main
#include "oms/oms_driver.c"
#include "oms/oms_rx.c"
#include "oms/oms_tx.c"
#undef main

/* Install the fake environment with `n` interfaces and a driver present. */
static void mock_setup(unsigned n)
{
    unsigned i;

    gMockNInterfaces = n;
    gDriverPresent = 1;
    gFindCalls = 0;
    gNextDeviceCalls = 0;
    gNMInstallCalls = 0;
    gNMRemoveCalls = 0;
    gInstalledNMProc = NULL;
    gCapturedCount = 0u;
    gTxCapturedCount = 0u;
    gAddedCount = 0u;
    gFakeTable.version = kUSBMIDI9DispatchTableVersion;
    gFakeTable.enumerateInterfaces = fake_enumerate;
    gFakeTable.getInterfaceInfo = fake_get_info;
    gFakeTable.dequeueBytes = fake_dequeue;
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

/* Run one poll (as the Notification Manager would). */
static void mock_poll(void)
{
    if (gInstalledNMProc != NULL) {
        gInstalledNMProc(0u, gInstalledNMRefCon);
    }
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

/* ---- tests ----------------------------------------------------------- */

static void test_init_locate(void)
{
    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(g_oms.table == &gFakeTable);
    CHECK(g_oms.relocates == 1ul);
    CHECK(gFindCalls == 1);
    CHECK(gNextDeviceCalls == 1);

    /* OMS disposes us afterwards; state clears. */
    CHECK(oms_driver_main(omdvDispose, 0L, 0L) == 0L);
    CHECK(g_oms.table == NULL);
}

static void test_init_no_driver(void)
{
    mock_setup(0u);
    gDriverPresent = 0;
    /* The Spec: omdvInit returns an error -> OMS sends omdvDispose. */
    CHECK(oms_driver_main(omdvInit, 0L, 0L) != 0L);
}

static void test_init_version_gate(void)
{
    /* A dispatch table older than the client understands must be
     * rejected (usbmidi9_dispatch.h version rule). */
    mock_setup(1u);
    gFakeTable.version = 0u;
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == (long)kUSBBadDispatchTable);
    CHECK(g_oms.table == NULL);
    gFakeTable.version = kUSBMIDI9DispatchTableVersion;
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(g_oms.table == &gFakeTable);
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
    CHECK(sendPars.proc == (OMSReadHook2)oms_tx_send);
    CHECK(sendPars.paramD0 == (1L << 8));           /* (iface<<8)|cable */
    CHECK(sendPars.paramD1 == 0L);
}

static void test_receive_path(void)
{
    static const unsigned char g4packets[8] = {
        0x09u, 0x90u, 0x30u, 0x50u,   /* the real G4 Keystation packets */
        0x09u, 0x90u, 0x30u, 0x00u
    };
    OMSPortID portID;

    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(oms_driver_main(omdvAddDevices, (long)(Ptr)mock_add1, 0L) == 0L);
    portID.driverID = kUSBMIDI9OMSDriverSignature;
    portID.whichInterface = 1;
    portID.whichPort = 0;
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID, 0x100L) == 0L);

    CHECK(oms_driver_main(omdvStartMIDI, 0L, 0L) == 0L);
    CHECK(oms_driver_main(omdvStartMIDI2, 0L, 0L) == 0L);
    CHECK(g_oms.midiStarted == 1u);
    CHECK(gNMInstallCalls == 1);
    CHECK(gInstalledNMProc != NULL);

    /* Feed the real G4 traffic and run the poll task. */
    mock_queue(0u, g4packets, 8u);
    mock_poll();

    CHECK(gCapturedCount == 2u);
    CHECK(gCapturedRefNum[0] == 0x100);
    CHECK(gCaptured[0].flags == omsNoCont);
    CHECK(gCaptured[0].len == 3u);
    CHECK(gCaptured[0].data[0] == 0x90u);
    CHECK(gCaptured[0].data[1] == 0x30u);
    CHECK(gCaptured[0].data[2] == 0x50u);
    CHECK(gCaptured[0].srcIORefNum == 0x100);
    CHECK(gCaptured[1].data[2] == 0x00u);

    CHECK(oms_driver_main(omdvStopMIDI, 0L, 0L) == 0L);
    CHECK(g_oms.midiStarted == 0u);
    CHECK(gNMRemoveCalls == 1);
}

static void test_receive_sysex(void)
{
    static const unsigned char sysex[8] = {
        0x04u, 0xF0u, 0x41u, 0x10u,
        0x08u, 0x42u, 0x12u, 0xF7u     /* CIN 0x8: end with 3 bytes */
    };
    OMSPortID portID;

    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(oms_driver_main(omdvAddDevices, (long)(Ptr)mock_add1, 0L) == 0L);
    portID.driverID = kUSBMIDI9OMSDriverSignature;
    portID.whichInterface = 1;
    portID.whichPort = 0;
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID, 0x200L) == 0L);
    CHECK(oms_driver_main(omdvStartMIDI2, 0L, 0L) == 0L);

    mock_queue(0u, sysex, 8u);
    mock_poll();

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
    mock_poll();
    CHECK(gCapturedCount == 0u);            /* -1: do not deliver */

    /* Later OMS enables the port; the next data is delivered. */
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID, 0x300L) == 0L);
    mock_queue(0u, note, 4u);
    mock_poll();
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
    mock_poll();
    CHECK(gCapturedCount == 0u);

    /* The stream on cable 1 must stay aligned: a later Note Off on the
     * same cable decodes correctly once OMS enables it. */
    {
        static const unsigned char noteoff[4] = { 0x19u, 0x80u, 0x3Cu, 0x00u };
        portID.whichPort = 1;
        CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID,
                              0x400L) == 0L);
        mock_queue(0u, noteoff, 4u);
        mock_poll();
        CHECK(gCapturedCount == 1u);
        CHECK(gCapturedRefNum[0] == 0x400);
        CHECK(gCaptured[0].data[0] == 0x80u);
    }
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
    sendPars.proc(&pkt, sendPars.paramD0);
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
    sendPars.proc(&pkt, sendPars.paramD0);

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
    sendPars.proc(&pkt, sendPars.paramD0);
    CHECK(gTxCapturedCount == 2u);
    CHECK(gTxCaptured[1].pkt[0] == 0x0Du);
    CHECK(gTxCaptured[1].pkt[1] == 0xC0u);
    CHECK(gTxCaptured[1].pkt[2] == 0x05u);
    CHECK(gTxCaptured[1].pkt[3] == 0x00u);

    /* 4-byte non-SysEx packet is malformed: dropped, counted. */
    pkt.len = 4u;
    sendPars.proc(&pkt, sendPars.paramD0);
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
    sendPars.proc(&pkt, sendPars.paramD0);      /* -> 0x4 [F0 7E 7F] */

    pkt.flags = omsMidCont;
    pkt.len = 3u;
    pkt.data[0] = 0x01u;
    pkt.data[1] = 0x02u;
    pkt.data[2] = 0x03u;
    sendPars.proc(&pkt, sendPars.paramD0);      /* -> 0x4 [00 01 02] */

    pkt.flags = omsEndCont;
    pkt.len = 2u;
    pkt.data[0] = 0x04u;
    pkt.data[1] = 0xF7u;
    sendPars.proc(&pkt, sendPars.paramD0);      /* -> 0x8 [03 04 F7] */

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
    sendPars.proc(&pkt, sendPars.paramD0);
    CHECK(gTxCapturedCount == 5u);
    CHECK(gTxCaptured[3].pkt[0] == 0x24u);      /* cable 2, CIN 0x4 */
    CHECK(gTxCaptured[3].pkt[1] == 0xF0u);
    CHECK(gTxCaptured[3].pkt[2] == 0x7Eu);
    CHECK(gTxCaptured[3].pkt[3] == 0x7Fu);
    CHECK(gTxCaptured[4].pkt[0] == 0x26u);      /* cable 2, CIN 0x6 */
    CHECK(gTxCaptured[4].pkt[1] == 0xF7u);
    CHECK(g_oms.txConverted == 5ul);
}

/* RX: a whole SysEx arriving in one end packet is a COMPLETE message:
 * delivered with omsNoCont, not a dangling end. */
static void test_receive_whole_sysex_single_packet(void)
{
    static const unsigned char sysex[4] = { 0x07u, 0xF0u, 0xF7u, 0x00u };
    OMSPortID portID;

    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(oms_driver_main(omdvAddDevices, (long)(Ptr)mock_add1, 0L) == 0L);
    portID.driverID = kUSBMIDI9OMSDriverSignature;
    portID.whichInterface = 1;
    portID.whichPort = 0;
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID,
                          0x100L) == 0L);
    CHECK(oms_driver_main(omdvStartMIDI2, 0L, 0L) == 0L);

    mock_queue(0u, sysex, 4u);
    mock_poll();

    CHECK(gCapturedCount == 1u);
    CHECK(gCaptured[0].flags == omsNoCont);
    CHECK(gCaptured[0].len == 2u);
    CHECK(gCaptured[0].data[0] == 0xF0u);
    CHECK(gCaptured[0].data[1] == 0xF7u);
}

static void test_poll_relocate(void)
{
    OMSPortID portID;
    static const unsigned char note[4] = { 0x0Au, 0x90u, 0x3Cu, 0x40u };

    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(oms_driver_main(omdvAddDevices, (long)(Ptr)mock_add1, 0L) == 0L);
    portID.driverID = kUSBMIDI9OMSDriverSignature;
    portID.whichInterface = 1;
    portID.whichPort = 0;
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID, 0x100L) == 0L);
    CHECK(oms_driver_main(omdvStartMIDI2, 0L, 0L) == 0L);

    /* Unplug: the driver fragment unloads; the poll re-locates, finds
     * nothing, and does not touch the dangling table. */
    gDriverPresent = 0;
    mock_queue(0u, note, 4u);
    mock_poll();
    CHECK(g_oms.table == NULL);
    CHECK(g_oms.locateFailures == 1ul);
    CHECK(gCapturedCount == 0u);

    /* Replug: the next poll re-locates and drains again. */
    gDriverPresent = 1;
    mock_queue(0u, note, 4u);
    mock_poll();
    CHECK(g_oms.table == &gFakeTable);
    CHECK(gCapturedCount == 1u);
}

/* A SysEx message spanning two polls must survive: the per-poll
 * dispatch re-location must NOT reset the port streams while the driver
 * stays attached. Poll 2 is a CIN 0x4 continuation WITHOUT F0: with the
 * every-tick reset bug, the cleared in_sysex flag would tag it a fresh
 * START (and count a malformed start); with the fix it stays MID. */
static void test_sysex_across_polls(void)
{
    static const unsigned char sysex_start[4] = { 0x04u, 0xF0u, 0x41u, 0x10u };
    static const unsigned char sysex_mid[4] = { 0x04u, 0x42u, 0x12u, 0xF7u };
    OMSPortID portID;

    mock_setup(1u);
    CHECK(oms_driver_main(omdvInit, 0L, 0L) == 0L);
    CHECK(oms_driver_main(omdvAddDevices, (long)(Ptr)mock_add1, 0L) == 0L);
    portID.driverID = kUSBMIDI9OMSDriverSignature;
    portID.whichInterface = 1;
    portID.whichPort = 0;
    CHECK(oms_driver_main(omdvSetPortReceiveRefNum, (long)(Ptr)&portID,
                          0x100L) == 0L);
    CHECK(oms_driver_main(omdvStartMIDI2, 0L, 0L) == 0L);

    mock_queue(0u, sysex_start, 4u);
    mock_poll();
    CHECK(gCapturedCount == 1u);
    CHECK(gCaptured[0].flags == omsStartCont);

    /* Second poll: the run continues — a MID chunk, not a fresh START. */
    mock_queue(0u, sysex_mid, 4u);
    mock_poll();
    CHECK(gCapturedCount == 2u);
    CHECK(gCaptured[1].flags == omsMidCont);
    CHECK(gCaptured[1].data[0] == 0x42u);
    CHECK(gCaptured[1].data[1] == 0x12u);
    CHECK(gCaptured[1].data[2] == 0xF7u);
    CHECK(g_oms.ifaces[0].ports[0].rx.in_sysex == 1u);
    CHECK(g_oms.ifaces[0].ports[0].rx.stats.malformed_start == 0ul);
}

int test_oms_driver_run(void)
{
    g_failures = 0;

    test_init_locate();
    test_init_no_driver();
    test_init_version_gate();
    test_add_devices();
    test_port_refnum_and_send_proc();
    test_receive_path();
    test_receive_sysex();
    test_receive_refnum_minus_one();
    test_receive_unconfigured_cable();
    test_send_hook();
    test_send_hook_sysex_chunks();
    test_receive_whole_sysex_single_packet();
    test_poll_relocate();
    test_sysex_across_polls();

    if (g_failures != 0) {
        printf("oms_driver: %d check(s) failed\n", g_failures);
    }
    return g_failures;
}

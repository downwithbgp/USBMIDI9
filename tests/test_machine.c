/*
 * Host tests for the Classic driver's asynchronous state machine
 * (classic/usb_driver.c) driven through a scripted mock USL.
 *
 * The driver is compiled into this test translation unit (including
 * classic/usb_driver.c) against the stub headers in host-check/,
 * and every USL call it makes (USBAllocMem, USBConfigureInterface,
 * USBFindNextPipe, USBBulkRead, ...) is intercepted by the mock below.
 * All assertions go through the driver's real public surface: the
 * dispatch tables (TheClassDriverPluginDispatchTable,
 * USBMIDI9DispatchTable).
 *
 * This exercises, on the host:
 *   - the full happy path: alloc ring -> configure -> find bulk IN ->
 *     alloc read buffer -> read loop -> ring -> dequeue
 *   - init-stage failures (find pipe not found) stop the machine cleanly
 *   - synchronous completion inside a USL call (the USL invokes the
 *     completion before returning) advances the machine exactly once
 *   - a synchronous completion on the read resubmit does NOT double-submit
 *   - the pathological "kUSBNoErr without completion callback" and
 *     "completion invoked synchronously on every read" cases are both
 *     bounded by the transDepth re-entrancy guard
 *   - the removal protocol: kUSBDeviceBusy while pending, abort, drain,
 *     finalize
 *   - the safe bulk-read helper: task-level submission calls USBBulkRead
 *     directly; completion-context (secondary interrupt level)
 *     submission goes through the authentic CallSecondaryInterruptHandler2
 *     trampoline (including immediate-error propagation through its
 *     result contract); stall retry keeps the direct synchronous
 *     stall-clear; removal still prevents resubmission
 *
 * Portable C (C89/C90).
 */

#include <stdio.h>
#include <string.h>

#include <MacTypes.h>
#include <MacErrors.h>
#include <DriverServices.h>
#include <USB.h>

/* ---- Mock USL (implementations of the stub-header prototypes) ---- */

#define MOCK_PIPE_REF 0x1000
#define MOCK_MAX_PACKET 64
#define MOCK_BUF_SIZE 128

static unsigned char gMockBuffer[MOCK_BUF_SIZE];

static int gMode;                   /* scenario selector */
#define MODE_NORMAL 0               /* async: return kUSBPending, test drains */
#define MODE_SYNC 1                 /* init calls complete synchronously */
#define MODE_NOERR_NO_CB 2          /* return kUSBNoErr, never call completion */
#define MODE_SYNC_READ_LOOP 3       /* every USBBulkRead completes synchronously */

static int gSyncReadNext;           /* next USBBulkRead completes synchronously */

static USBPB *gPendingPB;           /* the call currently "in flight" */
static int gPendingKind;
#define KIND_NONE 0
#define KIND_ALLOC 1
#define KIND_CONFIGURE 2
#define KIND_FIND 3
#define KIND_READ 4
#define KIND_DEALLOC 5

static int gAllocCalls;
static int gConfigureCalls;
static int gFindCalls;
static int gReadCalls;              /* outstanding = number of USBBulkReads issued */
static int gAbortCalls;
static int gDeallocCalls;
static int gStallClearCalls;

static int gExecLevel;              /* CurrentExecutionLevel() result */
static int gTrampolineCalls;        /* CallSecondaryInterruptHandler2 invocations */

static char gTrace[64][12];         /* sequence of mock calls (debugging) */
static int gTraceCount;

static void trace_call(const char *name)
{
    if (gTraceCount < 64) {
        int i;
        for (i = 0; name[i] != '\0' && i < 11; i++) {
            gTrace[gTraceCount][i] = name[i];
        }
        gTrace[gTraceCount][i] = '\0';
        gTraceCount++;
    }
}

static OSStatus gImmediateErr;      /* MODE_NORMAL: immediate error to return */

static void mock_queue(USBPB *pb, int kind)
{
    gPendingPB = pb;
    gPendingKind = kind;
}

static void mock_complete_sync(USBPB *pb)
{
    pb->usbStatus = kUSBNoErr;
    pb->usbCompletion(pb);
}

UInt16 USBToHostWord(UInt16 value)
{
    return value;                   /* mock: already host order */
}

OSStatus USBAllocMem(USBPB *pb)
{
    trace_call("alloc");
    gAllocCalls++;
    if (gMode == MODE_NOERR_NO_CB) {
        return kUSBNoErr;
    }
    if (gMode == MODE_SYNC) {
        pb->usbBuffer = gMockBuffer;
        pb->usbActCount = pb->usbReqCount;
        mock_complete_sync(pb);
        return kUSBNoErr;
    }
    if (gImmediateErr != kUSBNoErr) {
        return gImmediateErr;
    }
    mock_queue(pb, KIND_ALLOC);
    return kUSBPending;
}

OSStatus USBConfigureInterface(USBPB *pb)
{
    trace_call("configure");
    gConfigureCalls++;
    if (gMode == MODE_NOERR_NO_CB) {
        return kUSBNoErr;
    }
    if (gMode == MODE_SYNC) {
        pb->usbOther = 1;           /* one pipe */
        mock_complete_sync(pb);
        return kUSBNoErr;
    }
    if (gImmediateErr != kUSBNoErr) {
        return gImmediateErr;
    }
    mock_queue(pb, KIND_CONFIGURE);
    return kUSBPending;
}

OSStatus USBFindNextPipe(USBPB *pb)
{
    trace_call("find");
    gFindCalls++;
    if (gMode == MODE_NOERR_NO_CB) {
        return kUSBNoErr;
    }
    if (gMode == MODE_SYNC) {
        pb->usbReference = MOCK_PIPE_REF;
        pb->usb.cntl.WValue = MOCK_MAX_PACKET;
        mock_complete_sync(pb);
        return kUSBNoErr;
    }
    if (gImmediateErr != kUSBNoErr) {
        return gImmediateErr;
    }
    mock_queue(pb, KIND_FIND);
    return kUSBPending;
}

OSStatus USBBulkRead(USBPB *pb)
{
    trace_call("read");
    gReadCalls++;
    if (gMode == MODE_NOERR_NO_CB) {
        return kUSBNoErr;
    }
    if (gMode == MODE_SYNC_READ_LOOP) {
        /* Pathological: every read completes instantly with 0 bytes and
         * the callback invoked synchronously (tests the re-entrancy
         * bound in USBMIDI9InitiateTransaction). */
        pb->usbActCount = 0;
        mock_complete_sync(pb);
        return kUSBNoErr;
    }
    if (gSyncReadNext) {
        /* One-shot: complete synchronously (used by the double-submit
         * test). The read loop must never spin here, so only when asked. */
        gSyncReadNext = 0;
        pb->usbActCount = 0;
        mock_complete_sync(pb);
        return kUSBNoErr;
    }
    if (gImmediateErr != kUSBNoErr) {
        return gImmediateErr;
    }
    mock_queue(pb, KIND_READ);
    return kUSBPending;
}

OSStatus USBAbortPipeByReference(USBReference ref)
{
    (void)ref;
    gAbortCalls++;
    return kUSBNoErr;
}

OSStatus USBClearPipeStallByReference(USBPipeRef ref)
{
    (void)ref;
    gStallClearCalls++;
    return kUSBNoErr;
}

OSStatus USBDeallocMem(USBPB *pb)
{
    (void)pb;
    gDeallocCalls++;
    return kUSBNoErr;
}

OSStatus CurrentExecutionLevel(void)
{
    return (OSStatus)gExecLevel;
}

OSStatus CallSecondaryInterruptHandler2(
    OSStatus (*theHandler)(void *callerRefCon, void *result),
    void *refCon, void *callerRefCon, void *result)
{
    (void)refCon;
    /* The authentic mechanism runs the handler at secondary interrupt
     * level with the USL result delivered through *result; the handler's
     * return value is the trampoline's status (the samples ignore it). */
    gTrampolineCalls++;
    return theHandler(callerRefCon, result);
}

/* Test harness helpers: complete the pending call with a scripted
 * result. The caller fills the result fields first. */
static void complete_pending(void)
{
    USBPB *pb = gPendingPB;
    int kind = gPendingKind;

    gPendingPB = 0;
    gPendingKind = KIND_NONE;
    pb->usbCompletion(pb);
    (void)kind;
}

/* ---- Include the driver (after the mocks it calls) ---- */

#include "classic/usb_driver.c"

/* ---- Test framework ---- */

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

/* Fake descriptors passed to initializeInterfaceProc. */
static USBInterfaceDescriptor gFakeInterface = {
    9, 4, 1, 0, 1, 0x01u, 0x03u, 0, 0
};
static USBDeviceDescriptor gFakeDevice = {
    18, 1, 0x0100u, 0, 0, 0, 64, 0x0a4du, 0x0090u, 0x0100u, 0, 0, 0, 1
};

/* Tests share the driver's static registry: dispose every instance left
 * by a previous test (finalize disposes one matching instance per call;
 * the mock USL makes the dealloc safe even with a queued read). */
static void cleanup_all(void)
{
    UInt32 count;

    do {
        (void)TheClassDriverPluginDispatchTable.finalizeProc(
            (USBDeviceRef)0x1234u, &gFakeDevice);
        (void)USBMIDI9DispatchTable.enumerateInterfaces(0, 0u, &count);
    } while (count > 0u);
}

static void mock_reset(int mode)
{
    gMode = mode;
    gPendingPB = 0;
    gPendingKind = KIND_NONE;
    gImmediateErr = kUSBNoErr;
    gAllocCalls = 0;
    gConfigureCalls = 0;
    gFindCalls = 0;
    gReadCalls = 0;
    gAbortCalls = 0;
    gSyncReadNext = 0;
    gDeallocCalls = 0;
    gStallClearCalls = 0;
    gExecLevel = kTaskLevel;
    gTrampolineCalls = 0;
    gTraceCount = 0;
    memset(gMockBuffer, 0, sizeof(gMockBuffer));
}

/* Drive init through the pending (MODE_NORMAL) path to the first read. */
static void drive_init_to_read(void)
{
    OSStatus err;

    err = TheClassDriverPluginDispatchTable.initializeInterfaceProc(
        1u, &gFakeInterface, &gFakeDevice, (USBInterfaceRef)0x1234u);
    CHECK(err == kUSBNoErr);
    CHECK(gPendingKind == KIND_ALLOC);

    gPendingPB->usbBuffer = gMockBuffer;
    gPendingPB->usbActCount = gPendingPB->usbReqCount;
    complete_pending();                       /* alloc ring done */
    CHECK(gPendingKind == KIND_CONFIGURE);

    complete_pending();                       /* configure done */
    CHECK(gPendingKind == KIND_FIND);

    gPendingPB->usbReference = MOCK_PIPE_REF;
    gPendingPB->usb.cntl.WValue = MOCK_MAX_PACKET;
    complete_pending();                       /* find done */
    CHECK(gPendingKind == KIND_ALLOC);

    gPendingPB->usbBuffer = gMockBuffer;
    gPendingPB->usbActCount = gPendingPB->usbReqCount;
    complete_pending();                       /* alloc read buffer done */
    CHECK(gPendingKind == KIND_READ);
}

/* 1. Happy path: init, first read, data into the ring, resubmit. */
static void test_happy_path(void)
{
    static const unsigned char packet[4] = { 0x09u, 0x90u, 0x3Cu, 0x57u };
    unsigned char out[16];
    struct USBMIDI9InterfaceInfo info;
    UInt32 count;
    UInt32 n;

    mock_reset(MODE_NORMAL);
    cleanup_all();
    drive_init_to_read();

    /* Deliver a 4-byte Event Packet read. */
    memcpy(gPendingPB->usbBuffer, packet, 4);
    gPendingPB->usbActCount = 4;
    gPendingPB->usbStatus = kUSBNoErr;
    complete_pending();

    /* The bytes are in the ring and the read was resubmitted. */
    CHECK(gPendingKind == KIND_READ);
    n = USBMIDI9DispatchTable.dequeueBytes(0u, out, sizeof(out));
    CHECK(n == 4u);
    CHECK(memcmp(out, packet, 4) == 0);
    CHECK(USBMIDI9DispatchTable.dequeueBytes(0u, out, sizeof(out)) == 0u);

    /* Interface info: generic MIDIStreaming, vid/pid, max packet 64. */
    CHECK(USBMIDI9DispatchTable.getInterfaceInfo(0u, &info) == kUSBNoErr);
    CHECK(info.vendorID == 0x0a4du);
    CHECK(info.productID == 0x0090u);
    CHECK(info.interfaceClass == 0x01u);
    CHECK(info.interfaceSubClass == 0x03u);
    CHECK(info.maxPacketSize == 64u);
    CHECK(USBMIDI9DispatchTable.enumerateInterfaces(0, 0u, &count) == kUSBNoErr);
    CHECK(count == 1u);
}

/* Event callback globals (v0x0002 setEventCallback). */
static unsigned gEventCalls;
static UInt32 gEventIndex;
static UInt32 gEventRefcon;

static void event_callback(UInt32 index, UInt32 refcon)
{
    gEventCalls++;
    gEventIndex = index;
    gEventRefcon = refcon;
}

/* 1b. v0x0002 event callback: not registered -> no push (Probe path);
 * registered -> the completion invokes it after enqueueing; NULL clears
 * it; out-of-range indices are rejected. */
static void test_event_callback(void)
{
    static const unsigned char packet[4] = { 0x09u, 0x90u, 0x3Cu, 0x57u };
    unsigned char out[16];
    UInt32 n;

    mock_reset(MODE_NORMAL);
    cleanup_all();
    gEventCalls = 0;
    drive_init_to_read();

    /* Not registered: the read completes, no callback, ring intact. */
    memcpy(gPendingPB->usbBuffer, packet, 4);
    gPendingPB->usbActCount = 4;
    gPendingPB->usbStatus = kUSBNoErr;
    complete_pending();
    CHECK(gEventCalls == 0u);
    n = USBMIDI9DispatchTable.dequeueBytes(0u, out, sizeof(out));
    CHECK(n == 4u);

    /* Register, deliver a second packet: the callback fires with the
     * interface index and refcon, after the bytes were enqueued (the
     * ring is drained in the completion's context by the client hook). */
    CHECK(USBMIDI9DispatchTable.setEventCallback(0u, event_callback,
                                                 0xCAFEu) == kUSBNoErr);
    memcpy(gPendingPB->usbBuffer, packet, 4);
    gPendingPB->usbActCount = 4;
    gPendingPB->usbStatus = kUSBNoErr;
    complete_pending();
    CHECK(gEventCalls == 1u);
    CHECK(gEventIndex == 0u);
    CHECK(gEventRefcon == 0xCAFEu);
    n = USBMIDI9DispatchTable.dequeueBytes(0u, out, sizeof(out));
    CHECK(n == 4u);

    /* Out-of-range index is rejected. */
    CHECK(USBMIDI9DispatchTable.setEventCallback(9u, event_callback,
                                                 0u) == kUSBNotFound);

    /* Clearing (NULL callback) restores the no-push behavior. */
    CHECK(USBMIDI9DispatchTable.setEventCallback(0u, NULL, 0u) == kUSBNoErr);
    gEventCalls = 0;
    memcpy(gPendingPB->usbBuffer, packet, 4);
    gPendingPB->usbActCount = 4;
    gPendingPB->usbStatus = kUSBNoErr;
    complete_pending();
    CHECK(gEventCalls == 0u);
}

/* 1c. Removal clears the registered callback: no push into a client
 * whose fragment may be going away. */
static void test_removal_clears_callback(void)
{
    mock_reset(MODE_NORMAL);
    cleanup_all();
    gEventCalls = 0;
    drive_init_to_read();
    CHECK(USBMIDI9DispatchTable.setEventCallback(0u, event_callback,
                                                 7u) == kUSBNoErr);

    /* kNotifyDriverBeingRemoved marks the instance removing and clears
     * the hook (the read is outstanding, so the removal returns busy). */
    CHECK(TheClassDriverPluginDispatchTable.notificationProc(
              kNotifyDriverBeingRemoved, 0, 0u) == kUSBDeviceBusy);
    CHECK(gUSBMIDI9Instances[0].eventCallback == NULL);

    /* The aborted completion must not invoke the (cleared) hook. */
    gPendingPB->usbStatus = kUSBAbortedError;
    gPendingPB->usbActCount = 0;
    complete_pending();
    CHECK(gEventCalls == 0u);
    CHECK(gPendingKind == KIND_NONE);
}

/* 2. Init failure: find pipe returns kUSBNotFound -> machine stops, no
 * garbage pipe ref; removal can still finalize. */
static void test_find_failure_stops_machine(void)
{
    struct USBMIDI9InterfaceInfo info;
    UInt32 count;
    int deallocs_before;

    mock_reset(MODE_NORMAL);
    cleanup_all();

    /* The machine stopped at find: only the ring was allocated, so
     * finalize must deallocate exactly one block (the read buffer was
     * never allocated). */
    deallocs_before = gDeallocCalls;

    CHECK(TheClassDriverPluginDispatchTable.initializeInterfaceProc(
              1u, &gFakeInterface, &gFakeDevice, (USBInterfaceRef)0x1234u)
          == kUSBNoErr);
    gPendingPB->usbBuffer = gMockBuffer;
    gPendingPB->usbActCount = gPendingPB->usbReqCount;
    complete_pending();
    complete_pending();                       /* configure */
    gPendingPB->usbStatus = kUSBNotFound;
    complete_pending();                       /* find fails */

    /* Stopped: no pending call, no pipe/max packet stored. */
    CHECK(gPendingKind == KIND_NONE);
    CHECK(USBMIDI9DispatchTable.getInterfaceInfo(0u, &info) == kUSBNoErr);
    CHECK(info.maxPacketSize == 0u);

    /* Removal is not blocked by a phantom pending completion. */
    CHECK(TheClassDriverPluginDispatchTable.notificationProc(
              kNotifyDriverBeingRemoved, 0, 0u) == kUSBNoErr);
    CHECK(TheClassDriverPluginDispatchTable.finalizeProc(
              (USBDeviceRef)0x1234u, &gFakeDevice) == kUSBNoErr);
    CHECK(USBMIDI9DispatchTable.enumerateInterfaces(0, 0u, &count)
          == kUSBNoErr);
    CHECK(count == 0u);
    CHECK(gDeallocCalls == deallocs_before + 1);   /* ring only */
}

/* 3. Synchronous completion inside every USL call: the machine advances
 * exactly once per call and ends with exactly one pending read. */
static void test_sync_completion_init(void)
{
    OSStatus err;

    mock_reset(MODE_SYNC);
    cleanup_all();
    err = TheClassDriverPluginDispatchTable.initializeInterfaceProc(
        1u, &gFakeInterface, &gFakeDevice, (USBInterfaceRef)0x1234u);
    CHECK(err == kUSBNoErr);
    CHECK(gAllocCalls == 2);                  /* ring + read buffer */
    CHECK(gConfigureCalls == 1);
    CHECK(gFindCalls == 1);
    CHECK(gPendingKind == KIND_READ);         /* exactly one read in flight */
    CHECK(gReadCalls == 1);
    if (g_failures > 0) {                     /* debugging aid */
        int ti;
        printf("mock trace:");
        for (ti = 0; ti < gTraceCount; ti++) {
            printf(" %s", gTrace[ti]);
        }
        printf("\n");
    }
}

/* 4. Synchronous completion on the read resubmit: still exactly one
 * outstanding read (no double-submit on the shared USBPB). */
static void test_sync_completion_read_no_double_submit(void)
{
    mock_reset(MODE_NORMAL);
    cleanup_all();
    drive_init_to_read();
    CHECK(gReadCalls == 1);

    gSyncReadNext = 1;                        /* next resubmit sync-completes */
    gPendingPB->usbActCount = 0;
    gPendingPB->usbStatus = kUSBNoErr;
    complete_pending();                       /* completion resubmits */

    /* The resubmitted read completed synchronously and resubmitted once
     * more; exactly one read is pending (no double-submit). */
    CHECK(gReadCalls == 3);
    CHECK(gPendingKind == KIND_READ);
    CHECK(gSyncReadNext == 0);
}

/* 5. Pathological "kUSBNoErr without callback" is bounded by the sync
 * depth guard: the machine stops instead of recursing forever. */
static void test_noerr_no_callback_guard(void)
{
    struct USBMIDI9InterfaceInfo info;
    UInt32 count;

    mock_reset(MODE_NOERR_NO_CB);
    cleanup_all();
    CHECK(TheClassDriverPluginDispatchTable.initializeInterfaceProc(
              1u, &gFakeInterface, &gFakeDevice, (USBInterfaceRef)0x1234u)
          == kUSBNoErr);
    /* Guard fired: machine stopped, no pending completion blocks removal. */
    CHECK(gPendingKind == KIND_NONE);
    CHECK(TheClassDriverPluginDispatchTable.notificationProc(
              kNotifyDriverBeingRemoved, 0, 0u) == kUSBNoErr);
    CHECK(USBMIDI9DispatchTable.getInterfaceInfo(0u, &info) == kUSBNoErr);
    CHECK(info.maxPacketSize == 0u);
    CHECK(TheClassDriverPluginDispatchTable.finalizeProc(
              (USBDeviceRef)0x1234u, &gFakeDevice) == kUSBNoErr);
    CHECK(USBMIDI9DispatchTable.enumerateInterfaces(0, 0u, &count) == kUSBNoErr);
    CHECK(count == 0u);
}

/* 5b. Pathological synchronous-callback read loop: every USBBulkRead
 * invokes the completion synchronously with 0 bytes, so the resubmit
 * recurses; the re-entrancy bound must stop the machine (not overflow
 * the stack), leaving it finalizable. */
static void test_sync_read_loop_guard(void)
{
    UInt32 count;

    mock_reset(MODE_NORMAL);
    cleanup_all();
    drive_init_to_read();

    gMode = MODE_SYNC_READ_LOOP;
    gPendingPB->usbActCount = 0;
    gPendingPB->usbStatus = kUSBNoErr;
    complete_pending();                       /* resubmit -> sync loop */

    /* The bound stopped the machine: no pending call, and removal is
     * not blocked (finalize can run). Traced: 17 reads = the init read
     * plus 16 resubmits before the 17th nested entry fires the bound. */
    CHECK(gPendingKind == KIND_NONE);
    CHECK(gReadCalls <= (int)kUSBMIDI9MaxSyncDepth + 1);
    CHECK(TheClassDriverPluginDispatchTable.notificationProc(
              kNotifyDriverBeingRemoved, 0, 0u) == kUSBNoErr);
    CHECK(TheClassDriverPluginDispatchTable.finalizeProc(
              (USBDeviceRef)0x1234u, &gFakeDevice) == kUSBNoErr);
    CHECK(USBMIDI9DispatchTable.enumerateInterfaces(0, 0u, &count) == kUSBNoErr);
    CHECK(count == 0u);
}

/* 6. Immediate error from a USL call stops the machine without a pending
 * completion (removal can finalize immediately). */
static void test_immediate_error(void)
{
    UInt32 count;

    mock_reset(MODE_NORMAL);
    cleanup_all();
    gImmediateErr = kUSBPipeStalledError;     /* first call (alloc ring) fails */
    CHECK(TheClassDriverPluginDispatchTable.initializeInterfaceProc(
              1u, &gFakeInterface, &gFakeDevice, (USBInterfaceRef)0x1234u)
          == kUSBNoErr);
    CHECK(gPendingKind == KIND_NONE);
    CHECK(TheClassDriverPluginDispatchTable.notificationProc(
              kNotifyDriverBeingRemoved, 0, 0u) == kUSBNoErr);
    CHECK(TheClassDriverPluginDispatchTable.finalizeProc(
              (USBDeviceRef)0x1234u, &gFakeDevice) == kUSBNoErr);
    CHECK(USBMIDI9DispatchTable.enumerateInterfaces(0, 0u, &count) == kUSBNoErr);
    CHECK(count == 0u);
}

/* 7. Removal protocol: busy while a read is pending, abort the pipe,
 * drain on kUSBAbortedError (never retried), then finalize. */
static void test_removal_protocol(void)
{
    UInt32 count;
    OSStatus status;
    int deallocs_before;

    mock_reset(MODE_NORMAL);
    cleanup_all();
    drive_init_to_read();

    /* The full path allocated ring + read buffer; finalize must
     * deallocate exactly both. */
    deallocs_before = gDeallocCalls;

    status = TheClassDriverPluginDispatchTable.notificationProc(
        kNotifyDriverBeingRemoved, 0, 0u);
    CHECK(status == kUSBDeviceBusy);          /* completion outstanding */
    CHECK(gAbortCalls == 1);                  /* pipe aborted */

    /* The pending read completes with the abort; it must NOT resubmit. */
    gPendingPB->usbStatus = kUSBAbortedError;
    gPendingPB->usbActCount = 0;
    complete_pending();
    CHECK(gPendingKind == KIND_NONE);

    status = TheClassDriverPluginDispatchTable.notificationProc(
        kNotifyDriverBeingRemoved, 0, 0u);
    CHECK(status == kUSBNoErr);               /* drained: finalize allowed */
    CHECK(TheClassDriverPluginDispatchTable.finalizeProc(
              (USBDeviceRef)0x1234u, &gFakeDevice) == kUSBNoErr);
    CHECK(USBMIDI9DispatchTable.enumerateInterfaces(0, 0u, &count) == kUSBNoErr);
    CHECK(count == 0u);
    CHECK(gDeallocCalls == deallocs_before + 2);   /* ring + read buffer */
}

/* 8. Safe bulk read — task level: init and the first read submission
 * run at task level, so USBBulkRead is called directly and the
 * CallSecondaryInterruptHandler2 trampoline is never used. */
static void test_safe_read_task_level_direct(void)
{
    mock_reset(MODE_NORMAL);
    cleanup_all();
    drive_init_to_read();
    CHECK(gReadCalls == 1);
    CHECK(gTrampolineCalls == 0);
}

/* 9. Safe bulk read — secondary interrupt level: a successful data
 * completion delivered at secondary interrupt level resubmits through
 * the authentic trampoline; USBBulkRead is reached through it exactly
 * once, never by direct recursion from the completion context. A later
 * completion at task level goes direct again. */
static void test_safe_read_completion_resubmit_via_trampoline(void)
{
    static const unsigned char packet[4] = { 0x09u, 0x90u, 0x3Cu, 0x57u };
    unsigned char out[16];
    int reads_before;
    int tramps_before;

    mock_reset(MODE_NORMAL);
    cleanup_all();
    drive_init_to_read();                 /* first read issued at task level */
    reads_before = gReadCalls;
    tramps_before = gTrampolineCalls;
    CHECK(reads_before == 1);
    CHECK(tramps_before == 0);

    /* The USL completes the read at secondary interrupt level. */
    gExecLevel = kSecondaryInterruptLevel;
    memcpy(gPendingPB->usbBuffer, packet, 4);
    gPendingPB->usbActCount = 4;
    gPendingPB->usbStatus = kUSBNoErr;
    complete_pending();

    /* Resubmission: exactly one trampoline call, exactly one more
     * USBBulkRead (through it), read pending again, bytes in the ring. */
    CHECK(gTrampolineCalls == tramps_before + 1);
    CHECK(gReadCalls == reads_before + 1);
    CHECK(gPendingKind == KIND_READ);
    CHECK(USBMIDI9DispatchTable.dequeueBytes(0u, out, sizeof(out)) == 4u);
    CHECK(memcmp(out, packet, 4) == 0);

    /* A second completion at task level resubmits directly. */
    gExecLevel = kTaskLevel;
    gPendingPB->usbActCount = 0;
    gPendingPB->usbStatus = kUSBNoErr;
    complete_pending();
    CHECK(gTrampolineCalls == tramps_before + 1);
    CHECK(gReadCalls == reads_before + 2);
}

/* 10. Safe bulk read — stall retry at secondary interrupt level: the
 * stall-clear stays a direct synchronous USL call (sample-verified:
 * PrinterClassDriver's ReadCompletion calls USBClearPipeStallByReference
 * directly), and the retry read goes through the trampoline. */
static void test_safe_read_stall_retry_via_trampoline(void)
{
    int stall_clears_before;
    int tramps_before;
    int reads_before;

    mock_reset(MODE_NORMAL);
    cleanup_all();
    drive_init_to_read();
    stall_clears_before = gStallClearCalls;
    tramps_before = gTrampolineCalls;
    reads_before = gReadCalls;

    gExecLevel = kSecondaryInterruptLevel;
    gPendingPB->usbStatus = kUSBPipeStalledError;
    gPendingPB->usbActCount = 0;
    complete_pending();

    CHECK(gStallClearCalls == stall_clears_before + 1);
    CHECK(gTrampolineCalls == tramps_before + 1);
    CHECK(gReadCalls == reads_before + 1);
    CHECK(gPendingKind == KIND_READ);
}

/* 11. Safe bulk read — immediate error delivered through the trampoline:
 * a secondary-level resubmission whose USBBulkRead fails immediately
 * (non-pending) propagates through the *result contract and stops the
 * machine; removal can still finalize. */
static void test_safe_read_immediate_error_via_trampoline(void)
{
    int tramps_before;
    int reads_before;
    UInt32 count;

    mock_reset(MODE_NORMAL);
    cleanup_all();
    drive_init_to_read();
    tramps_before = gTrampolineCalls;
    reads_before = gReadCalls;

    gExecLevel = kSecondaryInterruptLevel;
    gImmediateErr = kUSBNotRespondingErr;
    gPendingPB->usbStatus = kUSBNoErr;
    gPendingPB->usbActCount = 0;
    complete_pending();   /* resubmit -> trampoline -> immediate error */

    CHECK(gTrampolineCalls == tramps_before + 1);
    CHECK(gReadCalls == reads_before + 1);
    CHECK(gPendingKind == KIND_NONE);   /* stopped, no phantom pending */
    CHECK(TheClassDriverPluginDispatchTable.notificationProc(
              kNotifyDriverBeingRemoved, 0, 0u) == kUSBNoErr);
    CHECK(TheClassDriverPluginDispatchTable.finalizeProc(
              (USBDeviceRef)0x1234u, &gFakeDevice) == kUSBNoErr);
    CHECK(USBMIDI9DispatchTable.enumerateInterfaces(0, 0u, &count) == kUSBNoErr);
    CHECK(count == 0u);
}

/* 12. Safe bulk read — removal still prevents resubmission, even when
 * the abort completion runs at secondary interrupt level: no trampoline
 * call, no new USBBulkRead. */
static void test_safe_read_removal_prevents_resubmission(void)
{
    int tramps_before;
    int reads_before;

    mock_reset(MODE_NORMAL);
    cleanup_all();
    drive_init_to_read();
    tramps_before = gTrampolineCalls;
    reads_before = gReadCalls;

    CHECK(TheClassDriverPluginDispatchTable.notificationProc(
              kNotifyDriverBeingRemoved, 0, 0u) == kUSBDeviceBusy);

    gExecLevel = kSecondaryInterruptLevel;
    gPendingPB->usbStatus = kUSBAbortedError;
    gPendingPB->usbActCount = 0;
    complete_pending();

    CHECK(gPendingKind == KIND_NONE);
    CHECK(gTrampolineCalls == tramps_before);
    CHECK(gReadCalls == reads_before);
}

/* 13. Driver description version: the NumVersion stage byte must use the
 * authentic MacTypes.h release-stage constant (finalStage). The invented
 * kReleaseStageFinal name was rejected by the real CodeWarrior build
 * (undefined identifier); this pins the field and the constant so the
 * M1B source-gate correction cannot regress. */
static void test_driver_description_version(void)
{
    NumVersion v = TheUSBDriverDescription.usbDriverType.usbDriverVersion;

    CHECK(v.majorRev == 1u);
    CHECK(v.minorAndBugRev == 0u);
    CHECK(v.stage == finalStage);     /* authentic UI 3.3 MacTypes.h constant */
    CHECK(v.nonRelRev == 0u);
    CHECK(finalStage == 0x80);        /* final stage, not develop/alpha/beta */
}

int test_machine_run(void)
{
    test_happy_path();
    test_event_callback();
    test_removal_clears_callback();
    test_find_failure_stops_machine();
    test_sync_completion_init();
    test_sync_completion_read_no_double_submit();
    test_noerr_no_callback_guard();
    test_sync_read_loop_guard();
    test_immediate_error();
    test_removal_protocol();
    test_safe_read_task_level_direct();
    test_safe_read_completion_resubmit_via_trampoline();
    test_safe_read_stall_retry_via_trampoline();
    test_safe_read_immediate_error_via_trampoline();
    test_safe_read_removal_prevents_resubmission();
    test_driver_description_version();
    return g_failures;
}

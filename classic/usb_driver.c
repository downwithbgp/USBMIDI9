/*
 * USBMIDI9 Classic Mac OS USB interface class driver.
 *
 * See usb_driver.h for the model. Flow (all verified DDK 1.4.1 patterns,
 * see docs/classic-usb-driver.md §5.3-§5.6, §8.2):
 *
 *   initializeInterfaceProc (system task time, synchronous)
 *     - validate the interface (class 0x01 / subclass 0x03, endpoints)
 *       and decline with an error if unsupported (next-best driver tried)
 *     - claim a per-interface instance from the static registry
 *     - start the refcon-driven asynchronous state machine, return noErr
 *
 *   state machine (one USBPB per instance, kCompletionPending bit)
 *     USBAllocMem ring  ->  USBConfigureInterface  ->  USBFindNextPipe
 *     (bulk IN, MaxPacketSize)  ->  USBAllocMem read buffer  ->
 *     USBBulkRead loop
 *
 *   completion routine (secondary interrupt or task level)
 *     - record usbStatus/usbActCount
 *     - on success copy the bytes into the resident ring
 *     - resubmit unless removing/stopped; never retry an unexpected abort
 *
 *   removal (kNotifyDriverBeingRemoved, system task time)
 *     - mark removing, abort outstanding pipe, return kUSBDeviceBusy
 *       while any completion is outstanding; finalize runs only after
 *       every completion has drained, then disposes the instance.
 *
 * The driver exports TheUSBDriverDescription,
 * TheClassDriverPluginDispatchTable, and USBMIDI9DispatchTable
 * (codewarrior/USBMIDI9.exp).
 *
 * No OMS. No FreeMIDI. No vendor/product-specific behavior: the
 * Keystation is never mentioned in this driver.
 */

#include <MacTypes.h>
#include <MacErrors.h>
#include <DriverServices.h>
#include <USB.h>

#include "usb_driver.h"

/* ------------------------------------------------------------------ */
/* Per-interface instance registry                                     */
/*                                                                    */
/* Instances live in a fixed static array so no heap allocation ever   */
/* happens at interrupt time. Slots 0..count-1 are compact; indices    */
/* are stable while an instance is alive (they may shift when a        */
/* different interface is removed). Registry mutations happen only at  */
/* system task time (initialize/finalize/notify); the completion       */
/* routine touches only its own instance, never the registry.          */
/* ------------------------------------------------------------------ */

static usbmidi9_instance gUSBMIDI9Instances[kUSBMIDI9MaxInterfaces];
static UInt32 gUSBMIDI9InstanceCount;

static void USBMIDI9InitParamBlock(usbmidi9_instance *inst, USBReference ref);
static void USBMIDI9InitiateTransaction(usbmidi9_instance *inst);
static void USBMIDI9CompletionProc(USBPB *pb);
static OSStatus USBMIDI9SecondaryUSBBulkRead(void *pb, void *result);
static OSStatus USBMIDI9SafeUSBBulkRead(usbmidi9_instance *inst);
static void USBMIDI9FillInterfaceInfo(const usbmidi9_instance *inst,
                                      struct USBMIDI9InterfaceInfo *out);
static usbmidi9_instance *USBMIDI9ClaimInstance(void);
static void USBMIDI9DisposeInstance(UInt32 index);
static OSStatus USBMIDI9ValidateHWProc(USBDeviceRef device,
                                       USBDeviceDescriptorPtr pDesc);
static OSStatus USBMIDI9InitializeDeviceProc(USBDeviceRef device,
                                             USBDeviceDescriptorPtr pDesc,
                                             UInt32 busPowerAvailable);
static OSStatus USBMIDI9InitializeInterfaceProc(
    UInt32 interfaceNum, USBInterfaceDescriptorPtr pInterface,
    USBDeviceDescriptorPtr pDevice, USBInterfaceRef interfaceRef);
static OSStatus USBMIDI9FinalizeProc(USBDeviceRef device,
                                     USBDeviceDescriptorPtr pDesc);
static OSStatus USBMIDI9NotifyProc(UInt32 notification, void *pointer,
                                   UInt32 refcon);

/* ------------------------------------------------------------------ */
/* Parameter block setup, after the DDK samples (InitParamBlock /      */
/* SetParamBlock). pbLength is set once at claim time, not here.       */
/* ------------------------------------------------------------------ */

static void USBMIDI9InitParamBlock(usbmidi9_instance *inst, USBReference ref)
{
    inst->pb.usbReference = ref;
    inst->pb.pbVersion = kUSBCurrentPBVersion;
    inst->pb.usb.cntl.WIndex = 0;
    inst->pb.usb.cntl.WValue = 0;
    inst->pb.usbBuffer = nil;
    inst->pb.usbReqCount = 0;
    inst->pb.usbActCount = 0;
    inst->pb.usbFlags = 0;
    inst->pb.usbClassType = 0;
    inst->pb.usbSubclass = 0;
    inst->pb.usbOther = 0;
    inst->pb.usbStatus = kUSBNoErr;
    inst->pb.usbCompletion = USBMIDI9CompletionProc;
}

/* ------------------------------------------------------------------ */
/* Safe bulk-read submission, modeled on the DDK 1.4.1                 */
/* PrinterClassDriver/USBModem SafeUSBBulkRead pattern.                */
/*                                                                    */
/* The authentic samples gate on USB version (Gestalt 'usbv' < 1.2);  */
/* that gate is inert on the G4 (USB >= 1.2), so this driver gates on  */
/* the execution level instead — the DDK-documented generic mechanism  */
/* (Rev 26 p. 102): completion context is not guaranteed (secondary    */
/* interrupt or system task level); CurrentExecutionLevel() discovers  */
/* it; CallSecondaryInterruptHandler2 continues at secondary interrupt */
/* level. The resubmission from USBMIDI9CompletionProc must not        */
/* re-enter USBBulkRead directly from completion context.              */
/* ------------------------------------------------------------------ */

static OSStatus USBMIDI9SecondaryUSBBulkRead(void *pb, void *result)
{
    *(OSStatus *)result = USBBulkRead((USBPB *)pb);
    return noErr;
}

static OSStatus USBMIDI9SafeUSBBulkRead(usbmidi9_instance *inst)
{
    OSStatus result;

    if (CurrentExecutionLevel() != kTaskLevel) {
        CallSecondaryInterruptHandler2(USBMIDI9SecondaryUSBBulkRead, nil,
                                       &inst->pb, &result);
    } else {
        result = USBBulkRead(&inst->pb);
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* Asynchronous state machine. Each case initializes the PB for its    */
/* USL call, sets kCompletionPending, and issues the call. An          */
/* immediate (non-pending) error means no completion will come: clear  */
/* the pending bit and stop so removal can still finalize.             */
/* ------------------------------------------------------------------ */

static void USBMIDI9InitiateTransaction(usbmidi9_instance *inst)
{
    UInt32 issued;                  /* stage of the call we are about to make */
    UInt32 issuedToken;             /* token of this call (see completion) */
    OSStatus err;

    if (inst->removing) {
        inst->pb.usbRefcon = kReturnFromDriver;
        return;
    }

    /* Re-entrancy bound: a USL that completes every call synchronously
     * (with or without invoking the completion) would make the machine
     * recurse forever through the completion; stop it instead. */
    inst->transDepth++;
    if (inst->transDepth > kUSBMIDI9MaxSyncDepth) {
        inst->transDepth--;
        inst->pb.usbRefcon = kReturnFromDriver;
        return;
    }

    issued = inst->pb.usbRefcon & kStageMask;
    issuedToken = ++inst->callToken;
    inst->callInFlight = issuedToken;   /* mark the call about to be issued */
    switch (issued) {
        case kAllocRingState:
            USBMIDI9InitParamBlock(inst, inst->interfaceRef);
            inst->pb.usbReqCount = kUSBMIDI9RingSize;
            inst->pb.usbRefcon |= kCompletionPending;
            err = USBAllocMem(&inst->pb);
            break;

        case kConfigureInterfaceState:
            USBMIDI9InitParamBlock(inst, inst->interfaceRef);
            inst->pb.usbRefcon |= kCompletionPending;
            err = USBConfigureInterface(&inst->pb);
            break;

        case kFindBulkInPipeState:
            USBMIDI9InitParamBlock(inst, inst->interfaceRef);
            inst->pb.usbFlags = kUSBIn;
            inst->pb.usbClassType = kUSBBulk;
            inst->pb.usbRefcon |= kCompletionPending;
            err = USBFindNextPipe(&inst->pb);
            break;

        case kAllocReadBufferState:
            USBMIDI9InitParamBlock(inst, inst->interfaceRef);
            inst->pb.usbReqCount = inst->maxPacketSize
                                   + (kUSBMIDI9ReadAlignment - 1u);
            inst->pb.usbRefcon |= kCompletionPending;
            err = USBAllocMem(&inst->pb);
            break;

        case kReadBulkInPipeState:
            USBMIDI9InitParamBlock(inst, inst->bulkInPipe);
            inst->pb.usbBuffer = inst->readBuffer;
            inst->pb.usbReqCount = inst->maxPacketSize;
            inst->pb.usbRefcon |= kCompletionPending;
            err = USBMIDI9SafeUSBBulkRead(inst);
            break;

        default:
            inst->transDepth--;
            inst->pb.usbRefcon = kReturnFromDriver;
            return;
    }

    if (err != kUSBPending) {
        if (inst->callInFlight != issuedToken) {
            /* The call's completion ran synchronously inside the USL
             * call and already advanced or stopped the machine; nothing
             * to do here. */
        } else if (err == kUSBNoErr
                   && !(inst->pb.usbRefcon & kReturnFromDriver)) {
            /* Synchronous success without a completion callback: drive
             * the success transition ourselves so the machine can never
             * wedge with kCompletionPending set and no completion in
             * flight (which would block removal forever). */
            inst->pb.usbRefcon &= ~kCompletionPending;
            inst->pb.usbStatus = kUSBNoErr;
            inst->pb.usbActCount = 0;
            USBMIDI9CompletionProc(&inst->pb);
        } else if (err == kUSBNoErr) {
            /* Machine already stopped (re-entrancy bound or a nested
             * completion error); keep it stopped. */
        } else {
            /* Immediate error: no completion will be called for this PB. */
            inst->pb.usbRefcon = kReturnFromDriver;
        }
    }
    inst->transDepth--;
}

/* ------------------------------------------------------------------ */
/* Completion routine. Runs at secondary interrupt level or system     */
/* task level; only USL calls and the ring are touched here.           */
/* ------------------------------------------------------------------ */

static void USBMIDI9CompletionProc(USBPB *pb)
{
    usbmidi9_instance *inst = (usbmidi9_instance *)pb;
    UInt32 stage = pb->usbRefcon & kStageMask;

    /* Mark the completed call as no longer in flight, so a synchronous
     * return from the USL call can tell that its completion already ran
     * (see USBMIDI9InitiateTransaction). */
    inst->callInFlight = 0;

    switch (stage) {
        case kAllocRingState:
            if (pb->usbStatus == kUSBNoErr) {
                inst->ringBuffer = (Ptr)pb->usbBuffer;
                inst->pb.usbRefcon = kConfigureInterfaceState;
            } else {
                inst->pb.usbRefcon = kReturnFromDriver;
            }
            break;

        case kConfigureInterfaceState:
            if (pb->usbStatus == kUSBNoErr) {
                /* Pipes are open; pipe count is in usbOther (not needed). */
                inst->pb.usbRefcon = kFindBulkInPipeState;
            } else {
                inst->pb.usbRefcon = kReturnFromDriver;
            }
            break;

        case kFindBulkInPipeState:
            if (pb->usbStatus == kUSBNoErr) {
                inst->bulkInPipe = pb->usbReference;
                inst->maxPacketSize = pb->usb.cntl.WValue;
                inst->pb.usbRefcon = kAllocReadBufferState;
            } else {
                /* No bulk IN pipe (kUSBNotFound) or other failure: stop;
                 * never store a garbage pipe ref (removal would abort it). */
                inst->pb.usbRefcon = kReturnFromDriver;
            }
            break;

        case kAllocReadBufferState:
            if (pb->usbStatus == kUSBNoErr) {
                inst->readBufferBase = (Ptr)pb->usbBuffer;
                /* Align to MaxPacketSize (performance; Rev 26 p. 230).
                 * unsigned long is 32-bit on the PPC target (identical
                 * to the UInt32 idiom) and full pointer width on 64-bit
                 * hosts, so this is exact on both. */
                inst->readBuffer = (Ptr)(((unsigned long)inst->readBufferBase
                                          + (unsigned long)(kUSBMIDI9ReadAlignment - 1u))
                                         & ~(unsigned long)(kUSBMIDI9ReadAlignment - 1u));
                inst->pb.usbRefcon = kReadBulkInPipeState;
            } else {
                inst->pb.usbRefcon = kReturnFromDriver;
            }
            break;

        case kReadBulkInPipeState:
            /* Record actual byte count/status, then copy to the ring. */
            inst->lastReadStatus = pb->usbStatus;
            inst->lastReadCount = pb->usbActCount;
            if (pb->usbStatus == kUSBNoErr && pb->usbActCount > 0u) {
                (void)um9_ring_enqueue((unsigned char *)inst->ringBuffer,
                                       kUSBMIDI9RingSize,
                                       &inst->ringHead, &inst->ringTail,
                                       pb->usbBuffer, pb->usbActCount);
            }
            if (pb->usbStatus == kUSBNoErr) {
                inst->pb.usbRefcon = kReadBulkInPipeState;   /* resubmit */
            } else if (pb->usbStatus == kUSBPipeStalledError) {
                /* Clear the stall and retry (verified sample pattern). */
                (void)USBClearPipeStallByReference(inst->bulkInPipe);
                inst->pb.usbRefcon = kReadBulkInPipeState;
            } else {
                /* kUSBAbortedError (never retried), kUSBNotRespondingErr,
                 * kUSBDeviceDisconnected, or anything else: stop. The
                 * removal notification completes the cleanup. */
                inst->pb.usbRefcon = kReturnFromDriver;
            }
            break;

        default:
            inst->pb.usbRefcon = kReturnFromDriver;
            break;
    }

    inst->pb.usbRefcon &= ~kCompletionPending;
    if (!(inst->pb.usbRefcon & kReturnFromDriver) && !inst->removing) {
        USBMIDI9InitiateTransaction(inst);
    }
}

/* ------------------------------------------------------------------ */
/* USBMIDI9DispatchTable procs (called by the Probe at task time).     */
/* ------------------------------------------------------------------ */

static void USBMIDI9FillInterfaceInfo(const usbmidi9_instance *inst,
                                      struct USBMIDI9InterfaceInfo *out)
{
    out->index = (UInt32)(inst - gUSBMIDI9Instances);
    out->vendorID = inst->vendorID;
    out->productID = inst->productID;
    out->interfaceNum = inst->interfaceNum;
    out->interfaceClass = inst->interfaceClass;
    out->interfaceSubClass = inst->interfaceSubClass;
    out->interfaceProtocol = inst->interfaceProtocol;
    out->maxPacketSize = inst->maxPacketSize;
    out->availableBytes = um9_ring_used(&inst->ringHead, &inst->ringTail,
                                        kUSBMIDI9RingSize);
}

static OSStatus USBMIDI9EnumerateInterfaces(
    struct USBMIDI9InterfaceInfo *outArray, UInt32 maxCount, UInt32 *outCount)
{
    UInt32 i;

    if (outCount == nil) {
        return paramErr;
    }
    if (maxCount > 0u && outArray == nil) {
        return paramErr;
    }

    *outCount = gUSBMIDI9InstanceCount;
    for (i = 0u; i < gUSBMIDI9InstanceCount && i < maxCount; i++) {
        USBMIDI9FillInterfaceInfo(&gUSBMIDI9Instances[i], &outArray[i]);
    }
    return kUSBNoErr;
}

static OSStatus USBMIDI9GetInterfaceInfo(UInt32 index,
                                         struct USBMIDI9InterfaceInfo *outInfo)
{
    if (outInfo == nil) {
        return paramErr;
    }
    if (index >= gUSBMIDI9InstanceCount) {
        return kUSBNotFound;
    }
    USBMIDI9FillInterfaceInfo(&gUSBMIDI9Instances[index], outInfo);
    return kUSBNoErr;
}

static UInt32 USBMIDI9DequeueBytes(UInt32 index, void *buffer, UInt32 maxBytes)
{
    usbmidi9_instance *inst;

    if (index >= gUSBMIDI9InstanceCount) {
        return 0u;
    }
    inst = &gUSBMIDI9Instances[index];
    return um9_ring_dequeue((unsigned char *)inst->ringBuffer,
                            kUSBMIDI9RingSize,
                            &inst->ringHead, &inst->ringTail,
                            buffer, maxBytes);
}

/* ------------------------------------------------------------------ */
/* Apple-required exports (codewarrior/USBMIDI9.exp).                  */
/* ------------------------------------------------------------------ */

/* Generic interface match: class 0x01 (Audio), subclass 0x03
 * (MIDIStreaming), any protocol; no vendor/product anywhere. Loading
 * options 0: generic and interface matching allowed. */
USBDriverDescription TheUSBDriverDescription = {
    /* Signature / version */
    kTheUSBDriverDescriptionSignature,
    kInitialUSBDriverDescriptor,

    /* Device Info: not device specific */
    { 0, 0, 0, 0 },                 /* vendor, product, release, protocol */

    /* Interface Info: generic USB-MIDI */
    { 0, 0, kUSBMIDI9InterfaceClass, kUSBMIDI9InterfaceSubClass, 0 },

    /* Driver Info */
    { kUSBMIDI9DriverName, 0, 0, { 1, 0, finalStage, 0 } },

    /* Loading Options: generic matching allowed, interface matching
     * allowed, protocol not required to match. */
    0
};

USBClassDriverPluginDispatchTable TheClassDriverPluginDispatchTable = {
    kClassDriverPluginVersion,          /* version of this structure */
    USBMIDI9ValidateHWProc,             /* never called for interface drivers */
    USBMIDI9InitializeDeviceProc,       /* device loading declined */
    USBMIDI9InitializeInterfaceProc,    /* the real entry point */
    USBMIDI9FinalizeProc,
    USBMIDI9NotifyProc
};

/* Driver-specific export for Probe/shim communication (HID dispatch
 * table precedent; Rev 26 Ch 4 p. 76). Declared through the struct tag
 * so the symbol name can match the type name (see usbmidi9_dispatch.h). */
struct USBMIDI9DispatchTable USBMIDI9DispatchTable = {
    kUSBMIDI9DispatchTableVersion,
    USBMIDI9EnumerateInterfaces,
    USBMIDI9GetInterfaceInfo,
    USBMIDI9DequeueBytes
};

/* ------------------------------------------------------------------ */
/* Dispatch table procs.                                               */
/* ------------------------------------------------------------------ */

static OSStatus USBMIDI9ValidateHWProc(USBDeviceRef device,
                                       USBDeviceDescriptorPtr pDesc)
{
    (void)device;
    (void)pDesc;
    /* Rev 26 requires validateHWProc non-nil in the table; interface
     * loading never calls it. */
    return kUSBNoErr;
}

static OSStatus USBMIDI9InitializeDeviceProc(USBDeviceRef device,
                                             USBDeviceDescriptorPtr pDesc,
                                             UInt32 busPowerAvailable)
{
    (void)device;
    (void)pDesc;
    (void)busPowerAvailable;
    /* USBMIDI9 is an interface driver only: decline device-level loading
     * so the USB Manager tries the next-best driver. */
    return kUSBInternalErr;
}

static OSStatus USBMIDI9InitializeInterfaceProc(
    UInt32 interfaceNum, USBInterfaceDescriptorPtr pInterface,
    USBDeviceDescriptorPtr pDevice, USBInterfaceRef interfaceRef)
{
    usbmidi9_instance *inst;

    /* Validate the supported interface; decline (return an error) so the
     * USB Manager tries the next-best driver. */
    if (pInterface == nil || pDevice == nil) {
        return kUSBInternalErr;
    }
    if (pInterface->interfaceClass != kUSBMIDI9InterfaceClass
        || pInterface->interfaceSubClass != kUSBMIDI9InterfaceSubClass) {
        return kUSBInternalErr;
    }
    if (pInterface->numEndpoints < 1u) {
        return kUSBInternalErr;
    }

    inst = USBMIDI9ClaimInstance();
    if (inst == nil) {
        return kUSBInternalErr;     /* registry full */
    }

    inst->interfaceRef = interfaceRef;
    inst->bulkInPipe = 0;
    inst->interfaceNum = interfaceNum;
    inst->interfaceClass = pInterface->interfaceClass;
    inst->interfaceSubClass = pInterface->interfaceSubClass;
    inst->interfaceProtocol = pInterface->interfaceProtocol;
    inst->vendorID = USBToHostWord(pDevice->vendor);
    inst->productID = USBToHostWord(pDevice->product);
    inst->maxPacketSize = 0;
    inst->readBufferBase = nil;
    inst->readBuffer = nil;
    inst->ringBuffer = nil;
    inst->ringHead = 0;
    inst->ringTail = 0;
    inst->lastReadStatus = kUSBNoErr;
    inst->lastReadCount = 0;
    inst->callToken = 0;
    inst->callInFlight = 0;
    inst->transDepth = 0;
    inst->removing = false;

    /* Start the asynchronous state machine; return noErr so the driver
     * stays loaded (Rev 26 p. 68-69). */
    USBMIDI9InitParamBlock(inst, interfaceRef);
    inst->pb.pbLength = sizeof(usbmidi9_instance);
    inst->pb.usbRefcon = kAllocRingState;
    USBMIDI9InitiateTransaction(inst);
    return kUSBNoErr;
}

static OSStatus USBMIDI9FinalizeProc(USBDeviceRef device,
                                     USBDeviceDescriptorPtr pDesc)
{
    UInt32 i;

    (void)pDesc;

    /* Per-connection finalize: the first argument is the interface
     * reference for interface drivers (UniversalModule finalize pattern).
     * Dispose that instance; its completions have all drained (the
     * removal notification held finalize off until then). */
    for (i = 0u; i < gUSBMIDI9InstanceCount; i++) {
        if (gUSBMIDI9Instances[i].interfaceRef
            == (USBInterfaceRef)device) {
            USBMIDI9DisposeInstance(i);
            break;
        }
    }
    return kUSBNoErr;
}

static OSStatus USBMIDI9NotifyProc(UInt32 notification, void *pointer,
                                   UInt32 refcon)
{
    (void)pointer;
    (void)refcon;

    switch (notification) {
        case kNotifyDriverBeingRemoved: {
            /* kNotifyDriverBeingRemoved carries no documented identifying
             * data (all DDK samples ignore pointer/refcon), so every
             * instance is marked removing and its outstanding transaction
             * is aborted. Exact for one device (the M1B target); with
             * several independent devices one unplug stops all of them
             * (safe, documented M1B limitation; multi-device is M2).
             * Return kUSBDeviceBusy while any completion is outstanding
             * so finalize runs only when it is safe (Rev 26 p. 71). */
            UInt32 i;
            Boolean busy = false;

            for (i = 0u; i < gUSBMIDI9InstanceCount; i++) {
                usbmidi9_instance *inst = &gUSBMIDI9Instances[i];
                inst->removing = true;
                if (inst->pb.usbRefcon & kCompletionPending) {
                    if (inst->bulkInPipe != 0) {
                        if (USBAbortPipeByReference(inst->bulkInPipe)
                            != kUSBNoErr) {
                            /* Abort failed: don't expect a completion. */
                            inst->pb.usbRefcon &= ~kCompletionPending;
                        }
                    }
                    busy = true;
                }
            }
            return busy ? kUSBDeviceBusy : kUSBNoErr;
        }

        case kNotifyExpertTerminating:
            return kUSBNoErr;

        default:
            return kUSBNoErr;
    }
}

/* ------------------------------------------------------------------ */
/* Registry helpers (system task time only).                           */
/* ------------------------------------------------------------------ */

static usbmidi9_instance *USBMIDI9ClaimInstance(void)
{
    if (gUSBMIDI9InstanceCount >= kUSBMIDI9MaxInterfaces) {
        return nil;
    }
    return &gUSBMIDI9Instances[gUSBMIDI9InstanceCount++];
}

static void USBMIDI9DeallocBlock(usbmidi9_instance *inst, Ptr block)
{
    if (block != nil) {
        inst->pb.usbReference = inst->interfaceRef;
        inst->pb.pbVersion = kUSBCurrentPBVersion;
        inst->pb.usbFlags = 0;
        inst->pb.usbRefcon = 0;
        inst->pb.usbBuffer = block;
        inst->pb.usbCompletion = kUSBNoCallBack;
        (void)USBDeallocMem(&inst->pb);
    }
}

static void USBMIDI9DisposeInstance(UInt32 index)
{
    usbmidi9_instance *inst = &gUSBMIDI9Instances[index];

    USBMIDI9DeallocBlock(inst, inst->ringBuffer);
    USBMIDI9DeallocBlock(inst, inst->readBufferBase);
    inst->ringBuffer = nil;
    inst->readBufferBase = nil;
    inst->readBuffer = nil;

    /* Compact the registry: move the last instance into this slot. */
    gUSBMIDI9InstanceCount--;
    if (index < gUSBMIDI9InstanceCount) {
        gUSBMIDI9Instances[index]
            = gUSBMIDI9Instances[gUSBMIDI9InstanceCount];
    }
}

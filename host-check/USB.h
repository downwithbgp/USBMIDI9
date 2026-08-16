/*
 * Minimal stub of the Mac OS USB DDK 1.4.1 USB.h surface used by the
 * USBMIDI9 Classic sources. NOT the real header — the G4 build uses the
 * authentic DDK 1.4.1 Interfaces/USB.h (plus the CodeWarrior-era
 * Universal Headers). Type/layout/constant verification:
 * docs/classic-usb-driver.md §8.1 (authentic USB.h 1.4.1, ADC CD-ROM
 * January 2001). Note: USB.h 1.4.1 does declare the USL prototypes,
 * gated by CALL_NOT_IN_CARBON (1 = Classic, the DDK-era default).
 */

#ifndef USBMIDI9_HOST_CHECK_USB_H
#define USBMIDI9_HOST_CHECK_USB_H

#include "MacTypes.h"
#include "MacErrors.h"
#include "CodeFragments.h"

#define CALL_NOT_IN_CARBON 1

typedef SInt32 USBReference;
typedef USBReference USBDeviceRef;
typedef USBReference USBInterfaceRef;
typedef USBReference USBPipeRef;
typedef UInt32 USBPipeState;
typedef UInt32 USBCount;
typedef UInt32 USBFlags;

/* --- Descriptor structures (USB.h 1.4.1) --- */

struct USBDeviceDescriptor {
    UInt8 length;
    UInt8 descType;
    UInt16 usbRel;
    UInt8 deviceClass;
    UInt8 deviceSubClass;
    UInt8 protocol;
    UInt8 maxPacketSize;
    UInt16 vendor;
    UInt16 product;
    UInt16 devRel;
    UInt8 manuIdx;
    UInt8 prodIdx;
    UInt8 serialIdx;
    UInt8 numConf;
};
typedef struct USBDeviceDescriptor USBDeviceDescriptor;
typedef USBDeviceDescriptor *USBDeviceDescriptorPtr;

struct USBInterfaceDescriptor {
    UInt8 length;
    UInt8 descriptorType;
    UInt8 interfaceNumber;
    UInt8 alternateSetting;
    UInt8 numEndpoints;
    UInt8 interfaceClass;
    UInt8 interfaceSubClass;
    UInt8 interfaceProtocol;
    UInt8 interfaceStrIndex;
};
typedef struct USBInterfaceDescriptor USBInterfaceDescriptor;
typedef USBInterfaceDescriptor *USBInterfaceDescriptorPtr;

/* --- Parameter block --- */

struct usbControlBits {
    UInt8 BMRequestType;
    UInt8 BRequest;
    UInt16 WValue;
    UInt16 WIndex;
    UInt16 reserved4;
};

union USBVariantBits {
    struct usbControlBits cntl;
};
typedef union USBVariantBits USBVariantBits;

struct USBPB;   /* forward: USBCompletion refers to it */

typedef void (*USBCompletion)(struct USBPB *pb);

struct USBPB {
    void *qlink;
    UInt16 qType;
    UInt16 pbLength;
    UInt16 pbVersion;
    UInt16 reserved1;
    UInt32 reserved2;
    OSStatus usbStatus;
    USBCompletion usbCompletion;
    UInt32 usbRefcon;
    USBReference usbReference;
    void *usbBuffer;
    USBCount usbReqCount;
    USBCount usbActCount;
    USBFlags usbFlags;
    USBVariantBits usb;
    UInt32 usbFrame;
    UInt8 usbClassType;
    UInt8 usbSubclass;
    UInt8 usbProtocol;
    UInt8 usbOther;
    UInt32 reserved6;
    UInt16 reserved7;
    UInt16 reserved8;
};
typedef struct USBPB USBPB;

/* --- Constants (values verified from USB.h 1.4.1) --- */

#define kUSBCurrentPBVersion 0x0100u
#define kUSBNoCallBack ((USBCompletion)-1L)

#define kUSBOut 0
#define kUSBIn 1
#define kUSBBulk 2
#define kUSBAnyType 0xFFu

#define kTheUSBDriverDescriptionSignature 0x75736264u /* FOUR_CHAR_CODE('usbd') */
#define kInitialUSBDriverDescriptor 0
#define kClassDriverPluginVersion 0x00001100u

#define kUSBDoNotMatchGenericDevice 0x00000001u
#define kUSBDoNotMatchInterface     0x00000002u
#define kUSBProtocolMustMatch       0x00000004u
#define kUSBInterfaceMatchOnly      0x00000008u

#define kNotifyExpertTerminating    0x00000008u
#define kNotifyDriverBeingRemoved   0x0000000Bu

#define kNoDeviceRef (-1)
#define kUSBAnyClass    0xFFFFu
#define kUSBAnySubClass 0xFFFFu
#define kUSBAnyProtocol 0xFFFFu
#define kUSBAnyVendor   0xFFFFu
#define kUSBAnyProduct  0xFFFFu

/* --- USB Manager device notification (USBManagerLib) ---
 * Authentic model: Mac OS USB DDK API Reference Rev. 26, Ch 6 "USB
 * Manager Reference" (p. 185-188), and the verified imports of Opcode's
 * OMS 2.3.8 OMS USB Manager (USBInstallDeviceNotification /
 * USBRemoveDeviceNotification from USBManagerLib). The notification
 * values below are the ones the real OMS USB Manager switches on in its
 * notification callback (PEF disassembly: 0=AddDevice, 1=RemoveDevice,
 * 2=AddInterface, 3=RemoveInterface; input filter kNotifyAnyEvent=0xff
 * per Rev 26 Ch 6). Signatures follow USB.h 1.4.1 exactly (the G4 build
 * uses the same USB Manager surface in DDK 1.5.1f1): the callback takes
 * void *pb and USBGetDriverConnectionID takes a POINTER to the device
 * ref. */

typedef UInt8 USBNotificationType;

enum {
    kNotifyAddDevice        = 0,
    kNotifyRemoveDevice     = 1,
    kNotifyAddInterface     = 2,
    kNotifyRemoveInterface  = 3,
    kNotifyAnyEvent         = 0xFF
};

struct USBDeviceNotificationParameterBlock;
/* Authentic DDK form (USB.h 1.4.1): CALLBACK_API_C( void,
 * USBDeviceNotificationCallbackProcPtr )(void *pb) — the pb arrives as
 * void * and the callback casts it. Apple's own StorageClassShim.c
 * sample assigns its typed callback with an explicit cast. */
typedef void (*USBDeviceNotificationCallbackProcPtr)(void *pb);

struct USBDeviceNotificationParameterBlock {
    UInt16 pbLength;
    UInt16 pbVersion;
    USBNotificationType usbDeviceNotification;  /* in: filter, out: event */
    UInt8 reserved1;
    USBDeviceRef usbDeviceRef;
    UInt16 usbClass;
    UInt16 usbSubClass;
    UInt16 usbProtocol;
    UInt16 usbVendor;
    UInt16 usbProduct;
    OSStatus result;
    UInt32 token;
    USBDeviceNotificationCallbackProcPtr callback;
    UInt32 refcon;
};
typedef struct USBDeviceNotificationParameterBlock
    USBDeviceNotificationParameterBlock;
typedef USBDeviceNotificationParameterBlock *
    USBDeviceNotificationParameterBlockPtr;

void USBInstallDeviceNotification(USBDeviceNotificationParameterBlock *pb);
OSStatus USBRemoveDeviceNotification(UInt32 token);
/* Authentic DDK signature (USB.h 1.4.1; Rev 26 Ch 6 p. 181; Apple's
 * SampleShim.c calls USBGetDriverConnectionID(&pb->usbDeviceRef,
 * &connID)): the first parameter is a POINTER to the device ref. The
 * G4 build (DDK 1.5.1f1 header) rejects the by-value form with
 * "cannot convert 'long' to 'long *'". */
OSStatus USBGetDriverConnectionID(USBDeviceRef *deviceRef,
                                  CFragConnectionID *connID);

/* --- Driver description and dispatch table (USB.h 1.4.1) --- */

struct USBDeviceInfo {
    UInt16 usbVendorID;
    UInt16 usbProductID;
    UInt16 usbDeviceReleaseNumber;
    UInt16 usbDeviceProtocol;
};

struct USBInterfaceInfo {
    UInt8 usbConfigValue;
    UInt8 usbInterfaceNum;
    UInt8 usbInterfaceClass;
    UInt8 usbInterfaceSubClass;
    UInt8 usbInterfaceProtocol;
};

struct USBDriverType {
    Str31 nameInfoStr;
    UInt8 usbDriverClass;
    UInt8 usbDriverSubClass;
    struct NumVersion usbDriverVersion;
};

struct USBDriverDescription {
    OSType usbDriverDescSignature;
    UInt32 usbDriverDescVersion;
    struct USBDeviceInfo usbDeviceInfo;
    struct USBInterfaceInfo usbInterfaceInfo;
    struct USBDriverType usbDriverType;
    UInt32 usbDriverLoadingOptions;
};
typedef struct USBDriverDescription USBDriverDescription;

typedef OSStatus (*USBDValidateHWProcPtr)(USBDeviceRef device,
                                          USBDeviceDescriptorPtr pDesc);
typedef OSStatus (*USBDInitializeDeviceProcPtr)(USBDeviceRef device,
                                                 USBDeviceDescriptorPtr pDesc,
                                                 UInt32 busPowerAvailable);
typedef OSStatus (*USBDInitializeInterfaceProcPtr)(
    UInt32 interfaceNum, USBInterfaceDescriptorPtr pInterface,
    USBDeviceDescriptorPtr pDevice, USBInterfaceRef interfaceRef);
typedef OSStatus (*USBDFinalizeProcPtr)(USBDeviceRef device,
                                        USBDeviceDescriptorPtr pDesc);
typedef UInt32 USBDriverNotification;
typedef OSStatus (*USBDDriverNotifyProcPtr)(USBDriverNotification notification,
                                            void *pointer, UInt32 refcon);

struct USBClassDriverPluginDispatchTable {
    UInt32 pluginVersion;
    USBDValidateHWProcPtr validateHWProc;
    USBDInitializeDeviceProcPtr initializeDeviceProc;
    USBDInitializeInterfaceProcPtr initializeInterfaceProc;
    USBDFinalizeProcPtr finalizeProc;
    USBDDriverNotifyProcPtr notificationProc;
};
typedef struct USBClassDriverPluginDispatchTable
    USBClassDriverPluginDispatchTable;

/* --- USL prototypes (present in USB.h 1.4.1, CALL_NOT_IN_CARBON) --- */

UInt16 USBToHostWord(UInt16 value);
OSStatus USBConfigureInterface(USBPB *pb);
OSStatus USBFindNextPipe(USBPB *pb);
OSStatus USBBulkRead(USBPB *pb);
OSStatus USBClearPipeStallByReference(USBPipeRef ref);
OSStatus USBAbortPipeByReference(USBReference ref);
OSStatus USBAllocMem(USBPB *pb);
OSStatus USBDeallocMem(USBPB *pb);
OSStatus USBGetNextDeviceByClass(USBDeviceRef *deviceRef,
                                 CFragConnectionID *connID, UInt16 theClass,
                                 UInt16 theSubClass, UInt16 theProtocol);

#endif /* USBMIDI9_HOST_CHECK_USB_H */

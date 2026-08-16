/*
 * Minimal stub of the Opcode OMS client/driver type surface used by the
 * USBMIDI9 OMS shim sources. NOT the real headers — this directory exists
 * only so `make check-classic` can syntax/type-check the Classic sources
 * on Linux. The Power Mac G4 build uses the authentic Opcode OMS SDK
 * headers (OMS.h, OMSDriver.h, OMSDrvUPPs.h — see
 * ~/research/oms/PROVENANCE.md and docs/research.md "OMS").
 *
 * Every type/constant below was read from the authentic OMS 2.0 SDK
 * headers (28-Jan-98): OMS.h / OMSTypes.h. Field order and types are
 * copied verbatim; the real headers additionally apply mac68k alignment
 * pragmas on PPC builds, which the G4 compiler handles and this stub
 * intentionally omits (the host test only needs self-consistent layout).
 *
 * `pascal` is defined empty for the Linux compile; on the G4 the real
 * Universal Headers define it as the PPC pascal calling convention.
 */

#ifndef USBMIDI9_HOST_CHECK_OMS_H
#define USBMIDI9_HOST_CHECK_OMS_H

#include "MacTypes.h"

#define pascal                /* empty on host; real convention on G4 */

#define OMS_STRING_LEN        31

/* OD_MAX_MANUF_LEN / OD_MAX_MODEL_LEN (OMS.h) */
#define OD_MAX_MANUF_LEN      23
#define OD_MAX_MODEL_LEN      23

/* Primitive types (OMSTypes.h). */
typedef unsigned char OMSBool;
typedef unsigned long OMSSignature;   /* same as OSType */
typedef unsigned short OMSUniqueID;
typedef unsigned char OMSString[OMS_STRING_LEN + 1];   /* pascal string */

/* OMSFile = FSSpec on the Mac (OMSTypes.h). FSSpec layout per
 * Files.h (Universal Interfaces): vRefNum, parID, name. */
typedef struct FSSpec {
    short vRefNum;
    long parID;
    Str31 name;
} FSSpec;
typedef FSSpec OMSFile;

/* Packet continuation / timestamp flag bits (OMS.h). */
#define omsContMask          0x03
#define omsNoCont            0x00
#define omsStartCont         0x01
#define omsMidCont           0x03
#define omsEndCont           0x02
#define omsPktBeatTStamped   0x80
#define omsPktSMPTETStamped  0x40

/* OMSDevice flags (OMS.h). */
enum {
    kIsPatcher = 0x80, kIsController = 0x40, kIsReceiver = 0x20,
    kIsMultitimbral = 0x10, kOutConnected = 0x08, kInConnected = 0x04,
    kOutConnectedToTopLevel = 0x02, kInConnectedToTopLevel = 0x01
};
enum {
    kIsTopLevelDriverOwned = 0x80, kSendsSync = 0x20,
    kReceivesSync = 0x10, kOMSInvisible = 0x08, kNoChildDevices = 0x04,
    kPseudoVirtual = 0x02, kIntfCantRouteSync = 0x01
};
#define kIsMIDIInterface kIsTopLevelDriverOwned   /* historical */

/* OMSSerPortID (OMS.h). */
typedef struct OMSSerPortID {
    short hwDrvNum;
    short drvPortNum;
} OMSSerPortID;

/* OMSDevice (OMS.h). Field order/types verbatim; "fields below this
 * point are not guaranteed to exist" per the header. */
typedef struct OMSDevice {
    unsigned long deviceRefNum;      /* only low word significant */
    struct OMSDevice **parentDevice;
    struct OMSDevice **siblingDevices;
    struct OMSDevice **childDevices;
    unsigned char driverSpecific[4];
    short whichOut;
    OMSSignature ownerDriver;
    OMSUniqueID uniqueID;
    short obsoleteGalaxyID;
    unsigned char flags1;
    unsigned char parentPatcherPgm;
    unsigned char patcherDfltProgram;
    unsigned char flags2;
    unsigned short flags3;
    short deviceSize;
    OMSUniqueID driverOwnedUniqueID;
    short nOutputPorts;
    short midiDeviceID;
    short midiChannels;
    short iconID;
    OMSString devName;
    unsigned char manuf[OD_MAX_MANUF_LEN + 1];
    unsigned char model[OD_MAX_MODEL_LEN + 1];
    struct OMSDevice **pairedDevice;
    unsigned char minDeviceID, maxDeviceID;
    unsigned char reserved[2];
    char manufModel8[8];
    OMSSerPortID serPortID;
    short locationIconID;
    OMSString locationName;
} OMSDevice, **OMSDeviceH;

/* OMSPortID (OMS.h). */
typedef struct OMSPortID {
    OMSSignature driverID;           /* ownerDriver from OMSDevice */
    short whichInterface;            /* whichOut from OMSDevice */
    short whichPort;                 /* attached devices' whichOut */
} OMSPortID;

/* OMSDeviceList (OMSDriver.h). */
typedef struct OMSDeviceList {
    short numDevices;
    OMSDeviceH device[20];
} OMSDeviceList, **OMSDeviceListH;

/* OMSAddDevParams (OMSDriver.h), compat-level-1 omdvAddDevices par2. */
typedef struct OMSAddDevParams {
    unsigned char *portsUsed;
    unsigned char *baudRatePerPort;
    OMSBool addHardwareManually;
} OMSAddDevParams;

/* OMSPacket (OMS.h) — the driver-internal packet form; len INCLUDES the
 * 6-byte header except when passed to OMSReceivedFromPort, where the Spec
 * requires the number of MIDI data bytes only. */
typedef struct OMSPacket {
    unsigned char flags;
    unsigned char len;               /* including 6 bytes before data */
    unsigned short srcIORefNum;
    unsigned short appConnRefCon;
    unsigned char data[4];
} OMSPacket;

/* OMS 2.0 MIDI packet (OMS.h). */
typedef struct OMSMIDIPacket {
    long beatTimeStamp;
    long smpteTimeStamp;
    unsigned char flags;
    unsigned char len;               /* number of MIDI bytes ONLY */
    unsigned short srcIORefNum;
    unsigned short appConnRefCon;
    unsigned char data[4];
} OMSMIDIPacket;

typedef struct OMSMIDIPacket255 {
    long beatTimeStamp;
    long smpteTimeStamp;
    unsigned char flags;
    unsigned char len;
    unsigned short srcIORefNum;
    unsigned short appConnRefCon;
    unsigned char data[255];
} OMSMIDIPacket255;

/* OMSCALLBACK / TYPEDEF_OMSPROC (OMSTypes.h). */
#define OMSCALLBACK(rettype) pascal rettype
#define TYPEDEF_OMSPROC(rettype, name) typedef pascal rettype (*name)

/* OMSReadHook2 (OMS.h) — the send proc an OMS driver returns for a port. */
TYPEDEF_OMSPROC(void, OMSReadHook2)(OMSMIDIPacket *packet, long readHookRefCon);

/* OMS_MAC_CFM (OMSTypes.h): 1 on the G4 PPC CFM target (authentic:
 * `#if USESROUTINEDESCRIPTORS || TARGET_RT_MAC_CFM`). OMS.h includes
 * the generated OMSUPPs.h (verbatim) right here, exactly like the
 * authentic header: it supplies OMSReadHook2UPP / NewOMSReadHook2 /
 * CallOMSReadHook2, whose procInfo describes a Pascal stack-based
 * routine. */
#define OMS_MAC_CFM 1
#include "OMSUPPs.h"

/* OMS error type (OMSTypes.h): typedef short OMSErr; */
typedef short OMSErr;

/* OMS glue API (OMS.h, authentic signatures — OMSAPI(pascal) is empty
 * on the host). LinkToOMSGlue initializes the glue without sign-in
 * (Spec: call it from an OMS driver that needs to call OMS);
 * OMSGetCallAddress returns the 68K address of an internal routine
 * (callOMSReceivedFromPort = 112, OMSGlueProcs.h); PPC code must call
 * the returned 68K address via CallUniversalProc (MixedMode.h). */
extern OMSErr LinkToOMSGlue(void);
extern long OMSGetCallAddress(short callNum);

#endif /* USBMIDI9_HOST_CHECK_OMS_H */

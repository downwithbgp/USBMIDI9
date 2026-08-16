/*
 * Minimal stub of the Opcode OMSDriver.h surface used by the USBMIDI9 OMS
 * shim. NOT the real header — see OMS.h in this directory for the policy.
 * Every item below was read from the authentic OMS 2.0 SDK OMSDriver.h
 * (28-Jan-98) and the OMS Programming Interface spec (Mar 1995),
 * "OMS Drivers" chapter.
 */

#ifndef USBMIDI9_HOST_CHECK_OMSDRIVER_H
#define USBMIDI9_HOST_CHECK_OMSDRIVER_H

#include "OMS.h"

/* Messages to the driver code resource: pascal long OMdv(short msg,
 * long par1, long par2); (OMSDriver.h). */
enum {
    omdvInit                = 0,    /* par1: OMSFile *file; return OSErr */
    omdvDispose             = 1,    /* no parameters */
    omdvAddDevices          = 2,    /* compat 1: par1 OMSDvrAdd1DevProc1,
                                       par2 OMSAddDevParams * */
    omdvConfigure           = 3,    /* config dialog */
    omdvSetInterfaceList    = 4,    /* par1: list of interfaces */
    omdvGetMenu             = 5,    /* return MenuHandle or null */
    omdvDoMenu              = 6,    /* par1: menu item number */
    omdvCloseUserInterface  = 7,
    omdvStartMIDI           = 16,   /* par1: port usage */
    omdvStopMIDI            = 17,
    omdvGetPortSendProc     = 18,   /* par1: OMSPortID *,
                                       par2: OMSSendParams * */
    omdvSetPortReceiveRefNum = 19,  /* par1: ptr to OMSPortID,
                                       par2: low word=refNum */
    omdvStartupComplete     = 30,
    omdvStartMIDI2          = 35,   /* no parameters */
    omdvClosedMIDISetup     = 36,
    omdvStudioSetupChanged  = 37,
    omdvTestDevice          = 39,
    omdvDifferentStudioSetup = 40,
    omdvRemoveOutput        = 41,   /* par1: ptr to OMSPortID */
    omdvConnectsChanged     = 42,   /* par1: OMSConnectionListH */
    omdvGetCommSpeed        = 43    /* par1: OMSPortID *, return speed */
};

/* Bits in OMSDriverParams.flags (OMSDriver.h). */
enum {
    kNoSyncRouting          = 0x80,
    kUseDeviceInfoDialog    = 0x20,
    kAllowIDEditing         = 0x10,
    kAlwaysLoad             = 0x08,
    kOmitFromAutoSetup      = 0x04
};

/* Structure of the driver's 'OMdi' resource (OMSDriver.h). */
typedef struct OMSDriverParams {
    short id;                        /* load order; assigned by Opcode */
    OMSBool xxisSmart;
    OMSBool hasMenuOrWindows;
    short xxportNumM, xxportNumB;    /* obsolete */
    unsigned char flags;
    unsigned char driverCompatibilityLevel;
    unsigned char reservedFlags[6];
} OMSDriverParams;

/* Driver entry point (OMSDriver.h): OMSCALLBACK(long) main(short msg,
 * long par1, long par2); */
typedef long (*OMSDriverProc)(short msg, long par1, long par2);

/* omdvAddDevices callbacks (OMSDriver.h; OMSDrvUPPs.h). */
TYPEDEF_OMSPROC(OMSDeviceH, OMSDvrAdd1DevProc0)(OMSDevice *device);
TYPEDEF_OMSPROC(OMSDeviceH, OMSDvrAdd1DevProc1)(OMSDevice *device,
                                                short devSize);

/* OMSSendParams (OMSDriver.h) — the driver fills proc/paramD0/paramD1;
 * OMS fills omsUniqueID. */
typedef struct OMSSendParams {
    OMSReadHook2 proc;               /* returned by driver */
    long paramD0, paramD1;           /* returned by driver */
    OMSUniqueID omsUniqueID;         /* passed by system */
} OMSSendParams;

/* OMS driver support routines (OMSDriver.h). The Spec: use
 * OMSReceivedFromPort to pass a received packet to OMS; may be called at
 * interrupt level; OMSPacket.len must be the number of MIDI bytes only
 * for this call; the source is identified by ioRefNum from
 * omdvSetPortReceiveRefNum. */
extern void OMSReceivedFromPort(OMSPacket *pkt, short destRefNum);
extern short OMSOpenDriverResFile(OMSSignature driverID);
extern void OMSCloseDriverResFile(OMSSignature driverID);

#endif /* USBMIDI9_HOST_CHECK_OMSDRIVER_H */

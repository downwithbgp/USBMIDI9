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

/* The authentic OMSDriver.h includes OMSDrvUPPs.h here (it supplies
 * OMSDvrAdd1DevProc0/1UPP + CallOMSDvrAdd1DevProc0/1; on the PPC CFM
 * build the callback must be invoked via CallUniversalProc, not by
 * direct call — see OMSDrvUPPs.h). */
#include "OMSDrvUPPs.h"

/* OMSSendParams (OMSDriver.h) — the driver fills proc/paramD0/paramD1;
 * OMS fills omsUniqueID. proc is an OMSReadHook2UPP — the
 * RoutineDescriptor the driver builds with NewOMSReadHook2 (OMS.h /
 * OMSUPPs.h) — NOT a raw OMSReadHook2 function: the real G4 build
 * rejected the raw assignment with "cannot convert 'pascal void
 * (*)...'". */
typedef struct OMSSendParams {
    OMSReadHook2UPP proc;            /* returned by driver */
    long paramD0, paramD1;           /* returned by driver */
    OMSUniqueID omsUniqueID;         /* passed by system */
} OMSSendParams;

/* OMS driver support routines (OMSDriver.h). The Spec: use
 * OMSReceivedFromPort to pass a received packet to OMS; may be called at
 * interrupt level; OMSPacket.len must be the number of MIDI bytes only
 * for this call; the source is identified by ioRefNum from
 * omdvSetPortReceiveRefNum.
 *
 * OMSReceivedFromPort is a 68K assembly routine (A1 = pkt, D0 =
 * destRefNum); the authentic OMSDriver.h declares it ONLY #ifndef
 * powerc with `#pragma parameter OMSReceivedFromPort(__A1, __D0)`. PPC
 * code must NOT reference the symbol: it resolves the address via
 * OMSGetCallAddress(callOMSReceivedFromPort) and calls it through
 * CallUniversalProc (see oms_rx.c). The guard below mirrors the
 * authentic header, so a `-Dpowerc` host build enforces the PPC path. */
#ifndef powerc
extern void OMSReceivedFromPort(OMSPacket *pkt, short destRefNum);
#endif
extern short OMSOpenDriverResFile(OMSSignature driverID);
extern void OMSCloseDriverResFile(OMSSignature driverID);

#endif /* USBMIDI9_HOST_CHECK_OMSDRIVER_H */

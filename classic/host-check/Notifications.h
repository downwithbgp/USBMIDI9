/*
 * Minimal stub of the Classic Mac OS Notification Manager surface used by
 * the USBMIDI9 OMS shim (poll timer). NOT the real header — see
 * MacTypes.h in this directory for the policy. Layout per the Universal
 * Interfaces (Notifications.h); the NMInstall/NMRemove signatures match
 * the classic Trap Manager API. Precedent for using the Notification
 * Manager from a USB-MIDI shim: Opcode's OMS 2.3.8 "OMS USB Manager"
 * imports NMInstall/NMRemove (verified PEF import list).
 */

#ifndef USBMIDI9_HOST_CHECK_NOTIFICATIONS_H
#define USBMIDI9_HOST_CHECK_NOTIFICATIONS_H

#include "MacTypes.h"

/* NMRec (Notifications.h): the periodic-task record passed to NMInstall. */
struct NMRec {
    struct NMRec *qLink;       /* next notification queue entry */
    UInt32 eventTime;          /* next time to execute (in ticks) */
    UInt16 nMark;              /* mark for post/purge/prime (obsolete) */
    UInt16 nIcon;              /* icon number (obsolete) */
    UInt16 nSound;             /* sound number (obsolete) */
    UInt32 nMsg;               /* message to send or proc ptr to call */
    UInt32 nRefCon;            /* refCon passed to the proc */
    UInt32 nReserved;          /* reserved */
};
typedef struct NMRec NMRec;
typedef NMRec *NMRecPtr;

/* The notification procedure receives (nMessage, nRefCon). */
typedef void (*NMProcPtr)(UInt32 nMessage, UInt32 nRefCon);

extern OSErr NMInstall(NMRecPtr nmRecPtr);
extern OSErr NMRemove(NMRecPtr nmRecPtr);

#endif /* USBMIDI9_HOST_CHECK_NOTIFICATIONS_H */

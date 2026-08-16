/*
 * Minimal stub of the Classic Mac OS Notification Manager surface, modeled
 * on the AUTHENTIC Universal Interfaces 3.3.2 Notification.h (the real
 * file name is "Notification.h", singular). NOT the real header — the G4
 * build uses the authentic UI 3.3.2 header; this stub exists only for the
 * Linux host compile check. See docs/host-check-audit.md (row
 * Notification.h) for the exact/intentional differences.
 *
 * Authentic NMRec (UI 3.3.2): qLink, qType, nmFlags, nmPrivate,
 * nmReserved, nmMark, nmIcon, nmSound, nmStr, nmResp, nmRefCon. There is
 * NO eventTime and NO nMsg. NMInstall/NMRemove install/remove a
 * user-visible notification (alert); they are NOT a periodic-timer API.
 * Verified against the real OMS 2.3.8 USB components (PEF disassembly):
 * both Opcode drivers use NMInstall only in PostNotification-style alert
 * paths (NMRec qType = 8 = ORD(nmType), nmStr = alert text, nmResp =
 * response UPP), never as a timer.
 */

#ifndef USBMIDI9_HOST_CHECK_NOTIFICATIONS_H
#define USBMIDI9_HOST_CHECK_NOTIFICATIONS_H

#include "MacTypes.h"
#include "OSUtils.h"        /* QElemPtr (queue element) */

/* NMRec (Notification.h, UI 3.3.2): the notification record passed to
 * NMInstall. `qType` must be ORD(nmType) = 8. */
struct NMRec {
    QElemPtr qLink;         /* next queue entry */
    short qType;            /* queue type -- ORD(nmType) = 8 */
    short nmFlags;          /* reserved */
    long nmPrivate;         /* reserved */
    short nmReserved;       /* reserved */
    short nmMark;           /* item to mark in Apple menu */
    Handle nmIcon;          /* handle to small icon */
    Handle nmSound;         /* handle to sound record */
    StringPtr nmStr;        /* string to appear in alert */
    NMUPP nmResp;           /* pointer to response routine */
    long nmRefCon;          /* for application use */
};
typedef struct NMRec NMRec;
typedef NMRec *NMRecPtr;

/* The notification procedure receives the NMRec (not a message/refCon
 * pair). NMUPP is the routine-descriptor wrapper (UI 3.3.2); on the G4
 * NMUPP is the UPP type from the real header. */
typedef void (*NMProcPtr)(NMRecPtr nmReqPtr);
typedef NMProcPtr NMUPP;

extern OSErr NMInstall(NMRecPtr nmReqPtr);
extern OSErr NMRemove(NMRecPtr nmReqPtr);

#endif /* USBMIDI9_HOST_CHECK_NOTIFICATIONS_H */

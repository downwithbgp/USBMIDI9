/*
 * Minimal stub of the Classic Mac OS Universal Interfaces surface used by
 * the USBMIDI9 Classic sources. NOT the real headers — this directory
 * exists only so `make check-classic` can syntax/type-check the Classic
 * sources on Linux. The Power Mac G4 build uses the real Universal
 * Headers (CodeWarrior) plus the DDK 1.4.1 USB.h; see
 * docs/classic-usb-driver.md for the provenance of every type below.
 */

#ifndef USBMIDI9_HOST_CHECK_MACTYPES_H
#define USBMIDI9_HOST_CHECK_MACTYPES_H

typedef unsigned char UInt8;
typedef unsigned short UInt16;
typedef unsigned int UInt32;
typedef signed char SInt8;
typedef signed int SInt32;
typedef unsigned char Boolean;
typedef UInt32 OSType;
typedef SInt32 OSStatus;
typedef SInt32 OSErr;
typedef char *Ptr;
typedef Ptr *Handle;             /* MacTypes.h: handle to a relocatable block */
typedef UInt8 *StringPtr;        /* MacTypes.h: Pascal string pointer */
typedef UInt8 Str31[32];   /* Pascal string: length byte + 31 chars */
typedef UInt8 Str63[64];   /* Pascal string: length byte + 63 chars */

/* ProcPtr (MacTypes.h) — generic function pointer. The authentic header
 * declares it as a FUNCTION type (`typedef CALLBACK_API_C(long, ProcPtr)();
 * `); CodeWarrior accepts casts to a function type, gcc/clang do not, so
 * the stub models the pointer-to-function form instead — this keeps the
 * verbatim OMSUPPs.h UPP macro cast `(ProcPtr)(userRoutine)` compiling
 * (same documented adaptation as FindSymbol's const-char name in
 * CodeFragments.h). The distinction is invisible at every call site. */
typedef void (*ProcPtr)();

/* NumVersion (MacTypes.h): major, minorAndBugRev, stage, nonRelRev. */
typedef struct NumVersion {
    UInt8 majorRev;
    UInt8 minorAndBugRev;
    UInt8 stage;
    UInt8 nonRelRev;
} NumVersion;

/* NumVersion release-stage constants (MacTypes.h, Universal Interfaces
 * 3.3): the `stage` byte of a NumVersion. The authentic header models
 * these as enum constants, NOT k-prefixed defines. */
enum {
    developStage    = 0x20,
    alphaStage      = 0x40,
    betaStage       = 0x60,
    finalStage      = 0x80
};

/* Memory Manager zone handle (Memory.h). */
typedef void *THz;

#define nil 0
#define true 1
#define false 0

/* Universal Interfaces declaration macros (MacTypes.h). */
#define CALLBACK_API_C(returnType, name) returnType name
#define EXTERN_API_C(returnType) returnType

#endif /* USBMIDI9_HOST_CHECK_MACTYPES_H */

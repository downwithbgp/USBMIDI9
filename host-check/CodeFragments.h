/*
 * Minimal stub of CodeFragments.h (Universal Interfaces). NOT the real
 * header — see MacTypes.h in this directory. The FindSymbol call shape
 * (connID, symName, symAddr, symClass) follows the DDK 1.4.1 samples
 * (HIDReader.c, ShimSerialStub.c), which is the convention the probe is
 * written against; see docs/classic-usb-driver.md §5.7.
 */

#ifndef USBMIDI9_HOST_CHECK_CODEFRAGMENTS_H
#define USBMIDI9_HOST_CHECK_CODEFRAGMENTS_H

#include "MacTypes.h"

typedef SInt32 CFragConnectionID;
typedef SInt32 CFragSymbolClass;

#define kTVectorCFragSymbol 1
#define kDataCFragSymbol 2

/* symName is a Pascal string (length byte + chars), so the parameter is
 * declared `const char *` rather than Str63 here: the array-typed form
 * makes gcc's -Wstringop-overflow analysis misfire on the call sites,
 * while the real CodeFragments.h parameter is read as a length-prefixed
 * string anyway. The call shape (connID, name, addr, class) matches the
 * DDK 1.4.1 samples. */
OSStatus FindSymbol(CFragConnectionID connID, const char *symName,
                    Ptr *symAddr, CFragSymbolClass *symClass);

#endif /* USBMIDI9_HOST_CHECK_CODEFRAGMENTS_H */

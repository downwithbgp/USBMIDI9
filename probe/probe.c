/*
 * USBMIDI9 Probe — Classic Mac OS 9 diagnostic console application.
 *
 * Locates the USBMIDI9 interface class driver's exported dispatch table
 * (USBGetNextDeviceByClass + FindSymbol, the documented mechanism; see
 * docs/classic-usb-driver.md §5.7 and the DDK's own HIDReader.c), then:
 *   - displays the attached USBMIDI9 interface information
 *   - polls the driver for raw received bytes and prints them in hex,
 *     grouped in 4-byte USB-MIDI Event Packets (e.g. "09 90 3C 57")
 *
 * The lookup is repeated on every poll: when a device is unplugged the
 * driver fragment is unloaded and any cached table pointer would dangle,
 * so the probe never caches it across polls (self-healing on replug).
 *
 * Console I/O via printf (CodeWarrior SIOUX console app). Quit with 'q'.
 *
 * No OMS, no FreeMIDI. No vendor/product-specific behavior.
 *
 * Built on the Power Mac G4 with CodeWarrior (console app + SIOUX);
 * compile-checked on Linux via `make check-classic` against the stub
 * headers in classic/host-check/.
 */

#include <stdio.h>

#include <MacTypes.h>
#include <MacErrors.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Events.h>
#include <CodeFragments.h>
#include <USB.h>

#include "../classic/usbmidi9_dispatch.h"

/* Exported symbol name of the driver dispatch table. CodeWarrior
 * supports Pascal string literals ("\p..."); other compilers (the Linux
 * host check) get a plain C string. */
#if defined(__MWERKS__)
#define kProbeDispatchSymbolName "\pUSBMIDI9DispatchTable"
#else
#define kProbeDispatchSymbolName "USBMIDI9DispatchTable"
#endif

/* Generic USB-MIDI interface match (mirrors the driver description). */
#define kProbeInterfaceClass    0x01u
#define kProbeInterfaceSubClass 0x03u

#define kProbePollTicks 15u     /* Delay(15) = about 0.25 s between polls */
#define kProbeMaxInterfaces 8u  /* matches kUSBMIDI9MaxInterfaces */
#define kProbeDequeueChunk 64u  /* bytes dequeued per interface per poll */

/* Keycode of 'Q' in the ADB/KeyMap encoding (GetKeys). */
#define kProbeQuitKeycode 12u

/* Locate the USBMIDI9 driver's exported dispatch table. Returns nil when
 * no driver is loaded (no USB-MIDI device attached). */
static struct USBMIDI9DispatchTable *ProbeFindDispatchTable(void)
{
    USBDeviceRef deviceRef;
    CFragConnectionID connID;
    CFragSymbolClass symClass;
    THz currentZone;
    struct USBMIDI9DispatchTable *table;
    OSErr err;

    deviceRef = kNoDeviceRef;
    for (;;) {
        err = USBGetNextDeviceByClass(&deviceRef, &connID,
                                      kProbeInterfaceClass,
                                      kProbeInterfaceSubClass,
                                      kUSBAnyProtocol);
        if (err != noErr) {
            return nil;                 /* no (more) matching driver */
        }
        table = nil;
        /* Class drivers load in the System Zone; look up the symbol
         * there (Rev 26 Ch 4 p. 83-85; HIDReader.c pattern). */
        currentZone = GetZone();
        SetZone(SystemZone());
        err = FindSymbol(connID, kProbeDispatchSymbolName,
                         (Ptr *)&table, &symClass);
        SetZone(currentZone);
        if (err == noErr && table != nil) {
            return table;
        }
    }
}

/* Print the attached interface table (reprinted only when the interface
 * count changes). */
static void ProbePrintInterfaceTable(struct USBMIDI9DispatchTable *table,
                                     UInt32 count)
{
    struct USBMIDI9InterfaceInfo info;
    UInt32 i;

    printf("Attached USB-MIDI interfaces: %lu\n", (unsigned long)count);
    for (i = 0u; i < count && i < kProbeMaxInterfaces; i++) {
        if (table->getInterfaceInfo(i, &info) != noErr) {
            printf("  [%lu] (info unavailable)\n", (unsigned long)i);
            continue;
        }
        printf("  [%lu] vid=%04X pid=%04X iface=%lu class=%02lX"
               " subclass=%02lX protocol=%02lX maxPacket=%lu queued=%lu\n",
               (unsigned long)info.index,
               (unsigned)info.vendorID, (unsigned)info.productID,
               (unsigned long)info.interfaceNum,
               (unsigned long)info.interfaceClass,
               (unsigned long)info.interfaceSubClass,
               (unsigned long)info.interfaceProtocol,
               (unsigned long)info.maxPacketSize,
               (unsigned long)info.availableBytes);
    }
}

/* Print raw bytes in hex, 16 bytes per line (e.g. "09 90 3C 57"). */
static void ProbePrintHex(const unsigned char *bytes, UInt32 count)
{
    UInt32 i;

    for (i = 0u; i < count; i++) {
        printf("%02X%s", (unsigned)bytes[i],
               ((i % 16u) == 15u || i + 1u == count) ? "\n" : " ");
    }
}

int main(void)
{
    struct USBMIDI9DispatchTable *table;
    UInt32 lastCount;                   /* printed when it changes */
    UInt32 count;
    UInt32 i;

    printf("USBMIDI9 Probe - USB-MIDI diagnostic for Classic Mac OS 9\n");
    printf("Press 'q' to quit.\n");

    lastCount = 0xFFFFFFFFu;            /* force first print */

    for (;;) {
        KeyMap keys;
        UInt32 finalTicks;

        GetKeys(keys);
        if ((keys[kProbeQuitKeycode / 8u]
             & (1u << (kProbeQuitKeycode % 8u))) != 0u) {
            break;
        }

        table = ProbeFindDispatchTable();
        if (table == nil) {
            if (lastCount != 0xFFFFFFFFu) {
                printf("(no USBMIDI9 driver: device removed?)\n");
                lastCount = 0xFFFFFFFFu;
            }
        } else if (table->version < kUSBMIDI9DispatchTableVersion) {
            printf("USBMIDI9 driver dispatch table version %lu is too old\n",
                   (unsigned long)table->version);
        } else {
            count = 0u;
            (void)table->enumerateInterfaces(nil, 0u, &count);
            if (count != lastCount) {
                ProbePrintInterfaceTable(table, count);
                lastCount = count;
            }
            for (i = 0u; i < count && i < kProbeMaxInterfaces; i++) {
                unsigned char bytes[kProbeDequeueChunk];
                UInt32 n;

                n = table->dequeueBytes(i, bytes, kProbeDequeueChunk);
                if (n > 0u) {
                    printf("[iface %lu] ", (unsigned long)i);
                    ProbePrintHex(bytes, n);
                }
            }
        }

        Delay(kProbePollTicks, &finalTicks);
    }

    printf("USBMIDI9 Probe: done.\n");
    return 0;
}

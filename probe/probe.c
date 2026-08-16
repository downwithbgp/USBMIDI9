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

#include "usbmidi9_dispatch.h"

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

/* Keycode of 'Q' in the ADB/KeyMap encoding (GetKeys): 0x0C per the
 * classic keycode table (Carbon kVK_ANSI_Q = 0x0C). */
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

/* Test one key in the KeyMap using the authentic byte view (Events.h:
 * KeyMap = UInt32[4], KeyMapByteArray = UInt8[16]). For keycode N the
 * real Mac OS 9 GetKeys byte representation is byte N/8 with mask
 * 1 << (N % 8) — verified on the G4 (the MSB-first 0x80 >> (N % 8)
 * inference was disproved by real hardware). The original code indexed
 * the UInt32 KeyMap as bytes AND inverted the within-byte bit order,
 * so Q (0x0C: byte 1, mask 0x10) never matched. */
static Boolean ProbeKeyDown(const KeyMap keys, UInt8 keycode)
{
    const KeyMapByteArray *bytes = (const KeyMapByteArray *)keys;

    if (keycode >= 128u) {
        return false;               /* the KeyMap covers keycodes 0..127 */
    }
    return ((*bytes)[keycode / 8u] & (1u << (keycode % 8u))) != 0u;
}

/* Probe poll state, kept by the caller so the poll loop can be driven
 * from host tests (tests/test_probe.c, which includes this file into
 * its translation unit). */
struct USBMIDI9ProbeState {
    UInt32 lastCount;       /* last printed interface count */
    int noDriverReported;   /* 1 = the no-driver message is on screen */
};

static Boolean USBMIDI9ProbePoll(struct USBMIDI9ProbeState *state);

/* Perform one poll iteration: refresh the driver/interface state, print
 * state transitions, drain received bytes. Returns nonzero when the
 * user pressed the quit key.
 *
 * State transitions (no spamming): the no-driver message prints on the
 * first poll without a driver (startup) and again whenever the driver
 * disappears after having been present. */
static Boolean USBMIDI9ProbePoll(struct USBMIDI9ProbeState *state)
{
    struct USBMIDI9DispatchTable *table;
    UInt32 count;
    UInt32 i;
    KeyMap keys;

    GetKeys(keys);
    if (ProbeKeyDown(keys, kProbeQuitKeycode)) {
        return true;
    }

    table = ProbeFindDispatchTable();
    if (table == nil) {
        if (!state->noDriverReported) {
            printf("(no USBMIDI9 driver loaded)\n");
            state->noDriverReported = 1;
        }
        state->lastCount = 0xFFFFFFFFu; /* reprint the table on reappearance */
    } else {
        state->noDriverReported = 0;    /* present: a future absence is reported */
        if (table->version < kUSBMIDI9DispatchTableVersion) {
            printf("USBMIDI9 driver dispatch table version %lu is too old\n",
                   (unsigned long)table->version);
        } else {
            count = 0u;
            (void)table->enumerateInterfaces(nil, 0u, &count);
            if (count != state->lastCount) {
                ProbePrintInterfaceTable(table, count);
                state->lastCount = count;
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
    }
    return false;
}

int main(void)
{
    struct USBMIDI9ProbeState state;
    UInt32 finalTicks;

    printf("USBMIDI9 Probe - USB-MIDI diagnostic for Classic Mac OS 9\n");
    printf("Press 'q' to quit.\n");

    state.lastCount = 0xFFFFFFFFu;  /* force first interface-table print */
    state.noDriverReported = 0;     /* report the no-driver state on the first poll */

    for (;;) {
        if (USBMIDI9ProbePoll(&state) != 0) {
            break;
        }
        Delay(kProbePollTicks, &finalTicks);
    }

    printf("USBMIDI9 Probe: done.\n");
    return 0;
}

/*
 * Host tests for the USBMIDI9 Probe's poll/state logic (probe/probe.c),
 * driven through the real probe code with mocked OS/USB services.
 *
 * probe/probe.c is compiled into this translation unit (the same
 * technique as tests/test_machine.c with classic/usb_driver.c) against
 * the stub headers in classic/host-check/. The OS calls the probe makes
 * (GetKeys, Delay, USBGetNextDeviceByClass, FindSymbol, GetZone,
 * SetZone, SystemZone) are provided by the mocks below so the poll loop
 * can be scripted. The probe's printf output is captured (not printed)
 * so the tests can assert on status transitions.
 *
 * Covers the M1B real-target corrections (found on the G4):
 *   - the initial no-driver state prints "(no USBMIDI9 driver loaded)"
 *     exactly once (previously suppressed by the lastCount sentinel)
 *   - repeated no-driver polls do not spam
 *   - driver disappearance prints the status again (one transition)
 *   - the quit key uses the authentic KeyMap representation
 *     (KeyMap = UInt32[4] + KeyMapByteArray byte view; bit N = byte
 *     N/8, mask 1 << (N%8) per the real Mac OS 9 GetKeys, verified on
 *     the G4): a KeyMap with bit 12 (Q) set quits, the MSB-first
 *     0x80 >> (N % 8) layout does not.
 *
 * Portable C (C89/C90).
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* vsnprintf is C99; declared here for the -std=c89 host build. */
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap);

#include <MacTypes.h>
#include <MacErrors.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Events.h>
#include <CodeFragments.h>
#include <USB.h>

#include "usbmidi9_dispatch.h"

/* ---- Output capture ---- */

static char gOut[4096];
static size_t gOutLen;

int probe_capture_printf(const char *fmt, ...);   /* used by probe.c */

#define printf probe_capture_printf
#define main probe_app_main
#include "probe/probe.c"
#undef printf
#undef main

int probe_capture_printf(const char *fmt, ...)
{
    va_list ap;
    int n;

    if (gOutLen >= sizeof(gOut)) {
        return 0;
    }
    va_start(ap, fmt);
    n = vsnprintf(gOut + gOutLen, sizeof(gOut) - gOutLen, fmt, ap);
    va_end(ap);
    if (n > 0) {
        gOutLen += (size_t)n;
        if (gOutLen >= sizeof(gOut)) {
            gOutLen = sizeof(gOut) - 1u;
        }
    }
    return 0;
}

static void out_reset(void)
{
    gOutLen = 0u;
    gOut[0] = '\0';
}

static int out_has(const char *needle)
{
    return strstr(gOut, needle) != NULL;
}

static int out_count(const char *needle)
{
    const char *p = gOut;
    int n = 0;

    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p++;
    }
    return n;
}

/* ---- Mock OS/USB services ---- */

static KeyMap gMockKeys;                       /* keyboard state GetKeys reports */
static int gDevicePresent;                     /* a matching device exists */
static struct USBMIDI9DispatchTable *gFoundTable;   /* FindSymbol result */
static struct USBMIDI9DispatchTable gScriptTable;
static UInt32 gScriptCount;                    /* interfaces the scripted table reports */

void GetKeys(KeyMap theKeys)
{
    UInt32 i;

    for (i = 0u; i < 4u; i++) {
        theKeys[i] = gMockKeys[i];
    }
}

void Delay(UInt32 ticks, UInt32 *finalTicks)
{
    (void)ticks;
    if (finalTicks != nil) {
        *finalTicks = 0u;
    }
}

THz GetZone(void)
{
    return (THz)0;
}

THz SystemZone(void)
{
    return (THz)0;
}

void SetZone(THz zone)
{
    (void)zone;
}

OSStatus USBGetNextDeviceByClass(USBDeviceRef *deviceRef,
                                 CFragConnectionID *connID, UInt16 theClass,
                                 UInt16 theSubClass, UInt16 theProtocol)
{
    (void)theClass;
    (void)theSubClass;
    (void)theProtocol;
    if (!gDevicePresent) {
        return paramErr;
    }
    if (deviceRef != nil && *deviceRef != kNoDeviceRef) {
        return paramErr;        /* one scripted device only */
    }
    if (deviceRef != nil) {
        *deviceRef = 1;
    }
    if (connID != nil) {
        *connID = 1;
    }
    return noErr;
}

OSStatus FindSymbol(CFragConnectionID connID, const char *symName,
                    Ptr *symAddr, CFragSymbolClass *symClass)
{
    (void)connID;
    (void)symName;
    if (gFoundTable == nil) {
        return paramErr;
    }
    if (symAddr != nil) {
        *symAddr = (Ptr)(void *)gFoundTable;
    }
    if (symClass != nil) {
        *symClass = kDataCFragSymbol;
    }
    return noErr;
}

static OSStatus mock_enumerate(struct USBMIDI9InterfaceInfo *outArray,
                               UInt32 maxCount, UInt32 *outCount)
{
    if (outCount != nil) {
        *outCount = gScriptCount;
    }
    if (outArray != nil && maxCount > 0u && gScriptCount > 0u) {
        outArray[0].index = 0u;
        outArray[0].vendorID = 0x0a4d;
        outArray[0].productID = 0x0090;
        outArray[0].interfaceNum = 0u;
        outArray[0].interfaceClass = 0x01u;
        outArray[0].interfaceSubClass = 0x03u;
        outArray[0].interfaceProtocol = 0u;
        outArray[0].maxPacketSize = 64u;
        outArray[0].availableBytes = 0u;
    }
    return noErr;
}

static OSStatus mock_get_info(UInt32 index, struct USBMIDI9InterfaceInfo *outInfo)
{
    if (outInfo == nil) {
        return paramErr;
    }
    if (index >= gScriptCount) {
        return kUSBNotFound;
    }
    outInfo->index = index;
    outInfo->vendorID = 0x0a4d;
    outInfo->productID = 0x0090;
    outInfo->interfaceNum = 0u;
    outInfo->interfaceClass = 0x01u;
    outInfo->interfaceSubClass = 0x03u;
    outInfo->interfaceProtocol = 0u;
    outInfo->maxPacketSize = 64u;
    outInfo->availableBytes = 0u;
    return noErr;
}

static UInt32 mock_dequeue(UInt32 index, void *buffer, UInt32 maxBytes)
{
    (void)index;
    (void)buffer;
    (void)maxBytes;
    return 0u;
}

static void script_table_with_count(UInt32 count)
{
    gDevicePresent = 1;
    gScriptCount = count;
    gFoundTable = &gScriptTable;
    gScriptTable.version = kUSBMIDI9DispatchTableVersion;
    gScriptTable.enumerateInterfaces = mock_enumerate;
    gScriptTable.getInterfaceInfo = mock_get_info;
    gScriptTable.dequeueBytes = mock_dequeue;
}

static void script_no_driver(void)
{
    gDevicePresent = 0;
    gFoundTable = nil;
}

/* ---- Tests ---- */

static int g_failures;

static void fail(const char *expr, const char *file, int line)
{
    printf("FAIL %s:%d: %s\n", file, line, expr);
    g_failures++;
}

#define CHECK(cond) do { \
    if (!(cond)) { \
        fail(#cond, __FILE__, __LINE__); \
    } \
} while (0)

static void state_init(struct USBMIDI9ProbeState *st)
{
    st->lastCount = 0xFFFFFFFFu;    /* same initialization as main() */
    st->noDriverReported = 0;
}

/* 1. Initial no-driver state: the status prints exactly once on the
 * first poll, and repeated no-driver polls stay silent. */
static void test_initial_no_driver_prints_once(void)
{
    struct USBMIDI9ProbeState st;
    UInt32 i;

    state_init(&st);
    script_no_driver();
    memset(gMockKeys, 0, sizeof(gMockKeys));
    out_reset();

    CHECK(USBMIDI9ProbePoll(&st) == 0);
    CHECK(out_count("(no USBMIDI9 driver loaded)") == 1);

    for (i = 0u; i < 3u; i++) {
        CHECK(USBMIDI9ProbePoll(&st) == 0);
    }
    CHECK(out_count("(no USBMIDI9 driver loaded)") == 1);   /* no spam */
}

/* 2. Driver present -> absent: the table prints on change, the
 * no-driver status prints again on disappearance, then stays silent. */
static void test_driver_disappearance_reports(void)
{
    struct USBMIDI9ProbeState st;

    state_init(&st);
    script_table_with_count(1u);
    memset(gMockKeys, 0, sizeof(gMockKeys));
    out_reset();

    CHECK(USBMIDI9ProbePoll(&st) == 0);
    CHECK(out_has("Attached USB-MIDI interfaces: 1"));
    CHECK(out_has("vid=0A4D"));                /* getInterfaceInfo path */
    CHECK(out_count("(no USBMIDI9 driver loaded)") == 0);

    /* Same count again: the table does not reprint. */
    out_reset();
    CHECK(USBMIDI9ProbePoll(&st) == 0);
    CHECK(out_count("Attached USB-MIDI interfaces: 1") == 0);

    /* Driver disappears: one transition report, then silence. */
    script_no_driver();
    out_reset();
    CHECK(USBMIDI9ProbePoll(&st) == 0);
    CHECK(out_count("(no USBMIDI9 driver loaded)") == 1);

    out_reset();
    CHECK(USBMIDI9ProbePoll(&st) == 0);
    CHECK(out_count("(no USBMIDI9 driver loaded)") == 0);

    /* Driver reappears with the same interface count: the table prints
     * again (lastCount was reset while absent). */
    script_table_with_count(1u);
    out_reset();
    CHECK(USBMIDI9ProbePoll(&st) == 0);
    CHECK(out_count("Attached USB-MIDI interfaces: 1") == 1);
    CHECK(out_count("(no USBMIDI9 driver loaded)") == 0);
}

/* 3. Quit key: the KeyMap is tested through the authentic byte view
 * (bit 12 = byte 1, mask 1 << (12 % 8) = 0x10 — the real Mac OS 9
 * GetKeys byte representation, verified on the G4). The MSB-first
 * 0x80 >> (N % 8) layout (0x08 in byte 1) is NOT the Q key. */
static void test_quit_key_uses_authentic_keymap(void)
{
    struct USBMIDI9ProbeState st;

    state_init(&st);
    script_no_driver();

    /* Bit 12 (Q, 0x0C) set through the KeyMapByteArray view. */
    memset(gMockKeys, 0, sizeof(gMockKeys));
    ((UInt8 *)gMockKeys)[12u / 8u] = (UInt8)(1u << (12u % 8u));
    CHECK(USBMIDI9ProbePoll(&st) != 0);        /* quits */

    /* The MSB-first layout (0x08 in byte 1 = bit 11) is NOT the Q key. */
    memset(gMockKeys, 0, sizeof(gMockKeys));
    ((UInt8 *)gMockKeys)[1] = 0x08u;
    CHECK(USBMIDI9ProbePoll(&st) == 0);        /* does not quit */

    /* No key down: does not quit. */
    memset(gMockKeys, 0, sizeof(gMockKeys));
    CHECK(USBMIDI9ProbePoll(&st) == 0);
}

int test_probe_run(void)
{
    test_initial_no_driver_prints_once();
    test_driver_disappearance_reports();
    test_quit_key_uses_authentic_keymap();
    return g_failures;
}

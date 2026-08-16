/*
 * USBMIDI9 OMS driver — resource source (Rez).
 *
 * Compiled on the Power Mac G4 with Rez (CodeWarrior's or MPW's), not on
 * Linux. The result is the driver file's resource fork; the file itself
 * is typed 'OMdv' with creator 'USM9' and lives in System Folder:OMS
 * Folder (verified contract: OMS Programming Interface spec, "OMS
 * Drivers"; real drivers: OMS 2.3.8 IAC/Standard Interface/MIDIPort 32,
 * Roland SC-8850 'RdSU').
 *
 * Status: the OMS PEF gate has PASSED on the real G4 (0 errors, 43
 * warnings after adding core/packets.c + USBManagerLib to the target);
 * the Rez source also PASSED on the real G4 (2026-08-16: resource-only
 * target, Linker: None, Make 0 errors — no 68K linker/main workaround
 * required). The MacOS Merge / Project Type = Resource File container
 * mechanism also PASSED (real "USBMIDI9 OMS Driver" with 'OMdi' 128,
 * 'SICN' 128, 'vers' 1 in ResEdit). Only packaging change remaining:
 * add 'OMdv' 128 via Rez read from OMdvData (see docs/g4-handoff.md;
 * oms/omdv.r + tools/omdvdata.c prepared in the repo).
 *
 * The authentic Universal Interfaces 3.3.2 Rez preamble is included so
 * every standard resource template/constant comes from Apple's headers
 * — nothing hand-declared:
 *   'vers' template  — MacTypes.r (via Types.r); the stage constants
 *                      development/alpha/beta/final (+ release) are
 *                      declared in that template
 *   'SICN' template  — Icons.r (via Types.r): an array of 32-byte hex
 *                      strings, one per 16x16 bitmap
 *   verUS (region)   — Script.r (via IntlResources.r, via Types.r)
 * 'OMdi' has NO Apple template (it is Opcode's own resource): its type
 * is declared here with the exact OMSDriverParams layout (OMSDriver.h).
 *
 * Resources:
 *   'OMdi' 128 — OMSDriverParams (OMSDriver.h), 16 bytes exact:
 *                short id(2) + OMSBool xxisSmart(1) + OMSBool
 *                hasMenuOrWindows(1) + short xxportNumM(2) + short
 *                xxportNumB(2) + flags(1) + driverCompatibilityLevel(1)
 *                + reservedFlags[6]. id 0x7F10 (unassigned in the
 *                inspected OMS 2.3.8 driver set; determines
 *                omdvAddDevices call order only), no flags, driver
 *                compatibility level 1 (OMS 2.0+ driver API).
 *   'SICN' 128 — small icon list: one 16x16 monochrome icon + its mask,
 *                32 bytes each (64 bytes total), matching the authentic
 *                64-byte SICN 128 of Opcode's own SampleCell and
 *                MIDIPort 32 drivers (verified in ~/research/oms).
 *   'vers' 1   — 1.0.0d1, development stage, region verUS; template
 *                from the authentic MacTypes.r (raw layout verified
 *                against the OMS 2.3.8 IAC driver's vers 1:
 *                02 38 80 02 00 00 + two Pascal strings).
 *   'ICN#'/icl8/icl4 — larger icons (present in verified period drivers;
 *                optional; omitted in v0.1 to keep the resource small).
 *   'BNDL'/'FREF' — Finder bundle (optional; omitted in v0.1).
 *
 * The 'OMdv' 128 CODE resource is NOT declared here: it is the PEF
 * container produced at link time (the Roland SC-8850 'OMdv' resource
 * contains "Joy!peffpwpc" at offset 4 after a 4-byte length header —
 * format reference). Exact CodeWarrior construction steps are in the G4
 * handoff (docs/g4-handoff.md).
 */

#include "Types.r"

/* OMSDriverParams layout (OMSDriver.h), as a Rez type. Opcode-specific:
 * no authentic Apple template exists. Field order/sizes mirror the
 * struct exactly (16 bytes, no padding). */
type 'OMdi' {
    integer;                /* id: driver load order (assigned by Opcode) */
    boolean;                /* xxisSmart (obsolete) */
    boolean;                /* hasMenuOrWindows */
    integer;                /* xxportNumM (obsolete) */
    integer;                /* xxportNumB (obsolete) */
    byte;                   /* flags (kNoSyncRouting etc.) */
    byte;                   /* driverCompatibilityLevel */
    hex string;             /* reservedFlags[6] */
};

resource 'OMdi' (128) {
    0x7F10,                 /* id */
    false,                  /* xxisSmart */
    false,                  /* hasMenuOrWindows */
    0,                      /* xxportNumM */
    0,                      /* xxportNumB */
    0x00,                   /* flags: 0 (loaded only when devices owned) */
    0x01,                   /* driverCompatibilityLevel: 1 (OMS 2.0+) */
    "000000000000"          /* reservedFlags[6] */
};

/* 16x16 monochrome icon: a MIDI keyboard block with a cable stub (rows
 * in hex; set bits (1) = black), followed by its mask (the opaque
 * silhouette). Template from the authentic Icons.r: each array element
 * is one 32-byte hex string — element 1 = icon, element 2 = mask (the
 * OMS Spec wants pairs, normal and highlighted; OMSDevice.iconID 0).
 * Size and 16x16 format verified against authentic Opcode/Roland OMS
 * drivers (64- and 128-byte SICN 128 resources are all lists of 32-byte
 * 16x16 bitmaps). Rez body syntax per Apple TN1019 (canonical): the
 * SICN array field is written with braces and $"..." hex strings. */
resource 'SICN' (128) {
    {
        $"000000000000000001800180018001800FF01668166810081008100810080FF0",
        $"000000000000000001800180018001801FF81FF81FF81FF81FF81FF81FF81FF8"
    }
};

/* Version resource: 1.0.0d1, development stage, region verUS. The
 * 'vers' template and its stage constants come from the authentic
 * MacTypes.r (included via Types.r) — NOT hand-declared. */
resource 'vers' (1) {
    0x01, 0x00,
    development, 0x01,
    verUS,
    "1.0.0d1",
    "USBMIDI9 OMS Driver 1.0.0d1"
};

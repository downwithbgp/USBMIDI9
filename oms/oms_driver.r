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
 * Resources:
 *   'OMdi' 128 — OMSDriverParams (OMSDriver.h). id 0x7F10 (unassigned in
 *                the inspected OMS 2.3.8 driver set; determines
 *                omdvAddDevices call order only), no flags, driver
 *                compatibility level 1 (OMS 2.0+ driver API).
 *   'SICN' 128 — small icon list: 16x16 monochrome bitmaps, 32 bytes
 *                each (icon, then its mask; the OMS Spec wants pairs,
 *                normal and highlighted — OMS Setup draws them).
 *                One 16x16 icon + 16x16 mask (64 bytes), matching the
 *                authentic 64-byte SICN 128 of Opcode's own SampleCell
 *                and MIDIPort 32 drivers (verified in ~/research/oms).
 *   'ICN#'/icl8/icl4 — larger icons (present in verified period drivers;
 *                optional; omitted in v0.1 to keep the resource small).
 *   'BNDL'/'FREF' — Finder bundle for the file's icon/version.
 *   'vers' 1 — version resource.
 *
 * The 'OMdv' 128 CODE resource is NOT declared here: it is the PEF
 * container produced at link time (the Roland SC-8850 'OMdv' resource
 * contains "Joy!peffpwpc" at offset 4 after a 4-byte length header —
 * format reference). Exact CodeWarrior construction steps are in the G4
 * handoff (docs/g4-handoff.md).
 */

/* OMSDriverParams layout (OMSDriver.h), as a Rez type. */
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
 * silhouette). 32 bytes icon + 32 bytes mask — one pair of the SICN
 * list (OMSDevice.iconID 0); further pairs can be appended on the G4
 * with ResEdit if more icons are wanted. Size and 16x16 format verified
 * against authentic Opcode/Roland OMS drivers (64- and 128-byte SICN
 * 128 resources are all lists of 32-byte 16x16 bitmaps). */
type 'SICN' {
    hex string;
    hex string;
};

resource 'SICN' (128) {
    "0000"
    "0000"
    "0000"
    "0000"
    "0180"
    "0180"
    "0180"
    "0180"
    "0FF0"
    "1668"
    "1668"
    "1008"
    "1008"
    "1008"
    "1008"
    "0FF0",
    "0000"
    "0000"
    "0000"
    "0000"
    "0180"
    "0180"
    "0180"
    "0180"
    "1FF8"
    "1FF8"
    "1FF8"
    "1FF8"
    "1FF8"
    "1FF8"
    "1FF8"
    "1FF8"
};

/* Version resource: 1.0.0d1, development stage. Syntax per the
 * authentic Universal Interfaces 3.3.2 MacTypes.r 'vers' template
 * (major/minorBug/stage/nonRelRev are single hex bytes; constants are
 * development/alpha/beta/final, not devStage) — raw layout verified
 * against the OMS 2.3.8 IAC driver's vers 1 (02 38 80 02 00 00 + two
 * Pascal strings). */
resource 'vers' (1) {
    0x01, 0x00,
    development, 0x01,
    verUS,
    "1.0.0d1",
    "USBMIDI9 OMS Driver 1.0.0d1"
};

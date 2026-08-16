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
 *   'SICN' 128 — icon pairs used by OMS Setup (Spec: pairs, normal and
 *                highlighted). Minimal 32x32 monochrome icon + mask.
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

/* Minimal 32x32 monochrome icon: a MIDI keyboard block with a cable
 * stub (rows in hex; bit 0 = black), followed by its mask (the opaque
 * shape). 128 bytes icon + 128 bytes mask. This is ONE pair of the
 * SICN list (OMSDevice.iconID 0); further pairs can be appended on the
 * G4 with ResEdit if more icons are wanted. */
type 'SICN' {
    hex string;
    hex string;
};

resource 'SICN' (128) {
    "00000000"
    "00000000"
    "00000000"
    "00000000"
    "00000000"
    "00000000"
    "00FFFFE0"
    "03FFFFF8"
    "03FFFFF8"
    "07EEEEFC"
    "67EEEEFC"
    "67EEEEFC"
    "67EEEEFC"
    "67EEEEFC"
    "67EEEEFC"
    "67EEEEFC"
    "67EEEEFC"
    "67EEEEFC"
    "67EEEEFC"
    "67EEEEFC"
    "67EEEEFC"
    "07EEEEFC"
    "07EEEEFC"
    "03FFFFF8"
    "03FFFFF8"
    "00FFFFE0"
    "00000000"
    "00000000"
    "00000000"
    "00000000"
    "00000000"
    "00000000",
    "00000000"
    "00000000"
    "00000000"
    "00000000"
    "00000000"
    "00000000"
    "07FFFFFC"
    "07FFFFFC"
    "07FFFFFC"
    "07FFFFFC"
    "67FFFFFC"
    "67FFFFFC"
    "67FFFFFC"
    "67FFFFFC"
    "67FFFFFC"
    "67FFFFFC"
    "67FFFFFC"
    "67FFFFFC"
    "67FFFFFC"
    "67FFFFFC"
    "67FFFFFC"
    "07FFFFFC"
    "07FFFFFC"
    "07FFFFFC"
    "07FFFFFC"
    "07FFFFFC"
    "00000000"
    "00000000"
    "00000000"
    "00000000"
    "00000000"
    "00000000"
};

/* Version resource: 1.0.0d1, development stage. */
resource 'vers' (1) {
    0x0100, 0x00,
    devStage, 0x0001,
    verUS, 0x0000,
    "1.0.0d1",
    "USBMIDI9 OMS Driver 1.0.0d1"
};

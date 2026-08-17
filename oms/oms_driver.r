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
 * 'SICN' 128, 'vers' 1 in ResEdit). The driver's PPC code resource is
 * carried as 'PPCC' 1 = the raw Target-A PEF (see oms/ppcc.r and
 * docs/g4-handoff.md) — the authenticated native-PPC carrier (OMS 2.3.8
 * loads 'PPCC' codeResID first and materializes the fragment via
 * GetDiskFragment; the OMdv path is the 68K fallback and must NOT hold
 * a PEF).
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
 * 'OMdi' has NO Apple template (it is Opcode's own resource). It is
 * deliberately NOT declared as a typed Rez template: the real G4 Rez
 * build proved that typed boolean fields pack as BITS, not as the
 * one-byte OMSBool members of OMSDriverParams — the typed template
 * emitted `7F 10 00 00 00 00 40 00 4C 0C 0C ...` instead of the
 * required `7F 10 00 00 00 00 00 01 00 01 00 00 00 00 00 00`. The
 * 0x40 at byte 6 made OMS read codeResID = 0x4000, so
 * Get1Resource('PPCC', 0x4000) returned NULL and the driver failed to
 * load with OMS error -192 (see docs/g4-handoff.md). The 4C 0C ...
 * tail shows the same non-byte-exact emission in the trailing fields
 * (the hex-string member was written with a plain "..." body instead
 * of the canonical $"..." hex form); no mechanism was pursued — raw
 * data removes the question. The resource is therefore written as RAW
 * DATA with the exact 16 bytes, in the canonical Rez `data` form — the
 * same form DeRez emits and Apple's own USBClassDriverIcons.r (Mac OS
 * USB DDK 1.4.1) uses. The host test tests/test_omdi_resource.c pins
 * the payload bytes and forbids the typed template from returning.
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
 * The 'PPCC' 1 CODE resource is NOT declared here: it is the raw
 * Target-A PEF imported by Rez from the built shared library (see
 * oms/ppcc.r: `read 'PPCC' (1) "::USBMIDI9_OMS";`). The logical
 * resource payload is the PEF container itself — 'Joy!peffpwpc' at
 * byte 0, NO length prefix: the 4-byte length seen at the start of
 * resource records in the authentic forks is the resource fork's own
 * record framing (the Resource Manager stores [length][data] and
 * Get1Resource returns only the data). The earlier "SC-8850 OMdv =
 * 4-byte length + PEF" reading was that framing, misread as payload.
 * Exact CodeWarrior construction steps are in the G4 handoff
 * (docs/g4-handoff.md).
 */

#include "Types.r"

/* 'OMdi' 128 — OMSDriverParams (OMSDriver.h), written as RAW DATA (see
 * the header note: the typed Rez template packs booleans as bits and
 * emitted 40 00 at bytes 6-7 on the real G4 — OMS -192). Logical
 * layout of the 16 bytes:
 *   0x00  short id                 7F 10  (0x7F10; omdvAddDevices order)
 *   0x02  OMSBool xxisSmart        00
 *   0x03  OMSBool hasMenuOrWindows 00
 *   0x04  short xxportNumM         00 00  (obsolete)
 *   0x06  short xxportNumB         00 01  (codeResID for the 'PPCC'
 *                                          lookup: OMS 2.3.8 loadCode
 *                                          pref=2 = Get1Resource
 *                                          ('PPCC', word at +6); must
 *                                          match the 'PPCC' 1 id in
 *                                          oms/ppcc.r)
 *   0x08  flags                    00     (none: loaded only when
 *                                          devices owned)
 *   0x09  driverCompatibilityLevel 01     (OMS 2.0+)
 *   0x0A  reservedFlags[6]         00 x 6
 * Exact bytes: 7F 10 00 00 00 00 00 01 00 01 00 00 00 00 00 00
 * (tests/test_omdi_resource.c re-verifies this payload on every
 * `make test`.) */
data 'OMdi' (128) {
    $"7F100000000000010001000000000000"
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

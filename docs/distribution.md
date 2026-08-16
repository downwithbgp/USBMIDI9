# USBMIDI9 distribution design (period-correct, 1999-2001)

Status: **design.** The release layout below is what a Mac MIDI driver
download looked like in the 1999-2001 window, based on period evidence we
hold: the Evolution eKeys 37 CD's "Driver Installation Guide"
("USB drivers ... into the Extensions folder inside the System Folder. The
OMS description file ... into the OMS folder inside the System Folder"),
the OMS 2.3.8 installer catalog (component file types/creators), and the
Roland SC-8850 driver packages (StuffIt/BinHex distribution).

Nothing here requires proprietary tools in the repository. The FINAL
Classic artifact is assembled on the G4 with authentic Mac tools.

## 1. Release shape

A Finder-visible folder, distributed as a StuffIt archive:

```text
USBMIDI9 0.1
    Read Me
    USBMIDI9
    USBMIDI9 OMS Driver
    Documentation
        Release Notes (developer)
```

Components (v0.1 — FreeMIDI driver NOT included; it is research-only):

| Component | File type | Creator | Destination | Notes |
|---|---|---|---|---|
| `USBMIDI9` | `'ndrv'` | `'usbd'` | System Folder:Extensions | the USB class driver (existing M1B artifact) |
| `USBMIDI9 OMS Driver` | `'OMdv'` | `'USM9'` | System Folder:OMS Folder | the OMS shim (M4 source; PEF `'PPCC'` 1 resource built on the G4) |
| `Read Me` | `'TEXT'` | `'ttxt'` | — (read in place) | period plain-text manual, MacRoman, CR line endings |
| Documentation/Release Notes | `'TEXT'` | `'ttxt'` | — | developer notes (provenance, known bugs) |

File type/creator conventions are taken from the verified OMS 2.3.8 and
FreeMIDI 1.45 installers (`ndrv`/`usbd` for the class driver; `'OMdv'` +
unique creator for OMS drivers; `'TEXT'`/`'ttxt'` for TeachText/SimpleText
documents — the 2.3.8 "XTC Read Me" is `'TEXT'`/`'ttxt'`).

## 2. Why StuffIt, and what the artifacts are

- **Primary downloadable artifact**: `USBMIDI9_0.1.sit` (StuffIt 5/6,
  created with StuffIt Deluxe or DropStuff on the G4). StuffIt preserves
  resource forks and Finder metadata; it is the era's universal format.
- **For FTP/web transport**: `USBMIDI9_0.1.sit.hqx` (BinHex of the .sit).
  BinHex survives 7-bit transports and is itself a period convention
  (Roland's SC-8850 drivers ship as `.hqx`).
- **Optional**: a Disk Copy image (`USBMIDI9_0.1.img`, Disk Copy 6.x) for
  CD-ROM mastering or self-mounting convenience. NOT required for v0.1.
- **Forbidden**: Unix-created ZIP or tar of the Classic files. They destroy
  resource forks and Finder metadata silently; the resulting artifact would
  not install.
- Self-extracting archives (`SEA`) were common but are optional; a `.sit`
  is simpler to verify.

## 3. Resource-fork and metadata requirements per component

- `USBMIDI9` (ndrv): data fork = the PEF (existing build); resource fork =
  `'vers'` + `'cfrg'` (+ optional `'BNDL'`/`'FREF'`); Finder type/creator
  must be set after the CodeWarrior build (the build already produces the
  ndrv; the installer/archive step sets metadata).
- `USBMIDI9 OMS Driver`: data fork empty; resource fork = `'OMdi'` 128 +
  `'PPCC'` 1 (raw PEF container, via `oms/ppcc.r`) + `'SICN'` 128 + `'vers'`
  (from `oms/oms_driver.r`, compiled on the G4). `'BNDL'`/`'FREF'` are
  optional Finder-bundle resources added on the G4 if a custom icon/name is
  wanted; the Rez
  source does not emit them in v0.1. Type `'OMdv'`, creator `'USM9'`.
- `Read Me` / `Release Notes`: plain text, **MacRoman** encoding, **CR**
  line endings (Classic TextEdit/SimpleText convention), type `'TEXT'`,
  creator `'ttxt'`, and a sensible Finder icon/comment. A Unix LF/UTF-8
  file opened in SimpleText displays garbage; the conversion happens on
  the G4 (or with `mac2unix`-style tools + careful re-encoding on Linux
  staging — but the G4 is authoritative).
- Every component: creation/modification dates set to the release date.

## 4. Reproducible release workflow

1. On Linux: stage the source texts (`Read Me`, `Release Notes`) with
   **LF + UTF-8** as the repo master (human-editable, diffable).
2. On the G4: build the two drivers in CodeWarrior (see the G4 handoff);
   convert the texts to MacRoman + CR (SimpleText save-as or a small
   ResEdit/AppleScript step); set type/creator with ResEdit or the Finder
   (Get Info).
3. On the G4: assemble the `USBMIDI9 0.1` folder; create the archive with
   StuffIt Deluxe / DropStuff (`USBMIDI9_0.1.sit`), then BinHex it
   (`USBMIDI9_0.1.sit.hqx`) for upload.
4. Verify (below) on a CLEAN OS 9 system (or the G4 after moving the
   drivers out).

## 5. Verification after expanding on a clean OS 9 system

- The archive expands to one folder named `USBMIDI9 0.1`.
- `Read Me` opens in SimpleText with correct text (no mojibake), CR line
  endings, and the `'TEXT'`/`'ttxt'` type/creator visible in Get Info.
- `USBMIDI9` and `USBMIDI9 OMS Driver` show the correct icons (from their
  `'BNDL'`/`'FREF'`/icon resources) and the correct type/creator in
  Get Info ("ndrv/usbd" and "OMdv/USM9").
- The driver loads: after copying both files to their destinations and
  restarting, the Probe enumerates the interface, and OMS Setup's "New
  Studio Setup" search finds the USBMIDI9 driver (see the G4 gate list).
- Finder "Show Info" on the archive itself shows a StuffIt icon.

## 6. Installation and uninstall (user-facing)

Install: copy `USBMIDI9` to System Folder:Extensions and
`USBMIDI9 OMS Driver` to System Folder:OMS Folder; restart. (A period
installer application is a later option — Installer VISE/StuffIt Installer
Maker are noted as historical tools, not project dependencies.)

Uninstall: remove both files (drag to Trash); restart. No preferences or
other files are created by v0.1 (no control panel, no setup document).

## 7. Release naming and versioning

- File: `USBMIDI9_0.1.sit` / `USBMIDI9_0.1.sit.hqx`.
- Version: 0.1 per the acceptance matrix (`docs/ROADMAP.md`); `'vers'`
  resources and the Read Me "Version History" agree.
- The Read Me states exactly what was tested (Mac OS 9.x, Power Mac G4,
  M-Audio/Evolution Keystation 49e) and that the driver matches
  standards-compliant USB-MIDI 1.0 interfaces generically — one-device
  validation is not universal compatibility, and the known hot-plug issue
  is documented plainly until fixed.

## 8. What is NOT in the repository

No StuffIt, BinHex, Disk Copy, or installer-authoring binaries. The repo
holds: the sources (drivers, texts), this design, and the G4 handoff steps.
Proprietary period material stays in `~/research/` (PROVENANCE.md).

# G4 capture checklist — CodeWarrior "Sample OMS PPC" target

Purpose: on the G4, open the OMS 2.0 SDK's `SampleOMSApp.mcp` in
CodeWarrior Pro 4 and record the settings of the **"Sample OMS PPC"**
target. This is the closest authentic known-good PPC target (an OMS
**client app**, not an OMS driver) — its PPC PEF/container/linker
configuration is the reference for USBMIDI9's PPC target.

**Caveat (do not assume):** an application target's main semantics are
NOT identical to an OMS PPCC driver entry. The app's entry is the CFM
`main` (or `__start`) of a standalone fragment; the OMS driver entry is
`OMSCALLBACK(long) main(short msg, long par1, long par2)` invoked by
OMS via `CallUniversalProc(mainAddr, uppOMSDriverProcInfo, ...)` (pascal
stack-based, 2B msg / 4B par1 / 4B par2, 4B result). What transfers is
the PEF container/linker/alignment configuration; what must be re-checked
is the main-symbol/export handling for a driver.

## Panels to capture (Project → Target Settings, target "Sample OMS PPC")

### PPC PEF panel (the critical one)
- [ ] Container type: 68K / PPC / CFM (expect PPC or CFM)
- [ ] Project type (CodeWarrior / Classic / Shared Library / ...)
- [ ] Fragment name / file name of the output
- [ ] **Main symbol name** (and whether it is derived from the first
      source symbol or explicit)
- [ ] Export style / export file (.exp?) — how symbols are exported
- [ ] Code model (r2-based / TOC / etc.)
- [ ] **Section alignment / container layout options** (the E2b failure
      was a code section at container offset 0xE2 — record what the panel
      shows for section placement/alignment)
- [ ] Any "Export TOC-relative symbols" / transition-vector-related
      option (the authentic TM's main symbol resolves to a vector in its
      data section; our CW build exported `main` class-2 → vector)

### PPC Linker panel
- [ ] PEF creation options, merge options (MacOS Merge?)
- [ ] Any alignment controls (16-byte container alignment)

### PPC CodeGen panel
- [ ] Code model / calling convention (pascal support?)
- [ ] TOC/r2 handling

### PPC Project panel
- [ ] Output file type/creator (the driver resource packaging expects
      the PEF as a 'PPCC' 1 resource with the 4-byte length prefix)

### MacOS Merge / Resource panels
- [ ] Whether the PEF is merged into a resource file, and how

### Other settings
- [ ] Access paths (exact — the OMS SDK headers + MSL + OMSGluePPC.lib)
- [ ] Libraries: MSL RuntimePPC.Lib, MSL C/C++/SIOUX PPC, InterfaceLib,
      OMSGluePPC.lib
- [ ] C/C++ compiler: any pascal-keyword / 4-byte-int settings
- [ ] Rez settings (if the target builds resources)

## Also capture

- [ ] The built artifact (`ToolboxPPC` + `.xSYM` exist in the SDK copy) —
      confirm the target builds unchanged on the G4 (no source edits).
- [ ] The produced PEF's container layout via pefcheck (host-side after
      copying the build output back) — section offsets/alignment, loader
      main fields, exports — as the known-good baseline.
- [ ] Screenshots or exact text of the PPC PEF + PPC Linker panels.

## Transfer plan (after capture)

1. Build the unmodified SampleApp PPC target on the G4; copy the output
   to the host; run `pefcheck` on it — expect PASS with 16-byte-aligned
   code and a CFM-style loader header.
2. Compare the captured PPC PEF panel settings against USBMIDI9's OMS
   target; derive the minimal setting deltas for a driver (main-symbol
   handling, export style, pascal calling convention).
3. Only a linker-generated USBMIDI9 candidate that passes `pefcheck` and
   whose structural differences from the known-working TM/601 artifacts
   are explained may go to a G4 OMS Search runtime test (one deliberate
   test after static validation — no iterative G4 debugging).

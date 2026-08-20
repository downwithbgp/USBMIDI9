# docs/re — USBMIDI9 reverse-engineering corpus

Durable knowledge about **OMS 2.3.8, PEF/CFM, and Mixed Mode** — what we
now know and how we know it. This corpus is meant to survive the project
and to be useful even after USBMIDI9 is finished. It answers the
question: *"What do we know about OMS 2.3.8, PEF/CFM and Mixed Mode, and
how do we know it?"* — the operational G4 build/test checklist lives in
`../g4-handoff.md`, NOT here.

## Files

| file | content |
|---|---|
| `evidence-ledger.md` | every important conclusion, classified PROVEN / STRONG INFERENCE / HYPOTHESIS / RETRACTED, with the supporting evidence |
| `oms-2.3.8-map.md` | base-independent map of the OMS 2.3.8 library/Setup code: offsets, names, calling conventions, callers |
| `mixed-mode.md` | RoutineDescriptor/UPP/CallUniversalProc mechanics + ProcInfo decodes |
| `ppc-68k-reference.md` | PPC/68K registers, instructions, calling conventions, and Mixed Mode boundary |
| `pef-cfm.md` | PEF v1 container format, Code Fragment Manager behavior, special main vs export |
| `runtime-traces.md` | MacsBug/G4 experiment ledger (load-specific addresses only here) |
| `false-leads.md` | retracted/invalid conclusions — so a future agent does not rediscover them |
| `artifacts.toml` | machine-readable artifact manifest (hash + provenance; proprietary binaries hash-only) |
| `ghidra-functions.csv` | offset → function-name export (the durable Ghidra knowledge) |

Tooling: `../../tools/re/` (host-side inspection scripts + smoke tests,
`make check-re-tools`); the structural PEF checker is `../../pefcheck/`.

## Conventions

- **Offsets are resource/file/container-relative, NEVER runtime
  absolute.** CFM fragments move between boots. Runtime addresses such
  as `018Cxxxx` appear ONLY in `runtime-traces.md`, marked load-specific.
- "OMS 2.3.8 library" = the `PROC` 1 of `Open Music System.rsrc`
  (omslib_proc1.bin, sha256 3655f74d…, 86507 B) unless stated otherwise.
- PPC code offsets are into the PEF container's Code section (or stated
  as container offsets).
- Confidence classes: **PROVEN** (byte/runtime evidence in this corpus),
  **STRONG INFERENCE** (converging evidence, no direct proof),
  **HYPOTHESIS** (plausible, untested), **RETRACTED** (shown wrong; see
  false-leads.md).
- Provenance markers: `evidence: <doc/artifact>`; rows with
  `source: session-memory` have no preserved raw artifact yet (see
  runtime-traces.md "TO PRESERVE").

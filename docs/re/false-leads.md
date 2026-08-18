# False leads — retracted / invalid conclusions

Plausible-but-wrong conclusions from the OMS/PEF/Mixed Mode work, with
the one-line reason each was retracted. Preserved so a future agent does
not rediscover them. Full evidence in evidence-ledger.md and the docs
cited.

| # | conclusion | status | why retracted / invalid |
|---|---|---|---|
| F1 | E2b (direct-code diagnostic, code section @0xE2) is a usable test of OMS entry behavior | **INVALID, FROZEN** | code-section containerOffset 0xE2 is not 16-byte aligned (0xE2 % 16 = 2) → invalid PEF; G4 illegal-instruction failure explained by the layout defect alone (pefcheck VERDICT INVALID; docs/oms-ppcc-entry-crash.md §G.6) |
| F2 | "PEF packed-data reserved opcode 5/6" | **RETRACTED** | parser bug from wrong op3/op4 (RepeatBlock/RepeatZero) semantics; with the corrected semantics the authentic TM (167/167) and production (468/468) data sections decode COMPLETELY (b08bf7c; evidence-ledger P5) |
| F3 | PEF-in-'OMdv' packaging with a 4-byte length prefix | **RETRACTED** | the `[be32 length][data]` framing is the Resource Manager's fork record framing, not payload; the OMdv path is the 68K fallback; 'PPCC' carries the raw PEF with no prefix (docs/g4-handoff.md; PEF6) |
| F4 | Typed Rez template for 'OMdi' 128 | **RETRACTED** | Rez packs `boolean` fields as bits → xxportNumB became 0x4000 → OMS -192; replaced by raw `data 'OMdi' (128)` hex payload (docs/g4-handoff.md "OMdi byte gate") |
| F5 | ".main IS the transition vector at 0x10000020" attribution | **RETRACTED** | corrected: `.main` (special main) IS the VECTOR — a data object, not code; the vector-dereference labels at the code target are Ghidra PefDebugAnalyzer inference (bc07115; docs/oms-ppcc-entry-crash.md §E2b) |
| F6 | DebugStr tracing is transparent across the PPC↔68K boundary | **RETRACTED** | DebugStr is a Mixed Mode cross-TOC call that perturbs the state being localized; the valid relative-offset GT showed the post-DebugStr continuation did NOT return to main+0x1A58 (runtime-traces T5; evidence-ledger P9) |
| F7 | "the failing Search run reached omdvAddDevices" | **RETRACTED** | the trace run fired E0 (msg=00FF) but NO I0..IR / T0..T5 — oms_init/omdvAddDevices were never reached (runtime-traces T3) |
| F8 | GT at stale absolute address 0x01936C30 | **INVALID** | the fragment moves between boots; absolute runtime addresses are load-specific and cannot be reused (PEF7; runtime-traces T5) |
| F9 | "0x00FF caller identified" | **RETRACTED** | no literal 0x00FF site exists in the library; the caller is a variable-message path, not conclusively identified (oms-2.3.8-map M3/M4; S2) |
| F10 | "the missing PEF special main is the root cause" (E2a interpretation) | **SUPERSEDED** | E2a's valid special main moved the failure (type-2/3 → type-10) — a real defect, but not the whole story; the entry path was subsequently CLOSED by the linker-generated special-main diagnostic (P10) |
| F11 | "DebugStr caused the production crash" | **RETRACTED** | DebugStr did NOT cause the crash; it only invalidated the DebugStr-based instrumentation (P9) — the crash is the 68K FFFFFFF3 fault (S1) |
| F12 | E-series manual PEF patching as the way forward | **CLOSED** | E1/E2a showed the entry-call path is exercised; E2b invalid; the real fix was the linker Main:main setting; "No E2c" (docs/oms-ppcc-entry-crash.md §H) |
| F13 | A direct OMS pointer/JSR/JMP target equal to `FFFFFFF3` explains the crash | **RETRACTED / HARD-CLOSED** | clean native-trap single-step proves `RTS` first consumes `[A7+4]=00000000`; address zero then executes `68F1` and branches to `FFFFFFF3` secondarily (runtime-traces T8/T9; raw transcript) |
| F14 | `JSR (A0)` at PROC1 +0x98BC proves A0 is raw 68K code | **RETRACTED** | live A0 begins with Mixed Mode magic `0xAAFE`; 68K JSR to a RoutineDescriptor is a valid Mixed Mode dispatch (runtime-traces T8/T9-A0) |

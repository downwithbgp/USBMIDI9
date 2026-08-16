# M4 tasks

Status: research gate PASSED (OMS); FreeMIDI partial. Spec review pending.

## Task list

1. **research/provenance (commit 1)**
   - `docs/research.md`: fill the OMS section (driver file/resources/entry/
     registration/send/receive/packets/timestamps/compat level, each with
     provenance), fill the FreeMIDI section (DDef/IDvr, 'Code'/'DDef'
     resources, PowerPlug, folder layout, coexistence), mark what is
     UNVERIFIED (FreeMIDI driver message protocol; OMS USB-Manager shlb
     loading contract).
   - `~/research/oms/PROVENANCE.md`: every acquired artifact (URL, hashes,
     dates, what it proved). NOT committed (outside repo).
   - `spec/oms/requirements.md` + `design.md` committed with this or the next
     commit.
   - Gate: `make test && make check-classic` still green; docs reviewed.

2. **core/midi_stream (commit 2)**
   - `core/midi_stream.{h,c}` per design §1.1.
   - `tests/test_midi_stream.c`: exhaustive (design §1.1 list) + randomized
     model-compare prop tests (feed random valid packets through
     `um9_rx_*`, compare against a reference decoder; round-trip tx→rx).
   - Makefile: add midi_stream to the lib and tests; keep strict flags.
   - Gate: `make test` + `make test-sanitize` (gcc + clang) green.

3. **oms/ shim (commit 3)**
   - `oms/oms_driver.h`, `oms/oms_driver.c` (main + all omdv* messages),
     `oms/oms_rx.c`, `oms/oms_tx.c`, `oms/oms_driver.r` (Rez source: OMdi 128,
     SICN/ICN#/icl4/icl8, BNDL/FREF, vers), `oms/ppcc.r` (PPCC 1 = raw PEF
     import).
   - `host-check/`: add OMS stub headers (OMS.h subset with ONLY the
     verified types/constants used + citations; OMSDriver.h subset; OMSTimer
     subset) — mirroring the existing USB stub style.
   - `tests/test_oms_driver.c`: mock OMS harness (message dispatch,
     registration payload, packet formatting incl. continuation flags,
     **`OMSSendParams` wiring: send hook receives paramD0 as readHookRefCon
     and the low word of paramD1 in pkt->appConnRefCon — assert both**,
     send-hook drop counter, no-USL-transfer hygiene).
   - `Makefile`: build midi_stream + oms_driver into the test binary;
     `check-classic` compiles oms/oms_driver.c.
   - `codewarrior/USBMIDI9_OMS.exp` (exports for the OMS driver target: the
     PEF's `main` entry + any glue) + build notes in the G4 handoff.
   - Gate: `make test`, `make test-sanitize`, `make check-classic` green;
     grep audit: no USL transfer calls inside `oms/` (grep
     `USBBulk|USBIntRead|USBIntWrite|USBDeviceRequest|USBConfigureInterface|
     USBAllocMem|USBDeallocMem|USBFindNextPipe`); every symbol cited.

4. **FreeMIDI research doc (commit 4)**
   - `docs/freemidi-driver-research.md` per design §3.
   - `docs/research.md` cross-links.
   - Gate: doc review (provenance + separation verified/inferred).

5. **distribution design (commit 5)**
   - `docs/distribution.md` per design §4.
   - Gate: doc review; no binaries added.

6. **user docs (commit 6)**
   - `Read Me` (repo root or docs/ source): period voice, per design §4.
   - `README.md` trimmed to landing page.
   - `docs/architecture.md`/`development.md` minor touch-ups only where the
     service boundary text is now stale.
   - Gate: doc review; period-voice check (no 2026-isms).

7. **roadmap + G4 handoff (commit 7)**
   - `docs/ROADMAP.md`: M2/M3/M4 status refresh, acceptance matrix.
   - `docs/classic-usb-driver.md` §10 or `docs/g4-handoff.md`: exact files for
     CodeWarrior, target type, access paths, .exp, Rez, gate order
     (Probe regression → OMS driver loads in OMS Setup → Keystation input in
     an OMS app → ReBirth → output).
   - Gate: full repo review (see review checklist below).

## Review checklist (run /review on the full diff before push)

- invented Classic API names (every oms/ symbol cited to OMSDriver.h / Spec)
- accidental modern APIs
- proprietary files entering Git
- endian/packing assumptions (big-endian PPC target; explicit LE reads)
- resource-fork loss (docs must not imply Unix ZIP for release artifacts)
- calling-convention assumptions (pascal, UPPs, interrupt-level send hook)
- fake hardware-success claims (output = UNVERIFIED; one-device validation
  language)
- USB logic leaking into oms/ (grep audit)
- documentation that sounds generated rather than period-authentic

## Verification gates

- `make test` (gcc + clang), `make test-sanitize`, `make check-classic` —
  after commits 2, 3, and at the end.
- `/review` on the spec (pre-implementation) and on the final diff.
- Manual audits: citation walk (every Classic symbol → stub header citation),
  no-USB-in-oms grep, docs voice pass.

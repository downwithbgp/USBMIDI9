# M1B follow-up — unrelated-device hot-plug freeze: audit + hardware isolation plan

Status: **audit done** (recorded in `docs/classic-usb-driver.md` §9.9, no
driver changes); hardware isolation experiments pending on the G4.

## Context

The M1B receive gate PASSED on the real G4 (matching, dispatch, bulk
receive, real USB-MIDI packets — `docs/classic-usb-driver.md` §9.8).
A separate defect remains: with USBMIDI9 active and the Keystation
continuously attached, unplugging the unrelated USB mouse and plugging a
normal HID keyboard into the G4's other USB port causes a **hard system
freeze** (no 'q', no Cmd-Opt-Esc; forced reboot).

Ground rules from the hardware report (all honored by this plan):

- DO NOT replace the hardware-proven read resubmission mechanism.
- Do a code/audit pass before patching — done (§9.9); no driver change
  is made until the freeze path is identified on hardware.
- Do not broaden removal-path changes unless our MIDI interface itself
  is actually receiving removal notification (audit: it should NOT be —
  `kNotifyDriverBeingRemoved` is delivered only for the driver's own
  matched interface; §9.9.1).

## Audit summary (evidence in `docs/classic-usb-driver.md` §9.9)

- **Callbacks/status changes when another device changes:** no driver
  notification expected; possible `kUSBAbortedError`/`kUSBNotRespondingErr`
  on the outstanding read if the changed port shares a controller with
  the Keystation; possible transient errors from the probe's
  `USBGetNextDeviceByClass` walk. (§9.9.1)
- **DDK samples:** none handle unrelated-device enumeration; contract is
  "USB software aborts cleanly, driver stops on unexpected abort".
  (§9.9.2)
- **Completion handling:** `kUSBNoErr` → enqueue + resubmit (proven,
  untouched); stall → clear + resubmit; any other error → stop loop,
  driver stays loaded, cleanup deferred to removal. Recorded
  limitation: no auto-restart after a non-removal abort. (§9.9.3)
- **Global/static state:** instance registry mutated only at system task
  time; completion touches only its own instance; probe never caches the
  table pointer. Nothing corruptible by an unrelated-device event.
  (§9.9.4)
- **Execution levels:** completion may run at secondary interrupt or
  task level — both level-safe after the §9.8 fix; probe runs at task
  level only. (§9.9.5)

## Ranked hypotheses

1. H1 — bystander: the Mac OS 9 USB software's own hot-plug/enumeration
   path freezes; USBMIDI9 is not on the freeze path.
2. H2 — bus-shared abort interaction: Keystation shares a controller
   with the changed port; re-enumeration aborts the outstanding read and
   the USB software's abort bookkeeping (or a resubmit racing teardown)
   wedges.
3. H3 — probe polling (`USBGetNextDeviceByClass` at task level) during
   re-enumeration. Low prior.
4. H4 — stall-clear/resubmit re-arm during a reset-induced stall. Low
   prior.

## Hardware experiments (next session on the G4)

Setup invariants: Keystation attached continuously; USBMIDI9 active;
record probe output before/after each event; force-reboot only when
frozen.

1. **E1 baseline:** mouse + Keystation, probe running. Unplug mouse;
   plug HID keyboard into the other port. Freeze?
2. **E2 probe off:** driver loaded, Keystation attached, probe NOT
   running. Same topology change. Freeze? (Isolates H3.)
3. **E3 driver off:** plain Mac OS 9 (USBMIDI9 not installed), Apple
   drivers only. Same topology change. Freeze? (Decides H1 vs driver
   involvement.)
4. **E4 bus split:** move the Keystation to a port on the OTHER USB
   controller than the mouse/keyboard port (note: the G4 has two
   controllers; verify which ports share a controller). Same topology
   change. Freeze? (Isolates H2.)
5. **E5 event split:** (a) unplug mouse only, wait; (b) plug keyboard
   only, wait. Does the freeze need both events?
6. **E6 post-event state (any non-freezing configuration):** after the
   topology change, do NOT replug anything; press a Keystation key and
   record probe output. Expected per audit: data flow stopped (read
   aborted, loop stopped — documented limitation) or, if the abort
   never reached us, data still flows. Record which.

## Definition of done

- Each experiment's outcome (freeze / no freeze + probe output) recorded
  in `docs/classic-usb-driver.md` §9.9 hardware record.
- The component on the freeze path is identified (USB software / driver
  / probe) — H1..H4 resolved or reduced.
- No driver source change is made before that identification; the
  receive path stays byte-identical to the hardware-proven version.
- If a driver change is then warranted, it gets its own spec (following
  `spec/m1b-readpath/tasks.md` structure) and a host regression test
  first.

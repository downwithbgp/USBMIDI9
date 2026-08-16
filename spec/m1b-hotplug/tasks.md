# M1B follow-up — unrelated-device hot-plug freeze: audit + hardware isolation plan

Status: **audit done** (recorded in `docs/classic-usb-driver.md` §9.9, no
driver changes); hardware isolation experiments pending on the G4.
E4 is SATISFIED by the existing test (cross-controller freeze
established — see Context); the pending matrix is E1-E3, E5, E6.

## Context

The M1B receive gate PASSED on the real G4 (matching, dispatch, bulk
receive, real USB-MIDI packets — `docs/classic-usb-driver.md` §9.8).
A separate defect remains: with USBMIDI9 active and the Keystation
continuously attached, unplugging the unrelated USB mouse and plugging a
normal HID keyboard into the G4's other USB port causes a **hard system
freeze** (no 'q', no Cmd-Opt-Esc; forced reboot).

**Bus topology (cross-controller).** Apple's Power Mac G4 Developer Note
states the two rear external USB ports are on **separate USB root
hubs/controllers**. The observed freeze therefore occurred
**cross-controller** (topology change on one controller, Keystation on
the other): no completion-status change on the Keystation's read is
expected, and the shared-controller form of H2 is falsified (§9.9
topology note).

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
  notification expected; `kUSBAbortedError`/`kUSBNotRespondingErr` on
  the outstanding read is possible only in a same-bus scenario — NOT
  applicable to the observed cross-controller event (G4 rear ports are
  on separate controllers; §9.9.1); possible transient errors from the
  probe's global `USBGetNextDeviceByClass` walk (§9.9.1).
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
   path freezes; USBMIDI9 is not on the freeze path. (Leading: no
   driver receive/completion code runs in reaction to the observed
   cross-controller event.)
2. H2 — global USB Manager / cross-controller interaction: the USB
   software's handling of a topology change on ONE controller wedges
   system-wide state (global device list, task, or secondary-interrupt
   dispatch), affecting the other controller. The shared-controller
   form (re-enumeration aborting the Keystation's read) is **falsified
   for the observed freeze** — the G4 rear ports are on separate
   controllers.
3. H3 — probe polling (`USBGetNextDeviceByClass` at task level) during
   re-enumeration: the probe's walk of the USB software's GLOBAL device
   list is the only cross-controller structure our code touches. Low
   prior; decided by E2.
4. H4 — stall-clear/resubmit re-arm during a reset-induced stall. Low
   prior; not applicable to the observed cross-controller event.

## Hardware experiments (next session on the G4)

Setup invariants: Keystation attached continuously; USBMIDI9 active;
record probe output before/after each event; force-reboot only when
frozen. **First:** (1) identify the G4 model (About This Computer / Open
Firmware) so the correct Power Mac G4 Developer Note applies to the
cross-controller claim (§9.9 topology note; §7 provenance row — capture
the note's section/URL then); (2) record/confirm the physical port
layout — which rear port holds the Keystation, which port the mouse
vacated and the keyboard entered — since the cross-controller reading,
E4-SATISFIED, and the H2 falsification all depend on it.

1. **E1 baseline:** mouse + Keystation, probe running. Unplug mouse;
   plug HID keyboard into the other port. Freeze? (Expected: yes —
   cross-controller freeze already established.)
2. **E2 probe off:** driver loaded, Keystation attached, probe NOT
   running. Same topology change. Freeze? (Isolates H3; **the decisive
   experiment** — the probe's global device-list walk is the only
   cross-controller structure our code touches.)
3. **E3 driver off:** plain Mac OS 9 (USBMIDI9 not installed), Apple
   drivers only. Same topology change. Freeze? (Decides H1 vs driver
   involvement; decisive for H1/H2.)
4. **E4 bus split: SATISFIED — removed.** The G4's two rear external
   USB ports are on separate controllers (Apple Power Mac G4 Developer
   Note), and the observed freeze already occurred cross-controller
   (topology change on one controller, Keystation on the other). The
   same-bus abort mechanism (falsified shared-controller form of H2)
   is not the trigger.
5. **E5 event split:** (a) unplug mouse only, wait; (b) plug keyboard
   only, wait. Does the freeze need both events?
6. **E6 post-event state (any non-freezing configuration):** after the
   topology change, do NOT replug anything; press a Keystation key and
   record probe output. Expected in the observed cross-controller
   configuration: data flow survives unchanged (no abort of our read
   expected); data flow stopping would itself be a finding (an
   unexpected completion status reached the driver). The abort-stop
   expectation applies only to a same-bus scenario, which the G4's
   rear-port layout does not produce. Record which.

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

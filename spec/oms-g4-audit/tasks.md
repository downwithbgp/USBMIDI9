# OMS G4 audit — implementation tasks

Requirements: `spec/oms-g4-audit/requirements.md` (R1-R8). Evidence
sources cited there. All code changes below are additive/corrective;
the hardware-proven class-driver data path (ring enqueue) is untouched.

## Design (evidence-backed decisions)

**D1 — Notification Manager is NOT a timer.** Authentic UI 3.3.2 NMRec
and NMProcPtr; NMInstall/NMRemove exist for user alerts only (verified:
both Opcode USB components' PostNotification decompilation). Our OMS
shim must not use it for receive scheduling. The host-check header is
corrected to the authentic model; nothing in oms/ calls it anymore.

**D2 — Receive = push at interrupt level.** The class driver's bulk-IN
completion already runs at interrupt level and enqueues into the
resident ring. Add ONE optional dispatch-table entry
`setEventCallback(UInt32 index, USBMIDI9EventCallbackProcPtr cb, UInt32
refcon)`; when set, the completion calls `cb(index, refcon)` right after
enqueue. The OMS shim registers `oms_rx_event` which drains the ring
through the EXISTING dequeueBytes and calls OMSReceivedFromPort
(interrupt-level legal, OMS Spec). Mirrors Opcode's USB OMSMIDIDriver
(IOCompletion -> installed read proc). NULL callback = today's Probe
behavior (no change). Backlog: at omdvStartMIDI2 and at attach,
dequeue-and-discard stale bytes, then after registering drain until
empty (loop).

**D2a — Version gate split.** `kUSBMIDI9DispatchTableVersion` becomes
0x0002. The Probe's acceptance minimum stays 0x0001 (its own local
constant — the Probe needs no callback). The OMS shim requires
version >= 0x0002 and a non-NULL setEventCallback; a v1 driver is
rejected by the shim (which then stays ready for replug via the USB
notification, without a table).

**D2b — Concurrency contract.** oms_rx_event runs inside the class
driver's own read completion: the driver fragment is alive for the whole
call; single-core G4 preemption cannot separate pointer capture from
use. oms_rx_event re-checks g_oms.table after capture (existing
discipline), performs no task-time OMS/USB calls, and never re-enters
the dispatch table (constraint for TODO(oms-output)).

**D3 — Lifecycle: locate once + USB notifications.** No per-tick walk.
- omdvInit: zero state; install USB device notification (pb class
  0x01/subclass 0x03, kNotifyAnyEvent; store the returned token, guard
  double-install); attempt locate (once); return 0 even when no device
  is attached or the notification install fails (SampleCell precedent;
  supports hot-plug — omdvInit failure would make OMS dispose the
  driver).
- omdvAddDevices / omdvStartMIDI2: ensure located (locate if NULL).
- Notification callback (task time): kNotifyAddDevice/Interface ->
  attach (locate once, register callback, reset ports on reappear,
  drain backlog); kNotifyRemoveDevice/Interface with matching
  deviceRef -> detach (clear table + stored ref before the driver
  fragment unloads; no stale pointer).
- omdvDispose: unregister callback if table valid (best effort),
  USBRemoveDeviceNotification(token), zero state.
- omdvStopMIDI: midiStarted = 0 only (callback stays registered so
  drains can keep the ring from overflowing? NO — drains are gated on
  midiStarted; ring overflow is the class driver's existing behavior).

**D4 — Class driver changes (additive).**
- usbmidi9_dispatch.h: version -> 0x0002; add the callback typedef +
  setEventCallback entry (documented: optional, interrupt-level,
  per-interface index).
- usb_driver.h/.c: per-instance callback + refcon; setEventCallback
  validates index and stores; completion path calls the callback after
  enqueue when non-NULL; removal path (kNotifyDriverBeingRemoved)
  clears the callback.

**D5 — Host tests** (tests/test_oms_driver.c rework; test_machine.c
additions): mocks replace NMInstall/NMRemove with
USBInstallDeviceNotification/USBRemoveDeviceNotification + a fake
setEventCallback that records the registered callback; data delivery is
driven by invoking the recorded callback (the class driver's push), NOT
by a poll loop. New assertions: (a) receiving data causes ZERO
USBGetNextDeviceByClass calls (the per-tick walk is gone); (b) locate
happens once per lifecycle transition; (c) matching-ref removal clears
the table, unrelated-ref removal does not; (d) replug resets stream
state (mid-SysEx unplug test preserved); (e) version gate: shim
rejects a v1 table (mock version set to 0x0001) but stays alive for
replug, and the Probe-style 0x0001 minimum still accepts v2; (f)
backlog: bytes queued before omdvStartMIDI2/attach are discarded, not
delivered; (g) callback gating when midiStarted == 0; (h) init twice
does not double-install the USB notification; (i) remove notification
for a device that was never located is a no-op.

## Tasks

- [T1] `host-check/Notifications.h` — authentic UI 3.3.2 model
  (R1a, R1b). Provenance comment; file rename note: authentic file is
  `Notification.h`; keep our filename for include compatibility but
  document it.
- [T2] `classic/usbmidi9_dispatch.h` — version 0x0002, callback typedef,
  setEventCallback (R3b, D2a). Update the class driver + Probe compile
  sites; the Probe keeps its own 0x0001 minimum.
- [T3] `classic/usb_driver.h` + `classic/usb_driver.c` — per-instance
  callback storage, setEventCallback proc, completion-path invocation,
  removal clears callback (R3a, R3b).
- [T4] `host-check/USB.h` — authentic USB Manager notification
  surface (R6b): USBDeviceNotificationParameterBlock,
  USBDeviceNotificationCallbackProcPtr, USBInstallDeviceNotification,
  USBRemoveDeviceNotification, kNotify* constants.
- [T5] `oms/oms_driver.h` — remove NMRec/timer fields
  (timerRunning, nmRec, oms_poll_task, kUSBMIDI9OMSPollTicks); add
  notification state (pb, token, stored deviceRef, callbackRegistered,
  notifierInstalled).
- [T6] `oms/oms_driver.c` — lifecycle rewrite (D3): oms_attach /
  oms_detach (attach = locate once + register callback + reset ports on
  reappear + discard stale backlog + drain-until-empty; detach = clear
  table + stored ref), USB notification proc (kNotifyAdd* -> attach,
  kNotifyRemove* with matching ref -> detach), init (notifier install
  with token guard, return 0 always), add/start/dispose updates
  (startMIDI2 discard + drain; dispose unregisters callback when table
  valid, USBRemoveDeviceNotification when installed), delete the poll
  task; keep oms_handle_message dispatch and
  omdvAddDevices/GetPortSendProc/SetPortReceiveRefNum behavior.
- [T7] `oms/oms_rx.c` — oms_rx_event callback (interrupt level; drains
  one interface through dequeueBytes; gated on midiStarted && table;
  re-checks table after capture; drain-until-empty helper);
  delete oms_poll_task; keep oms_rx_drain as the shared drain body.
- [T8] `tests/test_oms_driver.c` — rework mocks + tests per D5 (R8a).
- [T9] `tests/test_machine.c` — class-driver callback tests
  (register/unregister, invocation after enqueue, NULL passthrough).
- [T10] `docs/host-check-audit.md` — the header audit table (R6a):
  stub symbol/type | authentic source | exact/intentional difference,
  for every host-check header (MacTypes, MacErrors, Memory, Events,
  OSUtils, Notification(s), CodeFragments, DriverServices, USB, OMS,
  OMSDriver). Fetch/compare authentic UI 3.3.2 headers + OMS SDK
  headers + DDK USB.h.
- [T11] `docs/g4-handoff.md` — new lifecycle instructions (R7b): no
  NMInstall/NMRemove; USBManagerLib calls; receive = event callback.
- [T12] `docs/research.md` (OMS section) + `docs/ROADMAP.md` — honest
  status per R7a; record the NM-alert finding and the push/lifecycle
  design with the same provenance style.
- [T13] Gates: `make test && make test-sanitize && make check-classic`
  (R8a); /review on the final diff; focused commits + push (R8b);
  final report (R8c).

## Verification gates

- G1: `make test` green (host tests incl. new lifecycle tests).
- G2: `make test-sanitize` green.
- G3: `make check-classic` green (all Classic sources incl. oms/ and
  the class driver compile against the corrected stubs).
- G4: grep gate — no `oms_poll_task`, no `NMInstall`, no
  `kUSBMIDI9OMSPollTicks` anywhere in oms/; `USBGetNextDeviceByClass`
  appears in oms/ only inside oms_locate_dispatch (called from
  lifecycle transitions, never per tick).
- G5: audit table complete; every host-check header has a row.
- G6: /review of the final diff (Medium+ rule) before commit.

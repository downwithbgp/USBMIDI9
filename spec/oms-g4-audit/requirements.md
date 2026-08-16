# OMS G4 audit — authentic API corrections (requirements)

Status: **BLOCKED on authentic receive scheduling/lifetime** (this spec
unblocks it). Reviewer: "STOP OMS G4 BUILD — AUTHENTIC API AUDIT FAILURE".

Source of truth for every claim: `~/research/oms/PROVENANCE.md`,
`~/research/oms/spec.txt` (OMS Programming Interface spec, Mar 1995),
`~/research/usbddk/usb_api_ref_v26.txt` (Mac OS USB DDK API Reference
Rev. 26, 12/23/99), the authentic Universal Interfaces 3.3.2 headers
(mirror of `~/research/UniversalInterfaces3.3.2.sit.hqx`), and PEF
disassembly of the Opcode OMS 2.3.8 USB components (Ghidra 12.1).
Nothing proprietary is committed; findings are described, sources cited.

## R1 — Notification Manager model must be authentic (review item 1)

The host-check `host-check/Notifications.h` currently invents an
NMRec with `eventTime`, `nMsg`, `nRefCon` and models NMInstall/NMRemove
as a periodic callback scheduler. This is false.

- [R1a] The stub must model the authentic UI 3.3.2 `Notification.h`
  NMRec: `qLink, qType, nmFlags, nmPrivate, nmReserved, nmMark, nmIcon,
  nmSound, nmStr, nmResp, nmRefCon`; `NMProcPtr = void(*)(NMRecPtr)`;
  `NMInstall(NMRecPtr)`, `NMRemove(NMRecPtr)`. No invented fields.
- [R1b] No code may use NMInstall/NMRemove as a periodic timer.

Evidence (verified this session):
- UI 3.3.2 `Notification.h` (elliotnunn/UniversalInterfaces mirror of
  the same archive as `~/research/UniversalInterfaces3.3.2.sit.hqx`;
  local .sit image is truncated and unreadable — mirror used, same
  content). `struct NMRec` has NO eventTime/nMsg.
- OMS 2.3.8 "OMS USB Manager" PEF: imports NMInstall but NOT NMRemove;
  decompiled `PostNotification` builds a static NMRec (qType=8, nmStr =
  alert text, nmResp = UPP) and calls NMInstall ONCE per alert.
- OMS 2.3.8 "USB OMSMIDIDriver" ('ndrv') PEF: NMInstall/NMRemove/
  TickCount/SystemTask appear ONLY in `PostNotification`,
  `PendingNotificationAlerts`, `RemoveNotificationAlerts` — user-visible
  alerts ("The USB OMSMIDIDriver is too old for the OMS USB Manager").

## R2 — Why Opcode imports NMInstall/NMRemove (review item 2)

Answered by disassembly (above): user-visible Notification Manager
alerts with response routines. NOT a timer, NOT receive scheduling.

## R3 — Authentic receive mechanism (review item 3)

Research order followed; conclusions with evidence:

1. OMS SDK SampleCell: send-only sample; its omdvInit returns success
   with no hardware (driver may load without devices).
2. OMS Spec: OMSReceivedFromPort "May be called at interrupt level";
   drivers pass received packets with the ioRefNum from
   omdvSetPortReceiveRefNum; negative ioRefNum = do not deliver.
3. Opcode USB OMSMIDIDriver: receive = USBIntRead completion at
   interrupt level; the OMS side registers a read proc
   (`MIDIInstallRead` export; `DefaultMIDIReadProcPtr` fallback); work
   deferred with QueueSecondaryInterruptHandler where needed.
4. Opcode OMS USB Manager: locates the class driver ONCE via
   USBGetNextDeviceByClass + FindSymbol; learns of attach/detach via
   USBInstallDeviceNotification (imports). No periodic timer anywhere.

- [R3a] Receive must be push-based: the USBMIDI9 class driver's
  existing read completion (hardware-proven) must invoke an optional
  registered event callback after enqueueing bytes; the OMS shim's
  callback drains the ring (existing dequeueBytes) and calls
  OMSReceivedFromPort at interrupt level (legal per the Spec).
- [R3b] This requires a small additive extension to the neutral
  USBMIDI9 client API: `setEventCallback` in the versioned dispatch
  table (version 0x0002). The proven data path (ring enqueue) is NOT
  modified; the callback is optional (NULL = current Probe behavior).
- [R3c] Concurrency contract (argued, not assumed): the drain runs
  inside the class driver's own read completion (the driver fragment is
  alive for the whole call; single-core G4 task-time preemption cannot
  interleave a detach between pointer capture and use). oms_rx_event
  captures g_oms.table once, must not call any task-time OMS/USB API,
  and the TX path must never re-enter the dispatch table from the RX
  callback (constraint recorded for TODO(oms-output)).
- [R3d] Stale backlog: the ring keeps unread bytes and drops NEW bytes
  when full (classic/ring.c), and drains are gated on midiStarted. At
  omdvStartMIDI2 and at attach, dequeue-and-discard the stale backlog,
  and after (re)registering the callback drain until empty (loop, not a
  single chunk).

## R4 — Lifecycle without a per-tick USB walk (review item 4)

- [R4a] DELETE `oms_poll_task` and the whole per-tick
  USBGetNextDeviceByClass walk (kUSBMIDI9OMSPollTicks == 1 = 60 Hz
  global enumeration today). No per-tick USB activity may remain.
- [R4b] Locate the dispatch table only at lifecycle transitions:
  omdvInit, omdvAddDevices, omdvStartMIDI2, and device-add
  notification.
- [R4c] Attach/detach awareness via USBInstallDeviceNotification (the
  authentic mechanism, Rev 26 Ch 4: "Use the USBInstallDeviceNotification
  mechanism to be alerted when a device or interface is added or
  removed"; same imports as the real OMS USB Manager). Filter class
  0x01/subclass 0x03 in the pb; kNotifyAnyEvent (0xff). kNotifyAdd
  Device/Interface -> re-locate once + register callback + reset ports;
  kNotifyRemoveDevice/Interface with matching deviceRef -> detach.
  Stale-pointer safety argument (no documented ordering between the
  notification and the driver unload): the drain only ever runs inside
  the class driver's own completion (fragment alive during the call);
  single-core preemption cannot separate pointer capture from use; and
  the detach (task time) clears the table before any later OMS message
  could use it.
- [R4d] USBRemoveDeviceNotification at omdvDispose (Rev 26 Ch 6: a
  notification must be removed before the fragment unloads); guard
  double-install with the stored token; if installation failed at
  init, omdvInit still returns 0 (no hot-plug awareness, but the driver
  loads) and dispose skips the removal call.
- [R4e] Unrelated-device removals must not clear state: keep the
  deviceRef returned by the locate walk; only detach when the removed
  ref matches. Known limitation (M1B single-device scope): if the
  located device is removed while another USBMIDI9 stays attached, the
  survivor is re-located on the next OMS message/attach notification.

## R5 — Preserve good work (review item 5)

No revert of: OMS SDK provenance (docs/research.md, PROVENANCE.md),
OMdv resource/file research (oms/oms_driver.r), midi_stream converter
+ tests, authenticated omdv message dispatch (oms_handle_message),
FreeMIDI research boundary, distribution research, period docs.

## R6 — Header audit (review item 6)

- [R6a] Produce a table (docs/host-check-audit.md): for every
  `host-check/*.h`, list stub symbol/type, authentic source
  (UI 3.3.2 / OMS SDK 2.0 / DDK 1.4.1 + Rev 26), exact/intentional
  difference. Eliminate invented APIs.
- [R6b] Add the authentic USB Manager notification surface to the
  host-check USB.h (struct USBDeviceNotificationParameterBlock,
  USBInstallDeviceNotification, USBRemoveDeviceNotification,
  USBNotificationType constants incl. kNotifyAnyEvent=0xff and the
  verified values kNotifyAddDevice=0, kNotifyRemoveDevice=1,
  kNotifyAddInterface=2, kNotifyRemoveInterface=3).

## R7 — Honest status (review item 7)

- [R7a] docs/ROADMAP.md + docs/g4-handoff.md + docs/research.md:
  - OMS historical research gate: PASS
  - OMS adapter logic: partial/source work (corrected)
  - OMS real-target source gate: BLOCKED on authentic receive
    scheduling/lifetime (until this spec is implemented)
  - OMS G4 build/test: NOT YET attempted
- [R7b] g4-handoff must drop NMInstall/NMRemove from the G4 build
  instructions and describe the new lifecycle.

## R8 — Gates, commits, final report (review item 8)

- [R8a] `make test`, `make test-sanitize`, `make check-classic` all
  green after the corrections. Green host tests are NOT evidence of
  Classic API correctness — the audit table + primary sources are.
- [R8b] Focused commits, then push.
- [R8c] Final report must explain: how the false NM model entered the
  code; the authentic replacement with source evidence; how
  dispatch lifetime/replug works without a per-tick USB walk; which
  OMS files are safe to try in CodeWarrior.

## Non-goals (this pass)

- No OMSVersion() compat-1 check (Spec-required but out of scope;
  recorded as TODO).
- No change to Probe's own enumeration behavior (separate diagnostic;
  the G4 freeze audit continues per spec/m1b-hotplug).
- No bulk-OUT path (TODO(oms-output) unchanged).
- No FreeMIDI work.

# Deep source audit (2026-09)

Scope: the production sources — `core/` (packets, descriptors, ports,
midi_stream), `classic/` (usb_driver, ring, dispatch ABI), `oms/`
(oms_driver, oms_rx, oms_tx), `probe/probe.c` — read line by line, plus a
cross-check of the tests that cover the risky paths. The G4-only artifacts
(CodeWarrior targets, PEF/PPCC packaging) were out of scope.

## Fixes made from this audit

1. **`omdvGetPortSendProc` could hand OMS a NULL send proc.**
   `NewOMSReadHook2` (a RoutineDescriptor allocation) may fail at
   `omdvInit` under memory pressure; the old code still returned `noErr`
   with `sendPars->proc = NULL`, and OMS invokes the returned UPP without
   further checking. Now the build is retried at task time
   (`omdvGetPortSendProc` itself) and, if it still fails, the message
   returns `kUSBBadDispatchTable` with the out-params cleared. Covered by
   `test_send_proc_null_guard` (the host mock can now inject
   `NewRoutineDescriptor` failure).
2. **Unframed RX tail bytes were dropped silently.**
   `oms_rx_drain` dequeue chunks that do not end on a 4-byte Event Packet
   boundary (nonconformant device) left 1–3 bytes unaccounted: they are
   already dequeued and cannot be re-framed, so they are now counted in
   `g_oms.rxDropped`.
3. **Comment/behavior mismatch in `oms_rx_event`.** The comment claimed
   bytes arriving while MIDI is off stay in the ring until
   `omdvStartMIDI2`; the implementation drains and drops them immediately
   (`deliver = 0`), which is also what `test_event_gated_when_midi_stopped`
   asserts. The comment now matches the behavior.
4. **Stale TODO in `core/packets.h`.** It claimed SysEx
   packetization/reassembly "is not implemented here yet"; it is
   implemented in `midi_stream.h/.c`.

## Reviewed and found sound (evidence for the next G4 gate)

- `core/packets.c` CIN table, status/CIN mapping, and length checks match
  USB-MIDI 1.0 Table 4-1; `um9_packet_encode` length/data logic is exact.
- `core/descriptors.c` walker bounds are total: truncated/zero-length
  descriptors set an error and never advance past the buffer;
  `um9_desc_u8/u16le` cannot read outside the current descriptor.
- `core/ports.c` jack/endpoint caps stop recording without corruption;
  the `bNrInputPins`/`bNumEmbMIDIJack` clamps cannot underflow (the
  `bLength >= 9`/`>= 5` gates precede them).
- `core/midi_stream.c` SysEx packet math is exact for every length
  (`needed = 1 + (len-3+2)/3`), the middle/end split cannot write out of
  the caller's packet array, and the queue is a strict SPSC ring with
  drop-new accounting.
- `classic/ring.c` usable capacity is `size` (monotonic counters), wrap
  and partial enqueue/dequeue loops are correct, and the
  volatile-publish discipline (data before index) is documented.
- `classic/usb_driver.c` state machine: the issued-token check catches
  synchronous completions inside a USL call; the depth bound breaks a
  USL that completes every call synchronously; immediate errors clear
  `kCompletionPending` so removal can always finalize; the removal
  notification races (completion arriving during abort) are handled in
  both orders; dealloc happens only after the last completion drained.
- `oms/oms_driver.c` UPP lifecycle: the descriptor is created once per
  `omdvInit`, disposed on re-init and `omdvDispose`, never at interrupt
  time; the device-notification PB lives in `g_oms` for its whole
  lifetime; `oms_bind_dispatch` only ever binds from no-table.
- `oms_rx.c`/`oms_tx.c` packet conventions match the recorded evidence:
  `OMSPacket.len` = MIDI data bytes only for `OMSReceivedFromPort`
  (`host-check/OMS.h` note, `spec/oms/requirements.md` row 8), and
  `OMSMIDIPacket.len` = data bytes only for the send hook. The 2-byte
  SysEx carry re-chunks OMS 4-byte chunks into 3-byte CIN 0x4 packets plus
  a 1–3 byte end packet for every chunk boundary; the carry bounds
  (`<= 6`) hold.
- `probe/probe.c` never caches the dispatch table across polls (a cached
  pointer would dangle after unplug); the KeyMap test is the G4-verified
  byte/bit form.

## Open items (recorded, not fixed)

- **`core/ports.c` stops at the first MIDIStreaming interface** (documented
  TODO). Consequence to keep in mind for M2/M5: a device whose alternate
  setting 0 declares the interface with zero endpoints and puts them in a
  nonzero alternate setting yields no ports from a raw descriptor dump.
  Needs a fixture-driven test and, if real devices need it, alternate
  setting selection work.
- **One removal notification stops all instances** in
  `classic/usb_driver.c` (documented M1B single-device limitation; M2
  work).
- **Stale `valid` flags after an interface-count shrink** in the OMS shim
  (`oms_add_devices` marks `0..count-1` valid but never clears the rest).
  Memory-safe (the driver rejects out-of-range indices); clean up when
  multi-device support lands.
- **`oms_add_devices` discards the returned `OMSDeviceH`** (production
  build) — fine until device removal needs it.
- **Probe cosmetics**: the "dispatch table version too old" message
  reprints on every poll.

## Verification

`make test`, `make test-sanitize`, `make check-classic`, `make check-trace`
and `make check-re-tools` all pass on the audited tree; the new
`test_send_proc_null_guard` runs in `make test` and `make test-sanitize`.

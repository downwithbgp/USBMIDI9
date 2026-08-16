# M4 design — neutral MIDI stream layer + OMS shim

> **SUPERSEDED in part by `spec/oms-g4-audit/` (OMS receive-scheduling
> correction).** Section 2.3's poll-timer design (below) is historically
> false — the Notification Manager is an alert API, not a timer (verified
> by PEF disassembly of Opcode's own OMS 2.3.8 USB components), and a
> per-tick `USBGetNextDeviceByClass` walk is a real-G4 freeze suspect.
> The authentic replacement (push event callback via dispatch table
> v0x0002 + `USBInstallDeviceNotification` lifecycle, no timer, locate
> only at lifecycle transitions) is implemented and host-tested; see
> `spec/oms-g4-audit/requirements.md` and `tasks.md`. Sections 1-2.2
> (neutral boundary, midi_stream, omdv dispatch) remain current.

## 1. Neutral service boundary (frozen, with one justified addition)

The existing `USBMIDI9DispatchTable` provides what the OMS driver needs
from the transport: enumerate interfaces, per-interface info, polled byte
dequeue, and (v0x0002, added by the oms-g4-audit correction) an optional
interrupt-level event callback for push delivery (`setEventCallback`).

The OMS input contract (requirements #8) requires: parse raw received bytes
into **single conventional MIDI messages**, and for SysEx deliver messages with
**continuation flags** (`omsStartCont`/`omsMidCont`/`omsEndCont`). The USB-MIDI
event-packet layer already exposes 4-byte packets with CIN semantics
(`core/packets.c`). What is missing is the stream-level conversion, which is
USB-MIDI-semantics work (not OMS work): therefore it belongs in `core/` as a
portable, host-tested module.

### 1.1 `core/midi_stream.{h,c}` — USB-MIDI packet stream ⇄ MIDI byte stream

Stateful converter, one instance per cable:

```c
struct um9_rx_stream;   /* opaque; per-cable receive state */
void   um9_rx_init(struct um9_rx_stream *s);
/* feed one raw 4-byte USB-MIDI event packet; 0 on malformed input */
int    um9_rx_packet(struct um9_rx_stream *s, const unsigned char pkt[4]);
/* take next complete conventional MIDI message; returns 1 with a
 * message (status+data) and a SysEx-position flag (start/mid/end/none),
 * or 0 when none pending. */
int    um9_rx_message(struct um9_rx_stream *s, unsigned char *out,
                      unsigned *out_len, unsigned *sysex_pos);
```

Behavior (from USB-MIDI 1.0 Table 4-1 + the OMS packet requirements):

- CIN 0x9..0xF (channel voice): emit one message (data length per CIN).
- CIN 0x2/0x3/0x5 (system common, 1-3 bytes): emit as one message.
- CIN 0x4 (SysEx start/continue): accumulate; emit chunks with
  `sysex_pos` = start|mid. Each 4-byte packet carries 1-3 data bytes.
- CIN 0x6/0x7/0x8 (SysEx end with 1/2/3 bytes): append, emit final chunk
  marked end, reset accumulator. The final chunk carries the end packet's own
  data bytes — typically including the trailing F7 (per USB-MIDI 1.0 the
  terminator is a data byte of the end packet); no F7 is synthesized when the
  device omits it (documented device-quirk handling, counted).
- CIN 0x1 (cable event): not MIDI; dropped, counted.
- CIN 0x0 (misc): payload length undefined; dropped, counted.
- Malformed packet input never corrupts state.

Output side (host → device), for the OMS send hook:

```c
struct um9_tx_stream;   /* opaque; per-cable transmit state */
void  um9_tx_init(struct um9_tx_stream *s);
/* Encode one conventional MIDI message into 4-byte USB-MIDI packets.
 * Channel voice and fixed-length system common → one packet.
 * SysEx (F0 ... F7) → start packet(s) with CIN 0x4 (1-3 bytes each),
 * end packet with CIN 0x6/0x7/0x8 (1-3 bytes) or a bare 0x6 for F7 only.
 * Returns number of packets written (0 on invalid input). */
int   um9_tx_message(struct um9_tx_stream *s, unsigned cable,
                     const unsigned char *msg, unsigned len,
                     unsigned char out[][4], unsigned max_packets);
```

Both are C89, endian-neutral, no Classic/OMS dependency, fully host-tested
(Note On/Off, CC, PC, PB, channel pressure, realtime 0xF8/0xFE inline, MTC/SPP,
SysEx single/multi/empty, malformed CIN, cable 0..15, 3-byte packet limit,
multi-cable interleaving).

### 1.2 Why not in `classic/`?

Pure MIDI semantics; host-testable like `packets.c`/`ports.c`; shared by the
OMS shim and any future FreeMIDI shim. Not a general-purpose MIDI framework —
just the two stream adapters the clients need.

## 2. OMS shim (`oms/`)

### 2.1 Files

- `oms/oms_driver.h` — driver signature (`'USM9'`), message constants (from
  `OMSDriver.h`, cited), dispatch-table access decls.
- `oms/oms_driver.c` — `pascal long main(short msg, long par1, long par2)`:
  omdvInit (locate `USBMIDI9DispatchTable` via `USBGetNextDeviceByClass` +
  `FindSymbol` — the same verified pattern as the Probe and Opcode's OMS USB
  Manager; this is driver lookup, not USB I/O), omdvDispose,
  omdvAddDevices (compat level 1: one `OMSDevice` per attached interface;
  no VID/PID logic), omdvSetInterfaceList, omdvStartMIDI/omdvStartMIDI2/
  omdvStopMIDI (start/stop polling task), omdvGetPortSendProc (send hook),
  omdvSetPortReceiveRefNum (ioRefNum per port; −1 = drop), omdvConfigure /
  omdvTestDevice / omdvDifferentStudioSetup / omdvConnectsChanged /
  omdvRemoveOutput (documented no-ops with correct returns).
- `oms/oms_driver.r` — Rez source: `'OMdi'` 128 (id 0x7F10 — unassigned in the
  inspected 2.3.8 driver set; flags 0; `driverCompatibilityLevel` 1),
  `'SICN'` 128 (pairs, per the Spec) + `'ICN#'`/`'icl8'`/`'icl4'` (present in
  the verified Roland SC-8850 driver and the 2.3.8 MIDIPort 32 driver — cited),
  `'BNDL'`/`'FREF'`, `'vers'`. The `'OMdv'` 128 code resource is the PEF
  container produced at link time (CodeWarrior; Roland driver = format
  reference); exact construction steps go in the G4 handoff.
- `oms/oms_rx.c` — receive path: drain `dequeueBytes`, run `um9_rx_*`,
  format `OMSMIDIPacket` (flags continuation bits; `len` = data bytes only),
  deliver via `OMSReceivedFromPort(pkt, ioRefNum)`.
- `oms/oms_tx.c` — send hook: `OMSReadHook2`-shaped pascal proc; validates the
  packet, splits via `um9_tx_message`, hands 4-byte packets to the driver's
  bulk-OUT queue. **Output is marked UNVERIFIED**: the dispatch table v1 has
  no enqueue path; the send hook drops with a documented counter until the
  G4 gate adds `enqueueBytes` + bulk-OUT. No fake output claimed.

### 2.2 Driver identity

- Signature/creator: `'USM9'`.
- Port model: one interface = one OMS device (whichOut = interface index+1;
  OMSPortID.whichPort = cable for multi-cable interfaces — designed and
  harness-tested; single-cable first on the G4).
- Name: "USBMIDI9 Port N"; manuf "USBMIDI9"; model
  "USB-MIDI Interface" (no VID/PID-specific naming, per the brief).

### 2.3 Execution context

- omdv* messages arrive at task level (OMS calls drivers from its own task
  context — pin exact wording from the Spec's OMS Drivers chapter at
  implementation; the SDK headers carry no explicit level claim for the
  messages). The send hook MAY be called at interrupt level (verified, Spec:
  "may be called at interrupt level") — the send path only touches the ring +
  atomic counters, no Toolbox calls (ring is SPSC by design).
- Polling: a Time Manager / Notification Manager task runs while MIDI is
  started (omdvStartMIDI2 .. omdvStopMIDI), drains the ring through
  `um9_rx_*` → `OMSReceivedFromPort` (interrupt-safe per the Spec). Choice
  pinned against the SDK's OMS Timer spec at implementation.

### 2.4 Host tests

- `tests/test_midi_stream.c` — exhaustive converter tests (1.1 list).
- `tests/test_oms_driver.c` — compiles `oms/oms_driver.c` against
  `classic/host-check/` stubs + a mock OMS environment (mirrors
  `test_machine.c`): drives omdv* messages, asserts device registration
  payloads, packet formatting, continuation flags, send-hook behavior
  (**including `OMSSendParams` paramD0 → readHookRefCon and low word of
  paramD1 → pkt->appConnRefCon**), and no-USL-transfer-reference hygiene.
- `make check-classic` extended to compile `oms/oms_driver.c`.

## 3. FreeMIDI (research + design only)

`docs/freemidi-driver-research.md`: verified facts (requirements table 2),
binary observations (DDef/IDvr types, 'Code'/'DDef' resources, PowerPlug glue,
TheMOTUShimInterface), unanswered ABI questions (driver message protocol,
params layout, receive call), implementation plan for when an SDK is
authenticated. No code.

## 4. Distribution + docs

- `docs/distribution.md`: period release layout (Read Me, USBMIDI9, USBMIDI9
  OMS Driver, Documentation), per-component type/creator table (ndrv/usbd;
  OMdv/USM9; TEXT/ttxt; ...), resource-fork preservation (StuffIt primary;
  .sit.hqx for FTP/web; Disk Copy optional; NO Unix ZIP), Read Me encoding
  (MacRoman, CR) + Finder metadata, archive creation/verification procedure,
  uninstall procedure, release filename convention (USBMIDI9_0.1.sit.hqx).
- `README.md`: trimmed landing page; deep archaeology stays in docs.
- `Read Me` (user-facing, period voice): System Requirements, Installing
  USBMIDI9, Using USBMIDI9 with OMS, Removing USBMIDI9, Known Problems
  (hot-plug issue documented plainly), Compatibility (tested: Mac OS 9.x /
  Power Mac G4 / Keystation 49e; generic class-matching claim), Version
  History, Technical Notes. Plain text, no emoji/badges/marketing.
- `docs/ROADMAP.md` + `docs/classic-usb-driver.md` §10: acceptance matrix and
  G4 handoff (exact files, CodeWarrior target type, access paths, .exp
  exports, resources, gate order).

## 5. Commit sequence (focused commits)

1. `research: OMS driver API provenance + FreeMIDI evidence` (docs/research.md;
   PROVENANCE in ~/research; spec/oms/requirements.md).
2. `core: USB-MIDI stream ⇄ MIDI message converter + tests` (midi_stream +
   tests, Makefile).
3. `oms: USBMIDI9 OMS driver shim (source gate)` (oms/*, host-check stubs,
   test_oms_driver, check-classic).
4. `research: FreeMIDI driver research doc`.
5. `dist: period-correct distribution design`.
6. `docs: user Read Me + README cleanup`.
7. `roadmap: acceptance matrix + G4 handoff`.

After relevant commits: `make test`, `make test-sanitize`, `make check-classic`;
`/review` on the full diff before pushing.

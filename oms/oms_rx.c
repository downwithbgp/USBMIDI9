/*
 * USBMIDI9 OMS driver shim — receive path.
 *
 * Receive is push-based (v0.1 audit correction): the class driver's
 * read completion invokes oms_rx_event (dispatch table v0x0002
 * setEventCallback) at (secondary) interrupt level right after bytes
 * land in the interface's ring. The event callback drains the ring
 * through the neutral USB-MIDI stream converter and delivers
 * conventional MIDI messages to OMS as OMSPackets via
 * OMSReceivedFromPort, using the ioRefNum OMS passed in
 * omdvSetPortReceiveRefNum (-1 = do not deliver). There is NO periodic
 * poll task.
 *
 * Verified contract (OMS Programming Interface spec, "OMS Drivers"):
 *  - the driver parses received bytes into single-MIDI-message packets
 *    (SysEx split with continuation flags);
 *  - OMSReceivedFromPort may be called at interrupt level; for this call
 *    OMSPacket.len is the number of MIDI data bytes only;
 *  - the source is identified by the ioRefNum from
 *    omdvSetPortReceiveRefNum.
 *
 * Concurrency contract: oms_rx_event runs inside the class driver's own
 * read completion, so the driver fragment is alive for the whole call
 * and (single-core G4) no task-time preemption can interleave a detach
 * between pointer capture and use: g_oms.table is captured once, and
 * the drain is uninterruptible by task-level code. The callback
 * performs no task-time OMS/USB calls and never re-enters the dispatch
 * table except through dequeueBytes (the TX path must keep that
 * property too; TODO(oms-output)).
 *
 * Timestamps: the Spec permits (does not require) stamping via
 * OMSTimerGetOMSClockPosition/OMSTimerGetOMSSMPTETime with the
 * omsPktBeatTStamped/omsPktSMPTETStamped flags. v0.1 delivers
 * untimestamped packets (flags continuation bits only);
 * TODO(oms-timer): optional OMS Time Manager stamping.
 */

#include <MacTypes.h>
#include <OMS.h>
#include <OMSDriver.h>

#include "oms_driver.h"

/* Build an OMSPacket for OMSReceivedFromPort from one stream message.
 * A whole-SysEx message (single packet) is a COMPLETE message and is
 * delivered with omsNoCont, per the MIDI Manager / OMS convention. */
static void oms_rx_build_packet(struct oms_port *port,
                                const unsigned char *data, unsigned len,
                                unsigned sysex, OMSPacket *pkt)
{
    unsigned i;
    unsigned char flags;

    switch (sysex) {
    case UM9_SYSEX_START:
        flags = omsStartCont;
        break;
    case UM9_SYSEX_MID:
        flags = omsMidCont;
        break;
    case UM9_SYSEX_END:
        flags = omsEndCont;
        break;
    default:                        /* NONE: complete message (incl. a
                                     * whole SysEx in one packet) */
        flags = omsNoCont;
        break;
    }
    pkt->flags = flags;
    pkt->len = (unsigned char)len;              /* data bytes only */
    pkt->srcIORefNum = (unsigned short)port->ioRefNum;
    pkt->appConnRefCon = 0u;
    for (i = 0u; i < 4u; i++) {
        pkt->data[i] = (i < len) ? data[i] : 0u;
    }
}

/* Drain one interface's ring until empty. `deliver` selects whether the
 * decoded messages are handed to OMS (1) or dropped (0 — stale data
 * that arrived while MIDI was off). Called from the event callback
 * (interrupt level) and from attach/startMIDI2 (task time). */
void oms_rx_drain(unsigned ifaceIndex, unsigned deliver)
{
    struct oms_iface *iface;
    struct USBMIDI9DispatchTable *table;
    unsigned char buf[kUSBMIDI9OMSDequeueChunk];
    UInt32 n;

    if (ifaceIndex >= kUSBMIDI9OMSMaxInterfaces) {
        return;
    }
    iface = &g_oms.ifaces[ifaceIndex];
    table = g_oms.table;            /* capture once; the driver fragment
                                     * is alive for the whole call */
    if (table == NULL || !iface->valid) {
        return;
    }
    for (;;) {
        unsigned pos;

        n = table->dequeueBytes(ifaceIndex, buf, sizeof(buf));
        if (n == 0u) {
            break;                  /* ring empty */
        }
        g_oms.rxPackets += (unsigned long)(n / 4u);

        for (pos = 0u; pos + 3u < n; pos += 4u) {
            unsigned cable = (unsigned)(buf[pos] >> 4) & 0x0Fu;
            struct oms_port *port;

            if (cable >= kUSBMIDI9OMSCables) {
                g_oms.rxDropped++;          /* impossible; defensive */
                continue;
            }
            port = &iface->ports[cable];
            if (!port->valid) {
                /* No omdvSetPortReceiveRefNum for this cable yet: drain
                 * the ring through the stream anyway so later data stays
                 * aligned, but do not deliver. The Spec: the driver
                 * should initially assume negative refnums for all
                 * possible sources. */
                port->valid = 1u;
                port->ioRefNum = -1;
                um9_rx_init(&port->rx, cable);
                um9_tx_init(&port->tx, cable);
            }
            if (um9_rx_packet(&port->rx, &buf[pos], 4u) != 1) {
                continue;           /* never happens with 4-byte input */
            }
            for (;;) {
                unsigned char msg[3];
                unsigned len;
                unsigned sysex;
                OMSPacket pkt;

                if (um9_rx_message(&port->rx, msg, &len, &sysex) != 1) {
                    break;
                }
                g_oms.rxMessages++;
                if (!deliver || port->ioRefNum < 0) {
                    continue;       /* OMS does not want this port */
                }
                oms_rx_build_packet(port, msg, len, sysex, &pkt);
                OMSReceivedFromPort(&pkt, port->ioRefNum);
            }
        }
    }
}

/* Drain every valid interface until its ring is empty (deliver per the
 * `deliver` flag). Used at attach and omdvStartMIDI2. */
void oms_rx_drain_all(unsigned deliver)
{
    unsigned i;

    if (g_oms.table == NULL) {
        return;
    }
    for (i = 0u; i < kUSBMIDI9OMSMaxInterfaces; i++) {
        if (g_oms.ifaces[i].valid) {
            oms_rx_drain(i, deliver);
        }
    }
}

/* The class driver's event callback (dispatch table v0x0002): runs
 * inside the driver's read completion. Delivers only while MIDI is
 * running; bytes that arrive while MIDI is off stay in the ring and are
 * discarded at the next omdvStartMIDI2. */
void oms_rx_event(UInt32 ifaceIndex, UInt32 refcon)
{
    (void)refcon;

    g_oms.events++;
    oms_rx_drain(ifaceIndex, g_oms.midiStarted);
}

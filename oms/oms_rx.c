/*
 * USBMIDI9 OMS driver shim — receive path.
 *
 * The poll task drains each started interface's ring through the neutral
 * USB-MIDI stream converter and delivers conventional MIDI messages to
 * OMS as OMSPackets via OMSReceivedFromPort, using the ioRefNum OMS
 * passed in omdvSetPortReceiveRefNum (-1 = do not deliver).
 *
 * Verified contract (OMS Programming Interface spec, "OMS Drivers"):
 *  - the driver parses received bytes into single-MIDI-message packets
 *    (SysEx split with continuation flags);
 *  - OMSReceivedFromPort may be called at interrupt level; for this call
 *    OMSPacket.len is the number of MIDI data bytes only;
 *  - the source is identified by the ioRefNum from
 *    omdvSetPortReceiveRefNum.
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
#include <Notifications.h>

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

void oms_rx_drain(unsigned ifaceIndex)
{
    struct oms_iface *iface;
    struct USBMIDI9DispatchTable *table;
    unsigned char buf[kUSBMIDI9OMSDequeueChunk];
    UInt32 n;
    unsigned pos;

    if (ifaceIndex >= kUSBMIDI9OMSMaxInterfaces) {
        return;
    }
    iface = &g_oms.ifaces[ifaceIndex];
    if (!iface->valid || g_oms.table == NULL) {
        return;
    }
    table = g_oms.table;
    n = table->dequeueBytes(ifaceIndex, buf, sizeof(buf));
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
            /* No omdvSetPortReceiveRefNum for this cable yet: drain the
             * ring through the stream anyway so later data stays
             * aligned, but do not deliver. The Spec: the driver should
             * initially assume negative refnums for all possible
             * sources. */
            port->valid = 1u;
            port->ioRefNum = -1;
            um9_rx_init(&port->rx, cable);
            um9_tx_init(&port->tx, cable);
        }
        if (um9_rx_packet(&port->rx, &buf[pos], 4u) != 1) {
            continue;                   /* never happens with 4-byte input */
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
            if (port->ioRefNum < 0) {
                continue;               /* OMS does not want this port */
            }
            oms_rx_build_packet(port, msg, len, sysex, &pkt);
            OMSReceivedFromPort(&pkt, port->ioRefNum);
        }
    }
}

OMSCALLBACK(void) oms_poll_task(UInt32 nMessage, UInt32 nRefCon)
{
    unsigned i;

    (void)nMessage;
    (void)nRefCon;

    /* Re-locate the dispatch table each tick: when the device unplugs
     * the driver fragment unloads and a cached pointer would dangle
     * (the Probe's verified self-healing pattern). */
    if (oms_locate_dispatch() != noErr) {
        /* No USBMIDI9 driver attached right now; still keep polling so
         * a replug is picked up. */
    }
    if (g_oms.midiStarted) {
        for (i = 0u; i < kUSBMIDI9OMSMaxInterfaces; i++) {
            if (g_oms.ifaces[i].valid) {
                oms_rx_drain(i);
            }
        }
    }
    if (g_oms.timerRunning) {
        g_oms.nmRec.eventTime += kUSBMIDI9OMSPollTicks;
        NMInstall(&g_oms.nmRec);
    }
}

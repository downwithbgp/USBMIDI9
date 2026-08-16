/*
 * USBMIDI9 OMS driver shim — send hook.
 *
 * The omdvGetPortSendProc contract (Spec, compat level >= 1): the driver
 * returns an OMSReadHook2 which OMS calls with OMSMIDIPackets; paramD0 is
 * passed as the readHookRefCon and the low word of paramD1 is placed in
 * the packet's appConnRefCon. The proc may be called at interrupt level,
 * so this path only touches the port's carry buffer and counters — no
 * Toolbox calls, no allocation.
 *
 * OMS delivers SysEx as continuation-flagged chunks of up to 4 data
 * bytes (OMSMIDIPacket.data[4]). USB-MIDI 1.0 requires 3-byte CIN 0x4
 * packets, so chunks are re-chunked through a 2-byte carry: middle
 * packets are emitted as they fill 3 bytes, and the end packet (CIN
 * 0x6/0x7/0x8) always carries the trailing bytes, which end in F7.
 *
 * The converted 4-byte Event Packets go to oms_tx_transport_send, the
 * seam to the USB bulk-OUT path. v0.1 has no transport (the dispatch
 * table has no enqueueBytes), so the default seam drops and counts:
 * OUTPUT IS UNVERIFIED until the G4 gate adds enqueueBytes + bulk OUT.
 */

#include <MacTypes.h>
#include <OMS.h>
#include <OMSDriver.h>

#include "oms_driver.h"

/* v0.1 transport: no bulk-OUT path exists yet. */
static void oms_tx_drop(unsigned portCode, const unsigned char pkt[4])
{
    (void)pkt;
    (void)portCode;
    g_oms.txDropped++;
}

/* The transport seam; the G4 gate replaces the default with the real
 * enqueueBytes + bulk-OUT implementation. Test harnesses may override. */
void (*oms_tx_transport)(unsigned portCode, const unsigned char pkt[4])
    = oms_tx_drop;

/* Emit one converted Event Packet through the seam. */
static void oms_tx_emit(unsigned portCode, unsigned cable, unsigned cin,
                        const unsigned char *data, unsigned len)
{
    unsigned char pkt[4];
    unsigned i;

    pkt[0] = (unsigned char)(((cable & 0x0Fu) << 4) | (cin & 0x0Fu));
    for (i = 0u; i < 3u; i++) {
        pkt[1 + i] = (i < len) ? data[i] : 0u;
    }
    g_oms.txConverted++;
    oms_tx_transport(portCode, pkt);
}

/* CIN for a SysEx end packet carrying 1..3 trailing bytes. */
static unsigned oms_tx_end_cin(unsigned len)
{
    if (len <= 1u) {
        return UM9_CIN_SYSEX_END_1;
    }
    if (len == 2u) {
        return UM9_CIN_SYSEX_END_2;
    }
    return UM9_CIN_SYSEX_END_3;
}

/* Handle one SysEx chunk. `mode` follows the OMS continuation STATE
 * (OMS.h: omsNoCont=0, omsStartCont=1, omsEndCont=2, omsMidCont=3 —
 * NOT a bitmask), plus kTxWhole for a complete SysEx delivered in one
 * packet (noCont, F0-led). */
#define kTxStart 1u
#define kTxEnd   2u
#define kTxMid   3u
#define kTxWhole 4u

static void oms_tx_sysex_chunk(struct oms_port *port, unsigned portCode,
                               unsigned cable, unsigned mode,
                               const unsigned char *data, unsigned len)
{
    unsigned char carry[6];
    unsigned carryLen;
    unsigned isStart;
    unsigned isEnd;
    unsigned i;
    unsigned pos;

    isStart = (mode == kTxStart || mode == kTxWhole);
    isEnd = (mode == kTxEnd || mode == kTxWhole);

    if (isStart) {
        if (port->txCarryLen != 0u || len == 0u || data[0] != 0xF0u) {
            /* Carry from a previous run, or a start chunk without F0:
             * malformed; reset and count. */
            g_oms.txMalformed++;
            port->txCarryLen = 0u;
        }
    }
    if (mode == kTxWhole && (len < 2u || data[len - 1u] != 0xF7u)) {
        g_oms.txMalformed++;        /* complete SysEx must end in F7 */
        return;
    }
    if (len == 0u || len > 4u) {
        g_oms.txMalformed++;
        return;
    }
    if (port->txCarryLen + len > 6u) {
        g_oms.txMalformed++;        /* cannot happen; defensive */
        port->txCarryLen = 0u;
        return;
    }

    /* Append the chunk to the carry (a 3-byte start chunk plus up to 4
     * more bytes fits). */
    carryLen = port->txCarryLen;
    for (i = 0u; i < port->txCarryLen; i++) {
        carry[i] = port->txCarry[i];
    }
    for (i = 0u; i < len; i++) {
        carry[carryLen++] = data[i];
    }
    port->txCarryLen = 0u;

    if (isEnd) {
        /* Flush: 3-byte middle packets until 1-3 bytes remain, which
         * form the end packet (ending in F7). */
        pos = 0u;
        while (carryLen - pos > 3u) {
            oms_tx_emit(portCode, cable, UM9_CIN_SYSEX_START,
                        &carry[pos], 3u);
            pos += 3u;
        }
        oms_tx_emit(portCode, cable, oms_tx_end_cin(carryLen - pos),
                    &carry[pos], carryLen - pos);
    } else {
        /* start/mid: emit 3-byte middle packets; keep the remainder. */
        pos = 0u;
        while (carryLen - pos >= 3u) {
            oms_tx_emit(portCode, cable, UM9_CIN_SYSEX_START,
                        &carry[pos], 3u);
            pos += 3u;
        }
        port->txCarryLen = (unsigned char)(carryLen - pos);
        for (i = 0u; i < port->txCarryLen; i++) {
            port->txCarry[i] = carry[pos + i];
        }
    }
}

OMSCALLBACK(void) oms_tx_send(OMSMIDIPacket *pkt, long refCon)
{
    unsigned ifaceNo;
    unsigned cable;
    unsigned portCode;
    struct oms_port *port;
    unsigned cont;

    if (pkt == NULL) {
        g_oms.txMalformed++;
        return;
    }
    ifaceNo = ((unsigned)refCon >> 8) & 0xFFu;
    cable = (unsigned)refCon & 0xFFu;
    if (ifaceNo < 1u || ifaceNo > kUSBMIDI9OMSMaxInterfaces
        || cable >= kUSBMIDI9OMSCables) {
        g_oms.txMalformed++;
        return;
    }
    port = &g_oms.ifaces[ifaceNo - 1u].ports[cable];
    portCode = (ifaceNo << 8) | cable;

    cont = (unsigned)pkt->flags & omsContMask;
    if (cont == omsNoCont) {
        if (pkt->len >= 1u && pkt->data[0] == 0xF0u) {
            /* A COMPLETE SysEx in one packet: the MIDI Manager / OMS
             * convention is that a complete message carries no
             * continuation flags. Validate and flush as a full run. */
            oms_tx_sysex_chunk(port, portCode, cable, kTxWhole,
                               pkt->data, (unsigned)pkt->len);
        } else {
            /* A conventional MIDI message (1-3 bytes): reuse the neutral
             * encoder; a 4-byte non-SysEx packet is malformed. */
            unsigned char out[2][4];
            int n;

            if (pkt->len < 1u || pkt->len > 3u) {
                g_oms.txMalformed++;
                return;
            }
            n = um9_tx_message(&port->tx, cable, pkt->data,
                               (unsigned)pkt->len, out, 2u);
            if (n != 1) {
                g_oms.txMalformed++;
                return;
            }
            oms_tx_emit(portCode, cable,
                        (unsigned)out[0][0] & 0x0Fu, out[0] + 1u,
                        (unsigned)pkt->len);
        }
    } else {
        /* omsStartCont (1), omsEndCont (2), omsMidCont (3): SysEx
         * continuation chunks. The values are states, not bitmasks. */
        oms_tx_sysex_chunk(port, portCode, cable, cont, pkt->data,
                           (unsigned)pkt->len);
    }
}

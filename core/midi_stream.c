/*
 * See midi_stream.h for the module contract.
 */

#include <stddef.h>

#include "midi_stream.h"

void um9_rx_init(struct um9_rx_stream *s, unsigned cable)
{
    unsigned i;

    if (s == NULL) {
        return;
    }
    s->cable = (cable <= UM9_MAX_CABLE) ? cable : 0u;
    s->in_sysex = 0u;
    s->head = 0u;
    s->count = 0u;
    for (i = 0u; i < UM9_RX_QUEUE; i++) {
        s->pending[i].len = 0u;
        s->pending[i].sysex = UM9_SYSEX_NONE;
    }
    s->stats.packets = 0ul;
    s->stats.messages = 0ul;
    s->stats.dropped_misc = 0ul;
    s->stats.dropped_cable = 0ul;
    s->stats.dropped_full = 0ul;
    s->stats.dropped_misroute = 0ul;
    s->stats.malformed_start = 0ul;
    s->stats.sysex_end_no_eox = 0ul;
}

/* Append one received message to the pending queue. Drops (and counts)
 * the newest message when the queue is full. */
static void um9_rx_enqueue(struct um9_rx_stream *s, const unsigned char *data,
                           unsigned len, unsigned sysex)
{
    unsigned tail;
    struct um9_rx_msg *m;

    if (s->count >= UM9_RX_QUEUE) {
        s->stats.dropped_full++;
        return;
    }
    tail = (unsigned)(s->head + s->count) % UM9_RX_QUEUE;
    m = &s->pending[tail];
    m->data[0] = data[0];
    m->data[1] = (len > 1u) ? data[1] : 0u;
    m->data[2] = (len > 2u) ? data[2] : 0u;
    m->len = len;
    m->sysex = sysex;
    s->count++;
}

int um9_rx_packet(struct um9_rx_stream *s, const unsigned char *buf,
                  unsigned len)
{
    um9_packet pkt;
    unsigned sysex;
    unsigned char data[3];

    if (s == NULL || buf == NULL || len != UM9_PACKET_SIZE) {
        return 0;
    }
    if (!um9_packet_decode(buf, len, &pkt)) {
        return 0;
    }
    s->stats.packets++;

    /* A stream is per-cable; a mismatched nibble is client misrouting. */
    if (pkt.cable != s->cable) {
        s->stats.dropped_misroute++;
        return 1;
    }

    switch (pkt.cin) {
    case UM9_CIN_MISC:               /* 0x0: no defined payload */
        s->stats.dropped_misc++;
        return 1;
    case UM9_CIN_CABLE_EVENT:        /* 0x1: cable event, not MIDI */
        s->stats.dropped_cable++;
        return 1;
    case UM9_CIN_SYSEX_START:        /* 0x4: start or continue */
        if (!s->in_sysex) {
            if (pkt.data[0] != 0xF0u) {
                s->stats.malformed_start++;
            }
            s->in_sysex = 1u;
            sysex = UM9_SYSEX_START;
        } else {
            sysex = UM9_SYSEX_MID;
        }
        break;
    case UM9_CIN_SYSEX_END_1:        /* 0x6: end, one byte */
    case UM9_CIN_SYSEX_END_2:        /* 0x7: end, two bytes */
    case UM9_CIN_SYSEX_END_3:        /* 0x8: end, three bytes */
        if (pkt.length > 0u &&
            pkt.data[pkt.length - 1u] != 0xF7u) {
            s->stats.sysex_end_no_eox++;
        }
        /* A whole SysEx run delivered in ONE end packet (not already
         * inside a run, first byte F0) is a COMPLETE message: the MIDI
         * Manager / OMS convention is that a complete message carries
         * no continuation flags (omsNoCont), so tag it NONE. */
        if (!s->in_sysex && pkt.length > 0u && pkt.data[0] == 0xF0u) {
            sysex = UM9_SYSEX_NONE;
        } else {
            sysex = UM9_SYSEX_END;
        }
        s->in_sysex = 0u;
        break;
    default:                         /* 0x2, 0x3, 0x5, 0x9..0xF */
        sysex = UM9_SYSEX_NONE;
        break;
    }

    if (pkt.length == 0u) {
        return 1;                    /* defensive; all CINs here have 1..3 */
    }
    data[0] = pkt.data[0];
    data[1] = pkt.data[1];
    data[2] = pkt.data[2];
    um9_rx_enqueue(s, data, pkt.length, sysex);
    return 1;
}

int um9_rx_message(struct um9_rx_stream *s, unsigned char *out,
                   unsigned *out_len, unsigned *out_sysex)
{
    struct um9_rx_msg *m;

    if (s == NULL || s->count == 0u) {
        return 0;
    }
    m = &s->pending[s->head];
    if (out != NULL) {
        out[0] = m->data[0];
        out[1] = m->data[1];
        out[2] = m->data[2];
    }
    if (out_len != NULL) {
        *out_len = m->len;
    }
    if (out_sysex != NULL) {
        *out_sysex = m->sysex;
    }
    s->head = (unsigned char)((s->head + 1u) % UM9_RX_QUEUE);
    s->count--;
    s->stats.messages++;
    return 1;
}

void um9_tx_init(struct um9_tx_stream *s, unsigned cable)
{
    if (s == NULL) {
        return;
    }
    s->cable = (cable <= UM9_MAX_CABLE) ? cable : 0u;
    s->stats.messages = 0ul;
    s->stats.malformed = 0ul;
}

/* CIN for a SysEx end packet carrying `len` trailing bytes (1..3). */
static unsigned um9_tx_end_cin(unsigned len)
{
    if (len <= 1u) {
        return UM9_CIN_SYSEX_END_1;
    }
    if (len == 2u) {
        return UM9_CIN_SYSEX_END_2;
    }
    return UM9_CIN_SYSEX_END_3;
}

/* Build one 4-byte Event Packet. */
static void um9_tx_packet_build(unsigned char out[UM9_PACKET_SIZE],
                                unsigned cable, unsigned cin,
                                const unsigned char *data, unsigned len)
{
    unsigned i;

    out[0] = (unsigned char)(((cable & 0x0Fu) << 4) | (cin & 0x0Fu));
    for (i = 0u; i < 3u; i++) {
        out[1 + i] = (i < len) ? data[i] : 0u;
    }
}

/* Number of Event Packets needed for a SysEx run of `len` bytes
 * (len >= 4; the first packet takes 3 bytes including F0, the trailing
 * 1-3 bytes ending in F7 form the end packet). */
static unsigned um9_tx_sysex_packet_count(unsigned len)
{
    unsigned rest;                   /* bytes after the start packet */

    rest = len - 3u;
    return 1u + (rest + 2u) / 3u;    /* 1 end packet + middle packets */
}

int um9_tx_message(struct um9_tx_stream *s, unsigned cable,
                   const unsigned char *msg, unsigned len,
                   unsigned char out[][UM9_PACKET_SIZE], unsigned maxPackets)
{
    unsigned pos;
    unsigned needed;
    um9_packet pkt;

    if (s == NULL || msg == NULL || out == NULL) {
        if (s != NULL) {
            s->stats.malformed++;    /* NULL msg/out is caller error */
        }
        return 0;
    }
    if (cable > UM9_MAX_CABLE || len < 1u || msg[0] < 0x80u) {
        s->stats.malformed++;
        return 0;
    }

    if (msg[0] == 0xF0u) {           /* SysEx run: F0 .. F7 */
        if (len < 2u || msg[len - 1u] != 0xF7u) {
            s->stats.malformed++;
            return 0;
        }
        if (len <= 3u) {
            /* A short run fits one end packet (CIN 0x6/0x7/0x8). */
            if (maxPackets < 1u) {
                s->stats.malformed++;
                return 0;
            }
            um9_tx_packet_build(out[0], cable, um9_tx_end_cin(len), msg, len);
            s->stats.messages++;
            return 1;
        }
        needed = um9_tx_sysex_packet_count(len);
        if (needed > maxPackets) {
            s->stats.malformed++;
            return 0;
        }
        /* CIN 0x4 packets always carry three data bytes (USB-MIDI 1.0);
         * the trailing 1-3 bytes (ending in F7) form the end packet. */
        um9_tx_packet_build(out[0], cable, UM9_CIN_SYSEX_START, msg, 3u);
        pos = 3u;
        while (len - pos > 3u) {
            um9_tx_packet_build(out[1u + (pos - 3u) / 3u], cable,
                                UM9_CIN_SYSEX_START, msg + pos, 3u);
            pos += 3u;
        }
        um9_tx_packet_build(out[needed - 1u], cable,
                            um9_tx_end_cin(len - pos), msg + pos, len - pos);
        s->stats.messages++;
        return (int)needed;
    }

    if (msg[0] == 0xF7u) {           /* lone EOX: single end packet */
        if (len != 1u) {
            s->stats.malformed++;
            return 0;
        }
        if (maxPackets < 1u) {
            s->stats.malformed++;
            return 0;
        }
        um9_tx_packet_build(out[0], cable, UM9_CIN_SYSEX_END_1, msg, 1u);
        s->stats.messages++;
        return 1;
    }

    /* Channel voice and fixed-length system messages. */
    if (!um9_packet_encode(cable, msg, len, &pkt)) {
        s->stats.malformed++;
        return 0;
    }
    if (maxPackets < 1u) {
        s->stats.malformed++;
        return 0;
    }
    um9_tx_packet_build(out[0], pkt.cable, pkt.cin, pkt.data, pkt.length);
    s->stats.messages++;
    return 1;
}

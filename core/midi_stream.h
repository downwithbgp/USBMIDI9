/*
 * USBMIDI9 portable core: USB-MIDI packet stream <-> MIDI byte stream.
 *
 * The raw USB-MIDI transport delivers 4-byte Event Packets (cable nibble +
 * CIN + up to three MIDI data bytes). MIDI systems such as OMS consume
 * conventional MIDI messages instead: single channel/system messages, and
 * SysEx runs that must be cut at message boundaries with continuation
 * markers. This module adapts between the two views, preserving the cable
 * number and keeping every byte.
 *
 *   um9_rx_stream: one per cable. Feed 4-byte Event Packets; take
 *   conventional MIDI messages (1-3 bytes) and SysEx chunks (1-3 bytes,
 *   tagged start/mid/end) in arrival order. Realtime messages interleaved
 *   inside a SysEx run are delivered as separate messages without breaking
 *   the run.
 *
 *   um9_tx_stream: encode one conventional MIDI message into 4-byte Event
 *   Packets. Channel voice and fixed-length system common messages become
 *   one packet; a SysEx message (F0 ... F7) becomes a CIN 0x4 start packet,
 *   zero or more CIN 0x4 middle packets, and one CIN 0x6/0x7/0x8 end packet
 *   carrying the trailing 1-3 bytes (ending in F7). Per USB-MIDI 1.0,
 *   CIN 0x4 packets always carry three data bytes; runs of 2-3 bytes are a
 *   single end packet.
 *
 * Policy notes (USB-MIDI 1.0, Table 4-1):
 *   - CIN 0x0 (miscellaneous) and CIN 0x1 (cable events) carry no MIDI
 *     payload; they are dropped and counted.
 *   - A SysEx start packet whose first byte is not 0xF0 is malformed; the
 *     bytes still pass through (lenient) and the event is counted.
 *   - A SysEx end chunk whose last byte is not 0xF7 is a device quirk; the
 *     bytes pass through and the event is counted. No F7 is synthesized.
 *   - A packet whose cable nibble does not match the stream's cable is
 *     dropped and counted (client misrouting is a bug).
 *   - A packet fed while the bounded pending queue is full is dropped and
 *     counted (drop-new).
 *
 * Portable C (C89/C90), endian-neutral, no dependency on Classic Mac OS,
 * OMS, or FreeMIDI.
 */

#ifndef USBMIDI9_CORE_MIDI_STREAM_H
#define USBMIDI9_CORE_MIDI_STREAM_H

#include "packets.h"

/* Pending-message queue depth (bounded; overflow is counted, drop-new). */
#define UM9_RX_QUEUE 16u

/* SysEx position of a received message. UM9_SYSEX_NONE for all
 * non-SysEx messages AND for a whole SysEx run delivered in one end
 * packet (2-3 bytes starting with F0): a complete message carries no
 * continuation flags (omsNoCont), per the MIDI Manager / OMS
 * convention. */
#define UM9_SYSEX_NONE      0u
#define UM9_SYSEX_START     1u
#define UM9_SYSEX_MID       2u
#define UM9_SYSEX_END       3u

/* Receive-side counters (monotonic; never reset by the stream). */
struct um9_rx_stats {
    unsigned long packets;          /* 4-byte packets fed */
    unsigned long messages;         /* messages taken (incl. SysEx chunks) */
    unsigned long dropped_misc;     /* CIN 0x0 packets */
    unsigned long dropped_cable;    /* CIN 0x1 packets */
    unsigned long dropped_full;     /* pending queue full */
    unsigned long dropped_misroute; /* cable nibble mismatch */
    unsigned long malformed_start;  /* SysEx start without leading F0 */
    unsigned long sysex_end_no_eox; /* end chunk without trailing F7 */
};

/* One pending received message: up to three MIDI data bytes and the
 * SysEx position tag. */
struct um9_rx_msg {
    unsigned char data[3];
    unsigned len;                   /* 1..3 */
    unsigned sysex;                 /* UM9_SYSEX_* */
};

/* Per-cable receive stream. Opaque outside this module. */
struct um9_rx_stream {
    unsigned cable;                 /* 0..15, set by um9_rx_init */
    unsigned char in_sysex;         /* 1 while inside a SysEx run */
    unsigned char head;             /* pending queue head */
    unsigned char count;            /* pending queue length */
    struct um9_rx_msg pending[UM9_RX_QUEUE];
    struct um9_rx_stats stats;
};

/* Initialize a receive stream for the given cable (0..15). */
void um9_rx_init(struct um9_rx_stream *s, unsigned cable);

/* Feed one raw 4-byte USB-MIDI Event Packet. Returns 1 on success,
 * 0 on NULL input or a length other than UM9_PACKET_SIZE. Malformed
 * payloads are counted and dropped per the policy above; the stream
 * state is never corrupted. */
int um9_rx_packet(struct um9_rx_stream *s, const unsigned char *buf,
                  unsigned len);

/* Take the next received message, in arrival order. Returns 1 and fills
 * out (data bytes), *out_len (1..3) and *out_sysex (UM9_SYSEX_*) when a
 * message is pending; returns 0 when the queue is empty (out may be
 * NULL). Messages are never coalesced: one Event Packet produces at
 * most one message. */
int um9_rx_message(struct um9_rx_stream *s, unsigned char *out,
                   unsigned *out_len, unsigned *out_sysex);

/* Transmit-side counters. */
struct um9_tx_stats {
    unsigned long messages;         /* messages accepted */
    unsigned long malformed;        /* messages rejected */
};

/* Per-cable transmit stream. The encoder itself is stateless; the struct
 * carries the cable and counters. */
struct um9_tx_stream {
    unsigned cable;                 /* 0..15, set by um9_tx_init */
    struct um9_tx_stats stats;
};

/* Initialize a transmit stream for the given cable (0..15). */
void um9_tx_init(struct um9_tx_stream *s, unsigned cable);

/* Encode one conventional MIDI message (status byte first) into 4-byte
 * Event Packets.
 *
 * Accepted: channel voice messages (0x80..0xEF, correct length for the
 * status), fixed-length system common (F1/F2/F3), one-byte system messages
 * (F6/F8/F9/FA/FB/FC/FD/FE/FF), a lone F7 (encoded as a single CIN 0x6
 * end packet), and SysEx runs F0 ... F7 (len >= 2, last byte F7).
 *
 * Returns the number of packets written (1 for non-SysEx messages; 1..N
 * for SysEx), or 0 on any invalid input: NULL msg, cable > 15, len 0,
 * msg[0] < 0x80, an unknown status byte (F4/F5), a 1-byte channel status
 * (running status), a SysEx run not ending in F7, or a SysEx run that
 * would need more than maxPackets packets. On 0 nothing is written and
 * stats.malformed is incremented.
 */
int um9_tx_message(struct um9_tx_stream *s, unsigned cable,
                   const unsigned char *msg, unsigned len,
                   unsigned char out[][UM9_PACKET_SIZE], unsigned maxPackets);

#endif /* USBMIDI9_CORE_MIDI_STREAM_H */

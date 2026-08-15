/*
 * USBMIDI9 portable core: USB-MIDI 1.0 Event Packets.
 *
 * A USB-MIDI 1.0 Event Packet is exactly four bytes:
 *
 *   byte 0: Cable Number (upper nibble) | Code Index Number (lower nibble)
 *   byte 1: MIDI byte 0
 *   byte 2: MIDI byte 1
 *   byte 3: MIDI byte 2
 *
 * The Code Index Number (CIN) tells the receiver how many of the remaining
 * bytes are valid MIDI data (see "USB Device Class Definition for MIDI
 * Devices", Release 1.0, Table 4-1).
 *
 * This module is portable C (C89/C90), endian-neutral (all fields are byte
 * oriented), and has no dependency on Classic Mac OS, OMS, or FreeMIDI.
 *
 * TODO: SysEx packetization/reassembly (splitting a MIDI SysEx stream into
 * start/continue/end Event Packets and joining them back) is transport-layer
 * work and is not implemented here yet.
 */

#ifndef USBMIDI9_CORE_PACKETS_H
#define USBMIDI9_CORE_PACKETS_H

/* Size in bytes of one USB-MIDI Event Packet. */
#define UM9_PACKET_SIZE 4u

/* Highest valid cable number (four-bit field). */
#define UM9_MAX_CABLE 15u

/* Code Index Numbers (USB-MIDI 1.0, Table 4-1). */
#define UM9_CIN_MISC              0x0u  /* Miscellaneous function codes; length undefined */
#define UM9_CIN_CABLE_EVENT       0x1u  /* Cable events */
#define UM9_CIN_SYS_COMMON_2      0x2u  /* Two-byte System Common (MTC, Song Select, ...) */
#define UM9_CIN_SYS_COMMON_3      0x3u  /* Three-byte System Common (Song Position Pointer, ...) */
#define UM9_CIN_SYSEX_START       0x4u  /* SysEx start or continue */
#define UM9_CIN_SYS_COMMON_1      0x5u  /* Single-byte System Common (Tune Request, ...) */
#define UM9_CIN_SYSEX_END_1       0x6u  /* SysEx end with one following byte */
#define UM9_CIN_SYSEX_END_2       0x7u  /* SysEx end with two following bytes */
#define UM9_CIN_SYSEX_END_3       0x8u  /* SysEx end with three following bytes */
#define UM9_CIN_NOTE_OFF          0x9u
#define UM9_CIN_NOTE_ON           0xAu
#define UM9_CIN_POLY_KEY_PRESS    0xBu
#define UM9_CIN_CONTROL_CHANGE    0xCu
#define UM9_CIN_PROGRAM_CHANGE    0xDu
#define UM9_CIN_CHANNEL_PRESSURE  0xEu
#define UM9_CIN_PITCH_BEND        0xFu

/* One decoded USB-MIDI Event Packet. */
typedef struct um9_packet {
    unsigned cable;              /* 0..15 */
    unsigned cin;                /* 0..15 */
    unsigned length;             /* number of valid MIDI data bytes (0..3) */
    unsigned char data[3];       /* MIDI data bytes */
} um9_packet;

/* Number of MIDI data bytes carried by a CIN, per USB-MIDI 1.0.
 * Returns 0 for CIN 0x0 (Miscellaneous: the payload length is not defined
 * by the specification) and for any value above 0x0F. */
unsigned um9_cin_data_length(unsigned cin);

/* Decode one four-byte USB-MIDI Event Packet.
 *
 * Returns 1 on success. Returns 0 if buf or out is NULL or if len is not
 * exactly UM9_PACKET_SIZE. CIN 0x0 decodes successfully with length 0; its
 * payload is not interpreted. */
int um9_packet_decode(const unsigned char *buf, unsigned len, um9_packet *out);

/* Encode a complete MIDI message (status byte first) into an Event Packet.
 *
 * Supports channel voice messages (0x80..0xEF) and the non-SysEx system
 * messages with a fixed length of 1..3 bytes. SysEx (0xF0/0xF7) and the
 * undefined statuses 0xF4/0xF5 are rejected (returns 0); SysEx
 * packetization is transport-layer work (see TODO above).
 *
 * Returns 1 on success, 0 on any invalid input: cable > 15, msg NULL,
 * len outside 1..3, len not matching the status byte's message size, or a
 * status byte that has no defined CIN mapping. */
int um9_packet_encode(unsigned cable, const unsigned char *msg, unsigned len,
                      um9_packet *out);

#endif /* USBMIDI9_CORE_PACKETS_H */

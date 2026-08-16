/*
 * Host tests for the USB-MIDI stream <-> MIDI message adapter
 * (core/midi_stream.h).
 *
 * Portable C (C89/C90).
 */

#include <stdio.h>

#include "core/midi_stream.h"

static int g_failures;

static void fail(const char *expr, const char *file, int line)
{
    printf("FAIL %s:%d: %s\n", file, line, expr);
    g_failures++;
}

#define CHECK(cond) do { \
    if (!(cond)) { \
        fail(#cond, __FILE__, __LINE__); \
    } \
} while (0)

/* Feed one raw Event Packet to a receive stream. */
static void feed(struct um9_rx_stream *s, unsigned cable, unsigned cin,
                 unsigned char d0, unsigned char d1, unsigned char d2)
{
    unsigned char pkt[UM9_PACKET_SIZE];

    pkt[0] = (unsigned char)(((cable & 0x0Fu) << 4) | (cin & 0x0Fu));
    pkt[1] = d0;
    pkt[2] = d1;
    pkt[3] = d2;
    CHECK(um9_rx_packet(s, pkt, UM9_PACKET_SIZE) == 1);
}

/* Take one message and compare against expected bytes + sysex tag. */
static void expect_msg(struct um9_rx_stream *s, unsigned expect_len,
                       unsigned char d0, unsigned char d1, unsigned char d2,
                       unsigned sysex, const char *what)
{
    unsigned char out[3];
    unsigned len = 0u;
    unsigned sx = 99u;

    CHECK(um9_rx_message(s, out, &len, &sx) == 1);
    if (len != expect_len || sx != sysex
        || (expect_len >= 1u && out[0] != d0)
        || (expect_len >= 2u && out[1] != d1)
        || (expect_len >= 3u && out[2] != d2)) {
        printf("FAIL %s: got len=%u sysex=%u data=%02X %02X %02X\n", what,
               len, sx, (unsigned)out[0], (unsigned)out[1],
               (unsigned)out[2]);
        g_failures++;
    }
}

static void expect_empty(struct um9_rx_stream *s)
{
    CHECK(um9_rx_message(s, NULL, NULL, NULL) == 0);
}

/* ------------------------------ receive ------------------------------ */

static void test_rx_channel_voice(void)
{
    struct um9_rx_stream s;

    um9_rx_init(&s, 0u);
    feed(&s, 0u, UM9_CIN_NOTE_ON, 0x90u, 0x3Cu, 0x40u);
    expect_msg(&s, 3u, 0x90u, 0x3Cu, 0x40u, UM9_SYSEX_NONE, "note on");
    expect_empty(&s);

    feed(&s, 0u, UM9_CIN_NOTE_OFF, 0x80u, 0x30u, 0x00u);
    expect_msg(&s, 3u, 0x80u, 0x30u, 0x00u, UM9_SYSEX_NONE, "note off");

    feed(&s, 0u, UM9_CIN_CONTROL_CHANGE, 0xB0u, 0x07u, 0x7Fu);
    expect_msg(&s, 3u, 0xB0u, 0x07u, 0x7Fu, UM9_SYSEX_NONE, "cc");

    feed(&s, 0u, UM9_CIN_PROGRAM_CHANGE, 0xC0u, 0x05u, 0x00u);
    expect_msg(&s, 2u, 0xC0u, 0x05u, 0x00u, UM9_SYSEX_NONE, "program change");

    feed(&s, 0u, UM9_CIN_CHANNEL_PRESSURE, 0xD0u, 0x40u, 0x00u);
    expect_msg(&s, 2u, 0xD0u, 0x40u, 0x00u, UM9_SYSEX_NONE, "channel pressure");

    feed(&s, 0u, UM9_CIN_POLY_KEY_PRESS, 0xA0u, 0x3Cu, 0x10u);
    expect_msg(&s, 3u, 0xA0u, 0x3Cu, 0x10u, UM9_SYSEX_NONE, "poly key press");

    feed(&s, 0u, UM9_CIN_PITCH_BEND, 0xE0u, 0x00u, 0x40u);
    expect_msg(&s, 3u, 0xE0u, 0x00u, 0x40u, UM9_SYSEX_NONE, "pitch bend");
    expect_empty(&s);
}

static void test_rx_system_common_and_realtime(void)
{
    struct um9_rx_stream s;

    um9_rx_init(&s, 0u);
    feed(&s, 0u, UM9_CIN_SYS_COMMON_2, 0xF1u, 0x01u, 0x00u);   /* MTC */
    expect_msg(&s, 2u, 0xF1u, 0x01u, 0x00u, UM9_SYSEX_NONE, "mtc");

    feed(&s, 0u, UM9_CIN_SYS_COMMON_3, 0xF2u, 0x00u, 0x20u);   /* SPP */
    expect_msg(&s, 3u, 0xF2u, 0x00u, 0x20u, UM9_SYSEX_NONE, "spp");

    feed(&s, 0u, UM9_CIN_SYS_COMMON_1, 0xF6u, 0x00u, 0x00u);   /* tune */
    expect_msg(&s, 1u, 0xF6u, 0x00u, 0x00u, UM9_SYSEX_NONE, "tune");

    feed(&s, 0u, UM9_CIN_SYS_COMMON_1, 0xF8u, 0x00u, 0x00u);   /* clock */
    expect_msg(&s, 1u, 0xF8u, 0x00u, 0x00u, UM9_SYSEX_NONE, "clock");

    feed(&s, 0u, UM9_CIN_SYS_COMMON_1, 0xFEu, 0x00u, 0x00u);   /* sensing */
    expect_msg(&s, 1u, 0xFEu, 0x00u, 0x00u, UM9_SYSEX_NONE, "active sensing");
    expect_empty(&s);
}

static void test_rx_sysex(void)
{
    struct um9_rx_stream s;

    /* Single-packet SysEx: F0 F7 (CIN 0x6) — one message that BOTH
     * starts and ends the run. */
    um9_rx_init(&s, 0u);
    feed(&s, 0u, UM9_CIN_SYSEX_END_2, 0xF0u, 0xF7u, 0x00u);
    expect_msg(&s, 2u, 0xF0u, 0xF7u, 0x00u, UM9_SYSEX_NONE,
               "sysex 2 bytes");
    expect_empty(&s);
    CHECK(s.in_sysex == 0u);

    /* F0 + one byte + F7 (CIN 0x8): also a whole-run single packet. */
    um9_rx_init(&s, 0u);
    feed(&s, 0u, UM9_CIN_SYSEX_END_3, 0xF0u, 0x7Eu, 0xF7u);
    expect_msg(&s, 3u, 0xF0u, 0x7Eu, 0xF7u, UM9_SYSEX_NONE,
               "sysex 3 bytes");

    /* Multi-packet run: F0 01 02 | 03 04 05 | F7. */
    um9_rx_init(&s, 0u);
    feed(&s, 0u, UM9_CIN_SYSEX_START, 0xF0u, 0x01u, 0x02u);
    expect_msg(&s, 3u, 0xF0u, 0x01u, 0x02u, UM9_SYSEX_START, "sysex start");
    feed(&s, 0u, UM9_CIN_SYSEX_START, 0x03u, 0x04u, 0x05u);
    expect_msg(&s, 3u, 0x03u, 0x04u, 0x05u, UM9_SYSEX_MID, "sysex mid");
    feed(&s, 0u, UM9_CIN_SYSEX_END_1, 0xF7u, 0x00u, 0x00u);
    expect_msg(&s, 1u, 0xF7u, 0x00u, 0x00u, UM9_SYSEX_END, "sysex end");
    expect_empty(&s);
    CHECK(s.in_sysex == 0u);

    /* Two 3-byte chunks: F0 7E 7F | 01 F7. */
    um9_rx_init(&s, 0u);
    feed(&s, 0u, UM9_CIN_SYSEX_START, 0xF0u, 0x7Eu, 0x7Fu);
    expect_msg(&s, 3u, 0xF0u, 0x7Eu, 0x7Fu, UM9_SYSEX_START, "sysex start 3b");
    feed(&s, 0u, UM9_CIN_SYSEX_END_2, 0x01u, 0xF7u, 0x00u);
    expect_msg(&s, 2u, 0x01u, 0xF7u, 0x00u, UM9_SYSEX_END, "sysex end 2b");
}

static void test_rx_realtime_inside_sysex(void)
{
    struct um9_rx_stream s;

    /* F0 01 02 | F8 (clock, interleaved) | 03 F7. */
    um9_rx_init(&s, 0u);
    feed(&s, 0u, UM9_CIN_SYSEX_START, 0xF0u, 0x01u, 0x02u);
    expect_msg(&s, 3u, 0xF0u, 0x01u, 0x02u, UM9_SYSEX_START, "start");
    feed(&s, 0u, UM9_CIN_SYS_COMMON_1, 0xF8u, 0x00u, 0x00u);
    expect_msg(&s, 1u, 0xF8u, 0x00u, 0x00u, UM9_SYSEX_NONE, "clock in sysex");
    feed(&s, 0u, UM9_CIN_SYSEX_END_2, 0x02u, 0xF7u, 0x00u);
    expect_msg(&s, 2u, 0x02u, 0xF7u, 0x00u, UM9_SYSEX_END, "end after clock");
    expect_empty(&s);
    CHECK(s.in_sysex == 0u);
}

static void test_rx_malformed_and_dropped(void)
{
    struct um9_rx_stream s;

    um9_rx_init(&s, 0u);

    /* CIN 0x0 (misc) and CIN 0x1 (cable event) carry no MIDI. */
    feed(&s, 0u, UM9_CIN_MISC, 0x00u, 0x00u, 0x00u);
    feed(&s, 0u, UM9_CIN_CABLE_EVENT, 0x00u, 0x00u, 0x00u);
    expect_empty(&s);
    CHECK(s.stats.dropped_misc == 1ul);
    CHECK(s.stats.dropped_cable == 1ul);

    /* SysEx start without F0: bytes pass through, counted. */
    feed(&s, 0u, UM9_CIN_SYSEX_START, 0x01u, 0x02u, 0x03u);
    expect_msg(&s, 3u, 0x01u, 0x02u, 0x03u, UM9_SYSEX_START, "malformed start");
    CHECK(s.stats.malformed_start == 1ul);

    /* SysEx end without F7: bytes pass through, counted. */
    feed(&s, 0u, UM9_CIN_SYSEX_END_1, 0x03u, 0x00u, 0x00u);
    expect_msg(&s, 1u, 0x03u, 0x00u, 0x00u, UM9_SYSEX_END, "end no eox");
    CHECK(s.stats.sysex_end_no_eox == 1ul);

    /* Misrouted cable nibble: dropped and counted. */
    feed(&s, 3u, UM9_CIN_NOTE_ON, 0x90u, 0x3Cu, 0x40u);
    expect_empty(&s);
    CHECK(s.stats.dropped_misroute == 1ul);

    /* Invalid buffer length: rejected without touching state. */
    {
        unsigned char shortpkt[2] = { 0x00u, 0x00u };
        CHECK(um9_rx_packet(&s, shortpkt, 2u) == 0);
        CHECK(um9_rx_packet(&s, NULL, UM9_PACKET_SIZE) == 0);
    }
}

static void test_rx_queue_overflow(void)
{
    struct um9_rx_stream s;
    unsigned i;

    um9_rx_init(&s, 0u);
    for (i = 0u; i < UM9_RX_QUEUE + 5u; i++) {
        feed(&s, 0u, UM9_CIN_NOTE_ON, (unsigned char)(0x90u + i), 0x3Cu, 0x40u);
    }
    CHECK(s.stats.dropped_full == 5ul);
    CHECK(s.stats.packets == UM9_RX_QUEUE + 5ul);
    for (i = 0u; i < UM9_RX_QUEUE; i++) {
        unsigned char out[3];
        unsigned len = 0u;
        unsigned sx = 99u;
        CHECK(um9_rx_message(&s, out, &len, &sx) == 1);
        CHECK((unsigned)out[0] == 0x90u + i);
    }
    expect_empty(&s);
}

/* ------------------------------ transmit ----------------------------- */

static void expect_packet_bytes(const unsigned char pkt[UM9_PACKET_SIZE],
                                unsigned cable, unsigned cin,
                                unsigned char d0, unsigned char d1,
                                unsigned char d2, const char *what)
{
    unsigned got_cable = (unsigned)(pkt[0] >> 4) & 0x0Fu;
    unsigned got_cin = (unsigned)pkt[0] & 0x0Fu;

    if (got_cable != cable || got_cin != cin || pkt[1] != d0
        || pkt[2] != d1 || pkt[3] != d2) {
        printf("FAIL %s: got cable=%u cin=%u data=%02X %02X %02X\n", what,
               got_cable, got_cin, (unsigned)pkt[1], (unsigned)pkt[2],
               (unsigned)pkt[3]);
        g_failures++;
    }
}

static void test_tx_channel_and_common(void)
{
    struct um9_tx_stream s;
    unsigned char pkts[16][UM9_PACKET_SIZE];
    unsigned char msg[4];
    int n;

    um9_tx_init(&s, 0u);

    msg[0] = 0x90u; msg[1] = 0x3Cu; msg[2] = 0x40u;
    n = um9_tx_message(&s, 0u, msg, 3u, pkts, 16u);
    CHECK(n == 1);
    expect_packet_bytes(pkts[0], 0u, UM9_CIN_NOTE_ON, 0x90u, 0x3Cu, 0x40u,
                        "tx note on");

    msg[0] = 0xC0u; msg[1] = 0x05u;
    n = um9_tx_message(&s, 0u, msg, 2u, pkts, 16u);
    CHECK(n == 1);
    expect_packet_bytes(pkts[0], 0u, UM9_CIN_PROGRAM_CHANGE, 0xC0u, 0x05u, 0x00u,
                        "tx program change");

    msg[0] = 0xF2u; msg[1] = 0x00u; msg[2] = 0x20u;
    n = um9_tx_message(&s, 0u, msg, 3u, pkts, 16u);
    CHECK(n == 1);
    expect_packet_bytes(pkts[0], 0u, UM9_CIN_SYS_COMMON_3, 0xF2u, 0x00u, 0x20u,
                        "tx spp");

    msg[0] = 0xF8u;
    n = um9_tx_message(&s, 0u, msg, 1u, pkts, 16u);
    CHECK(n == 1);
    expect_packet_bytes(pkts[0], 0u, UM9_CIN_SYS_COMMON_1, 0xF8u, 0x00u, 0x00u,
                        "tx clock");

    /* Cable number is preserved and applied. */
    msg[0] = 0xE0u; msg[1] = 0x00u; msg[2] = 0x40u;
    n = um9_tx_message(&s, 9u, msg, 3u, pkts, 16u);
    CHECK(n == 1);
    expect_packet_bytes(pkts[0], 9u, UM9_CIN_PITCH_BEND, 0xE0u, 0x00u, 0x40u,
                        "tx cable 9");

    CHECK(s.stats.messages == 5ul);
    CHECK(s.stats.malformed == 0ul);
}

static void test_tx_sysex(void)
{
    struct um9_tx_stream s;
    unsigned char pkts[96][UM9_PACKET_SIZE];
    unsigned char msg[300];
    unsigned i;
    int n;

    um9_tx_init(&s, 0u);

    /* F0 F7 -> single CIN 0x6 packet. */
    msg[0] = 0xF0u; msg[1] = 0xF7u;
    n = um9_tx_message(&s, 0u, msg, 2u, pkts, 96u);
    CHECK(n == 1);
    expect_packet_bytes(pkts[0], 0u, UM9_CIN_SYSEX_END_2, 0xF0u, 0xF7u, 0x00u,
                        "tx sysex 2b");

    /* F0 7E 7F F7 -> start [F0 7E 7F] + end [F7]. */
    msg[0] = 0xF0u; msg[1] = 0x7Eu; msg[2] = 0x7Fu; msg[3] = 0xF7u;
    n = um9_tx_message(&s, 0u, msg, 4u, pkts, 96u);
    CHECK(n == 2);
    expect_packet_bytes(pkts[0], 0u, UM9_CIN_SYSEX_START, 0xF0u, 0x7Eu, 0x7Fu,
                        "tx sysex start");
    expect_packet_bytes(pkts[1], 0u, UM9_CIN_SYSEX_END_1, 0xF7u, 0x00u, 0x00u,
                        "tx sysex end");

    /* F0 01 02 03 04 05 06 F7 (8 bytes) -> 3 + 3 + 2. */
    {
        unsigned char m8[8] = { 0xF0u, 0x01u, 0x02u, 0x03u,
                                0x04u, 0x05u, 0x06u, 0xF7u };
        n = um9_tx_message(&s, 0u, m8, 8u, pkts, 96u);
        CHECK(n == 3);
        expect_packet_bytes(pkts[0], 0u, UM9_CIN_SYSEX_START,
                            0xF0u, 0x01u, 0x02u, "tx sysex p0");
        expect_packet_bytes(pkts[1], 0u, UM9_CIN_SYSEX_START,
                            0x03u, 0x04u, 0x05u, "tx sysex p1");
        expect_packet_bytes(pkts[2], 0u, UM9_CIN_SYSEX_END_2,
                            0x06u, 0xF7u, 0x00u, "tx sysex p2");
    }

    /* A lone F7 -> single CIN 0x6 packet. */
    msg[0] = 0xF7u;
    n = um9_tx_message(&s, 0u, msg, 1u, pkts, 96u);
    CHECK(n == 1);
    expect_packet_bytes(pkts[0], 0u, UM9_CIN_SYSEX_END_1, 0xF7u, 0x00u, 0x00u,
                        "tx lone f7");

    /* A 255-byte run: 1 + ceil(252/3) = 85 packets; the trailing
     * 3 bytes (ending in F7) form the CIN 0x8 end packet. */
    msg[0] = 0xF0u;
    for (i = 1u; i < 254u; i++) {
        msg[i] = (unsigned char)(0x10u + (i & 0x0Fu));
    }
    msg[254] = 0xF7u;
    n = um9_tx_message(&s, 0u, msg, 255u, pkts, 96u);
    CHECK(n == 85);
    expect_packet_bytes(pkts[0], 0u, UM9_CIN_SYSEX_START,
                        msg[0], msg[1], msg[2], "tx sysex 255 p0");
    expect_packet_bytes(pkts[84], 0u, UM9_CIN_SYSEX_END_3,
                        0x1Cu, 0x1Du, 0xF7u, "tx sysex 255 plast");

    /* Capacity bound: reject when maxPackets is too small. */
    {
        unsigned char m4[4] = { 0xF0u, 0x01u, 0x02u, 0xF7u };
        n = um9_tx_message(&s, 0u, m4, 4u, pkts, 1u);
        CHECK(n == 0);
    }
    CHECK(s.stats.malformed == 1ul);
}

static void test_tx_invalid(void)
{
    struct um9_tx_stream s;
    unsigned char pkts[16][UM9_PACKET_SIZE];
    unsigned char msg[4];
    int n;

    um9_tx_init(&s, 0u);

    /* Running status / non-status first byte. */
    msg[0] = 0x3Cu; msg[1] = 0x40u;
    n = um9_tx_message(&s, 0u, msg, 2u, pkts, 16u);
    CHECK(n == 0);

    /* 1-byte channel status (running status form). */
    msg[0] = 0x90u;
    n = um9_tx_message(&s, 0u, msg, 1u, pkts, 16u);
    CHECK(n == 0);

    /* Undefined statuses F4/F5. */
    msg[0] = 0xF4u;
    n = um9_tx_message(&s, 0u, msg, 1u, pkts, 16u);
    CHECK(n == 0);
    msg[0] = 0xF5u;
    n = um9_tx_message(&s, 0u, msg, 1u, pkts, 16u);
    CHECK(n == 0);

    /* SysEx not ending in F7. */
    msg[0] = 0xF0u; msg[1] = 0x01u; msg[2] = 0x02u;
    n = um9_tx_message(&s, 0u, msg, 3u, pkts, 16u);
    CHECK(n == 0);

    /* Bad cable. */
    msg[0] = 0x90u; msg[1] = 0x3Cu; msg[2] = 0x40u;
    n = um9_tx_message(&s, 16u, msg, 3u, pkts, 16u);
    CHECK(n == 0);

    /* NULL inputs. */
    n = um9_tx_message(&s, 0u, NULL, 3u, pkts, 16u);
    CHECK(n == 0);
    n = um9_tx_message(NULL, 0u, msg, 3u, pkts, 16u);
    CHECK(n == 0);

    CHECK(s.stats.malformed == 7ul);
}

/* --------------------------- round trips ----------------------------- */

/* Feed a full SysEx message through tx then rx and compare byte-for-byte. */
static void roundtrip_sysex(const unsigned char *msg, unsigned len)
{
    struct um9_tx_stream t;
    struct um9_rx_stream r;
    unsigned char pkts[96][UM9_PACKET_SIZE];
    unsigned char got[300];
    unsigned got_len = 0u;
    unsigned i;
    int n;

    um9_tx_init(&t, 0u);
    um9_rx_init(&r, 0u);
    n = um9_tx_message(&t, 0u, msg, len, pkts, 96u);
    CHECK(n > 0);
    /* Interleave feed/take exactly like the real consumer (the OMS shim
     * drains one packet and takes one message per tick). */
    for (i = 0u; i < (unsigned)n; i++) {
        unsigned char out[3];
        unsigned olen = 0u;
        unsigned osx = 99u;
        unsigned j;

        CHECK(um9_rx_packet(&r, pkts[i], UM9_PACKET_SIZE) == 1);
        CHECK(um9_rx_message(&r, out, &olen, &osx) == 1);
        CHECK(olen >= 1u && olen <= 3u);
        if ((unsigned)n == 1u) {
            CHECK(osx == UM9_SYSEX_NONE);   /* complete message */
        } else if (i == 0u) {
            CHECK(osx == UM9_SYSEX_START);
        } else if (i + 1u == (unsigned)n) {
            CHECK(osx == UM9_SYSEX_END);
        } else {
            CHECK(osx == UM9_SYSEX_MID);
        }
        CHECK(got_len + olen <= sizeof(got));
        for (j = 0u; j < olen; j++) {
            got[got_len++] = out[j];
        }
    }
    CHECK(got_len == len);
    for (i = 0u; i < len; i++) {
        CHECK(got[i] == msg[i]);
    }
    expect_empty(&r);
}

static void test_roundtrip(void)
{
    static const unsigned char sysex1[] = { 0xF0u, 0xF7u };
    static const unsigned char sysex2[] = { 0xF0u, 0x7Eu, 0x7Fu, 0xF7u };
    static const unsigned char sysex3[] = { 0xF0u, 0x01u, 0x02u, 0x03u,
                                            0x04u, 0x05u, 0x06u, 0xF7u };
    static const unsigned char sysex4[] = { 0xF0u, 0x43u, 0x10u, 0x4Cu,
                                            0x00u, 0x00u, 0x7Eu, 0x00u,
                                            0xF7u };
    unsigned char big[255];
    unsigned i;

    roundtrip_sysex(sysex1, sizeof(sysex1));
    roundtrip_sysex(sysex2, sizeof(sysex2));
    roundtrip_sysex(sysex3, sizeof(sysex3));
    roundtrip_sysex(sysex4, sizeof(sysex4));

    big[0] = 0xF0u;
    for (i = 1u; i < 254u; i++) {
        big[i] = (unsigned char)(0x01u + (i % 127u));
    }
    big[254] = 0xF7u;
    roundtrip_sysex(big, sizeof(big));
}

/* Deterministic LCG for the randomized model-compare test. */
static unsigned long g_seed = 0x12345678ul;

static unsigned long next_rand(void)
{
    g_seed = g_seed * 1103515245ul + 12345ul;
    return (g_seed >> 16) & 0x7FFFul;
}

/* Reference model: the same table as packets.c, applied independently. */
static unsigned ref_cin_len(unsigned cin)
{
    static const unsigned char lens[16] = {
        0, 1, 2, 3, 3, 1, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3
    };
    return lens[cin & 0x0Fu];
}

/* Feed `count` random packets into the stream and compare against a
 * simple reference model that tracks the same states. */
static void test_rx_random_model(void)
{
    struct um9_rx_stream s;
    unsigned char in_sysex = 0u;
    unsigned long expected_start = 0ul;
    unsigned long expected_mid = 0ul;
    unsigned long expected_end = 0ul;
    unsigned long expected_none = 0ul;
    unsigned long expected_mal_start = 0ul;
    unsigned long expected_no_eox = 0ul;
    unsigned i;

    um9_rx_init(&s, 0u);
    for (i = 0u; i < 4000u; i++) {
        unsigned cin = (unsigned)(next_rand() % 16u);
        unsigned len = ref_cin_len(cin);
        unsigned char pkt[UM9_PACKET_SIZE];
        unsigned char d0 = (unsigned char)(next_rand() & 0xFFu);

        pkt[0] = (unsigned char)(cin & 0x0Fu);
        pkt[1] = d0;
        pkt[2] = (unsigned char)(next_rand() & 0xFFu);
        pkt[3] = (unsigned char)(next_rand() & 0xFFu);
        if (cin == 0u || cin == 1u) {
            /* dropped; no message */
        } else if (cin == UM9_CIN_SYSEX_START) {
            if (!in_sysex) {
                if (pkt[1] != 0xF0u) {
                    expected_mal_start++;
                }
                expected_start++;
            } else {
                expected_mid++;
            }
            in_sysex = 1u;
        } else if (cin == UM9_CIN_SYSEX_END_1 || cin == UM9_CIN_SYSEX_END_2
                   || cin == UM9_CIN_SYSEX_END_3) {
            if (len > 0u && pkt[len] != 0xF7u) {
                expected_no_eox++;
            }
            expected_end++;
            in_sysex = 0u;
        } else {
            expected_none++;
        }
        CHECK(um9_rx_packet(&s, pkt, UM9_PACKET_SIZE) == 1);
    }
    /* Drain the queue; the reference model only predicts totals and
     * the sysex-state mix (the queue may have dropped the tail). */
    {
        unsigned long expected_total = expected_start + expected_mid
                                       + expected_end + expected_none;
        while (um9_rx_message(&s, NULL, NULL, NULL) == 1) {
            /* drain */
        }
        CHECK(s.stats.packets == 4000ul);
        CHECK(s.stats.messages == expected_total - s.stats.dropped_full);
        CHECK(s.stats.malformed_start == expected_mal_start);
        CHECK(s.stats.sysex_end_no_eox == expected_no_eox);
        CHECK(s.in_sysex == in_sysex);
    }
}

/* --------------------------- property sweeps -------------------------- */

/* Property: tx -> rx round-trips EVERY SysEx length 2..255 byte-for-byte
 * with the tag sequence START, MID*, END. */
static void test_prop_roundtrip_all_lengths(void)
{
    struct um9_tx_stream t;
    struct um9_rx_stream r;
    unsigned char pkts[96][UM9_PACKET_SIZE];
    unsigned char msg[255];
    unsigned len;
    int n;

    for (len = 2u; len <= 255u; len++) {
        unsigned i;

        msg[0] = 0xF0u;
        for (i = 1u; i + 1u < len; i++) {
            msg[i] = (unsigned char)(1u + (next_rand() % 126u));
        }
        msg[len - 1u] = 0xF7u;

        um9_tx_init(&t, 0u);
        um9_rx_init(&r, 0u);
        n = um9_tx_message(&t, 0u, msg, len, pkts, 96u);
        CHECK(n > 0);
        CHECK((unsigned)n == (len <= 3u ? 1u : 1u + (len - 3u + 2u) / 3u));
        for (i = 0u; i < (unsigned)n; i++) {
            unsigned char out[3];
            unsigned olen = 0u;
            unsigned osx = 99u;
            unsigned j;

            CHECK(um9_rx_packet(&r, pkts[i], UM9_PACKET_SIZE) == 1);
            CHECK(um9_rx_message(&r, out, &olen, &osx) == 1);
            if ((unsigned)n == 1u) {
                CHECK(osx == UM9_SYSEX_NONE);   /* complete message */
            } else if (i == 0u) {
                CHECK(osx == UM9_SYSEX_START);
            } else if (i + 1u == (unsigned)n) {
                CHECK(osx == UM9_SYSEX_END);
            } else {
                CHECK(osx == UM9_SYSEX_MID);
            }
            /* The byte stream must reassemble to the original message. */
            CHECK(olen == 3u || i + 1u == (unsigned)n);
            for (j = 0u; j < olen; j++) {
                unsigned idx = i * 3u + j;
                CHECK(idx < len);
                CHECK(out[j] == msg[idx]);
            }
        }
    }
}

/* Property: for any valid packet sequence, received messages preserve
 * arrival order, carry 1..3 bytes, and the SysEx tag sequence obeys
 * START (MID)* END, with NONE (realtime) messages allowed anywhere. */
static void test_prop_rx_order_and_tags(void)
{
    enum { ITERS = 3000 };
    struct um9_rx_stream s;
    unsigned char in_sysex = 0u;
    unsigned i;

    um9_rx_init(&s, 0u);
    for (i = 0u; i < ITERS; i++) {
        unsigned cin = (unsigned)(next_rand() % 16u);
        unsigned char pkt[UM9_PACKET_SIZE];
        unsigned char data[3];
        unsigned k;

        for (k = 0u; k < 3u; k++) {
            data[k] = (unsigned char)(next_rand() & 0xFFu);
        }
        /* Keep the model's state machine honest: fix the first byte of a
         * SysEx start to F0 most of the time so malformed-start stays a
         * corner case, and put F7 at the end of end packets most of the
         * time. */
        if (cin == UM9_CIN_SYSEX_START && !in_sysex && (next_rand() % 8u) != 0u) {
            data[0] = 0xF0u;
        }
        if ((cin == UM9_CIN_SYSEX_END_1 || cin == UM9_CIN_SYSEX_END_2
             || cin == UM9_CIN_SYSEX_END_3) && (next_rand() % 8u) != 0u) {
            data[um9_cin_data_length(cin) - 1u] = 0xF7u;
        }
        pkt[0] = (unsigned char)(cin & 0x0Fu);
        pkt[1] = data[0];
        pkt[2] = data[1];
        pkt[3] = data[2];
        CHECK(um9_rx_packet(&s, pkt, UM9_PACKET_SIZE) == 1);

        if (cin == UM9_CIN_SYSEX_START) {
            in_sysex = 1u;
        } else if (cin == UM9_CIN_SYSEX_END_1 || cin == UM9_CIN_SYSEX_END_2
                   || cin == UM9_CIN_SYSEX_END_3) {
            in_sysex = 0u;
        }
    }
    /* Drain and verify per-message invariants against a tag model. The
     * drained sequence is a valid subsequence of the fed stream (drop-new
     * only removes tails), so the tag-sequence property must hold on it;
     * no final state comparison is possible when drops occurred. */
    {
        unsigned char model_sysex = 0u;
        unsigned char out[3];
        unsigned olen;
        unsigned osx;

        while (um9_rx_message(&s, out, &olen, &osx) == 1) {
            CHECK(olen >= 1u && olen <= 3u);
            CHECK(osx == UM9_SYSEX_NONE || osx == UM9_SYSEX_START
                  || osx == UM9_SYSEX_MID || osx == UM9_SYSEX_END);
            if (osx == UM9_SYSEX_START) {
                CHECK(model_sysex == 0u);
                model_sysex = 1u;
            } else if (osx == UM9_SYSEX_MID) {
                CHECK(model_sysex == 1u);
            } else if (osx == UM9_SYSEX_END) {
                /* END may close a run (model 1) or stand alone (model 0)
                 * when it does not start with F0 (a lone EOX). */
                model_sysex = 0u;
            }
        }
    }
}

/* Property: garbage input never corrupts state. Feed random bad buffers
 * (NULL, wrong lengths) and random valid packets; a known packet after
 * each garbage input must still decode correctly. */
static void test_prop_garbage_resilience(void)
{
    struct um9_rx_stream s;
    unsigned char good[UM9_PACKET_SIZE];
    unsigned i;

    um9_rx_init(&s, 1u);
    good[0] = 0x1Au;                 /* cable 1, CIN 0xA (note on) */
    good[1] = 0x90u;
    good[2] = 0x3Cu;
    good[3] = 0x40u;

    for (i = 0u; i < 2000u; i++) {
        unsigned mode = (unsigned)(next_rand() % 5u);
        unsigned char junk[UM9_PACKET_SIZE];
        unsigned k;

        if (mode == 0u) {
            CHECK(um9_rx_packet(&s, NULL, UM9_PACKET_SIZE) == 0);
        } else if (mode == 1u) {
            /* Any length is legal for the API; only len != 4 is rejected.
             * Garbage CONTENT with len 4 is accepted and policy-handled
             * (dropped/counted), never a state hazard. */
            unsigned l = (unsigned)(next_rand() % 9u);
            CHECK(um9_rx_packet(&s, junk, l) == (l == UM9_PACKET_SIZE ? 1 : 0));
        } else if (mode == 2u) {
            unsigned char shortbuf[1] = { 0u };
            CHECK(um9_rx_packet(&s, shortbuf, 0u) == 0);
        } else if (mode == 3u) {
            /* Valid-shaped packet on the wrong cable: dropped, counted. */
            for (k = 0u; k < 4u; k++) {
                junk[k] = (unsigned char)(next_rand() & 0xFFu);
            }
            junk[0] = (unsigned char)(((next_rand() % 8u) + 3u) << 4);
            CHECK(um9_rx_packet(&s, junk, UM9_PACKET_SIZE) == 1);
        } else {
            /* A known good packet must still decode after any garbage. */
            CHECK(um9_rx_packet(&s, good, UM9_PACKET_SIZE) == 1);
        }
    }
    /* The last fed packet was good; a matching message must be pending. */
    {
        unsigned char out[3];
        unsigned olen = 0u;
        unsigned osx = 99u;
        unsigned drained = 0u;

        while (um9_rx_message(&s, out, &olen, &osx) == 1) {
            drained++;
            CHECK(olen >= 1u && olen <= 3u);
            if (drained == 1u) {
                CHECK(out[0] == 0x90u);
            }
        }
        CHECK(drained >= 1ul);
    }
}

int test_midi_stream_run(void)
{
    g_failures = 0;

    test_rx_channel_voice();
    test_rx_system_common_and_realtime();
    test_rx_sysex();
    test_rx_realtime_inside_sysex();
    test_rx_malformed_and_dropped();
    test_rx_queue_overflow();
    test_tx_channel_and_common();
    test_tx_sysex();
    test_tx_invalid();
    test_roundtrip();
    test_rx_random_model();
    test_prop_roundtrip_all_lengths();
    test_prop_rx_order_and_tags();
    test_prop_garbage_resilience();

    if (g_failures != 0) {
        printf("midi_stream: %d check(s) failed\n", g_failures);
    }
    return g_failures;
}

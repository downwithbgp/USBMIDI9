/*
 * Host tests for the portable USB-MIDI Event Packet layer (core/packets.h).
 *
 * Portable C (C89/C90).
 */

#include <stdio.h>

#include "core/packets.h"

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

/* Compare a decoded packet against expected values; report on mismatch. */
static void expect_packet(const um9_packet *p, unsigned cable, unsigned cin,
                          unsigned length, unsigned char d0, unsigned char d1,
                          unsigned char d2, const char *what)
{
    if (p == NULL || p->cable != cable || p->cin != cin || p->length != length
        || p->data[0] != d0 || p->data[1] != d1 || p->data[2] != d2) {
        if (p == NULL) {
            printf("FAIL %s: no packet\n", what);
        } else {
            printf("FAIL %s: got cable=%u cin=%u len=%u data=%02X %02X %02X\n",
                   what, p->cable, p->cin, p->length,
                   (unsigned)p->data[0], (unsigned)p->data[1],
                   (unsigned)p->data[2]);
        }
        g_failures++;
    }
}

typedef struct {
    unsigned char b0, b1, b2, b3;    /* raw Event Packet bytes */
    unsigned cable;
    unsigned cin;
    unsigned length;
    unsigned char d0, d1, d2;        /* expected decoded data */
    const char *name;
} decode_case;

static void test_decode_cases(void)
{
    static const decode_case cases[] = {
        /* The canonical example from the project brief: 09 90 3C 64. */
        { 0x09u, 0x90u, 0x3Cu, 0x64u, 0u, UM9_CIN_NOTE_OFF, 3u,
          0x90u, 0x3Cu, 0x64u, "note off (brief example)" },
        { 0x0Au, 0x90u, 0x3Cu, 0x40u, 0u, UM9_CIN_NOTE_ON, 3u,
          0x90u, 0x3Cu, 0x40u, "note on" },
        { 0x0Bu, 0xA0u, 0x3Cu, 0x40u, 0u, UM9_CIN_POLY_KEY_PRESS, 3u,
          0xA0u, 0x3Cu, 0x40u, "poly key pressure" },
        { 0x0Cu, 0xB0u, 0x07u, 0x7Fu, 0u, UM9_CIN_CONTROL_CHANGE, 3u,
          0xB0u, 0x07u, 0x7Fu, "control change" },
        { 0x0Du, 0xC0u, 0x05u, 0x00u, 0u, UM9_CIN_PROGRAM_CHANGE, 2u,
          0xC0u, 0x05u, 0x00u, "program change" },
        { 0x0Eu, 0xD0u, 0x40u, 0x00u, 0u, UM9_CIN_CHANNEL_PRESSURE, 2u,
          0xD0u, 0x40u, 0x00u, "channel pressure" },
        { 0x0Fu, 0xE0u, 0x00u, 0x40u, 0u, UM9_CIN_PITCH_BEND, 3u,
          0xE0u, 0x00u, 0x40u, "pitch bend" },
        /* Single-byte system messages. */
        { 0x05u, 0xF8u, 0x00u, 0x00u, 0u, UM9_CIN_SYS_COMMON_1, 1u,
          0xF8u, 0x00u, 0x00u, "timing clock" },
        { 0x05u, 0xF6u, 0x00u, 0x00u, 0u, UM9_CIN_SYS_COMMON_1, 1u,
          0xF6u, 0x00u, 0x00u, "tune request" },
        { 0xF5u, 0xFFu, 0x00u, 0x00u, 15u, UM9_CIN_SYS_COMMON_1, 1u,
          0xFFu, 0x00u, 0x00u, "cable 15 single-byte" },
        /* Two- and three-byte system common. */
        { 0x02u, 0xF3u, 0x01u, 0x00u, 0u, UM9_CIN_SYS_COMMON_2, 2u,
          0xF3u, 0x01u, 0x00u, "song select" },
        { 0x02u, 0xF1u, 0x20u, 0x00u, 0u, UM9_CIN_SYS_COMMON_2, 2u,
          0xF1u, 0x20u, 0x00u, "MTC" },
        { 0x03u, 0xF2u, 0x00u, 0x40u, 0u, UM9_CIN_SYS_COMMON_3, 3u,
          0xF2u, 0x00u, 0x40u, "song position pointer" },
        /* SysEx start/continue. */
        { 0x04u, 0xF0u, 0x7Eu, 0x00u, 0u, UM9_CIN_SYSEX_START, 3u,
          0xF0u, 0x7Eu, 0x00u, "sysex start" },
        { 0x04u, 0x01u, 0x02u, 0x03u, 0u, UM9_CIN_SYSEX_START, 3u,
          0x01u, 0x02u, 0x03u, "sysex continue" },
        /* SysEx termination cases. */
        { 0x06u, 0xF7u, 0x00u, 0x00u, 0u, UM9_CIN_SYSEX_END_1, 1u,
          0xF7u, 0x00u, 0x00u, "sysex end with one byte" },
        { 0x07u, 0x01u, 0xF7u, 0x00u, 0u, UM9_CIN_SYSEX_END_2, 2u,
          0x01u, 0xF7u, 0x00u, "sysex end with two bytes" },
        { 0x08u, 0x01u, 0x02u, 0xF7u, 0u, UM9_CIN_SYSEX_END_3, 3u,
          0x01u, 0x02u, 0xF7u, "sysex end with three bytes" },
        /* Cable event. */
        { 0x01u, 0x00u, 0x00u, 0x00u, 0u, UM9_CIN_CABLE_EVENT, 1u,
          0x00u, 0x00u, 0x00u, "cable event" },
        /* CIN 0x0 (Miscellaneous): decodes, but the payload length is not
         * defined by the specification, so length is reported as 0. */
        { 0x00u, 0xAAu, 0xBBu, 0xCCu, 0u, UM9_CIN_MISC, 0u,
          0xAAu, 0xBBu, 0xCCu, "misc (length undefined)" }
    };
    unsigned n = (unsigned)(sizeof(cases) / sizeof(cases[0]));
    unsigned i;

    for (i = 0u; i < n; i++) {
        unsigned char pkt[4];
        um9_packet p;

        pkt[0] = cases[i].b0;
        pkt[1] = cases[i].b1;
        pkt[2] = cases[i].b2;
        pkt[3] = cases[i].b3;
        CHECK(um9_packet_decode(pkt, 4u, &p) == 1);
        expect_packet(&p, cases[i].cable, cases[i].cin, cases[i].length,
                      cases[i].d0, cases[i].d1, cases[i].d2, cases[i].name);
    }
}

static void test_decode_all_cables(void)
{
    unsigned cable;

    /* Cable extraction must work for every nibble value. */
    for (cable = 0u; cable < 16u; cable++) {
        unsigned char pkt[4];
        um9_packet p;

        pkt[0] = (unsigned char)((cable << 4) | UM9_CIN_NOTE_ON);
        pkt[1] = 0x90u;
        pkt[2] = 0x3Cu;
        pkt[3] = 0x40u;
        CHECK(um9_packet_decode(pkt, 4u, &p) == 1);
        CHECK(p.cable == cable);
        CHECK(p.cin == UM9_CIN_NOTE_ON);
        CHECK(p.length == 3u);
    }
}

static void test_cin_length_table(void)
{
    static const unsigned expected[16] = {
        0, 1, 2, 3, 3, 1, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3
    };
    unsigned i;

    for (i = 0u; i < 16u; i++) {
        CHECK(um9_cin_data_length(i) == expected[i]);
    }
    CHECK(um9_cin_data_length(16u) == 0u);
    CHECK(um9_cin_data_length(255u) == 0u);
}

typedef struct {
    unsigned status;
    unsigned len;
    unsigned cin;
} encode_case;

static void test_encode_roundtrip(void)
{
    static const encode_case cases[] = {
        { 0x80u, 3u, UM9_CIN_NOTE_OFF },
        { 0x90u, 3u, UM9_CIN_NOTE_ON },
        { 0xA0u, 3u, UM9_CIN_POLY_KEY_PRESS },
        { 0xB0u, 3u, UM9_CIN_CONTROL_CHANGE },
        { 0xC0u, 2u, UM9_CIN_PROGRAM_CHANGE },
        { 0xD0u, 2u, UM9_CIN_CHANNEL_PRESSURE },
        { 0xE0u, 3u, UM9_CIN_PITCH_BEND },
        { 0xF1u, 2u, UM9_CIN_SYS_COMMON_2 },
        { 0xF2u, 3u, UM9_CIN_SYS_COMMON_3 },
        { 0xF3u, 2u, UM9_CIN_SYS_COMMON_2 },
        { 0xF6u, 1u, UM9_CIN_SYS_COMMON_1 },
        { 0xF8u, 1u, UM9_CIN_SYS_COMMON_1 },
        { 0xFAu, 1u, UM9_CIN_SYS_COMMON_1 },
        { 0xFBu, 1u, UM9_CIN_SYS_COMMON_1 },
        { 0xFCu, 1u, UM9_CIN_SYS_COMMON_1 },
        { 0xFEu, 1u, UM9_CIN_SYS_COMMON_1 },
        { 0xFFu, 1u, UM9_CIN_SYS_COMMON_1 }
    };
    unsigned n = (unsigned)(sizeof(cases) / sizeof(cases[0]));
    unsigned i;

    for (i = 0u; i < n; i++) {
        unsigned char msg[3];
        um9_packet p;
        unsigned j;

        for (j = 0u; j < 3u; j++) {
            msg[j] = 0u;
        }
        msg[0] = (unsigned char)cases[i].status;
        msg[1] = 0x11u;
        msg[2] = 0x22u;

        /* Encode on cable 3, then decode: fields and data must round-trip. */
        CHECK(um9_packet_encode(3u, msg, cases[i].len, &p) == 1);
        expect_packet(&p, 3u, cases[i].cin, cases[i].len,
                      (unsigned char)cases[i].status,
                      cases[i].len >= 2u ? 0x11u : 0u,
                      cases[i].len >= 3u ? 0x22u : 0u, "encode round-trip");
    }
}

static void test_encode_rejects(void)
{
    static const unsigned char note_on[3] = { 0x90u, 0x3Cu, 0x40u };
    static const unsigned char not_status[3] = { 0x3Cu, 0x40u, 0x00u };
    static const unsigned char sysex[3] = { 0xF0u, 0x01u, 0x02u };
    static const unsigned char sysex_end[2] = { 0xF7u, 0x00u };
    static const unsigned char undefined[2] = { 0xF4u, 0x00u };
    um9_packet p;

    CHECK(um9_packet_encode(0u, NULL, 3u, &p) == 0);          /* NULL msg */
    CHECK(um9_packet_encode(0u, note_on, 0u, &p) == 0);       /* empty */
    CHECK(um9_packet_encode(0u, note_on, 4u, &p) == 0);       /* too long */
    CHECK(um9_packet_encode(16u, note_on, 3u, &p) == 0);      /* cable > 15 */
    CHECK(um9_packet_encode(0u, note_on, 2u, &p) == 0);       /* note on: 3 */
    CHECK(um9_packet_encode(0u, not_status, 3u, &p) == 0);    /* not a status */
    CHECK(um9_packet_encode(0u, sysex, 3u, &p) == 0);         /* SysEx: TODO */
    CHECK(um9_packet_encode(0u, sysex_end, 2u, &p) == 0);     /* SysEx end */
    CHECK(um9_packet_encode(0u, undefined, 2u, &p) == 0);     /* 0xF4 */
    CHECK(um9_packet_encode(0u, note_on, 3u, NULL) == 0);     /* NULL out */
}

static void test_decode_arg_validation(void)
{
    static const unsigned char pkt[4] = { 0x09u, 0x90u, 0x3Cu, 0x64u };
    um9_packet p;

    CHECK(um9_packet_decode(NULL, 4u, &p) == 0);       /* NULL buf */
    CHECK(um9_packet_decode(pkt, 4u, NULL) == 0);      /* NULL out */
    CHECK(um9_packet_decode(pkt, 3u, &p) == 0);        /* too short */
    CHECK(um9_packet_decode(pkt, 5u, &p) == 0);        /* too long */
    CHECK(um9_packet_decode(NULL, 0u, NULL) == 0);     /* both NULL */
}

int test_packets_run(void)
{
    g_failures = 0;
    test_decode_cases();
    test_decode_all_cables();
    test_cin_length_table();
    test_encode_roundtrip();
    test_encode_rejects();
    test_decode_arg_validation();
    return g_failures;
}

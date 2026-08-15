/*
 * Host tests for the safe descriptor walker (core/descriptors.h) and the
 * MIDIStreaming topology/ports parser (core/ports.h).
 *
 * The primary fixture is a SYNTHETIC descriptor buffer modeled on the
 * Evolution eKeys-49 / Keystation 49e topology documented in
 * docs/hardware.md. It is not captured from a real device; real raw bytes
 * are not available yet (fixtures/keystation-49e.bin is reserved for them).
 *
 * Portable C (C89/C90).
 */

#include <stdio.h>
#include <string.h>

#include "core/descriptors.h"
#include "core/ports.h"

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

/*
 * Synthetic fixture: a configuration with two interfaces; interface 1 is a
 * MIDIStreaming interface with the jack/endpoint topology observed on the
 * Keystation 49e:
 *
 *   MIDI IN  jacks: 1 (embedded), 2 (external)
 *   MIDI OUT jacks: 3 (embedded, source jack 2), 4 (external, source jack 1)
 *   EP 0x81 (bulk IN)  -> embedded jack 3
 *   EP 0x02 (bulk OUT) -> embedded jack 1
 *
 * MS header: bcdMSC 0x0100, wTotalLength 80 (0x50), both little-endian.
 * Configuration wTotalLength: 98 (0x62), little-endian.
 */
static const unsigned char keystation_model[] = {
    /* Configuration descriptor */
    0x09u, 0x02u, 0x62u, 0x00u, 0x02u, 0x01u, 0x00u, 0x80u, 0x32u,
    /* Interface 0: AudioControl, no endpoints */
    0x09u, 0x04u, 0x00u, 0x00u, 0x00u, 0x01u, 0x01u, 0x00u, 0x00u,
    /* Interface 1: MIDIStreaming, 2 endpoints */
    0x09u, 0x04u, 0x01u, 0x00u, 0x02u, 0x01u, 0x03u, 0x00u, 0x00u,
    /* CS_INTERFACE: MS header */
    0x07u, 0x24u, 0x01u, 0x00u, 0x01u, 0x50u, 0x00u,
    /* CS_INTERFACE: MIDI IN jack 1 (embedded) */
    0x06u, 0x24u, 0x02u, 0x01u, 0x01u, 0x00u,
    /* CS_INTERFACE: MIDI IN jack 2 (external) */
    0x06u, 0x24u, 0x02u, 0x02u, 0x02u, 0x00u,
    /* CS_INTERFACE: MIDI OUT jack 3 (embedded), 1 source pin -> jack 2 */
    0x09u, 0x24u, 0x03u, 0x03u, 0x01u, 0x01u, 0x02u, 0x00u, 0x00u,
    /* CS_INTERFACE: MIDI OUT jack 4 (external), 1 source pin -> jack 1 */
    0x09u, 0x24u, 0x03u, 0x04u, 0x02u, 0x01u, 0x01u, 0x00u, 0x00u,
    /* Standard endpoint 0x81, bulk, max packet 64 */
    0x09u, 0x05u, 0x81u, 0x02u, 0x40u, 0x00u, 0x00u, 0x00u, 0x00u,
    /* CS_ENDPOINT: MS general, 1 embedded jack -> 3 */
    0x08u, 0x25u, 0x01u, 0x01u, 0x03u, 0x00u, 0x00u, 0x00u,
    /* Standard endpoint 0x02, bulk, max packet 64 */
    0x09u, 0x05u, 0x02u, 0x02u, 0x40u, 0x00u, 0x00u, 0x00u, 0x00u,
    /* CS_ENDPOINT: MS general, 1 embedded jack -> 1 */
    0x08u, 0x25u, 0x01u, 0x01u, 0x01u, 0x00u, 0x00u, 0x00u
};

static void test_iter_walks_fixture(void)
{
    um9_desc_iter it;
    unsigned count;
    unsigned first_type;
    unsigned first_blen;

    count = 0u;
    first_type = 0u;
    first_blen = 0u;

    um9_desc_iter_init(&it, keystation_model, sizeof(keystation_model));
    while (um9_desc_iter_next(&it)) {
        if (count == 0u) {
            first_type = it.btype;
            first_blen = it.blen;
        }
        count++;
    }
    CHECK(it.err == UM9_DESC_ERR_NONE);
    CHECK(count == 12u);
    CHECK(first_blen == 9u);
    CHECK(first_type == UM9_DT_CONFIG);
}

static void test_iter_malformed(void)
{
    static const unsigned char truncated_header[1] = { 0x09u };
    static const unsigned char blen_too_big[2] = { 0x09u, 0x02u };
    static const unsigned char zero_len[2] = { 0x00u, 0x04u };
    static const unsigned char one_len[2] = { 0x01u, 0x04u };
    static const unsigned char mid_truncated[11] = {
        0x09u, 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x05u, 0x05u
    };
    um9_desc_iter it;

    /* Not even a full header. */
    um9_desc_iter_init(&it, truncated_header, 1u);
    CHECK(um9_desc_iter_next(&it) == 0);
    CHECK(it.err == UM9_DESC_ERR_TRUNCATED);

    /* bLength claims more bytes than remain. */
    um9_desc_iter_init(&it, blen_too_big, 2u);
    CHECK(um9_desc_iter_next(&it) == 0);
    CHECK(it.err == UM9_DESC_ERR_TRUNCATED);

    /* Zero-length descriptors (bLength 0 and 1) are rejected. */
    um9_desc_iter_init(&it, zero_len, 2u);
    CHECK(um9_desc_iter_next(&it) == 0);
    CHECK(it.err == UM9_DESC_ERR_ZERO_LEN);

    um9_desc_iter_init(&it, one_len, 2u);
    CHECK(um9_desc_iter_next(&it) == 0);
    CHECK(it.err == UM9_DESC_ERR_ZERO_LEN);

    /* First descriptor valid, second truncated mid-way. */
    um9_desc_iter_init(&it, mid_truncated, sizeof(mid_truncated));
    CHECK(um9_desc_iter_next(&it) == 1);
    CHECK(it.blen == 9u);
    CHECK(um9_desc_iter_next(&it) == 0);
    CHECK(it.err == UM9_DESC_ERR_TRUNCATED);

    /* Empty buffer is a clean end, not an error. */
    um9_desc_iter_init(&it, NULL, 0u);
    CHECK(um9_desc_iter_next(&it) == 0);
    CHECK(it.err == UM9_DESC_ERR_NONE);
}

static void test_iter_u16le(void)
{
    /* bLength 4, type 0x24, bytes 0x00 0x01 at offset 2: 0x0100 LE. */
    static const unsigned char desc[4] = { 0x04u, 0x24u, 0x00u, 0x01u };
    um9_desc_iter it;

    um9_desc_iter_init(&it, desc, sizeof(desc));
    CHECK(um9_desc_iter_next(&it) == 1);
    CHECK(um9_desc_u16le(&it, 2u) == 0x0100u);   /* explicit LE, not host order */
    CHECK(um9_desc_u8(&it, 0u) == 0x04u);
    CHECK(um9_desc_u8(&it, 3u) == 0x01u);
    CHECK(um9_desc_u8(&it, 4u) == 0u);           /* out of range -> 0 */
    CHECK(um9_desc_u16le(&it, 3u) == 0u);        /* second byte out of range */
    CHECK(um9_desc_u16le(&it, 5u) == 0u);        /* out of range */
}

static void test_ms_parse_fixture(void)
{
    um9_ms_info info;
    um9_ports ports;
    unsigned i;
    int jack3_seen;
    int jack1_seen;

    CHECK(um9_ms_parse(keystation_model, sizeof(keystation_model), &info) == 1);
    CHECK(info.found == 1u);
    CHECK(info.interface_number == 1u);
    CHECK(info.alternate_setting == 0u);
    CHECK(info.num_endpoints_declared == 2u);
    CHECK(info.bcd_msc == 0x0100u);
    CHECK(info.wtotal_length == 80u);
    CHECK(info.num_in_jacks == 2u);
    CHECK(info.num_out_jacks == 2u);
    CHECK(info.num_endpoints == 2u);

    /* Jack 3: embedded MIDI OUT jack sourced from jack 2. */
    jack3_seen = 0;
    for (i = 0u; i < info.num_jacks; i++) {
        if (info.jacks[i].id == 3u) {
            jack3_seen = 1;
            CHECK(info.jacks[i].valid == 1u);
            CHECK(info.jacks[i].type == UM9_JACK_EMBEDDED);
            CHECK(info.jacks[i].direction == UM9_JACK_DIR_OUT);
            CHECK(info.jacks[i].num_sources == 1u);
            CHECK(info.jacks[i].source_ids[0] == 2u);
        }
    }
    CHECK(jack3_seen == 1);

    /* Jack 1: embedded MIDI IN jack. */
    jack1_seen = 0;
    for (i = 0u; i < info.num_jacks; i++) {
        if (info.jacks[i].id == 1u) {
            jack1_seen = 1;
            CHECK(info.jacks[i].type == UM9_JACK_EMBEDDED);
            CHECK(info.jacks[i].direction == UM9_JACK_DIR_IN);
        }
    }
    CHECK(jack1_seen == 1);

    /* Endpoint 0x81 (IN) -> embedded jack 3; endpoint 0x02 (OUT) -> jack 1. */
    CHECK(info.endpoints[0].address == 0x81u);
    CHECK(info.endpoints[0].attributes == 0x02u);
    CHECK(info.endpoints[0].num_embedded == 1u);
    CHECK(info.endpoints[0].embedded_ids[0] == 3u);
    CHECK(info.endpoints[1].address == 0x02u);
    CHECK(info.endpoints[1].num_embedded == 1u);
    CHECK(info.endpoints[1].embedded_ids[0] == 1u);

    /* Derived logical ports: one input (EP 0x81, cable 0, jack 3) and one
     * output (EP 0x02, cable 0, jack 1). */
    CHECK(um9_ports_parse(keystation_model, sizeof(keystation_model),
                          &ports) == 1);
    CHECK(ports.count == 2u);
    CHECK(ports.ports[0].valid == 1u);
    CHECK(ports.ports[0].endpoint_address == 0x81u);
    CHECK(ports.ports[0].direction == UM9_PORT_DIR_IN);
    CHECK(ports.ports[0].cable == 0u);
    CHECK(ports.ports[0].embedded_jack_id == 3u);
    CHECK(ports.ports[1].endpoint_address == 0x02u);
    CHECK(ports.ports[1].direction == UM9_PORT_DIR_OUT);
    CHECK(ports.ports[1].cable == 0u);
    CHECK(ports.ports[1].embedded_jack_id == 1u);
}

static void test_ms_parse_stops_at_next_interface(void)
{
    /* A second MIDIStreaming interface follows the first one. The parser
     * must stop at the interface boundary and ignore the extra endpoint. */
    static const unsigned char extra[] = {
        0x09u, 0x04u, 0x02u, 0x00u, 0x02u, 0x01u, 0x03u, 0x00u, 0x00u,
        0x09u, 0x05u, 0x83u, 0x02u, 0x40u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x08u, 0x25u, 0x01u, 0x01u, 0x05u, 0x00u, 0x00u, 0x00u
    };
    unsigned char buf[sizeof(keystation_model) + sizeof(extra)];
    um9_ms_info info;

    memcpy(buf, keystation_model, sizeof(keystation_model));
    memcpy(buf + sizeof(keystation_model), extra, sizeof(extra));

    CHECK(um9_ms_parse(buf, sizeof(buf), &info) == 1);
    CHECK(info.found == 1u);
    CHECK(info.interface_number == 1u);
    CHECK(info.num_endpoints == 2u);   /* EP 0x83 belongs to interface 2 */
}

static void test_ms_parse_no_ms_interface(void)
{
    static const unsigned char audio_only[] = {
        0x09u, 0x02u, 0x12u, 0x00u, 0x01u, 0x01u, 0x00u, 0x80u, 0x32u,
        0x09u, 0x04u, 0x00u, 0x00u, 0x00u, 0x01u, 0x01u, 0x00u, 0x00u
    };
    um9_ms_info info;
    um9_ports ports;

    CHECK(um9_ms_parse(audio_only, sizeof(audio_only), &info) == 1);
    CHECK(info.found == 0u);
    CHECK(um9_ports_parse(audio_only, sizeof(audio_only), &ports) == 1);
    CHECK(ports.count == 0u);
}

static void test_ms_parse_malformed(void)
{
    static const unsigned char zero_len[2] = { 0x00u, 0x04u };
    um9_ms_info info;
    um9_ports ports;

    /* Truncated fixture: the last CS_ENDPOINT descriptor is cut short. */
    CHECK(um9_ms_parse(keystation_model, sizeof(keystation_model) - 4u,
                       &info) == 0);
    CHECK(um9_ports_parse(keystation_model, sizeof(keystation_model) - 4u,
                          &ports) == 0);

    /* Zero-length first descriptor. */
    CHECK(um9_ms_parse(zero_len, sizeof(zero_len), &info) == 0);

    /* Empty buffer: valid input, but no MIDIStreaming interface. */
    CHECK(um9_ms_parse(NULL, 0u, &info) == 1);
    CHECK(info.found == 0u);

    /* NULL output pointers are rejected. */
    CHECK(um9_ms_parse(keystation_model, sizeof(keystation_model), NULL) == 0);
    CHECK(um9_ports_parse(keystation_model, sizeof(keystation_model), NULL) == 0);
    CHECK(um9_ports_from_ms(NULL, &ports) == 0);
}

int test_descriptors_run(void)
{
    g_failures = 0;
    test_iter_walks_fixture();
    test_iter_malformed();
    test_iter_u16le();
    test_ms_parse_fixture();
    test_ms_parse_stops_at_next_interface();
    test_ms_parse_no_ms_interface();
    test_ms_parse_malformed();
    return g_failures;
}

/*
 * Host tests for the safe descriptor walker (core/descriptors.h) and the
 * MIDIStreaming topology/ports parser (core/ports.h).
 *
 * Two fixtures are used:
 *
 *  - keystation_model below is a SYNTHETIC descriptor buffer modeled on the
 *    Evolution eKeys-49 / Keystation 49e topology documented in
 *    docs/hardware.md. It is retained because it exercises malformed-input
 *    and boundary cases the real capture does not.
 *
 *  - fixtures/keystation-49e.bin is the REAL 119-byte descriptor capture
 *    from the physical device (18-byte device descriptor plus 101-byte
 *    configuration descriptor, configuration wTotalLength 0x0065), read
 *    from disk by test_real_keystation_fixture().
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
    /* CS_INTERFACE: MIDI IN jack 1 (embedded). NOTE: bJackType sits at
     * offset 3 and bJackID at offset 4 for BOTH jack kinds (USB-MIDI 1.0,
     * Tables 6-3 and 6-4); on this device the IN jack type and ID values
     * coincide (1/1, 2/2), so the dedicated test_jack_field_layout() test
     * below uses distinct values to pin the field order down. */
    0x06u, 0x24u, 0x02u, 0x01u, 0x01u, 0x00u,
    /* CS_INTERFACE: MIDI IN jack 2 (external) */
    0x06u, 0x24u, 0x02u, 0x02u, 0x02u, 0x00u,
    /* CS_INTERFACE: MIDI OUT jack 3 (embedded), 1 source pin -> jack 2.
     * NOTE: same bJackType (offset 3) / bJackID (offset 4) order as MIDI
     * IN jacks (USB-MIDI 1.0, Table 6-4). */
    0x09u, 0x24u, 0x03u, 0x01u, 0x03u, 0x01u, 0x02u, 0x00u, 0x00u,
    /* CS_INTERFACE: MIDI OUT jack 4 (external), 1 source pin -> jack 1 */
    0x09u, 0x24u, 0x03u, 0x02u, 0x04u, 0x01u, 0x01u, 0x00u, 0x00u,
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
    CHECK(info.endpoints[0].max_packet_size == 64u);
    CHECK(info.endpoints[0].num_embedded == 1u);
    CHECK(info.endpoints[0].embedded_ids[0] == 3u);
    CHECK(info.endpoints[1].address == 0x02u);
    CHECK(info.endpoints[1].attributes == 0x02u);
    CHECK(info.endpoints[1].max_packet_size == 64u);
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

/* Parse the REAL descriptor capture (fixtures/keystation-49e.bin) through
 * the generic parser and verify the observed device properties. The capture
 * is 119 bytes: an 18-byte device descriptor plus a 101-byte configuration
 * descriptor set (configuration wTotalLength 0x0065). No Keystation-specific
 * parsing logic exists in the production code; only the generic walker and
 * topology parser are exercised. */
static void test_real_keystation_fixture(void)
{
    static const char path[] = "fixtures/keystation-49e.bin";
    unsigned char buf[256];
    FILE *f;
    size_t got;
    um9_ms_info info;
    um9_ports ports;
    unsigned i;
    int jack_seen;

    f = fopen(path, "rb");
    if (f == NULL) {
        fail("cannot open fixtures/keystation-49e.bin", __FILE__, __LINE__);
        return;
    }
    got = fread(buf, 1u, sizeof(buf), f);
    fclose(f);
    CHECK((unsigned)got == 119u);

    CHECK(um9_ms_parse(buf, (unsigned)got, &info) == 1);

    /* Device identity from the standard device descriptor. */
    CHECK(info.vid == 0x0a4du);
    CHECK(info.pid == 0x0090u);

    /* One MIDIStreaming interface: number 1, Audio/MIDIStreaming class. */
    CHECK(info.found == 1u);
    CHECK(info.interface_number == 1u);
    CHECK(info.interface_class == 0x01u);
    CHECK(info.interface_subclass == 0x03u);
    CHECK(info.num_endpoints_declared == 2u);

    /* MS header: version and class-specific set length. */
    CHECK(info.bcd_msc == 0x0100u);
    CHECK(info.wtotal_length == 0x0041u);

    /* Jacks: 2 MIDI IN (embedded 1, external 2), 2 MIDI OUT (embedded 3
     * sourced from jack 2, external 4 sourced from jack 1). */
    CHECK(info.num_in_jacks == 2u);
    CHECK(info.num_out_jacks == 2u);
    CHECK(info.num_jacks == 4u);

    jack_seen = 0;
    for (i = 0u; i < info.num_jacks; i++) {
        if (info.jacks[i].id == 1u) {
            jack_seen = 1;
            CHECK(info.jacks[i].type == UM9_JACK_EMBEDDED);
            CHECK(info.jacks[i].direction == UM9_JACK_DIR_IN);
        }
    }
    CHECK(jack_seen == 1);

    jack_seen = 0;
    for (i = 0u; i < info.num_jacks; i++) {
        if (info.jacks[i].id == 2u) {
            jack_seen = 1;
            CHECK(info.jacks[i].type == UM9_JACK_EXTERNAL);
            CHECK(info.jacks[i].direction == UM9_JACK_DIR_IN);
        }
    }
    CHECK(jack_seen == 1);

    jack_seen = 0;
    for (i = 0u; i < info.num_jacks; i++) {
        if (info.jacks[i].id == 3u) {
            jack_seen = 1;
            CHECK(info.jacks[i].type == UM9_JACK_EMBEDDED);
            CHECK(info.jacks[i].direction == UM9_JACK_DIR_OUT);
            CHECK(info.jacks[i].num_sources == 1u);
            CHECK(info.jacks[i].source_ids[0] == 2u);   /* sourced from jack 2 */
        }
    }
    CHECK(jack_seen == 1);

    jack_seen = 0;
    for (i = 0u; i < info.num_jacks; i++) {
        if (info.jacks[i].id == 4u) {
            jack_seen = 1;
            CHECK(info.jacks[i].type == UM9_JACK_EXTERNAL);
            CHECK(info.jacks[i].direction == UM9_JACK_DIR_OUT);
            CHECK(info.jacks[i].num_sources == 1u);
            CHECK(info.jacks[i].source_ids[0] == 1u);   /* sourced from jack 1 */
        }
    }
    CHECK(jack_seen == 1);

    /* Bulk endpoints with their embedded jack associations. */
    CHECK(info.num_endpoints == 2u);
    CHECK(info.endpoints[0].address == 0x81u);
    CHECK(info.endpoints[0].attributes == 0x02u);        /* bulk */
    CHECK(info.endpoints[0].max_packet_size == 64u);
    CHECK(info.endpoints[0].num_embedded == 1u);
    CHECK(info.endpoints[0].embedded_ids[0] == 3u);
    CHECK(info.endpoints[1].address == 0x02u);
    CHECK(info.endpoints[1].attributes == 0x02u);        /* bulk */
    CHECK(info.endpoints[1].max_packet_size == 64u);
    CHECK(info.endpoints[1].num_embedded == 1u);
    CHECK(info.endpoints[1].embedded_ids[0] == 1u);

    /* Logical ports: one input (EP 0x81, cable 0, jack 3) and one output
     * (EP 0x02, cable 0, jack 1). */
    CHECK(um9_ports_parse(buf, (unsigned)got, &ports) == 1);
    CHECK(ports.count == 2u);
    CHECK(ports.ports[0].endpoint_address == 0x81u);
    CHECK(ports.ports[0].direction == UM9_PORT_DIR_IN);
    CHECK(ports.ports[0].cable == 0u);
    CHECK(ports.ports[0].embedded_jack_id == 3u);
    CHECK(ports.ports[1].endpoint_address == 0x02u);
    CHECK(ports.ports[1].direction == UM9_PORT_DIR_OUT);
    CHECK(ports.ports[1].cable == 0u);
    CHECK(ports.ports[1].embedded_jack_id == 1u);
}

/* Pin down the jack descriptor field order with values that cannot pass
 * under a swapped layout: per USB-MIDI 1.0 Tables 6-3 and 6-4, BOTH MIDI IN
 * and MIDI OUT jack descriptors place bJackType at offset 3 and bJackID at
 * offset 4. (The real capture is ambiguous for the IN jacks because their
 * type and ID values coincide: 1/1 and 2/2.) */
static void test_jack_field_layout(void)
{
    static const unsigned char buf[] = {
        /* Interface: MIDIStreaming */
        0x09u, 0x04u, 0x00u, 0x00u, 0x02u, 0x01u, 0x03u, 0x00u, 0x00u,
        /* CS_INTERFACE: MS header */
        0x07u, 0x24u, 0x01u, 0x00u, 0x01u, 0x2Eu, 0x00u,
        /* CS_INTERFACE: MIDI IN jack, type 1 (embedded), id 7 */
        0x06u, 0x24u, 0x02u, 0x01u, 0x07u, 0x00u,
        /* CS_INTERFACE: MIDI IN jack, type 2 (external), id 8 */
        0x06u, 0x24u, 0x02u, 0x02u, 0x08u, 0x00u,
        /* CS_INTERFACE: MIDI OUT jack, type 1 (embedded), id 9, src 8 */
        0x09u, 0x24u, 0x03u, 0x01u, 0x09u, 0x01u, 0x08u, 0x00u, 0x00u,
        /* CS_INTERFACE: MIDI OUT jack, type 2 (external), id 10, src 7 */
        0x09u, 0x24u, 0x03u, 0x02u, 0x0Au, 0x01u, 0x07u, 0x00u, 0x00u
    };
    um9_ms_info info;
    unsigned i;

    CHECK(um9_ms_parse(buf, sizeof(buf), &info) == 1);
    CHECK(info.num_in_jacks == 2u);
    CHECK(info.num_out_jacks == 2u);
    for (i = 0u; i < info.num_jacks; i++) {
        const um9_jack *jack = &info.jacks[i];

        if (jack->id == 7u) {
            CHECK(jack->type == UM9_JACK_EMBEDDED);
            CHECK(jack->direction == UM9_JACK_DIR_IN);
        } else if (jack->id == 8u) {
            CHECK(jack->type == UM9_JACK_EXTERNAL);
            CHECK(jack->direction == UM9_JACK_DIR_IN);
        } else if (jack->id == 9u) {
            CHECK(jack->type == UM9_JACK_EMBEDDED);
            CHECK(jack->direction == UM9_JACK_DIR_OUT);
            CHECK(jack->num_sources == 1u);
            CHECK(jack->source_ids[0] == 8u);
        } else if (jack->id == 10u) {
            CHECK(jack->type == UM9_JACK_EXTERNAL);
            CHECK(jack->direction == UM9_JACK_DIR_OUT);
            CHECK(jack->num_sources == 1u);
            CHECK(jack->source_ids[0] == 7u);
        } else {
            fail("unexpected jack id", __FILE__, __LINE__);
        }
    }
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
    test_jack_field_layout();
    test_real_keystation_fixture();
    return g_failures;
}

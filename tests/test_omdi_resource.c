/*
 * Host regression check for the 'OMdi' 128 resource payload
 * (oms/oms_driver.r) — the resource OMS reads to find the driver's
 * PPC code resource.
 *
 * Root cause this guards (real-G4 authenticated): the original typed
 * Rez template for 'OMdi' used `boolean` fields, which Rez packs as
 * BITS rather than the one-byte OMSBool members of OMSDriverParams.
 * The real G4 build emitted `7F 10 00 00 00 00 40 00 4C 0C 0C ...` —
 * the 0x40 at byte 6 made the OMS 2.3.8 loader read codeResID =
 * 0x4000, Get1Resource('PPCC', 0x4000) returned NULL, and the driver
 * failed to load with OMS error -192. The resource is now raw data
 * with the exact 16 bytes; this test:
 *   - forbids the typed `type 'OMdi'` template and typed
 *     `resource 'OMdi'` from returning;
 *   - decodes the `data 'OMdi' (128)` hex string and requires the
 *     exact canonical payload;
 *   - cross-checks the field offsets (xxportNumB at +6 = 1 must match
 *     the 'PPCC' 1 id imported by oms/ppcc.r).
 *
 * Runs from the repo root (`make test`); reads oms/oms_driver.r and
 * oms/ppcc.r relative to the working directory (same convention as
 * tests/test_descriptors.c's fixtures/keystation-49e.bin).
 */

#include <stdio.h>
#include <string.h>

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

/* The exact logical 'OMdi' 128 payload (OMSDriverParams, OMSDriver.h):
 * id 0x7F10, xxisSmart 0, hasMenuOrWindows 0, xxportNumM 0,
 * xxportNumB 1 (= the 'PPCC' codeResID), flags 0,
 * driverCompatibilityLevel 1, reservedFlags[6] 0. */
static const unsigned char kExpectedOMdi[16] = {
    0x7Fu, 0x10u,                             /* id */
    0x00u,                                    /* xxisSmart */
    0x00u,                                    /* hasMenuOrWindows */
    0x00u, 0x00u,                             /* xxportNumM */
    0x00u, 0x01u,                             /* xxportNumB / codeResID */
    0x00u,                                    /* flags */
    0x01u,                                    /* driverCompatibilityLevel */
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u  /* reservedFlags[6] */
};

static int contains(const char *line, const char *needle)
{
    return strstr(line, needle) != NULL;
}

/* Copy the hex digits between the first pair of double quotes in
 * `line` into `digits` (cap includes the NUL); returns the digit
 * count. Whitespace inside the quotes is skipped (Rez allows it). */
static size_t quoted_hex(const char *line, char *digits, size_t cap)
{
    const char *open;
    const char *p;
    size_t n;

    open = strchr(line, '"');
    if (open == NULL) {
        return 0u;
    }
    n = 0u;
    for (p = open + 1; *p != '\0' && *p != '"'; p++) {
        if (*p == ' ' || *p == '\t') {
            continue;
        }
        if (n + 1u < cap) {
            digits[n++] = *p;
        }
    }
    digits[n] = '\0';
    return n;
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Verify the 'OMdi' 128 raw-data resource in oms/oms_driver.r. */
static void test_omdi_payload(void)
{
    static const char path[] = "oms/oms_driver.r";
    static const char expected_hex[] = "7F100000000000010001000000000000";
    char line[256];
    char digits[80];
    unsigned char payload[16];
    FILE *f;
    int in_omdi;
    int saw_data;
    int saw_hex;
    int saw_type;
    int saw_resource;
    size_t n;
    unsigned i;

    f = fopen(path, "r");
    if (f == NULL) {
        fail("cannot open oms/oms_driver.r", __FILE__, __LINE__);
        return;
    }
    in_omdi = 0;
    saw_data = 0;
    saw_hex = 0;
    saw_type = 0;
    saw_resource = 0;
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        if (contains(line, "type 'OMdi'")) {
            saw_type = 1;
        }
        if (contains(line, "resource 'OMdi'")) {
            saw_resource = 1;
        }
        if (contains(line, "data 'OMdi'")) {
            saw_data = 1;
            in_omdi = 1;
        }
        if (in_omdi && strchr(line, '"') != NULL) {
            n = quoted_hex(line, digits, sizeof(digits));
            if (n > 0u) {
                CHECK(strcmp(digits, expected_hex) == 0);
                saw_hex = 1;
                in_omdi = 0;
            }
        }
    }
    fclose(f);

    /* The typed template must never return: its boolean fields pack
     * as bits and produced the wrong codeResID on the real G4. */
    CHECK(saw_type == 0);
    CHECK(saw_resource == 0);
    /* The raw data resource must exist with the exact hex payload. */
    CHECK(saw_data == 1);
    CHECK(saw_hex == 1);

    /* Decode the expected hex and check the field offsets against
     * OMSDriverParams. */
    if (strlen(expected_hex) == 32u) {
        for (i = 0u; i < 16u; i++) {
            int hi = hex_val(expected_hex[2u * i]);
            int lo = hex_val(expected_hex[2u * i + 1u]);
            payload[i] = (unsigned char)((hi << 4) | lo);
        }
        CHECK(memcmp(payload, kExpectedOMdi, 16u) == 0);
        /* The word at +6 is the 'PPCC' codeResID the OMS loader reads
         * (loadCode pref=2 = Get1Resource('PPCC', word at +6)); it
         * must be 1, not the 0x4000 the typed template produced. */
        CHECK(payload[6] == 0x00u);
        CHECK(payload[7] == 0x01u);
    } else {
        CHECK(0);
    }
}

/* The 'PPCC' 1 id imported by oms/ppcc.r must match xxportNumB = 1. */
static void test_ppcc_id_matches_code_res_id(void)
{
    static const char path[] = "oms/ppcc.r";
    char line[256];
    FILE *f;
    int saw_read;

    f = fopen(path, "r");
    if (f == NULL) {
        fail("cannot open oms/ppcc.r", __FILE__, __LINE__);
        return;
    }
    saw_read = 0;
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        const char *p = line;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '*' || *p == '/') {
            continue;               /* comment lines don't count */
        }
        if (contains(line, "read 'PPCC'") && contains(line, "(1)")) {
            saw_read = 1;
        }
    }
    fclose(f);
    CHECK(saw_read == 1);
}

int test_omdi_resource_run(void)
{
    g_failures = 0;
    test_omdi_payload();
    test_ppcc_id_matches_code_res_id();
    printf("test_omdi_resource: %s\n", g_failures ? "FAIL" : "OK");
    return g_failures;
}

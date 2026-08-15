/*
 * Host tests for the Classic driver ring buffer (classic/ring.h).
 *
 * Portable C (C89/C90). Covers the producer/consumer invariants:
 * byte order is preserved, no byte is ever invented or duplicated, the
 * capacity is respected, overflow drops only the newest bytes (drop-new),
 * and wraparound is seamless. A model-based randomized test drives
 * enqueue/dequeue against a simple FIFO model and compares every step.
 */

#include <stdio.h>

#include "classic/ring.h"

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

/* Simple deterministic PRNG (LCG) so failures are reproducible. */
static unsigned long g_seed = 0x12345678ul;

static unsigned long prng(void)
{
    g_seed = g_seed * 1103515245ul + 12345ul;
    return (g_seed >> 16) & 0x7ffful;
}

/* Fixed 256-byte model FIFO; the active capacity mirrors the ring size. */
#define MODEL_CAP 256u

static unsigned char g_model[MODEL_CAP];
static unsigned g_model_head;
static unsigned g_model_tail;
static unsigned g_model_cap;

static unsigned model_used(void)
{
    return g_model_tail - g_model_head;
}

static unsigned model_push(const unsigned char *data, unsigned count)
{
    unsigned i;
    unsigned space = g_model_cap - model_used();
    unsigned n = (count < space) ? count : space;
    for (i = 0u; i < n; i++) {
        g_model[(g_model_tail + i) & (MODEL_CAP - 1u)] = data[i];
    }
    g_model_tail += n;
    return n;
}

static unsigned model_pop(unsigned char *out, unsigned max)
{
    unsigned i;
    unsigned n = (max < model_used()) ? max : model_used();
    for (i = 0u; i < n; i++) {
        out[i] = g_model[(g_model_head + i) & (MODEL_CAP - 1u)];
    }
    g_model_head += n;
    return n;
}

static void test_empty_and_reset(void)
{
    unsigned char buf[8];
    unsigned char out[8];
    unsigned head = 0u;
    unsigned tail = 0u;

    um9_ring_reset(&head, &tail);
    CHECK(um9_ring_used(&head, &tail, 8u) == 0u);
    CHECK(um9_ring_dequeue(buf, 8u, &head, &tail, out, 4u) == 0u);
    CHECK(um9_ring_enqueue(buf, 8u, &head, &tail, "ab", 0u) == 0u);
    CHECK(um9_ring_used(&head, &tail, 8u) == 0u);
}

static void test_basic_order(void)
{
    static const unsigned char in[] = { 0x09u, 0x90u, 0x3Cu, 0x57u };
    unsigned char buf[8];
    unsigned char out[8];
    unsigned head = 0u;
    unsigned tail = 0u;
    unsigned i;

    CHECK(um9_ring_enqueue(buf, 8u, &head, &tail, in, 4u) == 4u);
    CHECK(um9_ring_used(&head, &tail, 8u) == 4u);
    CHECK(um9_ring_dequeue(buf, 8u, &head, &tail, out, 8u) == 4u);
    for (i = 0u; i < 4u; i++) {
        CHECK(out[i] == in[i]);
    }
    CHECK(um9_ring_used(&head, &tail, 8u) == 0u);
}

static void test_wraparound(void)
{
    unsigned char buf[8];
    unsigned char out[8];
    unsigned head = 0u;
    unsigned tail = 0u;
    unsigned i;

    /* Fill 6 bytes, drain 4: tail=6, head=4 -> next enqueue wraps. */
    CHECK(um9_ring_enqueue(buf, 8u, &head, &tail, "abcdef", 6u) == 6u);
    CHECK(um9_ring_dequeue(buf, 8u, &head, &tail, out, 4u) == 4u);
    CHECK(out[0] == 'a' && out[3] == 'd');

    /* 6 more bytes: 2 fit contiguously (6,7), 4 wrap to (0..3). */
    CHECK(um9_ring_enqueue(buf, 8u, &head, &tail, "ghijkl", 6u) == 6u);
    CHECK(um9_ring_used(&head, &tail, 8u) == 8u);

    /* Drain everything: exact original order, no duplication/loss. */
    CHECK(um9_ring_dequeue(buf, 8u, &head, &tail, out, 8u) == 8u);
    CHECK(out[0] == 'e' && out[1] == 'f' && out[2] == 'g' && out[3] == 'h');
    CHECK(out[4] == 'i' && out[5] == 'j' && out[6] == 'k' && out[7] == 'l');
    CHECK(um9_ring_used(&head, &tail, 8u) == 0u);

    /* After a full wrap the ring still works from a clean state. */
    CHECK(um9_ring_enqueue(buf, 8u, &head, &tail, "xy", 2u) == 2u);
    CHECK(um9_ring_dequeue(buf, 8u, &head, &tail, out, 2u) == 2u);
    CHECK(out[0] == 'x' && out[1] == 'y');

    /* Repeat with a fresh ring for the boundary of exactly one wrap. */
    um9_ring_reset(&head, &tail);
    for (i = 0u; i < 8u; i++) {
        CHECK(um9_ring_enqueue(buf, 8u, &head, &tail, &i, 1u) == 1u);
    }
    for (i = 0u; i < 8u; i++) {
        unsigned char b = 0xffu;
        CHECK(um9_ring_dequeue(buf, 8u, &head, &tail, &b, 1u) == 1u);
        CHECK(b == (unsigned char)i);
    }
}

static void test_overflow_drops_new(void)
{
    unsigned char buf[8];
    unsigned char out[8];
    unsigned head = 0u;
    unsigned tail = 0u;
    unsigned i;

    CHECK(um9_ring_enqueue(buf, 8u, &head, &tail, "01234567", 8u) == 8u);
    /* Full: nothing more is stored, unread data is untouched. */
    CHECK(um9_ring_enqueue(buf, 8u, &head, &tail, "abc", 3u) == 0u);
    CHECK(um9_ring_used(&head, &tail, 8u) == 8u);
    /* Partially full: only the free space is filled. */
    CHECK(um9_ring_dequeue(buf, 8u, &head, &tail, out, 3u) == 3u);
    CHECK(um9_ring_enqueue(buf, 8u, &head, &tail, "xyz", 3u) == 3u);
    CHECK(um9_ring_dequeue(buf, 8u, &head, &tail, out, 8u) == 8u);
    for (i = 0u; i < 8u; i++) {
        CHECK(out[i] == (unsigned char)("34567xyz"[i]));
    }
}

static void test_partial_dequeue_and_bad_args(void)
{
    unsigned char buf[8];
    unsigned char out[8];
    unsigned head = 0u;
    unsigned tail = 0u;

    CHECK(um9_ring_enqueue(buf, 8u, &head, &tail, "hello", 5u) == 5u);
    CHECK(um9_ring_dequeue(buf, 8u, &head, &tail, out, 2u) == 2u);
    CHECK(out[0] == 'h' && out[1] == 'e');
    CHECK(um9_ring_dequeue(buf, 8u, &head, &tail, out, 9u) == 3u);
    CHECK(out[0] == 'l' && out[1] == 'l' && out[2] == 'o');
    CHECK(um9_ring_used(&head, &tail, 8u) == 0u);

    /* Non-power-of-two capacity is rejected everywhere. */
    CHECK(um9_ring_used(&head, &tail, 7u) == 0u);
    CHECK(um9_ring_enqueue(buf, 7u, &head, &tail, "ab", 2u) == 0u);
    CHECK(um9_ring_dequeue(buf, 7u, &head, &tail, out, 2u) == 0u);

    /* Null buffer/out are rejected. */
    CHECK(um9_ring_enqueue(0, 8u, &head, &tail, "ab", 2u) == 0u);
    CHECK(um9_ring_dequeue(0, 8u, &head, &tail, out, 2u) == 0u);
    CHECK(um9_ring_enqueue(buf, 8u, &head, &tail, 0, 2u) == 0u);
    CHECK(um9_ring_dequeue(buf, 8u, &head, &tail, 0, 2u) == 0u);
}

/* Counter wraparound: the ring counters are unbounded 32-bit counters;
 * after ~4G bytes they wrap mod 2^32. Seed head/tail near the boundary
 * (as a long-lived ring would be) and verify the arithmetic still holds:
 * used is the mod-2^32 difference and slots are (counter & mask). */
static void test_counter_wraparound(void)
{
    unsigned char buf[8];
    unsigned char out[16];
    unsigned head = 0xFFFFFFF8u;    /* 8 bytes before the 2^32 wrap */
    unsigned tail = 0xFFFFFFF8u;
    unsigned i;

    CHECK(um9_ring_used(&head, &tail, 8u) == 0u);
    /* 12 bytes: tail wraps past 2^32 to 4. */
    CHECK(um9_ring_enqueue(buf, 8u, &head, &tail, "01234567abcd", 12u) == 8u);
    CHECK(um9_ring_used(&head, &tail, 8u) == 8u);
    /* Drain 5, then 3: head wraps to 1, then 4. */
    CHECK(um9_ring_dequeue(buf, 8u, &head, &tail, out, 5u) == 5u);
    for (i = 0u; i < 5u; i++) {
        CHECK(out[i] == (unsigned char)("01234567abcd"[i]));
    }
    CHECK(um9_ring_used(&head, &tail, 8u) == 3u);
    CHECK(um9_ring_dequeue(buf, 8u, &head, &tail, out, 8u) == 3u);
    CHECK(out[0] == '5' && out[1] == '6' && out[2] == '7');
    CHECK(um9_ring_used(&head, &tail, 8u) == 0u);
    /* Ring remains fully usable after the wrap. */
    CHECK(um9_ring_enqueue(buf, 8u, &head, &tail, "xy", 2u) == 2u);
    CHECK(um9_ring_dequeue(buf, 8u, &head, &tail, out, 2u) == 2u);
    CHECK(out[0] == 'x' && out[1] == 'y');
}

/* Model-based fuzz: random enqueue/dequeue against a plain FIFO model;
 * the ring must agree with the model on every step (bytes, counts, and
 * the used count). Exercises wraparound and drop-new across sizes. */
static void test_randomized_model(void)
{
    static const unsigned sizes[] = { 2u, 4u, 8u, 16u, 64u, 256u };
    unsigned si;

    for (si = 0u; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
        unsigned char buf[256];
        unsigned char in[16];
        unsigned char out[16];
        unsigned char mout[16];
        unsigned head = 0u;
        unsigned tail = 0u;
        unsigned long iter;

        g_model_head = 0u;
        g_model_tail = 0u;
        g_model_cap = sizes[si];
        um9_ring_reset(&head, &tail);

        for (iter = 0u; iter < 20000ul; iter++) {
            unsigned long op = prng() % 3u;
            if (op == 0u || op == 1u) {          /* enqueue */
                unsigned count = (unsigned)(prng() % 13u);
                unsigned i;
                unsigned want;
                unsigned got;
                for (i = 0u; i < count; i++) {
                    in[i] = (unsigned char)prng();
                }
                want = model_push(in, count);
                got = um9_ring_enqueue(buf, sizes[si], &head, &tail, in, count);
                if (got != want) {
                    printf("FAIL fuzz size=%u iter=%lu enqueue got=%u want=%u\n",
                           sizes[si], iter, got, want);
                    g_failures++;
                    return;
                }
            } else {                             /* dequeue */
                unsigned max = (unsigned)(prng() % 13u);
                unsigned want;
                unsigned got;
                unsigned i;
                want = model_pop(mout, max);
                got = um9_ring_dequeue(buf, sizes[si], &head, &tail, out, max);
                if (got != want) {
                    printf("FAIL fuzz size=%u iter=%lu dequeue got=%u want=%u\n",
                           sizes[si], iter, got, want);
                    g_failures++;
                    return;
                }
                for (i = 0u; i < got; i++) {
                    if (out[i] != mout[i]) {
                        printf("FAIL fuzz size=%u iter=%lu byte %u: got %02X want %02X\n",
                               sizes[si], iter, i,
                               (unsigned)out[i], (unsigned)mout[i]);
                        g_failures++;
                        return;
                    }
                }
            }
            if (um9_ring_used(&head, &tail, sizes[si]) != model_used()) {
                printf("FAIL fuzz size=%u iter=%lu used=%u want=%u\n",
                       sizes[si], iter,
                       um9_ring_used(&head, &tail, sizes[si]), model_used());
                g_failures++;
                return;
            }
        }
    }
}

int test_ring_run(void)
{
    test_empty_and_reset();
    test_basic_order();
    test_wraparound();
    test_overflow_drops_new();
    test_partial_dequeue_and_bad_args();
    test_counter_wraparound();
    test_randomized_model();
    return g_failures;
}

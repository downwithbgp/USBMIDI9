/*
 * See ring.h. Fixed-capacity single-producer/single-consumer byte ring.
 *
 * Index discipline: head is the next byte to dequeue, tail is the next
 * byte to enqueue. Both are monotonically increasing counters; the
 * physical slot is (counter & (size - 1)). The used count is the plain
 * difference (tail - head), which never exceeds size because enqueue
 * drops new bytes when full. size must be a power of two.
 *
 * All ring state that is shared between the producer and the consumer
 * (both indices and the data bytes) is accessed through volatile
 * pointers: the producer's data stores precede its tail store in program
 * order and are visible to the consumer that reads tail before reading
 * data, on the single-CPU Classic target.
 */

#include "classic/ring.h"

static unsigned is_power_of_two(unsigned n)
{
    return n != 0u && (n & (n - 1u)) == 0u;
}

void um9_ring_reset(unsigned volatile *head, unsigned volatile *tail)
{
    *head = 0u;
    *tail = 0u;
}

unsigned um9_ring_used(const unsigned volatile *head,
                       const unsigned volatile *tail, unsigned size)
{
    if (!is_power_of_two(size)) {
        return 0u;
    }
    return *tail - *head;
}

unsigned um9_ring_enqueue(unsigned char *buf, unsigned size,
                          unsigned volatile *head, unsigned volatile *tail,
                          const void *data, unsigned count)
{
    unsigned volatile char *ring = (unsigned volatile char *)buf;
    const unsigned char *src = (const unsigned char *)data;
    unsigned mask;
    unsigned used;
    unsigned n;
    unsigned tail_off;
    unsigned free1;
    unsigned i;

    if (!is_power_of_two(size) || buf == 0 || data == 0 || count == 0u) {
        return 0u;
    }

    mask = size - 1u;
    used = *tail - *head;
    n = (count < size - used) ? count : (size - used);
    if (n == 0u) {
        return 0u;              /* full: drop new bytes, keep unread data */
    }

    tail_off = *tail & mask;
    free1 = size - tail_off;    /* contiguous space to the end of the ring */
    if (n <= free1) {
        for (i = 0u; i < n; i++) {
            ring[tail_off + i] = src[i];
        }
    } else {
        for (i = 0u; i < free1; i++) {
            ring[tail_off + i] = src[i];
        }
        for (i = 0u; i < n - free1; i++) {
            ring[i] = src[free1 + i];
        }
    }

    *tail = *tail + n;          /* publish: data stores above precede this */
    return n;
}

unsigned um9_ring_dequeue(unsigned char *buf, unsigned size,
                          unsigned volatile *head, unsigned volatile *tail,
                          void *out, unsigned max)
{
    unsigned volatile char *ring = (unsigned volatile char *)buf;
    unsigned char *dst = (unsigned char *)out;
    unsigned mask;
    unsigned used;
    unsigned n;
    unsigned head_off;
    unsigned avail1;
    unsigned i;

    if (!is_power_of_two(size) || buf == 0 || out == 0 || max == 0u) {
        return 0u;
    }

    mask = size - 1u;
    used = *tail - *head;
    n = (max < used) ? max : used;
    if (n == 0u) {
        return 0u;
    }

    head_off = *head & mask;
    avail1 = size - head_off;   /* contiguous data to the end of the ring */
    if (n <= avail1) {
        for (i = 0u; i < n; i++) {
            dst[i] = ring[head_off + i];
        }
    } else {
        for (i = 0u; i < avail1; i++) {
            dst[i] = ring[head_off + i];
        }
        for (i = 0u; i < n - avail1; i++) {
            dst[avail1 + i] = ring[i];
        }
    }

    *head = *head + n;          /* publish: data reads above precede this */
    return n;
}

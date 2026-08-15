/*
 * USBMIDI9 fixed-capacity byte ring for the Classic driver.
 *
 * Single-producer / single-consumer ring: the driver's read completion
 * routine (secondary interrupt level or system task level) enqueues raw
 * received bytes; the probe dequeues them at task level through the
 * USBMIDI9 dispatch table. No locking: the two sides touch disjoint state
 * (producer: tail + data; consumer: head + data), and all shared accesses
 * are volatile so the compiler cannot reorder them across the
 * producer-consumer boundary.
 *
 * Capacity is fixed and must be a power of two (indices are masked).
 * When the ring is full, incoming bytes are dropped (drop-new): unread
 * data is never overwritten, and the enqueue return value reports how
 * many bytes were actually stored.
 *
 * Portable C (C89/C90); no dependency on Classic Mac OS types. The
 * Classic driver casts its UInt32/volatile fields to the plain `unsigned`
 * parameters here (identical size on both targets).
 */

#ifndef USBMIDI9_CLASSIC_RING_H
#define USBMIDI9_CLASSIC_RING_H

/* Reset both indices to empty. Safe from either side. */
void um9_ring_reset(unsigned volatile *head, unsigned volatile *tail);

/* Number of bytes currently stored (0 if size is not a power of two). */
unsigned um9_ring_used(const unsigned volatile *head,
                       const unsigned volatile *tail, unsigned size);

/* Copy count bytes from data into the ring. Returns the number of bytes
 * stored: min(count, free space). When full, nothing is stored and 0 is
 * returned. Producer side (completion routine). */
unsigned um9_ring_enqueue(unsigned char *buf, unsigned size,
                          unsigned volatile *head, unsigned volatile *tail,
                          const void *data, unsigned count);

/* Copy up to max bytes out of the ring into out. Returns the number of
 * bytes copied (0 when empty). Consumer side (probe). */
unsigned um9_ring_dequeue(unsigned char *buf, unsigned size,
                          unsigned volatile *head, unsigned volatile *tail,
                          void *out, unsigned max);

#endif /* USBMIDI9_CLASSIC_RING_H */

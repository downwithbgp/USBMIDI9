/*
 * See descriptors.h for the module contract.
 */

#include <stddef.h>

#include "descriptors.h"

void um9_desc_iter_init(um9_desc_iter *it, const unsigned char *buf,
                        unsigned len)
{
    if (it == NULL) {
        return;
    }
    it->buf = buf;
    it->len = len;
    it->pos = 0u;
    it->off = 0u;
    it->blen = 0u;
    it->btype = 0u;
    it->err = UM9_DESC_ERR_NONE;
}

int um9_desc_iter_next(um9_desc_iter *it)
{
    unsigned blen;

    if (it == NULL || it->err != UM9_DESC_ERR_NONE) {
        return 0;
    }
    if (it->buf == NULL) {
        /* A NULL buffer is only valid with len 0; anything else is
         * truncated input. */
        if (it->pos < it->len) {
            it->err = UM9_DESC_ERR_TRUNCATED;
        }
        return 0;
    }
    if (it->pos >= it->len) {
        return 0;   /* clean end of buffer */
    }
    if (it->len - it->pos < 2u) {
        /* Not even room for bLength + bDescriptorType. */
        it->err = UM9_DESC_ERR_TRUNCATED;
        return 0;
    }

    blen = (unsigned)it->buf[it->pos];
    if (blen < 2u) {
        /* Zero-length descriptor (bLength 0 or 1). */
        it->err = UM9_DESC_ERR_ZERO_LEN;
        return 0;
    }
    if (blen > it->len - it->pos) {
        /* bLength runs past the end of the buffer. */
        it->err = UM9_DESC_ERR_TRUNCATED;
        return 0;
    }

    it->off = it->pos;
    it->blen = blen;
    it->btype = (unsigned)it->buf[it->pos + 1u];
    it->pos += blen;
    return 1;
}

unsigned um9_desc_u8(const um9_desc_iter *it, unsigned rel_off)
{
    if (it == NULL || it->buf == NULL) {
        return 0u;
    }
    if (rel_off >= it->blen) {
        return 0u;
    }
    return (unsigned)it->buf[it->off + rel_off];
}

unsigned um9_desc_u16le(const um9_desc_iter *it, unsigned rel_off)
{
    if (it == NULL || it->buf == NULL) {
        return 0u;
    }
    /* rel_off < blen <= 255 after the first check, so rel_off + 1 cannot
     * wrap. */
    if (rel_off >= it->blen || rel_off + 1u >= it->blen) {
        return 0u;
    }
    return (unsigned)it->buf[it->off + rel_off]
         | ((unsigned)it->buf[it->off + rel_off + 1u] << 8);
}

/*
 * USBMIDI9 portable core: safe USB descriptor walking.
 *
 * USB descriptors are untrusted byte streams (buffer + explicit length).
 * This module provides a bounds-checked walker over an arbitrary descriptor
 * buffer and typed, little-endian field readers. It never casts buffers to
 * structs, never dereferences beyond the supplied buffer, rejects zero-length
 * and truncated descriptors, and is endian-neutral (USB multi-byte fields are
 * little-endian; hosts may be big-endian, e.g. PowerPC).
 *
 * Portable C (C89/C90); no dependency on Classic Mac OS, OMS, or FreeMIDI.
 */

#ifndef USBMIDI9_CORE_DESCRIPTORS_H
#define USBMIDI9_CORE_DESCRIPTORS_H

/* Standard descriptor types (USB 1.1/2.0, Chapter 9). */
#define UM9_DT_DEVICE       0x01u
#define UM9_DT_CONFIG       0x02u
#define UM9_DT_INTERFACE    0x04u
#define UM9_DT_ENDPOINT     0x05u

/* Class-specific descriptor types (USB Audio/MIDI). */
#define UM9_DT_CS_INTERFACE 0x24u
#define UM9_DT_CS_ENDPOINT  0x25u

/* Audio class / MIDIStreaming subclass (USB Audio 1.0 / USB-MIDI 1.0). */
#define UM9_AUDIO_CLASS           0x01u
#define UM9_MIDISTREAMING_SUBCLASS 0x03u

/* Class-specific interface descriptor subtypes (USB-MIDI 1.0). */
#define UM9_MS_HEADER         0x01u
#define UM9_MS_MIDI_IN_JACK   0x02u
#define UM9_MS_MIDI_OUT_JACK  0x03u

/* Class-specific endpoint descriptor subtypes (USB-MIDI 1.0). */
#define UM9_MS_GENERAL        0x01u

/* MIDI jack types. */
#define UM9_JACK_EMBEDDED     0x01u
#define UM9_JACK_EXTERNAL     0x02u

/* Endpoint direction mask in bEndpointAddress. */
#define UM9_EP_DIR_IN         0x80u

/* Walker error codes. */
#define UM9_DESC_ERR_NONE      0u  /* clean end of buffer */
#define UM9_DESC_ERR_TRUNCATED 1u  /* bLength exceeds the remaining bytes */
#define UM9_DESC_ERR_ZERO_LEN  2u  /* bLength < 2 */

/* Bounds-checked walker over a descriptor buffer. */
typedef struct um9_desc_iter {
    const unsigned char *buf;  /* buffer (may be NULL only with len 0) */
    unsigned len;              /* total buffer length in bytes */
    unsigned pos;              /* scan position: start of the next descriptor */
    unsigned off;              /* offset of the current descriptor */
    unsigned blen;             /* bLength of the current descriptor */
    unsigned btype;            /* bDescriptorType of the current descriptor */
    unsigned err;              /* UM9_DESC_ERR_* once walking stops */
} um9_desc_iter;

/* Initialize a walker. */
void um9_desc_iter_init(um9_desc_iter *it, const unsigned char *buf,
                        unsigned len);

/* Advance to the next descriptor. Returns 1 while a valid descriptor is
 * available; its fields are in it->off/it->blen/it->btype. Returns 0 at the
 * clean end of the buffer (err == UM9_DESC_ERR_NONE) or on malformed input
 * (err set to UM9_DESC_ERR_TRUNCATED or UM9_DESC_ERR_ZERO_LEN). */
int um9_desc_iter_next(um9_desc_iter *it);

/* Read one byte at a relative offset inside the current descriptor.
 * Returns 0 if the offset is out of range. */
unsigned um9_desc_u8(const um9_desc_iter *it, unsigned rel_off);

/* Read a little-endian 16-bit field at a relative offset inside the current
 * descriptor. Returns 0 if the two bytes are not both in range. */
unsigned um9_desc_u16le(const um9_desc_iter *it, unsigned rel_off);

#endif /* USBMIDI9_CORE_DESCRIPTORS_H */

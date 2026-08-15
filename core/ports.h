/*
 * USBMIDI9 portable core: MIDIStreaming topology and logical ports.
 *
 * Parses an arbitrary USB descriptor buffer, locates the first interface
 * with bInterfaceClass = 0x01 (Audio) and bInterfaceSubClass = 0x03
 * (MIDIStreaming), and summarizes its topology: the MS header, MIDI IN/OUT
 * jacks, class-specific endpoint associations (which embedded jack feeds
 * which endpoint/cable), and the derived logical ports.
 *
 * Logical port model (USB-MIDI 1.0): each embedded jack associated with a
 * bulk endpoint is one logical port. The cable number in an Event Packet
 * selects the embedded jack (cable N = Nth embedded jack of the endpoint).
 * A bulk IN endpoint yields host input ports (device-to-host); a bulk OUT
 * endpoint yields host output ports (host-to-device).
 *
 * Limits are fixed and documented; parsing stops (without corruption) when a
 * limit is reached. Portable C (C89/C90); no dependency on Classic Mac OS,
 * OMS, or FreeMIDI.
 *
 * TODO: multiple MIDIStreaming interfaces (parse all of them, not just the
 * first one found).
 */

#ifndef USBMIDI9_CORE_PORTS_H
#define USBMIDI9_CORE_PORTS_H

/* Fixed parse limits. Descriptor counts are one-byte fields; these caps only
 * bound memory use and do not reflect any device requirement. */
#define UM9_MAX_JACKS           16u
#define UM9_MAX_JACK_SOURCES     8u
#define UM9_MAX_ENDPOINTS        8u
#define UM9_MAX_EMBEDDED_JACKS  16u
#define UM9_MAX_PORTS           32u

/* Jack direction. */
#define UM9_JACK_DIR_IN   0u   /* MIDI IN jack (data toward the device) */
#define UM9_JACK_DIR_OUT  1u   /* MIDI OUT jack (data from the device) */

/* Logical port direction (host perspective). */
#define UM9_PORT_DIR_IN   0u   /* host receives (bulk IN endpoint) */
#define UM9_PORT_DIR_OUT  1u   /* host sends (bulk OUT endpoint) */

/* A MIDI IN/OUT jack from a class-specific interface descriptor. */
typedef struct um9_jack {
    unsigned valid;             /* 1 once populated */
    unsigned id;                /* bJackID */
    unsigned type;              /* UM9_JACK_EMBEDDED or UM9_JACK_EXTERNAL */
    unsigned direction;         /* UM9_JACK_DIR_* */
    unsigned num_sources;       /* MIDI OUT jacks only: bNrInputPins */
    unsigned source_ids[UM9_MAX_JACK_SOURCES];  /* baSourceID list */
} um9_jack;

/* A bulk endpoint with its embedded jack associations (CS_ENDPOINT). */
typedef struct um9_endpoint {
    unsigned valid;             /* 1 once populated */
    unsigned address;           /* bEndpointAddress */
    unsigned attributes;        /* bmAttributes */
    unsigned max_packet_size;   /* wMaxPacketSize */
    unsigned num_embedded;      /* bNumEmbMIDIJack */
    unsigned embedded_ids[UM9_MAX_EMBEDDED_JACKS];  /* baAssocJackID list */
} um9_endpoint;

/* Summary of the first MIDIStreaming interface found in a buffer. */
typedef struct um9_ms_info {
    unsigned found;             /* 1 if a MIDIStreaming interface was found */
    unsigned vid;               /* idVendor of the first device descriptor in the buffer (0 if none) */
    unsigned pid;               /* idProduct, likewise */
    unsigned interface_number;  /* bInterfaceNumber */
    unsigned alternate_setting; /* bAlternateSetting */
    unsigned interface_class;   /* bInterfaceClass of the found interface */
    unsigned interface_subclass; /* bInterfaceSubClass of the found interface */
    unsigned num_endpoints_declared;  /* bNumEndpoints */
    unsigned bcd_msc;           /* MS header bcdMSC */
    unsigned wtotal_length;     /* MS header wTotalLength */
    unsigned num_jacks;
    um9_jack jacks[UM9_MAX_JACKS];
    unsigned num_in_jacks;      /* convenience counters */
    unsigned num_out_jacks;
    unsigned num_endpoints;
    um9_endpoint endpoints[UM9_MAX_ENDPOINTS];
} um9_ms_info;

/* A logical MIDI port (one embedded jack on one endpoint = one cable). */
typedef struct um9_port {
    unsigned valid;             /* 1 once populated */
    unsigned endpoint_address;  /* bEndpointAddress of the endpoint */
    unsigned direction;         /* UM9_PORT_DIR_* (host perspective) */
    unsigned cable;             /* cable number (0-based) */
    unsigned embedded_jack_id;  /* embedded jack for this cable */
} um9_port;

/* Derived logical ports. */
typedef struct um9_ports {
    unsigned count;
    um9_port ports[UM9_MAX_PORTS];
} um9_ports;

/* Walk a descriptor buffer and summarize the first MIDIStreaming interface.
 *
 * Returns 1 on success, including when no MIDIStreaming interface exists
 * (check out->found). Returns 0 if out is NULL or the buffer is malformed
 * (zero-length or truncated descriptor, per descriptors.h). */
int um9_ms_parse(const unsigned char *buf, unsigned len, um9_ms_info *out);

/* Derive logical ports from a parsed MIDIStreaming interface. */
int um9_ports_from_ms(const um9_ms_info *info, um9_ports *out);

/* Convenience: um9_ms_parse + um9_ports_from_ms. Returns 0 on malformed
 * input or NULL out; otherwise 1 (ports.count may be 0). */
int um9_ports_parse(const unsigned char *buf, unsigned len, um9_ports *out);

#endif /* USBMIDI9_CORE_PORTS_H */

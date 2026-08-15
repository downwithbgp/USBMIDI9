/*
 * See ports.h for the module contract.
 */

#include <stddef.h>
#include <string.h>

#include "descriptors.h"
#include "ports.h"

static um9_jack *um9_ms_add_jack(um9_ms_info *out, unsigned direction)
{
    um9_jack *jack;

    if (out->num_jacks >= UM9_MAX_JACKS) {
        return NULL;    /* limit reached: stop recording, keep parsing */
    }
    jack = &out->jacks[out->num_jacks];
    out->num_jacks++;
    jack->valid = 1;
    jack->id = 0u;
    jack->type = 0u;
    jack->direction = direction;
    jack->num_sources = 0u;
    if (direction == UM9_JACK_DIR_IN) {
        out->num_in_jacks++;
    } else {
        out->num_out_jacks++;
    }
    return jack;
}

static um9_endpoint *um9_ms_add_endpoint(um9_ms_info *out, unsigned address,
                                         unsigned attributes)
{
    um9_endpoint *ep;

    if (out->num_endpoints >= UM9_MAX_ENDPOINTS) {
        return NULL;    /* limit reached: stop recording, keep parsing */
    }
    ep = &out->endpoints[out->num_endpoints];
    out->num_endpoints++;
    ep->valid = 1;
    ep->address = address;
    ep->attributes = attributes;
    ep->num_embedded = 0u;
    return ep;
}

int um9_ms_parse(const unsigned char *buf, unsigned len, um9_ms_info *out)
{
    um9_desc_iter it;
    int in_ms;
    unsigned cur_ep_address;
    unsigned cur_ep_attributes;

    if (out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof(*out));

    in_ms = 0;
    cur_ep_address = 0u;
    cur_ep_attributes = 0u;

    um9_desc_iter_init(&it, buf, len);
    while (um9_desc_iter_next(&it)) {
        if (!in_ms) {
            if (it.btype == UM9_DT_INTERFACE && it.blen >= 9u
                && um9_desc_u8(&it, 5u) == UM9_AUDIO_CLASS
                && um9_desc_u8(&it, 6u) == UM9_MIDISTREAMING_SUBCLASS) {
                in_ms = 1;
                out->found = 1;
                out->interface_number = um9_desc_u8(&it, 2u);
                out->alternate_setting = um9_desc_u8(&it, 3u);
                out->num_endpoints_declared = um9_desc_u8(&it, 4u);
            }
            continue;
        }

        /* Inside the MIDIStreaming descriptor set: stop at the next
         * interface (or config/device) descriptor or at end of buffer. */
        if (it.btype == UM9_DT_INTERFACE || it.btype == UM9_DT_CONFIG
            || it.btype == UM9_DT_DEVICE) {
            break;
        }

        if (it.btype == UM9_DT_ENDPOINT && it.blen >= 7u) {
            cur_ep_address = um9_desc_u8(&it, 2u);
            cur_ep_attributes = um9_desc_u8(&it, 3u);
            continue;
        }

        if (it.btype == UM9_DT_CS_INTERFACE) {
            unsigned subtype = um9_desc_u8(&it, 2u);

            if (subtype == UM9_MS_HEADER && it.blen >= 7u) {
                out->bcd_msc = um9_desc_u16le(&it, 3u);
                out->wtotal_length = um9_desc_u16le(&it, 5u);
            } else if (subtype == UM9_MS_MIDI_IN_JACK && it.blen >= 6u) {
                um9_jack *jack = um9_ms_add_jack(out, UM9_JACK_DIR_IN);
                if (jack != NULL) {
                    jack->id = um9_desc_u8(&it, 3u);
                    jack->type = um9_desc_u8(&it, 4u);
                }
            } else if (subtype == UM9_MS_MIDI_OUT_JACK && it.blen >= 9u) {
                unsigned npins = um9_desc_u8(&it, 5u);
                unsigned i;
                um9_jack *jack = um9_ms_add_jack(out, UM9_JACK_DIR_OUT);
                if (jack != NULL) {
                    jack->id = um9_desc_u8(&it, 3u);
                    jack->type = um9_desc_u8(&it, 4u);
                    if (npins > UM9_MAX_JACK_SOURCES) {
                        npins = UM9_MAX_JACK_SOURCES;
                    }
                    if (npins > it.blen - 7u) {
                        npins = it.blen - 7u;   /* baSourceID region only */
                    }
                    jack->num_sources = npins;
                    for (i = 0u; i < npins; i++) {
                        jack->source_ids[i] = um9_desc_u8(&it, 6u + i);
                    }
                }
            }
            continue;
        }

        if (it.btype == UM9_DT_CS_ENDPOINT) {
            unsigned subtype = um9_desc_u8(&it, 2u);

            if (subtype == UM9_MS_GENERAL && it.blen >= 5u
                && cur_ep_address != 0u) {
                unsigned n = um9_desc_u8(&it, 3u);
                unsigned i;
                um9_endpoint *ep = um9_ms_add_endpoint(out, cur_ep_address,
                                                       cur_ep_attributes);
                if (ep != NULL) {
                    if (n > UM9_MAX_EMBEDDED_JACKS) {
                        n = UM9_MAX_EMBEDDED_JACKS;
                    }
                    if (n > it.blen - 4u) {
                        n = it.blen - 4u;   /* baAssocJackID region only */
                    }
                    ep->num_embedded = n;
                    for (i = 0u; i < n; i++) {
                        ep->embedded_ids[i] = um9_desc_u8(&it, 4u + i);
                    }
                }
            }
            continue;
        }
    }

    return (it.err == UM9_DESC_ERR_NONE) ? 1 : 0;
}

int um9_ports_from_ms(const um9_ms_info *info, um9_ports *out)
{
    unsigned e;
    unsigned i;

    if (info == NULL || out == NULL) {
        return 0;
    }
    out->count = 0u;
    for (e = 0u; e < info->num_endpoints; e++) {
        const um9_endpoint *ep = &info->endpoints[e];
        unsigned dir = (ep->address & UM9_EP_DIR_IN) ? UM9_PORT_DIR_IN
                                                     : UM9_PORT_DIR_OUT;

        for (i = 0u; i < ep->num_embedded; i++) {
            um9_port *port;

            if (out->count >= UM9_MAX_PORTS) {
                return 1;   /* cap reached; count reflects what fit */
            }
            port = &out->ports[out->count];
            out->count++;
            port->valid = 1;
            port->endpoint_address = ep->address;
            port->direction = dir;
            port->cable = i;
            port->embedded_jack_id = ep->embedded_ids[i];
        }
    }
    return 1;
}

int um9_ports_parse(const unsigned char *buf, unsigned len, um9_ports *out)
{
    um9_ms_info info;

    if (out == NULL) {
        return 0;
    }
    if (!um9_ms_parse(buf, len, &info)) {
        return 0;
    }
    return um9_ports_from_ms(&info, out);
}

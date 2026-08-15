/*
 * See packets.h for the module contract.
 */

#include <stddef.h>

#include "packets.h"

/* MIDI data byte count per CIN (USB-MIDI 1.0, Table 4-1). */
static const unsigned char um9_cin_lengths[16] = {
    0, 1, 2, 3, 3, 1, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3
};

unsigned um9_cin_data_length(unsigned cin)
{
    if (cin > 15u) {
        return 0u;
    }
    return (unsigned)um9_cin_lengths[cin];
}

int um9_packet_decode(const unsigned char *buf, unsigned len, um9_packet *out)
{
    unsigned byte0;

    if (buf == NULL || out == NULL || len != UM9_PACKET_SIZE) {
        return 0;
    }

    byte0 = (unsigned)buf[0];
    out->cable = (byte0 >> 4) & 0x0Fu;
    out->cin = byte0 & 0x0Fu;
    out->length = um9_cin_data_length(out->cin);
    out->data[0] = buf[1];
    out->data[1] = buf[2];
    out->data[2] = buf[3];
    return 1;
}

/* CIN for a MIDI status byte and the message length that status requires.
 * Returns -1 when the status has no fixed, supported mapping (SysEx
 * 0xF0/0xF7 and the undefined statuses 0xF4/0xF5). */
static int um9_cin_for_status(unsigned status, unsigned *need)
{
    if (status >= 0x80u && status <= 0xEFu) {
        unsigned cin = (status >> 4) + 1u;   /* 0x9..0xF */
        *need = (cin == UM9_CIN_PROGRAM_CHANGE || cin == UM9_CIN_CHANNEL_PRESSURE)
                    ? 2u : 3u;
        return (int)cin;
    }
    switch (status) {
    case 0xF1u:  /* MIDI Time Code */
    case 0xF3u:  /* Song Select */
        *need = 2u;
        return (int)UM9_CIN_SYS_COMMON_2;
    case 0xF2u:  /* Song Position Pointer */
        *need = 3u;
        return (int)UM9_CIN_SYS_COMMON_3;
    case 0xF6u:  /* Tune Request */
    case 0xF8u:  /* Timing Clock */
    case 0xF9u:
    case 0xFAu:  /* Start */
    case 0xFBu:  /* Continue */
    case 0xFCu:  /* Stop */
    case 0xFDu:
    case 0xFEu:  /* Active Sensing */
    case 0xFFu:  /* System Reset */
        *need = 1u;
        return (int)UM9_CIN_SYS_COMMON_1;
    default:
        return -1;
    }
}

int um9_packet_encode(unsigned cable, const unsigned char *msg, unsigned len,
                      um9_packet *out)
{
    int cin;
    unsigned need;
    unsigned i;

    if (cable > UM9_MAX_CABLE || msg == NULL || out == NULL) {
        return 0;
    }
    if (len < 1u || len > 3u) {
        return 0;
    }

    cin = um9_cin_for_status((unsigned)msg[0], &need);
    if (cin < 0 || len != need) {
        return 0;
    }

    out->cable = cable;
    out->cin = (unsigned)cin;
    out->length = len;
    for (i = 0u; i < 3u; i++) {
        out->data[i] = (i < len) ? msg[i] : 0u;
    }
    return 1;
}

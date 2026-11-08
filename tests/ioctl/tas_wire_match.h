/* tas_wire_match.h - recognises this run's probe frames in a capture from another NIC (#328).
 * Pure: unit tested offline by TC-MOCK-013 in tests/unit/test_tas_restore_contract.c. */
#ifndef TAS_WIRE_MATCH_H
#define TAS_WIRE_MATCH_H

#include <string.h>

#define TAS_WIRE_UDP_PORT   9
#define TAS_WIRE_TAG_BYTES  16
static const char g_tas_wire_magic[8] = {'T', 'A', 'S', 'W', 'I', 'R', 'E', '1'};

/** Writes the tag at the start of a probe payload of payload_bytes, at least TAS_WIRE_TAG_BYTES */
static void tas_wire_fill(unsigned char *payload, size_t payload_bytes, unsigned nonce, unsigned seq)
{
    memset(payload, 0, payload_bytes);
    memcpy(payload, g_tas_wire_magic, sizeof(g_tas_wire_magic));
    memcpy(payload + 8, &nonce, sizeof(nonce));
    memcpy(payload + 12, &seq, sizeof(seq));
}

/** TRUE for an unfragmented IPv4 UDP broadcast from mac to the discard port carrying this run's tag */
static int tas_wire_match(const unsigned char *f, size_t len, const unsigned char *mac, unsigned nonce, unsigned *seq)
{
    static const unsigned char broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    size_t ip = 14, udp, tag;

    if (len < ip + 20 || memcmp(f, broadcast, 6) != 0 || memcmp(f + 6, mac, 6) != 0 ||
        f[12] != 0x08 || f[13] != 0x00 || (f[ip] >> 4) != 4 || f[ip + 9] != 17 ||
        (((f[ip + 6] & 0x1F) << 8) | f[ip + 7]) != 0) {
        return 0;
    }
    if ((f[ip] & 0x0F) < 5) {
        return 0;
    }
    udp = ip + (size_t)(f[ip] & 0x0F) * 4;
    tag = udp + 8;
    if (len < tag + TAS_WIRE_TAG_BYTES || f[udp + 2] != 0 || f[udp + 3] != TAS_WIRE_UDP_PORT ||
        memcmp(f + tag, g_tas_wire_magic, sizeof(g_tas_wire_magic)) != 0 || memcmp(f + tag + 8, &nonce, sizeof(nonce)) != 0) {
        return 0;
    }
    memcpy(seq, f + tag + 12, sizeof(*seq));
    return 1;
}

#endif /* TAS_WIRE_MATCH_H */

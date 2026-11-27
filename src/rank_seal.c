/*
 * rank_seal.c -- (OpenSWOS-Indoor) the sealed messages of the ranking desk (include/rank_proto.h),
 * shared by the game (SDL builds with netplay) and the fork's matchmaking server, which compiles
 * this file too (third_party/matchmaker/CMakeLists.txt, RANK_SERVER).
 */
#if defined(SWOS_NETPLAY) || defined(RANK_SERVER)

#include "rank_proto.h"
#include "../third_party/monocypher/monocypher.h"

#include <stdio.h>
#include <string.h>

static const uint8_t k_answerAd[] = "SWRK-ANSWER";

int32_t Rank_Random(uint8_t *out, size_t len)
{
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got;
    if (!f)
        return 0;
    got = fread(out, 1, len, f);
    fclose(f);
    return got == len;
}

static void session_key(const uint8_t shared[32], const uint8_t eph_public[32],
                        const uint8_t server_public[32], uint8_t key[32])
{
    uint8_t buf[96];
    memcpy(buf, shared, 32);
    memcpy(buf + 32, eph_public, 32);
    memcpy(buf + 64, server_public, 32);
    crypto_blake2b(key, 32, buf, sizeof buf);
    crypto_wipe(buf, sizeof buf);
}

void Rank_PublicKey(const uint8_t secret[RANK_KEY_BYTES], uint8_t public_key[RANK_KEY_BYTES])
{
    crypto_x25519_public_key(public_key, secret);
}

int32_t Rank_SealRequest(const uint8_t server_public[RANK_KEY_BYTES], const char *plain, size_t len,
                         uint8_t *out, size_t cap, uint8_t key[RANK_KEY_BYTES])
{
    uint8_t eph_secret[32], shared[32];
    uint8_t *p = out;

    if (len > RANK_REQUEST_MAX || cap < RANK_HEAD_BYTES + len + RANK_MAC_BYTES)
        return -1;
    if (!Rank_Random(eph_secret, sizeof eph_secret))
        return -1;
    memcpy(p, RANK_MAGIC, 4);
    p[4] = RANK_VERSION;
    crypto_x25519_public_key(p + 5, eph_secret);
    if (!Rank_Random(p + 5 + 32, RANK_NONCE_BYTES)) {
        crypto_wipe(eph_secret, sizeof eph_secret);
        return -1;
    }
    p[5 + 32 + 24]     = (uint8_t)(len >> 8);
    p[5 + 32 + 24 + 1] = (uint8_t)len;
    crypto_x25519(shared, eph_secret, server_public);
    session_key(shared, p + 5, server_public, key);
    crypto_wipe(eph_secret, sizeof eph_secret);
    crypto_wipe(shared, sizeof shared);
    crypto_aead_lock(p + RANK_HEAD_BYTES, p + RANK_HEAD_BYTES + len, key, p + 5 + 32,
                     p, 5, (const uint8_t *)plain, len);
    return (int32_t)(RANK_HEAD_BYTES + len + RANK_MAC_BYTES);
}

int32_t Rank_RequestSize(const uint8_t *in, size_t len)
{
    size_t body;
    if (len >= 4 && memcmp(in, RANK_MAGIC, 4) != 0)
        return -1;
    if (len >= 5 && in[4] != RANK_VERSION)
        return -1;
    if (len < RANK_HEAD_BYTES)
        return 0;
    body = ((size_t)in[RANK_HEAD_BYTES - 2] << 8) | in[RANK_HEAD_BYTES - 1];
    if (body > RANK_REQUEST_MAX)
        return -1;
    return (int32_t)(RANK_HEAD_BYTES + body + RANK_MAC_BYTES);
}

int32_t Rank_OpenRequest(const uint8_t server_secret[RANK_KEY_BYTES], const uint8_t *in, size_t len,
                         char *plain, size_t cap, uint8_t key[RANK_KEY_BYTES])
{
    uint8_t server_public[32], shared[32];
    const int32_t size = Rank_RequestSize(in, len);
    size_t body;

    if (size <= 0 || (size_t)size != len)
        return -1;
    body = len - RANK_HEAD_BYTES - RANK_MAC_BYTES;
    if (cap < body + 1)
        return -1;
    crypto_x25519_public_key(server_public, server_secret);
    crypto_x25519(shared, server_secret, in + 5);
    session_key(shared, in + 5, server_public, key);
    crypto_wipe(shared, sizeof shared);
    if (crypto_aead_unlock((uint8_t *)plain, in + RANK_HEAD_BYTES + body, key, in + 5 + 32,
                           in, 5, in + RANK_HEAD_BYTES, body) != 0) {
        crypto_wipe(key, 32);
        return -1;
    }
    plain[body] = 0;
    return (int32_t)body;
}

int32_t Rank_SealAnswer(const uint8_t key[RANK_KEY_BYTES], const char *plain, size_t len,
                        uint8_t *out, size_t cap)
{
    if (len > RANK_ANSWER_MAX || len > 0xFFFF || cap < RANK_NONCE_BYTES + 2 + len + RANK_MAC_BYTES)
        return -1;
    if (!Rank_Random(out, RANK_NONCE_BYTES))
        return -1;
    out[RANK_NONCE_BYTES]     = (uint8_t)(len >> 8);
    out[RANK_NONCE_BYTES + 1] = (uint8_t)len;
    crypto_aead_lock(out + RANK_NONCE_BYTES + 2, out + RANK_NONCE_BYTES + 2 + len, key, out,
                     k_answerAd, sizeof k_answerAd - 1, (const uint8_t *)plain, len);
    return (int32_t)(RANK_NONCE_BYTES + 2 + len + RANK_MAC_BYTES);
}

int32_t Rank_OpenAnswer(const uint8_t key[RANK_KEY_BYTES], const uint8_t *in, size_t len,
                        char *plain, size_t cap)
{
    size_t body;
    if (len < RANK_NONCE_BYTES + 2 + RANK_MAC_BYTES)
        return -1;
    body = ((size_t)in[RANK_NONCE_BYTES] << 8) | in[RANK_NONCE_BYTES + 1];
    if (len != RANK_NONCE_BYTES + 2 + body + RANK_MAC_BYTES || cap < body + 1)
        return -1;
    if (crypto_aead_unlock((uint8_t *)plain, in + RANK_NONCE_BYTES + 2 + body, key, in,
                           k_answerAd, sizeof k_answerAd - 1, in + RANK_NONCE_BYTES + 2, body) != 0)
        return -1;
    plain[body] = 0;
    return (int32_t)body;
}

int32_t Rank_FromHex(const char *hex, uint8_t out[RANK_KEY_BYTES])
{
    int32_t i;
    if (!hex || strlen(hex) < 2 * RANK_KEY_BYTES)
        return 0;
    for (i = 0; i < 2 * RANK_KEY_BYTES; i++) {
        const char c = hex[i];
        const int32_t v = (c >= '0' && c <= '9') ? c - '0' :
                          (c >= 'a' && c <= 'f') ? c - 'a' + 10 :
                          (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (v < 0)
            return 0;
        if (i & 1)
            out[i / 2] = (uint8_t)(out[i / 2] | v);
        else
            out[i / 2] = (uint8_t)(v << 4);
    }
    return 1;
}

void Rank_ToHex(const uint8_t in[RANK_KEY_BYTES], char out[2 * RANK_KEY_BYTES + 1])
{
    static const char digits[] = "0123456789abcdef";
    int32_t i;
    for (i = 0; i < RANK_KEY_BYTES; i++) {
        out[2 * i]     = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 15];
    }
    out[2 * RANK_KEY_BYTES] = 0;
}

int32_t Rank_SelfTest(void)
{
    static const char req[] = "auth\nusername=SPIELER1\npassword=geheim\n";
    static const char ans[] = "200\n{\"status\":\"ok\",\"api_key\":\"abc\"}";
    uint8_t secret[32], pub[32], other[32], other_pub[32], ckey[32], skey[32];
    uint8_t wire[RANK_HEAD_BYTES + RANK_REQUEST_MAX + RANK_MAC_BYTES];
    uint8_t back[RANK_NONCE_BYTES + 2 + 256 + RANK_MAC_BYTES];
    char plain[RANK_REQUEST_MAX + 1];
    int32_t n, m, fails = 0;

    if (!Rank_Random(secret, 32) || !Rank_Random(other, 32))
        return 1;
    Rank_PublicKey(secret, pub);
    Rank_PublicKey(other, other_pub);
    n = Rank_SealRequest(pub, req, sizeof req - 1, wire, sizeof wire, ckey);
    if (n <= 0 || Rank_RequestSize(wire, (size_t)n) != n || Rank_RequestSize(wire, 10) != 0)
        fails |= 2;
    if (Rank_OpenRequest(secret, wire, (size_t)n, plain, sizeof plain, skey) != (int32_t)sizeof req - 1 ||
        strcmp(plain, req) != 0 || memcmp(ckey, skey, 32) != 0)
        fails |= 4;
    if (Rank_OpenRequest(other, wire, (size_t)n, plain, sizeof plain, skey) >= 0)
        fails |= 8;                                     /* another server's key */
    wire[RANK_HEAD_BYTES + 3] ^= 1;
    if (Rank_OpenRequest(secret, wire, (size_t)n, plain, sizeof plain, skey) >= 0)
        fails |= 16;                                    /* a changed byte */
    wire[RANK_HEAD_BYTES + 3] ^= 1;
    m = Rank_SealAnswer(ckey, ans, sizeof ans - 1, back, sizeof back);
    if (m <= 0 || Rank_OpenAnswer(ckey, back, (size_t)m, plain, sizeof plain) != (int32_t)sizeof ans - 1 ||
        strcmp(plain, ans) != 0)
        fails |= 32;
    if (m > 0) {
        back[m - 1] ^= 0x80;
        if (Rank_OpenAnswer(ckey, back, (size_t)m, plain, sizeof plain) >= 0)
            fails |= 64;                                /* a changed mac */
        back[m - 1] ^= 0x80;
        if (Rank_OpenAnswer(other_pub, back, (size_t)m, plain, sizeof plain) >= 0)
            fails |= 128;                               /* another key */
    }
    crypto_wipe(secret, sizeof secret);
    crypto_wipe(other, sizeof other);
    return fails;
}

#endif

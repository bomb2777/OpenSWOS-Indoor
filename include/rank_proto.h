/*
 * rank_proto.h -- (OpenSWOS-Indoor) the ranking desk: the game's account and ranking requests
 * to the fork's matchmaking server, which forwards them over HTTPS to SWOS United's API
 * (swos.swosunited.com, the OOO rankings) and sends the answer back. docs/INDOOR.md, "Ranked".
 *
 * Why through the server: the game speaks no HTTPS. Why sealed: the request carries the
 * player's password (log-in) or API key (everything else), and the link to the server is
 * plain TCP. Each request is sealed to the SERVER's public key (RANK_SERVER_PUBLIC_KEY,
 * include/project_identity.h) with a key pair made for it alone, so only that server can read
 * it, and the answer comes back under the same key:
 *
 *   shared = X25519(ephemeral secret, server public)      (the server: its secret, eph. public)
 *   key    = BLAKE2b-256(shared || ephemeral public || server public)
 *   AEAD   = XChaCha20-Poly1305 (Monocypher crypto_aead_lock), a random 24-byte nonce each way
 *
 * One TCP connection to the server's port (the TcpGate dispatches on "SWRK"), one request,
 * one answer, closed:
 *
 *   request: "SWRK" | version u8 (RANK_VERSION) | ephemeral public [32] | nonce [24]
 *            | length u16 BE | cipher text [length] | mac [16]
 *            additional data: the first 5 bytes ("SWRK" + version)
 *   answer:  nonce [24] | length u16 BE | cipher text [length] | mac [16]
 *            additional data: "SWRK-ANSWER"
 *
 * The plain text of a request: the operation on the first line, then one "name=value" line a
 * field (no newline in a value):
 *
 *   auth          username, password
 *   create        api_key, opponent_username            (ranking_id: the SERVER's, --rank-ranking)
 *   report        api_key, leg_id, score_own, score_opponent
 *                 [extra_time_own, extra_time_opponent] [penalties_own, penalties_opponent]
 *   add-leg       api_key, match_id
 *   close         api_key, match_id
 *   open-matches  api_key
 *
 * The plain text of an answer: the HTTP status on the first line, then the API's JSON body.
 * Status 0 with a body {"status":"desk_error","reason":...}: the desk itself refused or could
 * not reach the API (busy, too many requests, a bad request, the API down).
 */
#ifndef RANK_PROTO_H
#define RANK_PROTO_H

#include <stdint.h>
#include <stddef.h>

#define RANK_MAGIC          "SWRK"
#define RANK_VERSION        1
#define RANK_KEY_BYTES      32
#define RANK_NONCE_BYTES    24
#define RANK_MAC_BYTES      16
#define RANK_HEAD_BYTES     (4 + 1 + RANK_KEY_BYTES + RANK_NONCE_BYTES + 2)    /* 63 */
#define RANK_REQUEST_MAX    1024        /* plain text of a request */
#define RANK_ANSWER_MAX     16384       /* plain text of an answer (the API's JSON, cut there) */

#ifdef __cplusplus
extern "C" {
#endif

/* Random bytes from the system (/dev/urandom); 0 when it could not. */
int32_t Rank_Random(uint8_t *out, size_t len);

/* The client's side. Seal: writes the whole request into out (cap at least RANK_HEAD_BYTES +
 * len + RANK_MAC_BYTES), keeps the session key in key[32]; its size, or -1. Open: an answer
 * (all of it) into plain (cap at least its length + 1, NUL-terminated); its length, or -1 when
 * it is not one from the holder of that key. */
int32_t Rank_SealRequest(const uint8_t server_public[RANK_KEY_BYTES], const char *plain, size_t len,
                         uint8_t *out, size_t cap, uint8_t key[RANK_KEY_BYTES]);
int32_t Rank_OpenAnswer(const uint8_t key[RANK_KEY_BYTES], const uint8_t *in, size_t len,
                        char *plain, size_t cap);

/* The server's side. Need: how many bytes a request that begins with these `len` bytes has in
 * all (0: not enough yet to say; -1: not a request). Open: the request into plain (NUL-
 * terminated), the session key into key[32]; its length, or -1. Seal: the answer into out (cap
 * at least RANK_NONCE_BYTES + 2 + len + RANK_MAC_BYTES); its size, or -1. */
int32_t Rank_RequestSize(const uint8_t *in, size_t len);
int32_t Rank_OpenRequest(const uint8_t server_secret[RANK_KEY_BYTES], const uint8_t *in, size_t len,
                         char *plain, size_t cap, uint8_t key[RANK_KEY_BYTES]);
int32_t Rank_SealAnswer(const uint8_t key[RANK_KEY_BYTES], const char *plain, size_t len,
                        uint8_t *out, size_t cap);

/* A key pair: the public half of a secret. */
void    Rank_PublicKey(const uint8_t secret[RANK_KEY_BYTES], uint8_t public_key[RANK_KEY_BYTES]);
/* Hex (64 characters) <-> 32 bytes; 0 when the text is not that. */
int32_t Rank_FromHex(const char *hex, uint8_t out[RANK_KEY_BYTES]);
void    Rank_ToHex(const uint8_t in[RANK_KEY_BYTES], char out[2 * RANK_KEY_BYTES + 1]);

/* The round trips, tampering refused: 0 when all is well. --rank-selftest. */
int32_t Rank_SelfTest(void);

#ifdef __cplusplus
}
#endif

#endif /* RANK_PROTO_H */

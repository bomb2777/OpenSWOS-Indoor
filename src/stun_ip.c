/* stun_ip.c -- (OpenSWOS-Indoor) this machine's public IPv4 address, for the direct-IP
 * menu's YOUR IP row (src/p2p_menu.c): a host behind a router does not know the address
 * its friend must type, so ask a public STUN server -- one UDP Binding Request (RFC 5389),
 * whose answer's XOR-MAPPED-ADDRESS is where the request came from as the server saw it.
 *
 * Nothing but the 20-byte request is sent, from a socket of its own (not the game's link:
 * the port the router maps for it says nothing about the game's, which the host forwards
 * anyway). On a thread, so the menu never waits: each server gets STUN_WAIT_MS, then the
 * next. SDL builds with netplay only (the browser has no UDP).
 */
#include "stun_ip.h"

#if defined(SWOS_SDL_BUILD) && defined(SWOS_NETPLAY) && !defined(__EMSCRIPTEN__)

#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

enum { STUN_WAIT_MS = 1500 };
#define STUN_MAGIC 0x2112A442u

static const struct { const char *host; const char *port; } k_servers[] = {
    { "stun.l.google.com",   "19302" },
    { "stun1.l.google.com",  "19302" },
    { "stun.cloudflare.com", "3478"  },
};

static SDL_atomic_t s_state;            /* StunIpState */
static char         s_address[16];      /* written before s_state becomes KNOWN */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

/* One server: 1 and the address in `out`, or 0. */
static int32_t ask(const char *host, const char *port, char *out, uint32_t cap)
{
    struct addrinfo hints, *res = NULL;
    uint8_t req[20], ans[512];
    struct pollfd pfd;
    uint32_t start;
    int fd, ok = 0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;              /* the address a friend types is IPv4 */
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res)
        return 0;
    fd = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        freeaddrinfo(res);
        return 0;
    }
    /* Binding Request: type 0x0001, no attributes, the magic cookie, a transaction id. */
    memset(req, 0, sizeof req);
    req[1] = 0x01;
    req[4] = 0x21; req[5] = 0x12; req[6] = 0xA4; req[7] = 0x42;
    {
        uint32_t seed = SDL_GetTicks() ^ (uint32_t)(uintptr_t)&seed ^ 0x9E3779B9u;
        for (int i = 8; i < 20; i++) {
            seed = seed * 1103515245u + 12345u;
            req[i] = (uint8_t)(seed >> 16);
        }
    }
    if (sendto(fd, (const char *)req, sizeof req, 0, res->ai_addr, res->ai_addrlen) == (ssize_t)sizeof req) {
        start = SDL_GetTicks();
        for (;;) {
            int32_t left = STUN_WAIT_MS - (int32_t)(SDL_GetTicks() - start);
            ssize_t n;
            if (left <= 0)
                break;
            pfd.fd = fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            if (poll(&pfd, 1, left) <= 0)
                break;
            n = recv(fd, (char *)ans, sizeof ans, 0);
            /* A Binding Success Response to this request: type 0x0101, our cookie and id. */
            if (n < 20 || rd16(ans) != 0x0101 || rd32(ans + 4) != STUN_MAGIC ||
                memcmp(ans + 8, req + 8, 12) != 0)
                continue;
            {
                uint32_t len = rd16(ans + 2), at = 20, end = 20 + len;
                if (end > (uint32_t)n)
                    end = (uint32_t)n;
                while (at + 4 <= end) {
                    uint16_t type = rd16(ans + at), alen = rd16(ans + at + 2);
                    const uint8_t *v = ans + at + 4;
                    if (at + 4 + alen > end)
                        break;
                    /* XOR-MAPPED-ADDRESS (0x0020), or the old MAPPED-ADDRESS (0x0001);
                     * family 1 = IPv4: reserved, family, port, address. */
                    if ((type == 0x0020 || type == 0x0001) && alen >= 8 && v[1] == 0x01) {
                        uint32_t ip = rd32(v + 4) ^ (type == 0x0020 ? STUN_MAGIC : 0);
                        snprintf(out, cap, "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)(ip >> 16 & 255),
                                 (unsigned)(ip >> 8 & 255), (unsigned)(ip & 255));
                        ok = 1;
                        if (type == 0x0020)
                            break;          /* the XOR one wins over the old one */
                    }
                    at += 4 + ((alen + 3u) & ~3u);
                }
            }
            if (ok)
                break;
        }
    }
    close(fd);
    freeaddrinfo(res);
    return ok;
}

static int SDLCALL ask_thread(void *unused)
{
    char address[16];
    (void)unused;
    for (uint32_t i = 0; i < sizeof k_servers / sizeof k_servers[0]; i++)
        if (ask(k_servers[i].host, k_servers[i].port, address, sizeof address)) {
            memcpy(s_address, address, sizeof s_address);
            fprintf(stderr, "[stun] this machine's public address: %s (asked %s)\n", s_address, k_servers[i].host);
            SDL_AtomicSet(&s_state, STUN_IP_KNOWN);
            return 0;
        }
    fprintf(stderr, "[stun] no STUN server answered: the public address is unknown\n");
    SDL_AtomicSet(&s_state, STUN_IP_FAILED);
    return 0;
}

void StunIp_Request(void)
{
    SDL_Thread *t;
    int s = SDL_AtomicGet(&s_state);
    if (s == STUN_IP_ASKING || s == STUN_IP_KNOWN)
        return;
    SDL_AtomicSet(&s_state, STUN_IP_ASKING);
    t = SDL_CreateThread(ask_thread, "stun", NULL);
    if (!t)
        SDL_AtomicSet(&s_state, STUN_IP_FAILED);
    else
        SDL_DetachThread(t);
}

StunIpState StunIp_State(void)
{
    return (StunIpState)SDL_AtomicGet(&s_state);
}

const char *StunIp_Address(void)
{
    return SDL_AtomicGet(&s_state) == STUN_IP_KNOWN ? s_address : "";
}

#else

void        StunIp_Request(void) {}
StunIpState StunIp_State(void)   { return STUN_IP_UNAVAILABLE; }
const char *StunIp_Address(void) { return ""; }

#endif

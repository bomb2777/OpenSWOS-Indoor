/* stun_ip.h -- (OpenSWOS-Indoor) this machine's public IPv4 address, as the internet sees
 * it: asked of a public STUN server (src/stun_ip.c), for the direct-IP menu's YOUR IP row.
 * SDL builds with netplay; elsewhere it always answers STUN_IP_UNAVAILABLE. */
#ifndef STUN_IP_H
#define STUN_IP_H

#include "swos_types.h"

typedef enum {
    STUN_IP_NONE,           /* not asked yet */
    STUN_IP_ASKING,         /* a thread is asking */
    STUN_IP_KNOWN,          /* StunIp_Address() has it */
    STUN_IP_FAILED,         /* no server answered (offline, UDP blocked) */
    STUN_IP_UNAVAILABLE     /* this build cannot ask */
} StunIpState;

/* Ask, on a thread of its own, unless an answer is known or a question is out. A failed
 * question is asked again. Never blocks. */
void        StunIp_Request(void);
StunIpState StunIp_State(void);
/* "a.b.c.d" once STUN_IP_KNOWN, else "". */
const char *StunIp_Address(void);

#endif

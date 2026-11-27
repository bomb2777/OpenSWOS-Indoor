/*
 * indoor_ranking.c -- (OpenSWOS-Indoor) the fork's online provider: log-in to SWOS United's site
 * and its OOO Indoor ranking (docs/INDOOR.md, "Ranked"). SDL builds with netplay, not the web
 * build (a browser has no TCP to the server).
 *
 * It fills in the account hooks every OpenSWOS build has (include/online_account.h, registered
 * through ModApi_SetOnlineProvider by Account_Init):
 *   - LOGIN asks USERNAME + PASSWORD (a swosunited.com account, required): the password goes to
 *     the site once (auth/key), sealed through the fork's server, and only the API key it gives
 *     is kept -- in memory, for this run;
 *   - OPEN offers FRIENDLY or RANKED MATCH (SWOS 2020's openLobbyMenu). RANKED counts in the OOO
 *     Indoor ranking and is played in the hall only: the host's PITCH TYPE must be LINOLEUM;
 *   - the HOST's START waits for the site's match (create; add-leg for each rematch), whose ids
 *     travel to the guest in the lobby's rules;
 *   - after each match both games report their result, from their own side;
 *   - leaving the lobby the host closes the session (the site scores its legs every 5 minutes).
 *
 * The requests go one at a time through a thread of their own (include/rank_proto.h: sealed to
 * the server's key, RANK_SERVER_PUBLIC_KEY, one TCP connection each); their answers are handled
 * on the menus' thread, from the hooks the menus call each frame.
 *
 * Tests: SWOS_RANK_KEY (the server's public key, hex) and SWOS_MM_HOST/PORT point it at a test
 * server (scripts/fake_ooo_api.py behind it); SWOS_OOO_USER + SWOS_OOO_PASSWORD log in at the
 * lobby (--netplay-lobby has no log-in screen), SWOS_OOO_RANKED=1 makes the host's game ranked.
 */
#if defined(SWOS_NETPLAY) && !defined(__EMSCRIPTEN__)

#include "indoor_ranking.h"
#include "rank_proto.h"
#include "mod_api.h"
#include "online_account.h"
#include "netplay_lobby.h"
#include "mm_client.h"
#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "swos2020_menus.h"
#include "project_identity.h"
#include "mjson.h"

#include <SDL.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* ======================================================================
 * The requests: a queue, one thread, the answers back on the menus' thread
 * ====================================================================== */
enum { K_AUTH, K_OPEN_MATCHES, K_CREATE, K_ADD_LEG, K_REPORT, K_CLOSE };
static const char *const k_kindName[] = { "auth", "open-matches", "create", "add-leg", "report", "close" };

typedef struct {
    int32_t  kind;
    uint32_t matchId, legId;            /* what it was about (for the answer) */
    char     plain[RANK_REQUEST_MAX];
    int32_t  http;                      /* the answer: 0 the desk's or the network's error */
    char     answer[RANK_ANSWER_MAX];
    char     error[80];                 /* the network's error (http 0, no answer at all) */
} RankRequest;

#define QUEUE_MAX 8
static SDL_mutex    *s_lock;
static SDL_cond     *s_wake;
static SDL_Thread   *s_thread;
static RankRequest  *s_todo[QUEUE_MAX], *s_done[QUEUE_MAX];
static int32_t       s_todoCount, s_doneCount, s_busy;

static int wait_for(int fd, short events, int32_t ms)
{
    struct pollfd p;
    int r;
    p.fd = fd;
    p.events = events;
    p.revents = 0;
    do
        r = poll(&p, 1, ms);
    while (r < 0 && errno == EINTR);
    return r == 1 ? 0 : -1;
}

static int32_t server_key(uint8_t key[RANK_KEY_BYTES])
{
    const char *env = getenv("SWOS_RANK_KEY");
    return Rank_FromHex(env && env[0] ? env : RANK_SERVER_PUBLIC_KEY, key);
}

/* One request to the desk, blocking (the thread's). */
static void exchange(RankRequest *r)
{
    uint8_t pub[RANK_KEY_BYTES], key[RANK_KEY_BYTES];
    static uint8_t wire[RANK_HEAD_BYTES + RANK_REQUEST_MAX + RANK_MAC_BYTES];
    static uint8_t in[RANK_NONCE_BYTES + 2 + RANK_ANSWER_MAX + RANK_MAC_BYTES];
    struct addrinfo hints, *res = NULL;
    char portText[8];
    int32_t n, got = 0, need = 0;
    int fd, err = 0;
    socklen_t el = sizeof err;

    r->http = 0;
    r->answer[0] = 0;
    if (!server_key(pub)) {
        snprintf(r->error, sizeof r->error, "NO RANKING SERVER KEY IN THIS BUILD");
        return;
    }
    n = Rank_SealRequest(pub, r->plain, strlen(r->plain), wire, sizeof wire, key);
    memset(r->plain, 0, sizeof r->plain);       /* (a password, a key) */
    if (n <= 0) {
        snprintf(r->error, sizeof r->error, "CANNOT SEAL THE REQUEST");
        return;
    }
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portText, sizeof portText, "%u", (unsigned)MmClient_DefaultPort());
    if (getaddrinfo(MmClient_DefaultHost(), portText, &hints, &res) != 0 || !res) {
        snprintf(r->error, sizeof r->error, "CANNOT FIND THE SERVER");
        return;
    }
    fd = (int)socket(res->ai_family, SOCK_STREAM, 0);
    if (fd < 0) {
        freeaddrinfo(res);
        snprintf(r->error, sizeof r->error, "NO SOCKET");
        return;
    }
#ifdef SO_NOSIGPIPE
    {
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    }
#endif
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    if ((connect(fd, res->ai_addr, res->ai_addrlen) != 0 && errno != EINPROGRESS) ||
        wait_for(fd, POLLOUT, 8000) || getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &el) != 0 || err) {
        freeaddrinfo(res);
        close(fd);
        snprintf(r->error, sizeof r->error, "CANNOT REACH THE SERVER");
        return;
    }
    freeaddrinfo(res);
    for (int32_t done = 0; done < n;) {
        ssize_t k = send(fd, (const char *)wire + done, (size_t)(n - done), MSG_NOSIGNAL);
        if (k > 0)
            done += (int32_t)k;
        else if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) && !wait_for(fd, POLLOUT, 8000))
            continue;
        else {
            close(fd);
            snprintf(r->error, sizeof r->error, "THE CONNECTION BROKE");
            return;
        }
    }
    /* The answer: its length is in its head; the desk closes after it. */
    for (;;) {
        ssize_t k;
        if (need && got >= need)
            break;
        if (wait_for(fd, POLLIN, 25000)) {
            close(fd);
            snprintf(r->error, sizeof r->error, "NO ANSWER FROM THE SERVER");
            return;
        }
        k = recv(fd, (char *)in + got, sizeof in - (size_t)got, 0);
        if (k > 0)
            got += (int32_t)k;
        else if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            continue;
        else
            break;
        if (!need && got >= RANK_NONCE_BYTES + 2)
            need = RANK_NONCE_BYTES + 2 + ((in[RANK_NONCE_BYTES] << 8) | in[RANK_NONCE_BYTES + 1]) + RANK_MAC_BYTES;
    }
    close(fd);
    if (!need || got != need) {
        /* Closed without an answer: the server has no ranking desk, or not this key. */
        snprintf(r->error, sizeof r->error, "THE SERVER TAKES NO RANKING REQUESTS");
        return;
    }
    {
        static char plain[RANK_ANSWER_MAX + 1];
        const char *body;
        if (Rank_OpenAnswer(key, in, (size_t)got, plain, sizeof plain) < 0) {
            snprintf(r->error, sizeof r->error, "A BAD ANSWER FROM THE SERVER");
            return;
        }
        r->http = atoi(plain);
        body = strchr(plain, '\n');
        snprintf(r->answer, sizeof r->answer, "%s", body ? body + 1 : "");
        if (r->http == 0)
            snprintf(r->error, sizeof r->error, "THE RANKING SERVER COULD NOT DO IT");
    }
}

static int worker(void *unused)
{
    (void)unused;
    SDL_LockMutex(s_lock);
    for (;;) {
        RankRequest *r;
        while (!s_todoCount)
            SDL_CondWait(s_wake, s_lock);
        r = s_todo[0];
        memmove(s_todo, s_todo + 1, sizeof s_todo[0] * (size_t)(--s_todoCount));
        s_busy = 1;
        SDL_UnlockMutex(s_lock);
        exchange(r);
        SDL_LockMutex(s_lock);
        s_busy = 0;
        if (s_doneCount < QUEUE_MAX)
            s_done[s_doneCount++] = r;
        else
            free(r);
        SDL_CondBroadcast(s_wake);
    }
    return 0;
}

static int32_t queue_request(int32_t kind, uint32_t matchId, uint32_t legId, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
static int32_t queue_request(int32_t kind, uint32_t matchId, uint32_t legId, const char *fmt, ...)
{
    RankRequest *r;
    va_list ap;
    if (!s_lock) {
        s_lock = SDL_CreateMutex();
        s_wake = SDL_CreateCond();
        s_thread = SDL_CreateThread(worker, "ranking", NULL);
        if (s_thread)
            SDL_DetachThread(s_thread);
    }
    r = (RankRequest *)calloc(1, sizeof *r);
    if (!r)
        return 0;
    r->kind = kind;
    r->matchId = matchId;
    r->legId = legId;
    va_start(ap, fmt);
    vsnprintf(r->plain, sizeof r->plain, fmt, ap);
    va_end(ap);
    SDL_LockMutex(s_lock);
    if (s_todoCount >= QUEUE_MAX) {
        SDL_UnlockMutex(s_lock);
        free(r);
        return 0;
    }
    s_todo[s_todoCount++] = r;
    SDL_CondBroadcast(s_wake);
    SDL_UnlockMutex(s_lock);
    fprintf(stderr, "[ooo] %s ...\n", k_kindName[kind]);
    return 1;
}

static RankRequest *next_answer(void)
{
    RankRequest *r = NULL;
    if (!s_lock)
        return NULL;
    SDL_LockMutex(s_lock);
    if (s_doneCount) {
        r = s_done[0];
        memmove(s_done, s_done + 1, sizeof s_done[0] * (size_t)(--s_doneCount));
    }
    SDL_UnlockMutex(s_lock);
    return r;
}

/* Until every queued request has been answered, at most `ms`. */
static void drain(uint32_t ms)
{
    const uint32_t until = SDL_GetTicks() + ms;
    if (!s_lock)
        return;
    SDL_LockMutex(s_lock);
    while ((s_todoCount || s_busy) && (int32_t)(until - SDL_GetTicks()) > 0)
        SDL_CondWaitTimeout(s_wake, s_lock, 50);
    SDL_UnlockMutex(s_lock);
}

/* ======================================================================
 * The account and the lobby
 * ====================================================================== */
static char     s_user[64];
static char     s_apiKey[160];
static int32_t  s_login;                 /* 0 idle, 1 asking, 2 good, -1 refused */
static char     s_error[80];

static int32_t  s_inLobby, s_host;
static char     s_peer[LOBBY_NAME_MAX + 1];
static int32_t  s_asking;                /* a create / add-leg under way */
static uint32_t s_matchId;               /* the site's session (the host's; the guest's from the rules) */
static uint32_t s_usedLeg;               /* the leg the last match was reported on */
static int32_t  s_friendlyNow;           /* RANKED was given up for this lobby (told once) */
static int32_t  s_toldHall;              /* "LINOLEUM ONLY" said for the current pitch */
static char     s_matchText[48];
static int32_t  s_choice;                /* openLobbyMenu's answer */

static void notice(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void notice(const char *fmt, ...)
{
    char line[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fprintf(stderr, "[ooo] %s\n", line);
    if (s_inLobby)
        NetplayLobby_ChatNotice(line);
}

static void json_status(const RankRequest *r, char *out, int32_t cap)
{
    out[0] = 0;
    if (r->answer[0])
        mjson_get_string(r->answer, (int)strlen(r->answer), "$.status", out, cap);
}

static uint32_t json_u32(const RankRequest *r, const char *path)
{
    double v = 0;
    if (!r->answer[0] || !mjson_get_number(r->answer, (int)strlen(r->answer), path, &v) || v < 0)
        return 0;
    return (uint32_t)v;
}

/* What went wrong, in the menus' capitals. */
static const char *why(const RankRequest *r, const char *status)
{
    static char text[160];
    char reason[48] = "";
    if (r->http == 0 && r->answer[0])
        mjson_get_string(r->answer, (int)strlen(r->answer), "$.reason", reason, sizeof reason);
    if (r->http == 0)
        snprintf(text, sizeof text, "%s%s%s", r->error, reason[0] ? ": " : "", reason);
    else if (r->http == 401 && strcmp(status, "invalid_api_key") == 0)
        snprintf(text, sizeof text, "LOGGED OUT BY THE SITE - LOG IN AGAIN");
    else if (r->http == 429)
        snprintf(text, sizeof text, "TOO MANY REQUESTS - WAIT A FEW MINUTES");
    else
        snprintf(text, sizeof text, "%s (%d)", status[0] ? status : "NO ANSWER", r->http);
    for (char *p = text; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 'a' + 'A');
        else if (*p == '_')
            *p = ' ';
    return text;
}

static int32_t ranked_now(void)
{
    const LobbyRules *rules = NetplayLobby_Rules();
    return rules && rules->mode == LOBBY_MODE_RANKED;
}

static void give_up_ranked(const char *reason)
{
    LobbyRules r;
    if (!s_inLobby || !s_host || !ranked_now())
        return;
    r = *NetplayLobby_Rules();
    r.mode = LOBBY_MODE_FRIENDLY;
    r.matchId = r.legId = 0;
    NetplayLobby_SetRules(&r);
    s_friendlyNow = 1;
    notice("NOT RANKED: %s", reason);
    notice("THIS LOBBY PLAYS FRIENDLIES");
}

static void got_leg(uint32_t matchId, uint32_t legId)
{
    LobbyRules r;
    if (!s_inLobby || !s_host)
        return;
    s_matchId = matchId;
    r = *NetplayLobby_Rules();
    r.matchId = matchId;
    r.legId = legId;
    NetplayLobby_SetRules(&r);
    notice("OOO INDOOR: MATCH %u, LEG %u READY", (unsigned)matchId, (unsigned)legId);
}

/* The answers that have come, handled on this (the menus') thread. */
static void pump(void)
{
    RankRequest *r;
    while ((r = next_answer()) != NULL) {
        char status[32];
        json_status(r, status, sizeof status);
        fprintf(stderr, "[ooo] %s -> %d %s%s%s\n", k_kindName[r->kind], r->http, status,
                r->http ? "" : " ", r->http ? "" : r->error);
        switch (r->kind) {
        case K_AUTH:
            if (r->http == 200 && strcmp(status, "ok") == 0 &&
                mjson_get_string(r->answer, (int)strlen(r->answer), "$.api_key", s_apiKey, sizeof s_apiKey) > 0) {
                s_login = 2;
                /* Sessions an earlier run left open (a crash, a lost link): closed now, so
                 * their legs are scored. */
                queue_request(K_OPEN_MATCHES, 0, 0, "open-matches\napi_key=%s\n", s_apiKey);
            } else {
                s_login = -1;
                snprintf(s_error, sizeof s_error, "%s",
                         r->http == 401 ? "WRONG USERNAME OR PASSWORD (SWOSUNITED.COM)" :
                         r->http == 400 ? "TYPE YOUR USERNAME AND PASSWORD" : why(r, status));
            }
            break;
        case K_OPEN_MATCHES:
            for (int32_t i = 0; i < 10; i++) {
                char path[48];
                uint32_t id;
                snprintf(path, sizeof path, "$.open_sessions[%d].match_id", (int)i);
                id = json_u32(r, path);
                if (!id)
                    break;
                queue_request(K_CLOSE, id, 0, "close\napi_key=%s\nmatch_id=%u\n", s_apiKey, (unsigned)id);
            }
            break;
        case K_CREATE:
        case K_ADD_LEG:
            s_asking = 0;
            if (r->http == 200 && strcmp(status, "ok") == 0) {
                const uint32_t matchId = json_u32(r, "$.match_id");
                const uint32_t legId = r->kind == K_CREATE ? json_u32(r, "$.open_leg_ids[0]") : json_u32(r, "$.leg_id");
                if (matchId && legId) {
                    got_leg(matchId, legId);
                    break;
                }
            }
            if (r->kind == K_ADD_LEG && strcmp(status, "session_closed") == 0) {
                s_matchId = 0;                  /* the site closed it: a new session */
                break;
            }
            give_up_ranked(why(r, status));
            break;
        case K_REPORT:
            if (strcmp(status, "applied") == 0)
                notice("RESULT COUNTED IN THE OOO INDOOR RANKING");
            else if (strcmp(status, "waiting") == 0)
                notice("RESULT SENT - WAITING FOR %s", s_peer[0] ? s_peer : "THE OPPONENT");
            else if (strcmp(status, "conflict") == 0)
                notice("THE RESULTS DIFFER! CHECK THE MATCH ON SWOSUNITED.COM");
            else
                notice("RESULT NOT SENT: %s", why(r, status));
            break;
        case K_CLOSE:
            break;
        }
        free(r);
    }
}

/* ---- the provider's functions ---- */
static void p_init(void);

static int32_t p_has(AccountFeature feature)
{
    return feature == ACCOUNT_HAS_RANKED;
}

static int32_t p_enabled(void)               { return 1; }
static const char *p_prompt(void)            { return "LOG IN WITH YOUR SWOSUNITED.COM ACCOUNT"; }

static const char *p_url(AccountLink link)
{
    return link == ACCOUNT_LINK_REGISTER ? "https://swos.swosunited.com" : NULL;
}

static void p_begin(const char *user, const char *password)
{
    pump();
    snprintf(s_user, sizeof s_user, "%s", user);
    s_apiKey[0] = 0;
    s_error[0] = 0;
    if (strchr(user, '\n') || strchr(password, '\n')) {
        s_login = -1;
        snprintf(s_error, sizeof s_error, "TYPE YOUR USERNAME AND PASSWORD");
        return;
    }
    s_login = queue_request(K_AUTH, 0, 0, "auth\nusername=%s\npassword=%s\n", user, password) ? 1 : -1;
    if (s_login < 0)
        snprintf(s_error, sizeof s_error, "BUSY - TRY AGAIN");
}

static int32_t p_poll(void)
{
    pump();
    return s_login == 2 ? 1 : s_login < 0 ? -1 : 0;
}

static const char *p_error(void)             { return s_error; }

/* ---- OPEN: FRIENDLY or RANKED MATCH (SWOS 2020's openLobbyMenu) ---- */
static MenuEntry *entry(int32_t ordinal)     { return CalcMenuEntryAddress((uint16_t)ordinal); }

static void openLobbyMenu_OnInit(void)
{
    entry(OpenLobbyMenu_leagueAndTournament)->isInvisible = 1;
    entry(OpenLobbyMenu_ranked)->fg = (uintptr_t)"RANKED (OOO INDOOR)";
    entry(OpenLobbyMenu_description1)->fg = (uintptr_t)"RANKED: COUNTS IN THE OOO INDOOR RANKING";
    entry(OpenLobbyMenu_description2)->fg = (uintptr_t)"ON SWOSUNITED.COM - PLAYED IN THE HALL ONLY";
    entry(OpenLobbyMenu_description3)->fg = (uintptr_t)"(PITCH TYPE LINOLEUM). BOTH RESULTS MUST";
    entry(OpenLobbyMenu_description4)->fg = (uintptr_t)"AGREE, THE SITE SCORES THEM AFTER THE LOBBY.";
}
static void openLobbyMenu_OnDraw(void)       {}
static void selectFriendly(void)             { s_choice = LOBBY_MODE_FRIENDLY; SetExitMenuFlag(); }
static void selectRanked(void)               { s_choice = LOBBY_MODE_RANKED; SetExitMenuFlag(); }
static void selectLeagueAndTournament(void)  {}
static void selectExitOpenLobby(void)        { s_choice = -1; SetExitMenuFlag(); }

static int32_t p_chooseMode(void)
{
    extern void OnlineMenus_Show(const char *name);
    pump();
    if (!s_apiKey[0])
        return LOBBY_MODE_FRIENDLY;
    s_choice = -1;
    OnlineMenus_Show("swos2020/openLobbyMenu");
    return s_choice;
}

static void p_lobbyOpened(int32_t host)
{
    const char *user = getenv("SWOS_OOO_USER"), *password = getenv("SWOS_OOO_PASSWORD");
    pump();
    s_inLobby = 1;
    s_host = host;
    s_peer[0] = 0;
    s_asking = 0;
    s_matchId = 0;
    s_usedLeg = 0;
    s_friendlyNow = 0;
    s_toldHall = 0;
    /* Tests (--netplay-lobby has no log-in screen): log in here, and the host's game ranked. */
    if (user && password && !s_apiKey[0]) {
        p_begin(user, password);
        drain(15000);
        pump();
    }
    if (host && getenv("SWOS_OOO_RANKED") && s_apiKey[0]) {
        LobbyRules r = *NetplayLobby_Rules();
        r.mode = LOBBY_MODE_RANKED;
        NetplayLobby_SetRules(&r);
    }
    if (ranked_now())
        notice(host ? "RANKED: OOO INDOOR RANKING - PITCH TYPE MUST BE LINOLEUM"
                    : "RANKED: OOO INDOOR RANKING");
}

static void p_lobbyPeer(const char *name)
{
    snprintf(s_peer, sizeof s_peer, "%s", name);
}

static void p_lobbyPoll(void)
{
    pump();
}

static int32_t in_hall(const LobbyRules *rules)
{
    return rules->pitchType != 0xFFFF && (rules->pitchType & LOBBY_PITCH_INDOOR) != 0;
}

/* The host's START: a ranked match needs the hall, and the site's leg for it. */
static int32_t p_lobbyCanStart(void)
{
    const LobbyRules *rules;
    pump();
    if (!s_inLobby || !s_host || !ranked_now())
        return 1;
    rules = NetplayLobby_Rules();
    if (!in_hall(rules)) {
        if (!s_toldHall)
            notice("RANKED IS PLAYED IN THE HALL: SET PITCH TYPE TO LINOLEUM");
        s_toldHall = 1;
        return 0;
    }
    s_toldHall = 0;
    if (rules->legId && rules->legId != s_usedLeg)
        return 1;                       /* this match's leg is there */
    if (s_asking || !s_peer[0])
        return 0;
    if (!s_apiKey[0]) {
        give_up_ranked("NOT LOGGED IN");
        return 1;
    }
    s_asking = s_matchId ?
        queue_request(K_ADD_LEG, s_matchId, 0, "add-leg\napi_key=%s\nmatch_id=%u\n", s_apiKey, (unsigned)s_matchId) :
        queue_request(K_CREATE, 0, 0, "create\napi_key=%s\nopponent_username=%s\n", s_apiKey, s_peer);
    return 0;
}

static const char *p_matchText(void)
{
    const LobbyRules *rules = NetplayLobby_Rules();
    if (!rules || rules->mode != LOBBY_MODE_RANKED)
        return NULL;
    if (rules->legId)
        snprintf(s_matchText, sizeof s_matchText, "OOO INDOOR - MATCH %u LEG %u", (unsigned)rules->matchId,
                 (unsigned)rules->legId);
    else
        snprintf(s_matchText, sizeof s_matchText, "OOO INDOOR RANKING");
    return s_matchText;
}

static void p_matchPlayed(const AccountResult *res)
{
    char extra[128] = "";
    const LobbyRules *rules = NetplayLobby_Rules();
    pump();
    if (res->mode != LOBBY_MODE_RANKED || !res->legId)
        return;
    s_usedLeg = res->legId;
    if (res->matchId)
        s_matchId = res->matchId;
    if (!res->completed) {
        notice("NOT FINISHED: NO RESULT SENT");
        return;
    }
    if (!s_apiKey[0]) {
        notice("NOT LOGGED IN: NO RESULT SENT");
        return;
    }
    /* Extra time and penalties only when they were played, the same way on both sides: extra time
     * when a goal fell in it, or a shoot-out followed it (a cup with EXTRA TIME); the shoot-out
     * when it had goals. */
    if (res->mine[1] + res->theirs[1] > 0 ||
        (res->mine[2] + res->theirs[2] > 0 && rules && rules->legs && rules->tieBreak == LOBBY_TIE_ET_PEN))
        snprintf(extra, sizeof extra, "extra_time_own=%d\nextra_time_opponent=%d\n", (int)res->mine[1],
                 (int)res->theirs[1]);
    if (res->mine[2] + res->theirs[2] > 0)
        snprintf(extra + strlen(extra), sizeof extra - strlen(extra), "penalties_own=%d\npenalties_opponent=%d\n",
                 (int)res->mine[2], (int)res->theirs[2]);
    queue_request(K_REPORT, res->matchId, res->legId, "report\napi_key=%s\nleg_id=%u\nscore_own=%d\nscore_opponent=%d\n%s",
                  s_apiKey, (unsigned)res->legId, (int)res->mine[0], (int)res->theirs[0], extra);
}

static void p_lobbyClosed(void)
{
    if (!s_inLobby)
        return;
    pump();
    if (s_host && s_matchId && s_apiKey[0])
        queue_request(K_CLOSE, s_matchId, 0, "close\napi_key=%s\nmatch_id=%u\n", s_apiKey, (unsigned)s_matchId);
    /* The report and the close must be out before the game may quit: briefly, as the hook says. */
    drain(4000);
    pump();
    s_inLobby = 0;
    s_matchId = 0;
}

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(openLobbyMenu_OnInit),
    HANDLER(openLobbyMenu_OnDraw),
    HANDLER(selectFriendly),
    HANDLER(selectRanked),
    HANDLER(selectLeagueAndTournament),
    HANDLER(selectExitOpenLobby),
};

static void p_init(void)
{
    static int32_t registered;
    if (registered)
        return;
    registered = 1;
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
}

void IndoorRanking_Register(void)
{
    static ModOnlineProvider p;
    uint8_t key[RANK_KEY_BYTES];
    if (!server_key(key)) {
        fprintf(stderr, "[ooo] no ranking server key in this build: no log-in, no ranked matches\n");
        return;
    }
    memset(&p, 0, sizeof p);
    p.name          = "OOO INDOOR (SWOSUNITED.COM)";
    p.init          = p_init;
    p.has           = p_has;
    p.enabled       = p_enabled;
    p.prompt        = p_prompt;
    p.url           = p_url;
    p.begin         = p_begin;
    p.poll          = p_poll;
    p.error         = p_error;
    p.chooseMode    = p_chooseMode;
    p.lobbyOpened   = p_lobbyOpened;
    p.lobbyPeer     = p_lobbyPeer;
    p.lobbyPoll     = p_lobbyPoll;
    p.lobbyCanStart = p_lobbyCanStart;
    p.matchText     = p_matchText;
    p.matchPlayed   = p_matchPlayed;
    p.lobbyClosed   = p_lobbyClosed;
    ModApi_SetOnlineProvider("indoor", &p);
}

#else   /* no netplay, or the web build: no provider */

#include "indoor_ranking.h"
void IndoorRanking_Register(void) {}

#endif

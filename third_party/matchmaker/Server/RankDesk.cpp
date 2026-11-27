// OpenSWOS-Indoor: the ranking desk. See include/RankDesk.h, and include/rank_proto.h in the
// game tree for the protocol.
#include "RankDesk.h"
#include "rank_proto.h"
#include <iostream>

#ifdef _WIN32

RankDesk::RankDesk(const RankOptions& options, Ledger& ledger) : m_options(options), m_ledger(ledger)
{
    if (!options.keyFile.empty())
        std::cout << "[rank] not available on a Windows build of the server\n";
}
RankDesk::~RankDesk() {}
void RankDesk::Adopt(int, const std::string&, uint16_t) {}
void RankDesk::Update() {}
int RankDesk::KeyGen(const std::string&) { return 1; }

#else

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#ifdef SWOS_MM_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

namespace
{
    const size_t   kMaxConnections = 32;
    const int      kMaxWorkers     = 8;         // HTTPS calls at once
    const uint32_t kReadTimeoutMs  = 10000;     // for the request to arrive
    const int      kCallTimeoutMs  = 15000;     // for the API (connect, send, answer)
    const size_t   kPerIpLimit     = 30;        // requests an address may make ...
    const uint32_t kPerIpWindowMs  = 5 * 60 * 1000;     // ... in this long

    std::string Tag()
    {
        return "[rank] " + ServerTime::TimeOfDay() + " ";
    }

    std::string DeskError(const std::string& reason)
    {
        return "0\n{\"status\":\"desk_error\",\"reason\":" + JsonString(reason) + "}";
    }

    // ---- the request's fields ----
    typedef std::map<std::string, std::string> Fields;

    bool ParseRequest(const std::string& text, std::string* op, Fields* fields)
    {
        size_t at = text.find('\n');
        *op = text.substr(0, at);
        while (at != std::string::npos && at + 1 < text.size())
        {
            size_t end = text.find('\n', at + 1);
            std::string line = text.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
            at = end;
            if (line.empty())
                continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos || eq == 0 || fields->count(line.substr(0, eq)))
                return false;
            (*fields)[line.substr(0, eq)] = line.substr(eq + 1);
        }
        return !op->empty();
    }

    bool IsInt(const std::string& s, long max)
    {
        if (s.empty() || s.size() > 9)
            return false;
        for (char c : s)
            if (c < '0' || c > '9')
                return false;
        return atol(s.c_str()) <= max;
    }

    bool IsHex(const std::string& s)
    {
        if (s.empty() || s.size() > 128)
            return false;
        for (char c : s)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                return false;
        return true;
    }

    bool IsName(const std::string& s, size_t max)
    {
        if (s.empty() || s.size() > max)
            return false;
        for (unsigned char c : s)
            if (c < 0x20 || c == 0x7f)
                return false;
        return true;
    }

    // The API call for a request, or false (with why) when the desk does not allow it.
    bool BuildCall(const std::string& op, const Fields& f, int ranking, std::string* path,
                   std::string* body, std::string* why)
    {
        auto has = [&](const char* k) { return f.count(k) != 0; };
        auto get = [&](const char* k) { auto it = f.find(k); return it == f.end() ? std::string() : it->second; };
        auto only = [&](std::initializer_list<const char*> allowed) {
            for (auto& kv : f)
            {
                bool ok = false;
                for (const char* a : allowed)
                    ok = ok || kv.first == a;
                if (!ok)
                    return false;
            }
            return true;
        };
        auto key = [&]() { return "\"api_key\":" + JsonString(get("api_key")); };

        if (op == "auth")
        {
            if (!only({"username", "password"}) || !IsName(get("username"), 64) || !IsName(get("password"), 128))
                return *why = "bad auth request", false;
            *path = "/api/auth/key";
            *body = "{\"username\":" + JsonString(get("username")) + ",\"password\":" + JsonString(get("password")) + "}";
            return true;
        }
        if (!IsHex(get("api_key")))
            return *why = "bad api key", false;
        if (op == "create")
        {
            if (!only({"api_key", "opponent_username"}) || !IsName(get("opponent_username"), 64))
                return *why = "bad create request", false;
            if (ranking <= 0)
                return *why = "no ranking on this server", false;
            *path = "/api/swos-matches/create";
            *body = "{" + key() + ",\"ranking_id\":" + std::to_string(ranking) +
                    ",\"opponent_username\":" + JsonString(get("opponent_username")) + ",\"leg_count\":1}";
            return true;
        }
        if (op == "report")
        {
            if (!only({"api_key", "leg_id", "score_own", "score_opponent", "extra_time_own",
                       "extra_time_opponent", "penalties_own", "penalties_opponent"}) ||
                !IsInt(get("leg_id"), 999999999) || !IsInt(get("score_own"), 99) ||
                !IsInt(get("score_opponent"), 99) ||
                has("extra_time_own") != has("extra_time_opponent") ||
                has("penalties_own") != has("penalties_opponent"))
                return *why = "bad report request", false;
            *path = "/api/swos-matches/report";
            *body = "{" + key() + ",\"leg_id\":" + get("leg_id") + ",\"score_own\":" + get("score_own") +
                    ",\"score_opponent\":" + get("score_opponent");
            for (const char* pair : {"extra_time", "penalties"})
            {
                std::string own = std::string(pair) + "_own", opp = std::string(pair) + "_opponent";
                if (!has(own.c_str()))
                    continue;
                if (!IsInt(get(own.c_str()), 99) || !IsInt(get(opp.c_str()), 99))
                    return *why = "bad report request", false;
                *body += ",\"" + own + "\":" + get(own.c_str()) + ",\"" + opp + "\":" + get(opp.c_str());
            }
            *body += "}";
            return true;
        }
        if (op == "add-leg" || op == "close")
        {
            if (!only({"api_key", "match_id"}) || !IsInt(get("match_id"), 999999999))
                return *why = "bad " + op + " request", false;
            *path = "/api/swos-matches/" + op;
            *body = "{" + key() + ",\"match_id\":" + get("match_id") + "}";
            return true;
        }
        if (op == "open-matches")
        {
            if (!only({"api_key"}))
                return *why = "bad open-matches request", false;
            *path = "/api/open-matches";
            *body = "{" + key() + "}";
            return true;
        }
        return *why = "unknown operation", false;
    }

    // A number or string field of the API's JSON answer, for the log ("" when there is none).
    std::string JsonField(const std::string& json, const std::string& name)
    {
        size_t at = json.find("\"" + name + "\"");
        if (at == std::string::npos)
            return "";
        at = json.find(':', at);
        if (at == std::string::npos)
            return "";
        at++;
        while (at < json.size() && json[at] == ' ')
            at++;
        if (at < json.size() && json[at] == '"')
        {
            size_t end = json.find('"', at + 1);
            return end == std::string::npos ? "" : json.substr(at + 1, std::min<size_t>(end - at - 1, 40));
        }
        size_t end = at;
        while (end < json.size() && ((json[end] >= '0' && json[end] <= '9') || json[end] == '-'))
            end++;
        return json.substr(at, end - at);
    }

    // ---- the HTTP(S) call ----
    struct Url { bool tls = true; std::string host; std::string port = "443"; std::string base; };

    bool ParseUrl(const std::string& url, Url* u)
    {
        std::string rest;
        if (url.rfind("https://", 0) == 0)
            rest = url.substr(8), u->tls = true, u->port = "443";
        else if (url.rfind("http://", 0) == 0)
            rest = url.substr(7), u->tls = false, u->port = "80";
        else
            return false;
        size_t slash = rest.find('/');
        std::string hostPort = rest.substr(0, slash);
        u->base = slash == std::string::npos ? "" : rest.substr(slash);
        while (!u->base.empty() && u->base.back() == '/')
            u->base.pop_back();
        size_t colon = hostPort.rfind(':');
        if (colon != std::string::npos)
            u->port = hostPort.substr(colon + 1), hostPort = hostPort.substr(0, colon);
        u->host = hostPort;
        return !u->host.empty();
    }

    int ConnectTo(const Url& u, std::string* why)
    {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(u.host.c_str(), u.port.c_str(), &hints, &res) != 0 || !res)
            return *why = "cannot resolve " + u.host, -1;
        int fd = -1;
        for (addrinfo* a = res; a && fd < 0; a = a->ai_next)
        {
            fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (fd < 0)
                continue;
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
            int r = connect(fd, a->ai_addr, a->ai_addrlen);
            if (r != 0 && errno == EINPROGRESS)
            {
                pollfd p{fd, POLLOUT, 0};
                int err = 0;
                socklen_t len = sizeof err;
                r = (poll(&p, 1, 5000) == 1 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) ? 0 : -1;
            }
            if (r != 0)
            {
                close(fd);
                fd = -1;
            }
        }
        freeaddrinfo(res);
        if (fd < 0)
            return *why = "cannot connect to " + u.host, -1;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);
        timeval tv{kCallTimeoutMs / 1000, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        return fd;
    }

    // POST body to base + path. The status (0 when the call failed: why) and the answer's body.
    int Post(void* sslCtx, const std::string& api, const std::string& path, const std::string& body,
             std::string* answer, std::string* why)
    {
        Url u;
        if (!ParseUrl(api, &u))
            return *why = "bad --rank-api", 0;
        std::string request = "POST " + u.base + path + " HTTP/1.0\r\n"
                              "Host: " + u.host + "\r\n"
                              "Content-Type: application/json\r\n"
                              "Accept: application/json\r\n"
                              "User-Agent: OpenSWOS-Indoor-RankDesk/1\r\n"
                              "Content-Length: " + std::to_string(body.size()) + "\r\n"
                              "Connection: close\r\n\r\n" + body;
        int fd = ConnectTo(u, why);
        if (fd < 0)
            return 0;
        std::string in;
        char buf[4096];
        bool ok = true;
#ifdef SWOS_MM_TLS
        SSL* ssl = nullptr;
        if (u.tls)
        {
            ssl = SSL_new((SSL_CTX*)sslCtx);
            SSL_set_fd(ssl, fd);
            SSL_set_tlsext_host_name(ssl, u.host.c_str());
            SSL_set1_host(ssl, u.host.c_str());
            if (SSL_connect(ssl) != 1 || SSL_get_verify_result(ssl) != X509_V_OK)
            {
                *why = "TLS to " + u.host + " failed";
                ok = false;
            }
            for (size_t done = 0; ok && done < request.size();)
            {
                int n = SSL_write(ssl, request.data() + done, (int)(request.size() - done));
                if (n <= 0)
                    ok = false, *why = "cannot send to " + u.host;
                else
                    done += (size_t)n;
            }
            while (ok && in.size() < 1 << 20)
            {
                int n = SSL_read(ssl, buf, sizeof buf);
                if (n <= 0)
                    break;
                in.append(buf, (size_t)n);
            }
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
        else
#else
        (void)sslCtx;
        if (u.tls)
        {
            *why = "this server was built without OpenSSL: no https";
            ok = false;
        }
        else
#endif
        {
            for (size_t done = 0; ok && done < request.size();)
            {
                ssize_t n = send(fd, request.data() + done, request.size() - done, MSG_NOSIGNAL);
                if (n <= 0)
                    ok = false, *why = "cannot send to " + u.host;
                else
                    done += (size_t)n;
            }
            while (ok && in.size() < 1 << 20)
            {
                ssize_t n = recv(fd, buf, sizeof buf, 0);
                if (n <= 0)
                    break;
                in.append(buf, (size_t)n);
            }
        }
        close(fd);
        if (!ok)
            return 0;
        int status = 0;
        if (in.rfind("HTTP/1.", 0) != 0 || sscanf(in.c_str() + 8, " %d", &status) != 1 || status <= 0)
            return *why = "no HTTP answer from " + u.host, 0;
        size_t head = in.find("\r\n\r\n");
        *answer = head == std::string::npos ? "" : in.substr(head + 4);
        return status;
    }
}

// A call under way on its thread. The main loop reads `answer` once `done` is set.
struct RankDesk::Job
{
    std::atomic<bool> done{false};
    uint8_t key[32] = {};
    std::string op, logFields;
    std::string answer;             // the plain text sent back: status line + JSON
    int status = 0;
    std::string why;
    uint32_t startMs = 0;
};

RankDesk::RankDesk(const RankOptions& options, Ledger& ledger) : m_options(options), m_ledger(ledger)
{
    if (options.keyFile.empty())
        return;
    FILE* f = fopen(options.keyFile.c_str(), "r");
    char hex[80] = {};
    if (!f || !fgets(hex, sizeof hex, f) || !Rank_FromHex(hex, m_secret))
    {
        std::cout << "[rank] cannot read a key from " << options.keyFile << " (--rank-keygen FILE makes one)\n";
        if (f)
            fclose(f);
        return;
    }
    fclose(f);
    Url u;
    if (!ParseUrl(options.api, &u))
    {
        std::cout << "[rank] --rank-api must be an http:// or https:// address\n";
        return;
    }
#ifdef SWOS_MM_TLS
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx || SSL_CTX_set_default_verify_paths(ctx) != 1)
    {
        std::cout << "[rank] cannot set up TLS (the system's CA certificates)\n";
        if (ctx)
            SSL_CTX_free(ctx);
        return;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    m_ssl = ctx;
#else
    if (u.tls)
    {
        std::cout << "[rank] this server was built without OpenSSL: no https for " << options.api << "\n";
        return;
    }
#endif
    uint8_t pub[32];
    char pubHex[65];
    Rank_PublicKey(m_secret, pub);
    Rank_ToHex(pub, pubHex);
    m_enabled = true;
    std::cout << "[rank] on: " << options.api << ", ranking " << options.ranking << ", public key " << pubHex << "\n";
}

RankDesk::~RankDesk()
{
    for (auto& c : m_connections)
        if (c->fd >= 0)
            close(c->fd);
    // A call still under way holds its own reference to the TLS context (Start).
#ifdef SWOS_MM_TLS
    if (m_ssl)
        SSL_CTX_free((SSL_CTX*)m_ssl);
#endif
    memset(m_secret, 0, sizeof m_secret);
}

int RankDesk::KeyGen(const std::string& path)
{
    uint8_t secret[32], pub[32];
    char hex[65];
    if (!Rank_Random(secret, sizeof secret))
    {
        std::cerr << "no random bytes (/dev/urandom)\n";
        return 1;
    }
    struct stat st;
    if (stat(path.c_str(), &st) == 0)
    {
        std::cerr << path << " is there already: not overwritten (the games know its public key)\n";
        return 1;
    }
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    FILE* f = fd >= 0 ? fdopen(fd, "w") : nullptr;
    if (!f)
    {
        std::cerr << "cannot write " << path << "\n";
        return 1;
    }
    Rank_ToHex(secret, hex);
    fprintf(f, "%s\n", hex);
    fclose(f);
    Rank_PublicKey(secret, pub);
    Rank_ToHex(pub, hex);
    memset(secret, 0, sizeof secret);
    std::cout << "secret key written to " << path << " (keep it, mode 600)\n"
              << "public key (include/project_identity.h, RANK_SERVER_PUBLIC_KEY):\n" << hex << "\n";
    return 0;
}

bool RankDesk::Allowed(const std::string& ip)
{
    uint32_t now = ServerTime::NowMs();
    auto& times = m_recent[ip];
    while (!times.empty() && now - times.front() > kPerIpWindowMs)
        times.pop_front();
    if (times.size() >= kPerIpLimit)
        return false;
    times.push_back(now);
    if (m_recent.size() > 4096)                     // forget the quiet ones
        for (auto it = m_recent.begin(); it != m_recent.end();)
            it = it->second.empty() || now - it->second.back() > kPerIpWindowMs ? m_recent.erase(it) : std::next(it);
    return true;
}

void RankDesk::Adopt(int fd, const std::string& ip, uint16_t port)
{
    if (!m_enabled || m_connections.size() >= kMaxConnections)
    {
        close(fd);
        return;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    auto c = std::make_unique<Connection>();
    c->fd = fd;
    c->ip = ip;
    c->address = ip + ":" + std::to_string(port);
    c->sinceMs = ServerTime::NowMs();
    m_connections.push_back(std::move(c));
}

void RankDesk::Update()
{
    if (!m_enabled)
        return;
    uint32_t now = ServerTime::NowMs();
    for (auto& c : m_connections)
    {
        if (c->fd < 0)
            continue;
        if (c->job)
        {
            if (!c->job->done.load())
                continue;
            Job& j = *c->job;
            std::string status = j.status ? JsonField(j.answer, "status") : "desk_error";
            std::cout << Tag() << c->address << " " << j.op << j.logFields << " -> " << j.status
                      << " " << status << (j.why.empty() ? "" : " (" + j.why + ")") << " in "
                      << (now - j.startMs) << " ms\n";
            std::string line = "{\"type\":\"rank\",\"time\":" + JsonString(ServerTime::WallClock()) +
                               ",\"from\":" + JsonString(c->ip) + ",\"op\":" + JsonString(j.op) +
                               ",\"http\":" + std::to_string(j.status) + ",\"status\":" + JsonString(status);
            for (const char* id : {"match_id", "leg_id", "pending_legs"})
            {
                std::string v = JsonField(j.answer, id);
                if (!v.empty() && v.find_first_not_of("0123456789") == std::string::npos)
                    line += ",\"" + std::string(id) + "\":" + v;
            }
            m_ledger.Write(line + "}");
            Send(*c, j.answer);
            continue;
        }
        if (!Read(*c) || now - c->sinceMs > kReadTimeoutMs)
        {
            close(c->fd);
            c->fd = -1;
        }
    }
    for (size_t i = 0; i < m_connections.size();)
    {
        if (m_connections[i]->fd < 0)
            m_connections.erase(m_connections.begin() + i);
        else
            i++;
    }
}

bool RankDesk::Read(Connection& c)
{
    uint8_t buf[2048];
    for (;;)
    {
        ssize_t n = recv(c.fd, buf, sizeof buf, 0);
        if (n > 0)
            c.in.insert(c.in.end(), buf, buf + n);
        else if (n == 0)
            return false;
        else if (errno == EINTR)
            continue;
        else
            return errno == EAGAIN || errno == EWOULDBLOCK;

        int32_t need = Rank_RequestSize(c.in.data(), c.in.size());
        if (need < 0 || (need > 0 && c.in.size() > (size_t)need))
        {
            std::cout << Tag() << c.address << ": not a request this server understands\n";
            return false;
        }
        if (need > 0 && c.in.size() == (size_t)need)
        {
            Start(c);
            return c.fd >= 0;
        }
    }
}

void RankDesk::Start(Connection& c)
{
    char plain[RANK_REQUEST_MAX + 1];
    uint8_t key[32];
    if (Rank_OpenRequest(m_secret, c.in.data(), c.in.size(), plain, sizeof plain, key) < 0)
    {
        // Not sealed to this server's key (an old game, another server's, or garbage): no key
        // to answer with, so the connection is simply closed.
        std::cout << Tag() << c.address << ": a request that does not open with this server's key\n";
        close(c.fd);
        c.fd = -1;
        return;
    }
    auto job = std::make_shared<Job>();
    memcpy(job->key, key, sizeof key);
    memset(key, 0, sizeof key);
    job->startMs = ServerTime::NowMs();
    std::string op, path, body, why;
    Fields fields;
    std::string text(plain);
    memset(plain, 0, sizeof plain);
    bool ok = ParseRequest(text, &op, &fields);
    std::fill(text.begin(), text.end(), '\0');
    job->op = ok ? op : "?";
    // What the log may say of it: never the password or the key.
    for (const char* f : {"username", "opponent_username", "leg_id", "match_id", "score_own", "score_opponent",
                          "extra_time_own", "extra_time_opponent", "penalties_own", "penalties_opponent"})
        if (fields.count(f))
            job->logFields += std::string(" ") + f + "=" + fields[f].substr(0, 40);
    c.job = job;
    if (!ok || !BuildCall(op, fields, m_options.ranking, &path, &body, &why))
    {
        job->why = ok ? why : "bad request";
        job->answer = DeskError(job->why);
        job->done = true;
        return;
    }
    for (auto& kv : fields)
        std::fill(kv.second.begin(), kv.second.end(), '\0');
    int running = 0;
    for (auto& other : m_connections)
        if (other->job && other->job != job && !other->job->done.load())
            running++;
    if (!Allowed(c.ip))
    {
        job->why = "too many requests from this address";
        job->answer = DeskError("too_many_requests");
        job->done = true;
        return;
    }
    if (running >= kMaxWorkers)
    {
        job->why = "busy";
        job->answer = DeskError("busy");
        job->done = true;
        return;
    }
    void* ctx = m_ssl;
#ifdef SWOS_MM_TLS
    if (ctx)
        SSL_CTX_up_ref((SSL_CTX*)ctx);
#endif
    std::string api = m_options.api;
    std::thread([job, ctx, api, path, body]() mutable {
        std::string answer, why;
        int status = Post(ctx, api, path, body, &answer, &why);
        std::fill(body.begin(), body.end(), '\0');
#ifdef SWOS_MM_TLS
        if (ctx)
            SSL_CTX_free((SSL_CTX*)ctx);
#endif
        if (answer.size() > RANK_ANSWER_MAX - 16)
            answer.resize(RANK_ANSWER_MAX - 16);
        job->status = status;
        job->why = why;
        job->answer = status ? std::to_string(status) + "\n" + answer : DeskError(why);
        job->done = true;
    }).detach();
}

void RankDesk::Send(Connection& c, const std::string& plain)
{
    std::vector<uint8_t> out(RANK_NONCE_BYTES + 2 + plain.size() + RANK_MAC_BYTES);
    int32_t n = Rank_SealAnswer(c.job->key, plain.data(), plain.size(), out.data(), out.size());
    memset(c.job->key, 0, sizeof c.job->key);
    if (n > 0)
    {
        // Small (a few KB): sent blocking, with a short limit, then closed.
        fcntl(c.fd, F_SETFL, fcntl(c.fd, F_GETFL, 0) & ~O_NONBLOCK);
        timeval tv{2, 0};
        setsockopt(c.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        for (int32_t done = 0; done < n;)
        {
            ssize_t k = send(c.fd, out.data() + done, (size_t)(n - done), MSG_NOSIGNAL);
            if (k <= 0)
                break;
            done += (int32_t)k;
        }
    }
    close(c.fd);
    c.fd = -1;
}

#endif

// OpenSWOS: the WebSocket gate and the TCP listener it shares with the recorder.
// See include/WebGate.h.
#include "WebGate.h"
#include "Recorder.h"
#include "ReportDesk.h"
#include "RankDesk.h"
#include "Ledger.h"
#include <algorithm>
#include <iostream>

#ifdef _WIN32

WebGate::WebGate(bool enabled, uint16_t serverPort, const std::string&, const std::string&)
    : m_enabled(false), m_serverPort(serverPort)
{
    if (enabled)
        std::cout << "[web] not available on a Windows build of the server\n";
}
WebGate::~WebGate() {}
void WebGate::Adopt(int, const std::string&, uint16_t, bool) {}
void WebGate::Update() {}

TcpGate::TcpGate(uint16_t, WebGate* web, Recorder* recorder, ReportDesk* reports, RankDesk* rank)
    : m_web(web), m_recorder(recorder), m_reports(reports), m_rank(rank) {}
TcpGate::~TcpGate() {}
void TcpGate::Update() {}

#else

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#ifdef SWOS_MM_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace
{
const uint32_t kIdleTunnelMs     = 60000;       // a tunnel keeps its address this long
const uint32_t kPendingMs        = 10000;       // to say what a TCP connection is
const size_t   kMaxHandshake     = 8192;
const size_t   kMaxMessage       = 1u << 20;
const size_t   kMaxBacklog       = 4u << 20;    // unsent to one slow browser
const size_t   kMaxConnections   = 1024;
const size_t   kMaxPerAddress    = 64;
const uint32_t kTlsCheckMs       = 60000;       // how often to look for a renewed certificate

void NonBlocking(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

// ---- SHA-1 and base64: all the handshake needs (RFC 6455 §4.2.2) ----
struct Sha1
{
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    uint8_t block[64];
    size_t used = 0;
    uint64_t bits = 0;

    static uint32_t Rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
    void Compress()
    {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
                   (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
        for (int i = 16; i < 80; i++)
            w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++)
        {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
            uint32_t t = Rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = Rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    void Add(const uint8_t* p, size_t n)
    {
        for (size_t i = 0; i < n; i++)
        {
            block[used++] = p[i];
            bits += 8;
            if (used == 64) { Compress(); used = 0; }
        }
    }
    void Finish(uint8_t out[20])
    {
        uint64_t total = bits;
        uint8_t pad = 0x80;
        Add(&pad, 1);
        pad = 0;
        while (used != 56)
            Add(&pad, 1);
        for (int i = 7; i >= 0; i--)
        {
            uint8_t b = (uint8_t)(total >> (i * 8));
            Add(&b, 1);
        }
        for (int i = 0; i < 5; i++)
            for (int j = 0; j < 4; j++)
                out[i * 4 + j] = (uint8_t)(h[i] >> (24 - j * 8));
    }
};

std::string Base64(const uint8_t* p, size_t n)
{
    static const char k[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < n; i += 3)
    {
        uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) |
                     (i + 2 < n ? p[i + 2] : 0);
        out += k[(v >> 18) & 63];
        out += k[(v >> 12) & 63];
        out += i + 1 < n ? k[(v >> 6) & 63] : '=';
        out += i + 2 < n ? k[v & 63] : '=';
    }
    return out;
}

std::string Lower(std::string s)
{
    for (char& ch : s)
        if (ch >= 'A' && ch <= 'Z')
            ch = (char)(ch - 'A' + 'a');
    return s;
}

std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t");
    size_t b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// A file's mtime in nanoseconds, 0 if it cannot be read. certbot's live/ names are
// symlinks to a new file on each renewal; stat follows them.
int64_t Stamp(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return 0;
    return (int64_t)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
}

#ifdef SWOS_MM_TLS
// OpenSSL's reason for the last failure, for the log.
std::string TlsError()
{
    unsigned long e = ERR_get_error();
    ERR_clear_error();
    if (!e)
        return "unknown error";
    char buf[256];
    ERR_error_string_n(e, buf, sizeof buf);
    return buf;
}
#endif
} // namespace

// ============================================================================
// WebGate
// ============================================================================

WebGate::WebGate(bool enabled, uint16_t serverPort, const std::string& certPath,
                 const std::string& keyPath)
    : m_enabled(enabled), m_serverPort(serverPort), m_certPath(certPath), m_keyPath(keyPath)
{
    if (!enabled)
        return;
    std::cout << "[web] on: browsers connect over WebSocket on TCP " << serverPort
              << " and play through the relay\n";
    if (m_certPath.empty())
        return;
#ifdef SWOS_MM_TLS
    m_tls = LoadTls();
    m_tlsCheckedMs = ServerTime::NowMs();
    if (m_tls)
        std::cout << "[web] wss:// on the same port, certificate " << m_certPath << "\n";
    else
        std::cout << "[web] wss:// OFF: ws:// only until the certificate loads\n";
#else
    std::cout << "[web] wss:// OFF: this server was built without OpenSSL (install libssl-dev "
                 "and rebuild: scripts/run_matchmaker.sh --rebuild)\n";
#endif
}

WebGate::~WebGate()
{
    for (auto& c : m_connections)
        Close(*c, "server stopping");
    for (auto& t : m_tunnels)
        if (t.second->udp >= 0)
            close(t.second->udp);
#ifdef SWOS_MM_TLS
    if (m_tls)
        SSL_CTX_free(m_tls);
#endif
}

#ifdef SWOS_MM_TLS
ssl_ctx_st* WebGate::LoadTls()
{
    int64_t certStamp = Stamp(m_certPath), keyStamp = Stamp(m_keyPath);
    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx)
    {
        std::cout << "[web] TLS: " << TlsError() << "\n";
        return nullptr;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    // Partial writes, from a buffer that may have moved: Flush sends from the front
    // of a vector that is appended to between retries.
    SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    std::string what;
    if (SSL_CTX_use_certificate_chain_file(ctx, m_certPath.c_str()) != 1)
        what = "cannot use the certificate " + m_certPath;
    else if (SSL_CTX_use_PrivateKey_file(ctx, m_keyPath.c_str(), SSL_FILETYPE_PEM) != 1)
        what = "cannot use the key " + m_keyPath;
    else if (SSL_CTX_check_private_key(ctx) != 1)
        what = "the key " + m_keyPath + " does not match the certificate " + m_certPath;
    if (!what.empty())
    {
        std::cout << "[web] TLS: " << what << ": " << TlsError() << "\n";
        SSL_CTX_free(ctx);
        return nullptr;
    }
    m_certStamp = certStamp;
    m_keyStamp = keyStamp;
    return ctx;
}

// A renewed certificate is picked up within a minute. Connections already open keep
// the context they started with (OpenSSL counts its references); only new ones get
// the new one. A renewal that does not load leaves the old one serving.
void WebGate::ReloadTlsIfChanged()
{
    uint32_t now = ServerTime::NowMs();
    if (m_certPath.empty() || now - m_tlsCheckedMs < kTlsCheckMs)
        return;
    m_tlsCheckedMs = now;
    int64_t certStamp = Stamp(m_certPath), keyStamp = Stamp(m_keyPath);
    if (!certStamp || !keyStamp || (m_tls && certStamp == m_certStamp && keyStamp == m_keyStamp))
        return;
    SSL_CTX* fresh = LoadTls();
    if (!fresh)
    {
        // Do not retry a bad pair every minute; wait for the files to change again.
        m_certStamp = certStamp;
        m_keyStamp = keyStamp;
        return;
    }
    if (m_tls)
        SSL_CTX_free(m_tls);
    m_tls = fresh;
    std::cout << "[web] TLS: loaded the renewed certificate " << m_certPath << "\n";
}

// Non-blocking: called each pass until the handshake is done.
bool WebGate::TlsAccept(Connection& c)
{
    ERR_clear_error();
    int r = SSL_accept(c.ssl);
    if (r == 1)
    {
        c.tlsReady = true;
        return true;
    }
    int e = SSL_get_error(c.ssl, r);
    if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
        return ServerTime::NowMs() - c.openedMs < kPendingMs;
    // Usually a browser that does not trust the certificate, or a scanner.
    std::cout << "[web] " << c.address << ": TLS handshake failed: " << TlsError() << "\n";
    return false;
}
#else
ssl_ctx_st* WebGate::LoadTls() { return nullptr; }
void WebGate::ReloadTlsIfChanged() {}
bool WebGate::TlsAccept(Connection&) { return false; }
#endif

int WebGate::Receive(Connection& c, uint8_t* buf, size_t len)
{
#ifdef SWOS_MM_TLS
    if (c.ssl)
    {
        ERR_clear_error();
        int r = SSL_read(c.ssl, buf, (int)len);
        if (r > 0)
            return r;
        int e = SSL_get_error(c.ssl, r);
        return e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE ? 0 : -1;
    }
#endif
    ssize_t n = recv(c.fd, buf, len, 0);
    if (n > 0)
        return (int)n;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return 0;
    return -1;
}

void WebGate::Adopt(int fd, const std::string& ip, uint16_t port, bool tls)
{
    size_t fromHere = 0;
    for (auto& c : m_connections)
        if (c->ip == ip)
            fromHere++;
    if (!m_enabled || m_connections.size() >= kMaxConnections || fromHere >= kMaxPerAddress)
    {
        close(fd);
        return;
    }
    auto c = std::make_unique<Connection>();
    c->fd = fd;
    c->ip = ip;
    c->address = ip + ":" + std::to_string(port);
    c->openedMs = ServerTime::NowMs();
#ifdef SWOS_MM_TLS
    if (tls)
    {
        c->ssl = m_tls ? SSL_new(m_tls) : nullptr;
        if (!c->ssl || SSL_set_fd(c->ssl, fd) != 1)
        {
            if (c->ssl)
                SSL_free(c->ssl);
            close(fd);
            return;
        }
        c->tlsReady = false;
        c->address = "wss " + c->address;
    }
#else
    if (tls)
    {
        close(fd);
        return;
    }
#endif
    m_connections.push_back(std::move(c));
}

// The HTTP upgrade. RFC 6455 §4.2: echo the key's hash, and the subprotocol --
// Emscripten asks for "binary", and a browser drops a connection whose answer does
// not name the one it asked for.
bool WebGate::Handshake(Connection& c)
{
    static const char kEnd[] = "\r\n\r\n";
    auto end = std::search(c.in.begin(), c.in.end(), kEnd, kEnd + 4);
    if (end == c.in.end())
        return c.in.size() < kMaxHandshake;         // not all here yet
    std::string head(c.in.begin(), end);
    c.in.erase(c.in.begin(), end + 4);

    std::string key, protocol, upgrade;
    size_t at = head.find("\r\n");
    while (at != std::string::npos)
    {
        size_t next = head.find("\r\n", at + 2);
        std::string line = head.substr(at + 2, next == std::string::npos ? std::string::npos : next - at - 2);
        size_t colon = line.find(':');
        if (colon != std::string::npos)
        {
            std::string name = Lower(Trim(line.substr(0, colon)));
            std::string value = Trim(line.substr(colon + 1));
            if (name == "sec-websocket-key")
                key = value;
            else if (name == "sec-websocket-protocol")
                protocol = value;
            else if (name == "upgrade")
                upgrade = Lower(value);
        }
        at = next;
    }
    if (key.empty() || upgrade != "websocket")
    {
        static const char bad[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        c.out.insert(c.out.end(), bad, bad + sizeof bad - 1);
        c.closing = true;
        return true;
    }
    std::string accept = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    Sha1 sha;
    sha.Add((const uint8_t*)accept.data(), accept.size());
    uint8_t digest[20];
    sha.Finish(digest);
    std::string reply = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                        "Connection: Upgrade\r\nSec-WebSocket-Accept: " + Base64(digest, 20) + "\r\n";
    if (!protocol.empty())
    {
        // "binary" if it was offered, else the first one: we carry bytes either way.
        std::string chosen = Lower(protocol).find("binary") != std::string::npos
                                 ? "binary" : Trim(protocol.substr(0, protocol.find(',')));
        reply += "Sec-WebSocket-Protocol: " + chosen + "\r\n";
    }
    reply += "\r\n";
    c.out.insert(c.out.end(), reply.begin(), reply.end());
    c.upgraded = true;
    return true;
}

// RFC 6455 §5.2. A client's frames are masked; a message may come in fragments.
bool WebGate::Frames(Connection& c)
{
    size_t at = 0;
    for (;;)
    {
        size_t avail = c.in.size() - at;
        if (avail < 2)
            break;
        const uint8_t* p = c.in.data() + at;
        bool fin = (p[0] & 0x80) != 0;
        uint8_t op = p[0] & 0x0F;
        bool masked = (p[1] & 0x80) != 0;
        uint64_t len = p[1] & 0x7F;
        size_t head = 2;
        if (len == 126)
        {
            if (avail < 4) break;
            len = (uint64_t)p[2] << 8 | p[3];
            head = 4;
        }
        else if (len == 127)
        {
            if (avail < 10) break;
            len = 0;
            for (int i = 0; i < 8; i++)
                len = len << 8 | p[2 + i];
            head = 10;
        }
        if (!masked || len > kMaxMessage)
            return false;                           // a protocol error: drop it
        if (avail < head + 4 + len)
            break;
        const uint8_t* mask = p + head;
        std::vector<uint8_t> payload(p + head + 4, p + head + 4 + len);
        for (size_t i = 0; i < payload.size(); i++)
            payload[i] ^= mask[i & 3];
        at += head + 4 + (size_t)len;

        if (op == 0x8)                              // close: answer it, then go
        {
            SendFrame(c, 0x8, payload.data(), payload.size() < 2 ? payload.size() : 2);
            c.closing = true;
            break;
        }
        if (op == 0x9)                              // ping
        {
            SendFrame(c, 0xA, payload.data(), payload.size());
            continue;
        }
        if (op == 0xA)                              // pong
            continue;
        if (op == 0x1 || op == 0x2)                 // a message begins
        {
            c.message.swap(payload);
            c.messageOp = op;
        }
        else if (op == 0x0)                         // ... continues
        {
            if (c.message.size() + payload.size() > kMaxMessage)
                return false;
            c.message.insert(c.message.end(), payload.begin(), payload.end());
        }
        else
            return false;
        if (fin)
        {
            Deliver(c, c.message.data(), c.message.size());
            c.message.clear();
        }
    }
    c.in.erase(c.in.begin(), c.in.begin() + at);
    return true;
}

WebGate::Tunnel* WebGate::TunnelFor(const std::string& key)
{
    auto it = m_tunnels.find(key);
    if (it != m_tunnels.end())
        return it->second.get();
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return nullptr;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sockaddr_in server = local;
    server.sin_port = htons(m_serverPort);
    // Connected: it can only ever talk to this server's own UDP port.
    if (bind(s, (sockaddr*)&local, sizeof local) != 0 ||
        connect(s, (sockaddr*)&server, sizeof server) != 0)
    {
        close(s);
        return nullptr;
    }
    NonBlocking(s);
    socklen_t len = sizeof local;
    getsockname(s, (sockaddr*)&local, &len);
    auto t = std::make_unique<Tunnel>();
    t->udp = s;
    t->key = key;
    t->localPort = ntohs(local.sin_port);
    Tunnel* raw = t.get();
    m_tunnels[key] = std::move(t);
    return raw;
}

void WebGate::Deliver(Connection& c, const uint8_t* p, size_t len)
{
    bool first = c.first;
    c.first = false;
    if (first && len == 10 && p[0] == 0xFF && p[1] == 0xFF && p[2] == 0xFF && p[3] == 0xFF &&
        memcmp(p + 4, "port", 4) == 0)
    {
        // A bound browser socket names its port: the socket that follows on the same
        // port (GGPO's, after the match link's) must reach the server from the same
        // address, so it gets the same tunnel.
        c.browserPort = (uint16_t)(p[8] << 8 | p[9]);
        Tunnel* t = TunnelFor(c.ip + ":" + std::to_string(c.browserPort));
        if (!t)
        {
            c.closing = true;
            return;
        }
        if (t->conn && t->conn != &c)
            t->conn->tunnel = nullptr;              // the newer socket answers now
        t->conn = &c;
        c.tunnel = t;
        std::cout << "[web] " << c.address << ": browser socket :" << c.browserPort
                  << " is 127.0.0.1:" << t->localPort << " to the server\n";
        return;
    }
    if (!c.tunnel)
    {
        Tunnel* t = TunnelFor(c.ip + "#" + std::to_string(m_nextUnique++));
        if (!t)
        {
            c.closing = true;
            return;
        }
        t->conn = &c;
        c.tunnel = t;
        std::cout << "[web] " << c.address << ": unbound browser socket is 127.0.0.1:"
                  << t->localPort << " to the server\n";
    }
    if (send(c.tunnel->udp, p, len, 0) == (ssize_t)len)
        c.tunnel->toServer++;
}

void WebGate::SendFrame(Connection& c, uint8_t op, const uint8_t* p, size_t len)
{
    uint8_t head[10];
    size_t n = 2;
    head[0] = (uint8_t)(0x80 | op);
    if (len < 126)
        head[1] = (uint8_t)len;
    else if (len < 65536)
    {
        head[1] = 126;
        head[2] = (uint8_t)(len >> 8);
        head[3] = (uint8_t)len;
        n = 4;
    }
    else
    {
        head[1] = 127;
        for (int i = 0; i < 8; i++)
            head[2 + i] = (uint8_t)((uint64_t)len >> (56 - i * 8));
        n = 10;
    }
    c.out.insert(c.out.end(), head, head + n);
    c.out.insert(c.out.end(), p, p + len);
}

bool WebGate::Flush(Connection& c)
{
    while (!c.out.empty())
    {
#ifdef SWOS_MM_TLS
        if (c.ssl)
        {
            if (!c.tlsReady)
                break;                              // nothing of ours goes out before it
            ERR_clear_error();
            int r = SSL_write(c.ssl, c.out.data(), (int)std::min(c.out.size(), (size_t)1 << 20));
            if (r > 0)
            {
                c.out.erase(c.out.begin(), c.out.begin() + r);
                continue;
            }
            int e = SSL_get_error(c.ssl, r);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
                break;
            return false;
        }
#endif
        ssize_t n = send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
        if (n > 0)
        {
            c.out.erase(c.out.begin(), c.out.begin() + n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            break;
        return false;
    }
    return c.out.size() <= kMaxBacklog;
}

void WebGate::Close(Connection& c, const char* why)
{
    if (c.fd < 0)
        return;
    if (c.tunnel && c.tunnel->conn == &c)
    {
        c.tunnel->conn = nullptr;
        c.tunnel->idleSinceMs = ServerTime::NowMs();
    }
    if (c.upgraded)
        std::cout << "[web] " << c.address << (c.browserPort ? " (:" + std::to_string(c.browserPort) + ")" : "")
                  << " closed (" << why << ") after " << (ServerTime::NowMs() - c.openedMs) / 1000 << " s"
                  << (c.tunnel ? ", " + std::to_string(c.tunnel->toServer) + " datagrams in, " +
                                 std::to_string(c.tunnel->toBrowser) + " out" : "") << "\n";
#ifdef SWOS_MM_TLS
    if (c.ssl)
    {
        if (c.tlsReady)
            SSL_shutdown(c.ssl);                    // one try: say goodbye, do not wait
        SSL_free(c.ssl);
        c.ssl = nullptr;
    }
#endif
    close(c.fd);
    c.fd = -1;
    c.tunnel = nullptr;
}

// What the server sent each tunnel, to the browser socket it belongs to. A datagram
// for a tunnel with no WebSocket just now is lost, as UDP would lose it.
void WebGate::PumpTunnels()
{
    uint8_t buf[65536];
    for (auto& entry : m_tunnels)
    {
        Tunnel& t = *entry.second;
        for (;;)
        {
            ssize_t n = recv(t.udp, buf, sizeof buf, 0);
            if (n < 0)
                break;
            if (t.conn && t.conn->fd >= 0 && t.conn->upgraded && !t.conn->closing)
            {
                SendFrame(*t.conn, 0x2, buf, (size_t)n);
                t.toBrowser++;
            }
        }
    }
}

void WebGate::Update()
{
    if (!m_enabled)
        return;
    ReloadTlsIfChanged();
    uint8_t buf[16384];
    for (auto& cp : m_connections)
    {
        Connection& c = *cp;
        if (c.fd < 0)
            continue;
        if (!c.tlsReady)
        {
            if (!TlsAccept(c))
            {
                Close(c, "TLS handshake");
                continue;
            }
            if (!c.tlsReady)
                continue;                           // still shaking hands
        }
        bool gone = false;
        for (;;)
        {
            int n = Receive(c, buf, sizeof buf);
            if (n > 0)
            {
                c.in.insert(c.in.end(), buf, buf + n);
                continue;
            }
            gone = n < 0;
            break;
        }
        bool ok = true;
        if (!c.closing && !c.upgraded)
            ok = Handshake(c);
        if (ok && !c.closing && c.upgraded)
            ok = Frames(c);
        if (!ok)
            Close(c, "protocol error");
        else if (gone)
            Close(c, "closed");
    }
    PumpTunnels();
    for (auto& cp : m_connections)
    {
        Connection& c = *cp;
        if (c.fd < 0)
            continue;
        if (!Flush(c))
            Close(c, "could not keep up");
        else if (c.closing && c.out.empty())
            Close(c, "closed");
    }
    for (size_t i = 0; i < m_connections.size();)
    {
        if (m_connections[i]->fd < 0)
            m_connections.erase(m_connections.begin() + i);
        else
            i++;
    }
    uint32_t now = ServerTime::NowMs();
    for (auto it = m_tunnels.begin(); it != m_tunnels.end();)
    {
        Tunnel& t = *it->second;
        if (!t.conn && now - t.idleSinceMs > kIdleTunnelMs)
        {
            close(t.udp);
            it = m_tunnels.erase(it);
        }
        else
            ++it;
    }
}

// ============================================================================
// TcpGate
// ============================================================================

TcpGate::TcpGate(uint16_t port, WebGate* web, Recorder* recorder, ReportDesk* reports, RankDesk* rank)
    : m_web(web), m_recorder(recorder), m_reports(reports), m_rank(rank)
{
    if (!m_web && !m_recorder && !m_reports && !m_rank)
        return;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        std::cout << "[tcp] cannot open a socket: " << strerror(errno) << "\n";
        return;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(port);
    if (bind(fd, (sockaddr*)&sin, sizeof sin) != 0 || listen(fd, 64) != 0)
    {
        std::cout << "[tcp] cannot listen on TCP " << port << ": " << strerror(errno) << "\n";
        close(fd);
        return;
    }
    NonBlocking(fd);
    m_listen = fd;
}

TcpGate::~TcpGate()
{
    for (auto& p : m_pending)
        close(p.fd);
    if (m_listen >= 0)
        close(m_listen);
}

void TcpGate::Update()
{
    if (m_listen < 0)
        return;
    for (;;)
    {
        sockaddr_in from{};
        socklen_t len = sizeof from;
        int fd = accept(m_listen, (sockaddr*)&from, &len);
        if (fd < 0)
            break;
        NonBlocking(fd);
        Pending p;
        p.fd = fd;
        char ip[INET_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
        p.ip = ip;
        p.port = ntohs(from.sin_port);
        p.sinceMs = ServerTime::NowMs();
        m_pending.push_back(p);
    }
    // Say what each is from its first four bytes, without taking them.
    uint32_t now = ServerTime::NowMs();
    for (size_t i = 0; i < m_pending.size();)
    {
        Pending& p = m_pending[i];
        char head[4];
        ssize_t n = recv(p.fd, head, sizeof head, MSG_PEEK);
        bool decided = true;
        // A TLS handshake record: 0x16, then version 3.x.
        bool tls = n == 4 && head[0] == 0x16 && head[1] == 0x03;
        if (n == 4 && memcmp(head, "GET ", 4) == 0 && m_web)
            m_web->Adopt(p.fd, p.ip, p.port);
        else if (tls && m_web && m_web->TlsEnabled())
            m_web->Adopt(p.fd, p.ip, p.port, true);
        else if (n == 4 && memcmp(head, "SWMR", 4) == 0 && m_reports)     // a match report
            m_reports->Adopt(p.fd, p.ip + ":" + std::to_string(p.port));
        else if (n == 4 && memcmp(head, "SWRK", 4) == 0 && m_rank)        // OpenSWOS-Indoor: the ranking desk
            m_rank->Adopt(p.fd, p.ip, p.port);
        else if (n == 4 && !tls && memcmp(head, "GET ", 4) != 0 && m_recorder)
            m_recorder->Adopt(p.fd, p.ip + ":" + std::to_string(p.port));
        else if (n == 4 || n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) ||
                 now - p.sinceMs > kPendingMs)
            close(p.fd);                            // nobody here for it, or it said nothing
        else
            decided = false;
        if (decided)
            m_pending.erase(m_pending.begin() + i);
        else
            i++;
    }
}

#endif

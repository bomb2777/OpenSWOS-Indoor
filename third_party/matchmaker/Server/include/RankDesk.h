#pragma once
// OpenSWOS-Indoor: the ranking desk (not in upstream). include/rank_proto.h in the game tree has
// the protocol, docs/INDOOR.md ("Ranked") the reasons.
//
// The game logs in to SWOS United's site and reports its ranked (OOO) matches through here: it
// speaks no HTTPS. A connection the TCP gate hands over begins "SWRK": one request sealed to
// this server's key (--rank-key), which the desk opens, checks against the few API calls it
// allows, sends on over HTTPS to the API (--rank-api, default https://swos.swosunited.com) --
// with the ranking it was started for (--rank-ranking), never the game's -- and answers sealed
// with the API's reply. A password or API key is never logged or kept.
//
// The HTTPS call runs on a thread of its own (it can take seconds; the main loop must not
// wait), at most kMaxWorkers at once; the main loop polls for the finished ones.
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <deque>
#include "Ledger.h"

struct RankOptions
{
    std::string keyFile;            // --rank-key: the secret (64 hex characters); empty: off
    int ranking = 0;                // --rank-ranking: the API's ranking_id for create
    std::string api = "https://swos.swosunited.com";    // --rank-api (http:// for a test)
};

class RankDesk
{
public:
    RankDesk(const RankOptions& options, Ledger& ledger);
    ~RankDesk();

    bool Enabled() const { return m_enabled; }

    // A connection the TCP gate accepted that begins "SWRK".
    void Adopt(int fd, const std::string& ip, uint16_t port);

    // Once per main-loop pass: read requests, start their calls, send the finished answers.
    void Update();
    bool Busy() const { return !m_connections.empty(); }

    // --rank-keygen FILE: a new secret into FILE (hex), its public key on stdout. 0 = done.
    static int KeyGen(const std::string& path);

    struct Job;                     // (RankDesk.cpp)

private:
    struct Connection
    {
        int fd = -1;
        std::string ip, address;
        std::vector<uint8_t> in;
        uint32_t sinceMs = 0;
        std::shared_ptr<Job> job;   // the call under way, once the request is in
    };

    bool Read(Connection& c);       // false = done with it
    void Start(Connection& c);      // the whole request is in
    void Send(Connection& c, const std::string& plain);
    bool Allowed(const std::string& ip);

    bool m_enabled = false;
    RankOptions m_options;
    Ledger& m_ledger;
    uint8_t m_secret[32] = {};
    std::vector<std::unique_ptr<Connection>> m_connections;
    std::map<std::string, std::deque<uint32_t>> m_recent;     // requests per IP, the last 5 minutes
    void* m_ssl = nullptr;          // SSL_CTX*, shared by the workers
};

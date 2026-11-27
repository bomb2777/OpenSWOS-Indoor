#pragma once
#include "UserMap.h"
#include "IConnections.h"
#include <string>
#include "Utils.h"
#include "Game.h"

class User;
class Message;
class Relay;

class Connections : public IConnections
{
public:
    Connections(uint16_t port);
    void NewConnection(ENetPeer* peer);
    // data: the ENET disconnect's (OpenSWOS-Indoor: 1 = the game said goodbye, mm_client.c).
    void LostConnection(ENetPeer* peer, enet_uint32 data = 0);
    void ReceiveMessage(ENetPeer* peer, const unsigned char* data, size_t len);
    void Update();
    bool VerifyName(const std::string& name) override ;
    bool VerifyVersion(const std::string& version) override;
    void OnUserChangeState(class User*) override;
    ENetHost* Host() { return m_host.get(); }
    // OpenSWOS: the relay, when the server runs one (Relay.h). StartGame makes a
    // session for an OpenSWOS pair and hands each player its token.
    void SetRelay(Relay* relay) { m_relay = relay; }
    void BroadcastMessage(const class Message& m) const override;
    void BroadcastActiveUsers() const override;
    void BroadcastOpenGames() const override;
    bool OpenGame(User* creator, int min_players, int max_players, const std::string& tag = "") override;
    bool RequestToJoin(User* requestor, const std::string& data) override;
    void RemoveUserFromAnyGames(User* user) override;
    void Eject(User* owner, const std::string& other) override;
    bool Approve(User* owner, const std::string& other) override;
    bool StartGame(User* owner) override;
private:
    User* UserByName(const std::string& name);
    std::vector<Game> m_games;
    std::vector<User*> ActivePlayers() const;
    ENetAddress address;
    ENetHostPtr m_host;
    UserMap m_users;
    Relay* m_relay = nullptr;

};
#pragma once

#include <steam/steamnetworkingsockets.h>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include "Room.h"
#include "../protocol/Protocol.h"

namespace relay {

class Server
{
public:
    Server();
    ~Server();

    // Non-copyable
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Lifecycle
    bool start(uint16_t port);
    void stop();
    void update();

    // Statistics
    struct Stats
    {
        int activeRooms = 0;
        int totalPlayers = 0;
    };
    [[nodiscard]] Stats getStats() const;

    // Singleton for callback routing
    static Server* instance() { return s_instance; }

private:
    // Connection handling
    void onClientConnect(HSteamNetConnection conn);
    void onClientDisconnect(HSteamNetConnection conn);

    // Room management
    Room* findAvailableRoom();
    Room* createRoom(HSteamNetConnection hostConn);
    Room* findRoomByConnection(HSteamNetConnection conn);
    Room* findRoomByCode(const char* code);
    void  removeFromRoom(HSteamNetConnection conn);

    // Join request handling
    void handleJoinRequest(HSteamNetConnection conn, const JoinRequestMsg* msg);
    void sendJoinResponse(HSteamNetConnection conn, bool success, const char* error = nullptr);

    // Messaging
    void relayMessage(HSteamNetConnection sender, const uint8_t* data, size_t size);
    void sendRoomAssignment(HSteamNetConnection conn, const Room* room, int playerIndex, bool isHost);

    // Equal-players model: server collects states and broadcasts (using packed structs)
    void handlePlayerState(HSteamNetConnection conn, const NetPlayerState* state);
    void broadcastWorldStates();

    // GNS callback
    static void onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);

    // GNS handles
    ISteamNetworkingSockets* m_interface = nullptr;
    HSteamListenSocket       m_listenSocket = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup       m_pollGroup = k_HSteamNetPollGroup_Invalid;

    // Room storage
    std::array<Room, kMaxRooms> m_rooms;
    std::unordered_map<HSteamNetConnection, int> m_connectionToRoom;
    std::unordered_set<HSteamNetConnection> m_pendingConnections;
    std::unordered_map<std::string, Room*> m_roomsByCode;  // O(1) room code lookup

    // World state sequence counter
    uint32_t m_worldSequence = 0;

    // Keep-alive ping timing
    uint32_t m_lastPingTime = 0;
    static constexpr uint32_t kPingIntervalMs = 5000;  // Send ping every 5 seconds

    // Send ping to all connected clients
    void sendPingToAll();

    // Singleton
    static Server* s_instance;
};

} // namespace relay

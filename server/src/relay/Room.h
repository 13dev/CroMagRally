#pragma once

#include <steam/steamnetworkingsockets.h>
#include <array>
#include <string>
#include "../protocol/Protocol.h"

namespace relay {

class Room
{
public:
    Room();
    ~Room() = default;

    // Room lifecycle
    void activate(HSteamNetConnection hostConn);
    void deactivate();
    [[nodiscard]] bool isActive() const { return m_active; }

    // Player management
    int  addPlayer(HSteamNetConnection conn);
    void removePlayer(HSteamNetConnection conn);
    [[nodiscard]] int  getPlayerIndex(HSteamNetConnection conn) const;
    [[nodiscard]] int  getPlayerCount() const { return m_playerCount; }
    [[nodiscard]] bool isFull() const { return m_playerCount >= kMaxPlayersPerRoom; }

    // Host/game state
    [[nodiscard]] bool isHost(HSteamNetConnection conn) const { return conn == m_hostConnection; }
    [[nodiscard]] HSteamNetConnection getHost() const { return m_hostConnection; }
    [[nodiscard]] bool isGameStarted() const { return m_gameStarted; }
    void startGame() { m_gameStarted = true; }

    // Room code
    [[nodiscard]] const char* getCode() const { return m_code.data(); }

    // Connection access for message routing
    [[nodiscard]] const std::array<HSteamNetConnection, kMaxPlayersPerRoom>& getConnections() const
    {
        return m_connections;
    }

    // Player state tracking (for equal-players model, using packed structs)
    void updatePlayerState(int playerIndex, const NetPlayerState& state);
    [[nodiscard]] const NetPlayerState& getPlayerState(int playerIndex) const { return m_playerStates[playerIndex]; }
    [[nodiscard]] const std::array<NetPlayerState, kMaxPlayersPerRoom>& getAllPlayerStates() const { return m_playerStates; }

private:
    void generateCode();

    std::array<char, kRoomCodeLength + 1> m_code{};
    std::array<HSteamNetConnection, kMaxPlayersPerRoom> m_connections{};
    std::array<NetPlayerState, kMaxPlayersPerRoom> m_playerStates{};  // Cached player states for broadcasting
    HSteamNetConnection m_hostConnection = k_HSteamNetConnection_Invalid;
    int  m_playerCount = 0;
    bool m_active = false;
    bool m_gameStarted = false;
};

} // namespace relay

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
    // Returns true if state was accepted, false if rejected (old sequence)
    bool updatePlayerState(int playerIndex, const NetPlayerState& state);
    [[nodiscard]] const NetPlayerState& getPlayerState(int playerIndex) const { return m_playerStates[playerIndex]; }
    [[nodiscard]] const std::array<NetPlayerState, kMaxPlayersPerRoom>& getAllPlayerStates() const { return m_playerStates; }

    // Dirty flag for bandwidth optimization (only broadcast when state changed)
    [[nodiscard]] bool isDirty() const { return m_dirty; }
    void clearDirty() { m_dirty = false; }

    // Ready state tracking (per-player bitmask)
    void setPlayerReady(int playerIndex);
    [[nodiscard]] bool isPlayerReady(int playerIndex) const;
    [[nodiscard]] bool areAllPlayersReady() const;
    [[nodiscard]] uint8_t getReadyMask() const { return m_playerReadyMask; }

private:
    void generateCode();

    std::array<char, kRoomCodeLength + 1> m_code{};
    std::array<HSteamNetConnection, kMaxPlayersPerRoom> m_connections{};
    std::array<NetPlayerState, kMaxPlayersPerRoom> m_playerStates{};  // Cached player states for broadcasting
    std::array<uint32_t, kMaxPlayersPerRoom> m_lastSequence{};        // Last accepted sequence per player
    HSteamNetConnection m_hostConnection = k_HSteamNetConnection_Invalid;
    int  m_playerCount = 0;
    bool m_active = false;
    bool m_gameStarted = false;
    bool m_dirty = false;  // True when player state changed, needs broadcast
    uint8_t m_playerReadyMask = 0;  // Bitmask of ready players
};

} // namespace relay

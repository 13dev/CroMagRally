#include "Room.h"
#include <cstdlib>
#include <algorithm>
#include "common/log.h"

namespace relay {

Room::Room()
{
    std::fill(m_connections.begin(), m_connections.end(), k_HSteamNetConnection_Invalid);
}

void Room::activate(HSteamNetConnection hostConn)
{
    generateCode();
    m_hostConnection = hostConn;
    m_connections[0] = hostConn;
    m_playerCount = 1;
    m_active = true;
    m_gameStarted = false;

    LOG_ROOM_INFO("Created room {} with host {}", m_code.data(), hostConn);
}

void Room::deactivate()
{
    LOG_ROOM_INFO("Destroying room {}", m_code.data());

    std::fill(m_code.begin(), m_code.end(), '\0');
    std::fill(m_connections.begin(), m_connections.end(), k_HSteamNetConnection_Invalid);
    for (auto& state : m_playerStates)
    {
        state = NetPlayerState{};
    }
    std::fill(m_lastSequence.begin(), m_lastSequence.end(), 0);
    m_hostConnection = k_HSteamNetConnection_Invalid;
    m_playerCount = 0;
    m_active = false;
    m_gameStarted = false;
    m_playerReadyMask = 0;
}

int Room::addPlayer(HSteamNetConnection conn)
{
    if (m_playerCount >= kMaxPlayersPerRoom)
        return -1;

    for (int i = 0; i < kMaxPlayersPerRoom; ++i)
    {
        if (m_connections[i] == k_HSteamNetConnection_Invalid)
        {
            m_connections[i] = conn;
            ++m_playerCount;
            LOG_ROOM_INFO("{} joined room {} as player {} ({} total)",
                          conn, m_code.data(), i, m_playerCount);
            return i;
        }
    }
    return -1;
}

void Room::removePlayer(HSteamNetConnection conn)
{
    for (int i = 0; i < kMaxPlayersPerRoom; ++i)
    {
        if (m_connections[i] == conn)
        {
            m_connections[i] = k_HSteamNetConnection_Invalid;
            --m_playerCount;
            LOG_ROOM_INFO("{} left room {} ({} remain)", conn, m_code.data(), m_playerCount);
            return;
        }
    }
}

int Room::getPlayerIndex(HSteamNetConnection conn) const
{
    for (int i = 0; i < kMaxPlayersPerRoom; ++i)
    {
        if (m_connections[i] == conn)
            return i;
    }
    return -1;
}

void Room::generateCode()
{
    for (int i = 0; i < kRoomCodeLength; ++i)
    {
        m_code[i] = kRoomCodeChars[std::rand() % (sizeof(kRoomCodeChars) - 1)];
    }
    m_code[kRoomCodeLength] = '\0';
}

bool Room::updatePlayerState(int playerIndex, const NetPlayerState& state)
{
    // Bounds validation
    if (playerIndex < 0 || playerIndex >= kMaxPlayersPerRoom)
    {
        LOG_ROOM_WARN("{}: Invalid playerIndex {} in updatePlayerState", m_code.data(), playerIndex);
        return false;
    }

    // Validate that connection exists for this player
    if (m_connections[playerIndex] == k_HSteamNetConnection_Invalid)
    {
        LOG_ROOM_WARN("{}: Player {} not connected, ignoring state update", m_code.data(), playerIndex);
        return false;
    }

    // Sequence validation - reject old states (with wraparound handling)
    uint32_t lastSeq = m_lastSequence[playerIndex];
    uint32_t newSeq = state.sequence;

    // Handle sequence wraparound: if difference is huge, it's likely a wraparound
    if (lastSeq > 0 && newSeq <= lastSeq)
    {
        // Check if it's a legitimate wraparound (new sequence near 0, old near max)
        bool isWraparound = (lastSeq > 0xFFFF0000 && newSeq < 0x0000FFFF);
        if (!isWraparound)
        {
            // Old packet, silently ignore
            return false;
        }
    }
    m_lastSequence[playerIndex] = newSeq;

    // Validate player_num in state matches playerIndex (or correct it)
    if (state.player_num != playerIndex)
    {
        LOG_ROOM_WARN("{}: State player_num mismatch: {} != {}, correcting",
                      m_code.data(), state.player_num, playerIndex);
        // Make a corrected copy
        NetPlayerState correctedState = state;
        correctedState.player_num = static_cast<uint8_t>(playerIndex);
        m_playerStates[playerIndex] = correctedState;
        return true;
    }

    m_playerStates[playerIndex] = state;
    return true;
}

void Room::setPlayerReady(int playerIndex)
{
    if (playerIndex >= 0 && playerIndex < kMaxPlayersPerRoom)
    {
        m_playerReadyMask |= (1 << playerIndex);
    }
}

bool Room::isPlayerReady(int playerIndex) const
{
    if (playerIndex < 0 || playerIndex >= kMaxPlayersPerRoom)
        return false;
    return (m_playerReadyMask & (1 << playerIndex)) != 0;
}

bool Room::areAllPlayersReady() const
{
    if (m_playerCount <= 0)
        return false;

    // Check that all connected players are ready
    for (int i = 0; i < kMaxPlayersPerRoom; ++i)
    {
        if (m_connections[i] != k_HSteamNetConnection_Invalid)
        {
            if (!(m_playerReadyMask & (1 << i)))
                return false;
        }
    }
    return true;
}

} // namespace relay

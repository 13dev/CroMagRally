#include "Room.h"
#include <cstdlib>
#include <cstdio>
#include <algorithm>

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

    std::printf("[Room] Created room %s with host %u\n", m_code.data(), hostConn);
}

void Room::deactivate()
{
    std::printf("[Room] Destroying room %s\n", m_code.data());

    std::fill(m_code.begin(), m_code.end(), '\0');
    std::fill(m_connections.begin(), m_connections.end(), k_HSteamNetConnection_Invalid);
    for (auto& state : m_playerStates)
    {
        state = NetPlayerState{};
    }
    m_hostConnection = k_HSteamNetConnection_Invalid;
    m_playerCount = 0;
    m_active = false;
    m_gameStarted = false;
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
            std::printf("[Room] %u joined room %s as player %d (%d total)\n",
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
            std::printf("[Room] %u left room %s (%d remain)\n",
                        conn, m_code.data(), m_playerCount);
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

void Room::updatePlayerState(int playerIndex, const NetPlayerState& state)
{
    if (playerIndex >= 0 && playerIndex < kMaxPlayersPerRoom)
    {
        m_playerStates[playerIndex] = state;
    }
}

} // namespace relay

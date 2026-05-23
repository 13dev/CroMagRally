#include "Server.h"
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <netdb.h>
#include <arpa/inet.h>
#include "common/log.h"

namespace relay {

Server* Server::s_instance = nullptr;

Server::Server()
{
    s_instance = this;
}

Server::~Server()
{
    stop();
    s_instance = nullptr;
}

bool Server::start(uint16_t port)
{
    m_interface = SteamNetworkingSockets();
    if (!m_interface)
    {
        LOG_SERVER_ERROR("Failed to get ISteamNetworkingSockets");
        return false;
    }

    SteamNetworkingIPAddr listenAddr;
    listenAddr.Clear();
    listenAddr.m_port = port;

    // On Fly.io, UDP must bind to fly-global-services, not 0.0.0.0
    // See: https://fly.io/docs/networking/udp-and-tcp/
    const char* flyApp = std::getenv("FLY_APP_NAME");
    if (flyApp)
    {
        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        struct addrinfo* result = nullptr;
        if (getaddrinfo("fly-global-services", nullptr, &hints, &result) == 0 && result)
        {
            auto* addr = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));
            listenAddr.SetIPv4(ntohl(addr->sin_addr.s_addr), port);
            LOG_SERVER_INFO("Fly.io detected, binding to {}:{} (fly-global-services)", ipStr, port);
            freeaddrinfo(result);
        }
        else
        {
            LOG_SERVER_WARN("Could not resolve fly-global-services, using 0.0.0.0");
        }
    }
    else
    {
        LOG_SERVER_INFO("Local mode, binding to 0.0.0.0:{}", port);
    }

    // Configure connection callback
    SteamNetworkingConfigValue_t opts[1];
    opts[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                   reinterpret_cast<void*>(onConnectionStatusChanged));

    m_listenSocket = m_interface->CreateListenSocketIP(listenAddr, 1, opts);
    if (m_listenSocket == k_HSteamListenSocket_Invalid)
    {
        LOG_SERVER_ERROR("Failed to create listen socket");
        return false;
    }

    m_pollGroup = m_interface->CreatePollGroup();
    if (m_pollGroup == k_HSteamNetPollGroup_Invalid)
    {
        LOG_SERVER_ERROR("Failed to create poll group");
        return false;
    }

    LOG_SERVER_INFO("Listening on port {} (max {} clients)", port, kMaxClients);
    return true;
}

void Server::stop()
{
    if (m_listenSocket != k_HSteamListenSocket_Invalid)
    {
        m_interface->CloseListenSocket(m_listenSocket);
        m_listenSocket = k_HSteamListenSocket_Invalid;
    }
    if (m_pollGroup != k_HSteamNetPollGroup_Invalid)
    {
        m_interface->DestroyPollGroup(m_pollGroup);
        m_pollGroup = k_HSteamNetPollGroup_Invalid;
    }
}

void Server::update()
{
    m_interface->RunCallbacks();

    // Drain the entire message queue to prevent overflow
    SteamNetworkingMessage_t* msgs[256];
    int numMsgs;

    while ((numMsgs = m_interface->ReceiveMessagesOnPollGroup(m_pollGroup, msgs, 256)) > 0)
    {
        for (int i = 0; i < numMsgs; ++i)
        {
            auto* msg = msgs[i];
            if (msg->m_cbSize > 0)
            {
                relayMessage(msg->m_conn,
                             static_cast<const uint8_t*>(msg->m_pData),
                             static_cast<size_t>(msg->m_cbSize));
            }
            msg->Release();
        }
    }

    // Broadcast world states to all players (equal-players model)
    // Called every tick - server is running at kTickRateHz (120Hz)
    broadcastWorldStates();

    // Send periodic pings to keep connections alive
    uint32_t now = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
    if (now - m_lastPingTime >= kPingIntervalMs)
    {
        m_lastPingTime = now;
        sendPingToAll();
    }
}

Server::Stats Server::getStats() const
{
    Stats stats;
    for (const auto& room : m_rooms)
    {
        if (room.isActive())
        {
            ++stats.activeRooms;
            stats.totalPlayers += room.getPlayerCount();
        }
    }
    return stats;
}

void Server::onClientConnect(HSteamNetConnection conn)
{
    LOG_SERVER_INFO("Client connected: {} (pending JOIN_REQUEST)", conn);
    m_interface->SetConnectionPollGroup(conn, m_pollGroup);

    // Add to pending - wait for JOIN_REQUEST to assign to room
    m_pendingConnections.insert(conn);
}

void Server::onClientDisconnect(HSteamNetConnection conn)
{
    LOG_SERVER_INFO("Client disconnected: {}", conn);
    m_pendingConnections.erase(conn);
    removeFromRoom(conn);
}

Room* Server::findAvailableRoom()
{
    for (auto& room : m_rooms)
    {
        if (room.isActive() && !room.isGameStarted() && !room.isFull())
        {
            return &room;
        }
    }
    return nullptr;
}

Room* Server::findRoomByCode(const char* code)
{
    std::string key(code, kRoomCodeLength);
    auto it = m_roomsByCode.find(key);
    return (it != m_roomsByCode.end()) ? it->second : nullptr;
}

Room* Server::createRoom(HSteamNetConnection hostConn)
{
    for (auto& room : m_rooms)
    {
        if (!room.isActive())
        {
            room.activate(hostConn);
            m_connectionToRoom[hostConn] = static_cast<int>(&room - m_rooms.data());
            // Add to room code lookup map
            m_roomsByCode[std::string(room.getCode(), kRoomCodeLength)] = &room;
            return &room;
        }
    }
    return nullptr;
}

Room* Server::findRoomByConnection(HSteamNetConnection conn)
{
    auto it = m_connectionToRoom.find(conn);
    if (it != m_connectionToRoom.end())
    {
        int idx = it->second;
        if (idx >= 0 && idx < kMaxRooms && m_rooms[idx].isActive())
        {
            return &m_rooms[idx];
        }
    }
    return nullptr;
}

void Server::removeFromRoom(HSteamNetConnection conn)
{
    Room* room = findRoomByConnection(conn);
    if (!room)
        return;

    bool wasHost = room->isHost(conn);
    room->removePlayer(conn);
    m_connectionToRoom.erase(conn);

    // If host left, destroy the room
    if (wasHost)
    {
        // Remove all other players from tracking
        for (auto existingConn : room->getConnections())
        {
            if (existingConn != k_HSteamNetConnection_Invalid)
            {
                m_connectionToRoom.erase(existingConn);
            }
        }
        // Remove from room code lookup before deactivating
        m_roomsByCode.erase(std::string(room->getCode(), kRoomCodeLength));
        room->deactivate();
    }
    else if (room->getPlayerCount() <= 0)
    {
        // Remove from room code lookup before deactivating
        m_roomsByCode.erase(std::string(room->getCode(), kRoomCodeLength));
        room->deactivate();
    }
}

void Server::handleJoinRequest(HSteamNetConnection conn, const JoinRequestMsg* msg)
{
    // Check if connection already in a room (duplicate JOIN_REQUEST)
    auto it = m_connectionToRoom.find(conn);
    if (it != m_connectionToRoom.end())
    {
        LOG_SERVER_DEBUG("Duplicate JOIN_REQUEST from already-joined connection {}", conn);
        // Send current room assignment again (idempotent response)
        Room* existingRoom = findRoomByConnection(conn);
        if (existingRoom)
        {
            int idx = existingRoom->getPlayerIndex(conn);
            sendJoinResponse(conn, true);
            sendRoomAssignment(conn, existingRoom, idx, existingRoom->isHost(conn));
        }
        return;
    }

    // Remove from pending
    m_pendingConnections.erase(conn);

    // Check if room code is empty (all zeros) -> create new room (host)
    bool isCreateRequest = true;
    for (int i = 0; i < kRoomCodeLength; i++)
    {
        if (msg->roomCode[i] != '\0')
        {
            isCreateRequest = false;
            break;
        }
    }

    if (isCreateRequest)
    {
        // Host wants to create a new room
        Room* room = createRoom(conn);
        if (room)
        {
            LOG_ROOM_INFO("Created room {} with host {}", room->getCode(), conn);
            sendJoinResponse(conn, true);
            sendRoomAssignment(conn, room, 0, true);
        }
        else
        {
            sendJoinResponse(conn, false, "Server full - no rooms available");
            m_interface->CloseConnection(conn, 0, "No rooms available", false);
        }
    }
    else
    {
        // Client wants to join an existing room by code
        Room* room = findRoomByCode(msg->roomCode);
        if (!room)
        {
            char errorMsg[64];
            std::snprintf(errorMsg, sizeof(errorMsg), "Room '%.4s' not found", msg->roomCode);
            LOG_ROOM_WARN("{} (client {})", errorMsg, conn);
            sendJoinResponse(conn, false, errorMsg);
            m_interface->CloseConnection(conn, 0, errorMsg, false);
            return;
        }

        if (room->isFull())
        {
            LOG_ROOM_WARN("Room {} is full (client {})", room->getCode(), conn);
            sendJoinResponse(conn, false, "Room is full");
            m_interface->CloseConnection(conn, 0, "Room is full", false);
            return;
        }

        if (room->isGameStarted())
        {
            LOG_ROOM_WARN("Room {} game already started (client {})", room->getCode(), conn);
            sendJoinResponse(conn, false, "Game already started");
            m_interface->CloseConnection(conn, 0, "Game already started", false);
            return;
        }

        // Join the room
        int playerIndex = room->addPlayer(conn);
        if (playerIndex < 0)
        {
            sendJoinResponse(conn, false, "Failed to join room");
            m_interface->CloseConnection(conn, 0, "Failed to join room", false);
            return;
        }

        m_connectionToRoom[conn] = static_cast<int>(room - m_rooms.data());
        LOG_ROOM_INFO("Client {} joined room {} as player {}", conn, room->getCode(), playerIndex);

        sendJoinResponse(conn, true);
        sendRoomAssignment(conn, room, playerIndex, false);

        // Notify existing players of new player count
        for (auto existingConn : room->getConnections())
        {
            if (existingConn != k_HSteamNetConnection_Invalid && existingConn != conn)
            {
                int idx = room->getPlayerIndex(existingConn);
                sendRoomAssignment(existingConn, room, idx, room->isHost(existingConn));
            }
        }
    }
}

void Server::sendJoinResponse(HSteamNetConnection conn, bool success, const char* error)
{
    JoinResponseMsg msg;
    msg.success = success ? 1 : 0;
    if (error)
    {
        std::strncpy(msg.errorMsg, error, sizeof(msg.errorMsg) - 1);
        msg.errorMsg[sizeof(msg.errorMsg) - 1] = '\0';
    }
    else
    {
        msg.errorMsg[0] = '\0';
    }

    m_interface->SendMessageToConnection(conn, &msg, sizeof(msg), k_nSteamNetworkingSend_Reliable, nullptr);
}

void Server::relayMessage(HSteamNetConnection sender, const uint8_t* data, size_t size)
{
    // Global message size validation
    if (size < 1)
    {
        LOG_SERVER_WARN("Empty message from conn {}", sender);
        return;
    }
    if (size > kMaxMessageSize)
    {
        LOG_SERVER_WARN("Oversized message ({} bytes) from conn {}, dropping", size, sender);
        return;
    }

    auto msgType = static_cast<MsgType>(data[0]);

    // Per-message-type size validation
    switch (msgType)
    {
        case MsgType::JOIN_REQUEST:
            if (size < sizeof(JoinRequestMsg))
            {
                LOG_SERVER_WARN("JOIN_REQUEST too small ({} < {}) from conn {}",
                                size, sizeof(JoinRequestMsg), sender);
                return;
            }
            break;
        case MsgType::PLAYER_STATE:
            if (size < 1 + sizeof(NetPlayerState))
            {
                LOG_SERVER_WARN("PLAYER_STATE too small ({} < {}) from conn {}",
                                size, 1 + sizeof(NetPlayerState), sender);
                return;
            }
            break;
        case MsgType::GAME_START:
            if (size < sizeof(GameStartMsg))
            {
                LOG_SERVER_WARN("GAME_START too small ({} < {}) from conn {}",
                                size, sizeof(GameStartMsg), sender);
                return;
            }
            break;
        case MsgType::PLAYER_NAME:
            if (size < sizeof(PlayerNameMsg))
            {
                LOG_SERVER_WARN("PLAYER_NAME too small ({} < {}) from conn {}",
                                size, sizeof(PlayerNameMsg), sender);
                return;
            }
            break;
        case MsgType::WEAPON_EVENT:
            if (size < sizeof(NetWeaponEventMsg))
            {
                LOG_SERVER_WARN("WEAPON_EVENT too small ({} < {}) from conn {}",
                                size, sizeof(NetWeaponEventMsg), sender);
                return;
            }
            break;
        default:
            // Other message types pass through without strict size check
            break;
    }

    // Handle JOIN_REQUEST from pending connections (before room assignment)
    if (msgType == MsgType::JOIN_REQUEST)
    {
        if (size >= sizeof(JoinRequestMsg))
        {
            handleJoinRequest(sender, reinterpret_cast<const JoinRequestMsg*>(data));
        }
        return;  // Don't relay
    }

    // Handle PONG - just a keep-alive response, no action needed
    if (msgType == MsgType::PONG)
    {
        // Connection is alive, nothing to do
        return;  // Don't relay
    }

    // Handle WEAPON_EVENT - relay to all OTHER players in the room
    if (msgType == MsgType::WEAPON_EVENT)
    {
        Room* room = findRoomByConnection(sender);
        if (room)
        {
            // Relay to all players except sender
            for (auto conn : room->getConnections())
            {
                if (conn != k_HSteamNetConnection_Invalid && conn != sender)
                {
                    m_interface->SendMessageToConnection(
                        conn, data, static_cast<uint32_t>(size),
                        k_nSteamNetworkingSend_Reliable, nullptr
                    );
                }
            }

            // Debug log (limited)
            static int sWeaponLogCount = 0;
            if (sWeaponLogCount++ < 20)
            {
                LOG_SERVER_DEBUG("Relayed WEAPON_EVENT from conn {} to room {}", sender, room->getCode());
            }
        }
        return;  // Don't do default relay
    }

    // Handle PLAYER_STATE: decode packed struct and collect state for world broadcast
    if (msgType == MsgType::PLAYER_STATE)
    {
        if (size >= 1 + sizeof(NetPlayerState))
        {
            NetPlayerState state = {};
            size_t decoded = net_decode_player_state(data, size, &state);

            if (decoded > 0)
            {
                handlePlayerState(sender, &state);
            }
            else
            {
                LOG_SERVER_WARN("Failed to decode PlayerState: invalid message");
            }
        }
        return;  // Don't relay - server broadcasts WORLD_STATE instead
    }

    Room* room = findRoomByConnection(sender);
    if (!room)
    {
        LOG_SERVER_DEBUG("relayMessage: No room for conn {} (msgType={}), dropping",
                         sender, static_cast<int>(msgType));
        return;
    }

    // Debug: log message routing (limit to avoid spam)
    static int sLogCount = 0;
    if (sLogCount++ < 50)
    {
        LOG_SERVER_DEBUG("relay: type={}, sender={}, isHost={}, room={}",
                         static_cast<int>(msgType), sender, room->isHost(sender), room->getCode());
    }

    // Handle game start
    if (msgType == MsgType::GAME_START && room->isHost(sender))
    {
        room->startGame();
        LOG_SERVER_INFO("Room {} game started", room->getCode());
    }

    // Determine send flags
    int flags = isReliableMessage(msgType)
              ? k_nSteamNetworkingSend_Reliable
              : k_nSteamNetworkingSend_UnreliableNoDelay;

    // Broadcast SYNC, VEHICLE_TYPE, and PLAYER_NAME to ALL players (not just host)
    // These messages are needed by all clients for synchronization barriers and lobby display
    bool shouldBroadcastToAll = (msgType == MsgType::SYNC ||
                                  msgType == MsgType::VEHICLE_TYPE ||
                                  msgType == MsgType::PLAYER_NAME);

    if (shouldBroadcastToAll)
    {
        // Broadcast to all players except sender
        for (auto conn : room->getConnections())
        {
            if (conn != k_HSteamNetConnection_Invalid && conn != sender)
            {
                m_interface->SendMessageToConnection(conn, data,
                    static_cast<uint32_t>(size), flags, nullptr);
            }
        }
        return;  // Don't fall through to host-only routing
    }

    if (room->isHost(sender))
    {
        // Host -> broadcast to all clients
        for (auto conn : room->getConnections())
        {
            if (conn != k_HSteamNetConnection_Invalid && conn != sender)
            {
                m_interface->SendMessageToConnection(conn, data, static_cast<uint32_t>(size), flags, nullptr);
            }
        }
    }
    else
    {
        // Client -> forward to host only
        HSteamNetConnection host = room->getHost();
        if (host != k_HSteamNetConnection_Invalid)
        {
            m_interface->SendMessageToConnection(host, data, static_cast<uint32_t>(size), flags, nullptr);
        }
    }
}

void Server::sendRoomAssignment(HSteamNetConnection conn, const Room* room, int playerIndex, bool isHost)
{
    RoomAssignmentMsg msg;
    msg.setRoomCode(room->getCode());
    msg.playerIndex = playerIndex;
    msg.playerCount = room->getPlayerCount();
    msg.isHost = isHost ? 1 : 0;

    m_interface->SendMessageToConnection(conn, &msg, sizeof(msg), k_nSteamNetworkingSend_Reliable, nullptr);

    LOG_SERVER_DEBUG("Sent room assignment to {}: room={}, player={}, isHost={}",
                     conn, room->getCode(), playerIndex, isHost);
}

void Server::onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
{
    if (!s_instance)
        return;

    switch (info->m_info.m_eState)
    {
        case k_ESteamNetworkingConnectionState_Connecting:
            if (s_instance->m_interface->AcceptConnection(info->m_hConn) != k_EResultOK)
            {
                s_instance->m_interface->CloseConnection(info->m_hConn, 0, nullptr, false);
                LOG_SERVER_WARN("Failed to accept connection");
            }
            break;

        case k_ESteamNetworkingConnectionState_Connected:
            s_instance->onClientConnect(info->m_hConn);
            break;

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            s_instance->onClientDisconnect(info->m_hConn);
            s_instance->m_interface->CloseConnection(info->m_hConn, 0, nullptr, false);
            break;

        default:
            break;
    }
}

void Server::handlePlayerState(HSteamNetConnection conn, const NetPlayerState* state)
{
    Room* room = findRoomByConnection(conn);
    if (!room)
        return;

    int playerIndex = room->getPlayerIndex(conn);
    if (playerIndex < 0)
        return;

    // Store the player's state for later broadcast
    room->updatePlayerState(playerIndex, *state);

    // Debug log (limit spam)
    static int sStateLogCount = 0;
    if (sStateLogCount++ < 20)
    {
        LOG_SERVER_DEBUG("PlayerState STORED P{} (conn {}): pos=({:.0f},{:.0f},{:.0f})",
                         playerIndex, conn, state->pos_x, state->pos_y, state->pos_z);

        // Verify it was stored
        [[maybe_unused]] const auto& stored = room->getPlayerState(playerIndex);
        LOG_SERVER_DEBUG("PlayerState VERIFY P{}: pos=({:.0f},{:.0f},{:.0f})",
                         playerIndex, stored.pos_x, stored.pos_y, stored.pos_z);
    }
}

void Server::broadcastWorldStates()
{
    // For each active room with game started, broadcast world state to all players
    for (auto& room : m_rooms)
    {
        // Skip inactive rooms, pre-game lobbies, and rooms with no state changes
        if (!room.isActive() || !room.isGameStarted() || !room.isDirty())
            continue;

        room.clearDirty();

        // Build packed world state message
        NetWorldState world = {};
        world.sequence = ++m_worldSequence;
        world.server_time_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );

        // Copy all player states
        const auto& states = room.getAllPlayerStates();
        world.player_count = 0;
        for (int i = 0; i < kNetMaxPlayers && i < kMaxPlayersPerRoom; ++i)
        {
            if (room.getConnections()[i] != k_HSteamNetConnection_Invalid)
            {
                world.players[world.player_count] = states[i];
                // Ensure player_num is set correctly
                world.players[world.player_count].player_num = i;
                world.player_count++;
            }
        }

        // Debug: log broadcasts with VALID data only (not zeros)
        bool hasValidData = false;
        for (int i = 0; i < (int)world.player_count; ++i)
        {
            if (world.players[i].pos_x != 0.0f || world.players[i].pos_z != 0.0f)
            {
                hasValidData = true;
                break;
            }
        }

        static int sValidBroadcastLogCount = 0;
        if (hasValidData && sValidBroadcastLogCount++ < 20)
        {
            LOG_SERVER_DEBUG("Broadcasting WorldState WITH DATA: seq={}, players={}",
                             world.sequence, static_cast<int>(world.player_count));
            for (int i = 0; i < static_cast<int>(world.player_count); ++i)
            {
                LOG_SERVER_DEBUG("  P{}: pos=({:.0f},{:.0f},{:.0f})",
                                 world.players[i].player_num,
                                 world.players[i].pos_x,
                                 world.players[i].pos_y,
                                 world.players[i].pos_z);
            }
        }

        // Encode with memcpy: type byte + packed struct data
        uint8_t buffer[1 + sizeof(NetWorldState)];
        size_t msgSize = net_encode_world_state(buffer, sizeof(buffer), &world);

        if (msgSize == 0)
        {
            LOG_SERVER_ERROR("Failed to encode WorldState");
            continue;
        }

        // Broadcast to all players in the room
        for (auto conn : room.getConnections())
        {
            if (conn != k_HSteamNetConnection_Invalid)
            {
                m_interface->SendMessageToConnection(
                    conn, buffer, static_cast<uint32_t>(msgSize),
                    k_nSteamNetworkingSend_UnreliableNoDelay, nullptr
                );
            }
        }
    }
}

void Server::sendPingToAll()
{
    NetPingMsg ping;
    ping.type = static_cast<uint8_t>(MsgType::PING);
    ping.server_time_ms = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );

    // Send to all active connections (both pending and in rooms)
    for (auto conn : m_pendingConnections)
    {
        if (conn != k_HSteamNetConnection_Invalid)
        {
            m_interface->SendMessageToConnection(
                conn, &ping, sizeof(ping),
                k_nSteamNetworkingSend_Reliable, nullptr
            );
        }
    }

    for (const auto& room : m_rooms)
    {
        if (!room.isActive())
            continue;

        for (auto conn : room.getConnections())
        {
            if (conn != k_HSteamNetConnection_Invalid)
            {
                m_interface->SendMessageToConnection(
                    conn, &ping, sizeof(ping),
                    k_nSteamNetworkingSend_Reliable, nullptr
                );
            }
        }
    }
}

} // namespace relay

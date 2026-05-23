/****************************/
/*     BACKEND_GNS.CPP      */
/* GameNetworkingSockets    */
/* UDP networking client    */
/****************************/

#ifdef USE_GNS

extern "C" {
#include "Backend_Network.h"
}

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

// Shared network protocol (plain packed structs, no protobuf)
#include "common/net_protocol.h"
#include "common/log.h"

//==============================================================================
// CONSTANTS
//==============================================================================

static const int kMaxMessageSize = 4096;

//==============================================================================
// MESSAGE TYPES (wire protocol)
// Using NetMsgType from common/net_protocol.h as single source of truth.
// MsgType alias with UPPER_CASE values for consistency with existing code.
//==============================================================================

enum class MsgType : uint8_t
{
    CONFIG          = static_cast<uint8_t>(NetMsgType::config),
    SYNC            = static_cast<uint8_t>(NetMsgType::sync),
    HOST_CONTROL    = static_cast<uint8_t>(NetMsgType::host_control),
    CLIENT_CONTROL  = static_cast<uint8_t>(NetMsgType::client_control),
    VEHICLE_TYPE    = static_cast<uint8_t>(NetMsgType::vehicle_type),
    PLAYER_STATE    = static_cast<uint8_t>(NetMsgType::player_state),
    WORLD_STATE     = static_cast<uint8_t>(NetMsgType::world_state),
    PING            = static_cast<uint8_t>(NetMsgType::ping),
    PONG            = static_cast<uint8_t>(NetMsgType::pong),
    WEAPON_EVENT    = static_cast<uint8_t>(NetMsgType::weapon_event),
    ROOM_ASSIGNMENT = static_cast<uint8_t>(NetMsgType::room_assignment),
    GAME_START      = static_cast<uint8_t>(NetMsgType::game_start),
    PLAYER_NAME     = static_cast<uint8_t>(NetMsgType::player_name),
    JOIN_REQUEST    = static_cast<uint8_t>(NetMsgType::join_request),
    JOIN_RESPONSE   = static_cast<uint8_t>(NetMsgType::join_response),
};

#pragma pack(push, 1)
struct RoomAssignmentMsg
{
    uint8_t type;
    char roomCode[4];
    int32_t playerIndex;
    int32_t playerCount;
    int32_t isHost;
};

struct GameStartMsg
{
    uint8_t type;
    int32_t playerCount;
};

struct PlayerNameMsg
{
    uint8_t type;
    int32_t playerIndex;
    char name[31];
};

struct JoinRequestMsg
{
    uint8_t type;
    char roomCode[4];  // Empty (all zeros) = create new room (host)
    char playerName[32];
};

struct JoinResponseMsg
{
    uint8_t type;
    uint8_t success;
    char errorMsg[64];
};

// Equal-players model: use packed NetPlayerState and NetWorldState
// (defined in common/net_protocol.h)
#pragma pack(pop)

//==============================================================================
// INTERNAL STATE
//==============================================================================

static bool                         gNetInitialized = false;
static NetConnectionState           gConnectionState = NET_STATE_DISCONNECTED;
static char                         gLastError[256] = "";

// Server address
static char                         gServerHost[128] = NET_DEFAULT_SERVER_HOST;
static uint16_t                     gServerPort = NET_SERVER_PORT;

// GNS state
static ISteamNetworkingSockets*     gInterface = nullptr;
static HSteamNetConnection          gConnection = k_HSteamNetConnection_Invalid;

// Room/session state
static char                         gRoomCode[NET_ROOM_CODE_LENGTH + 1] = "";
static char                         gLocalPlayerName[NET_PLAYER_NAME_LENGTH] = "Player";
static int                          gLocalPlayerIndex = -1;
static bool                         gIsHosting = false;
static int                          gPlayerCount = 0;

// Join retry state (for handling race condition when room not yet queryable)
static int                          gJoinRetryCount = 0;
static const int                    kMaxJoinRetries = 3;
static const int                    kJoinRetryDelayMs = 500;
static uint32_t                     gJoinRetryTime = 0;

// Player tracking
static char                         gPlayerNames[NET_MAX_PLAYERS][NET_PLAYER_NAME_LENGTH];
static bool                         gPlayerActive[NET_MAX_PLAYERS];

// Callbacks
static NetConnectCallback           gConnectCallback = nullptr;
static NetDisconnectCallback        gDisconnectCallback = nullptr;
static NetReceiveCallback           gReceiveCallback = nullptr;
static NetStateChangeCallback       gStateChangeCallback = nullptr;
static Net_PlayerNameCallback       gPlayerNameCallback = nullptr;
static Net_WorldStateCallback       gWorldStateCallback = nullptr;
static Net_WeaponEventCallback      gWeaponEventCallback = nullptr;

//==============================================================================
// FORWARD DECLARATIONS
//==============================================================================

static void SetState(NetConnectionState state, const char* message);
static void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);
static void RetryJoinRoom(void);
static void SendJoinRequest(void);

//==============================================================================
// HELPER FUNCTIONS
//==============================================================================

static void SetLastError(const char* error)
{
    strncpy(gLastError, error, sizeof(gLastError) - 1);
    gLastError[sizeof(gLastError) - 1] = '\0';
}

static void SetState(NetConnectionState state, const char* message)
{
    if (state == gConnectionState)
        return;

    gConnectionState = state;
    LOG_GNS_INFO("State changed to {}: {}", static_cast<int>(state), message ? message : "");

    if (gStateChangeCallback)
        gStateChangeCallback(state, message);
}

static uint32_t GetTicksMs(void)
{
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

//==============================================================================
// JOIN REQUEST HELPERS
//==============================================================================

static void SendJoinRequest(void)
{
    if (gConnection == k_HSteamNetConnection_Invalid)
        return;

    JoinRequestMsg joinMsg;
    joinMsg.type = (uint8_t)MsgType::JOIN_REQUEST;
    if (gIsHosting)
    {
        // Empty room code = create new room
        memset(joinMsg.roomCode, 0, sizeof(joinMsg.roomCode));
    }
    else
    {
        // Join specific room by code
        memcpy(joinMsg.roomCode, gRoomCode, 4);
    }
    strncpy(joinMsg.playerName, gLocalPlayerName, sizeof(joinMsg.playerName) - 1);
    joinMsg.playerName[sizeof(joinMsg.playerName) - 1] = '\0';

    gInterface->SendMessageToConnection(gConnection, &joinMsg, sizeof(joinMsg),
        k_nSteamNetworkingSend_Reliable, nullptr);
    LOG_GNS_INFO("Sent JOIN_REQUEST (roomCode={:.4s}, hosting={}, attempt={})",
                 gIsHosting ? "####" : gRoomCode, gIsHosting, gJoinRetryCount + 1);
}

static void RetryJoinRoom(void)
{
    if (gJoinRetryCount >= kMaxJoinRetries)
    {
        LOG_GNS_WARN("Join retry limit reached ({} attempts)", kMaxJoinRetries);
        SetLastError("Room not found after multiple attempts");
        SetState(NET_STATE_ERROR, "Room not found");
        return;
    }

    gJoinRetryCount++;
    gJoinRetryTime = GetTicksMs() + kJoinRetryDelayMs;
    LOG_GNS_INFO("Will retry join in {}ms (attempt {}/{})",
                 kJoinRetryDelayMs, gJoinRetryCount + 1, kMaxJoinRetries);
}

static void DebugOutput(ESteamNetworkingSocketsDebugOutputType eType, const char* pszMsg)
{
    switch (eType)
    {
        case k_ESteamNetworkingSocketsDebugOutputType_Error:
            LOG_GNS_ERROR("{}", pszMsg);
            break;
        case k_ESteamNetworkingSocketsDebugOutputType_Warning:
            LOG_GNS_WARN("{}", pszMsg);
            break;
        case k_ESteamNetworkingSocketsDebugOutputType_Msg:
            LOG_GNS_INFO("{}", pszMsg);
            break;
        default:
            LOG_GNS_DEBUG("{}", pszMsg);
            break;
    }
}

//==============================================================================
// NETWORK LIFECYCLE
//==============================================================================

bool Net_Initialize(void)
{
    if (gNetInitialized)
        return true;

    // Initialize logging
    cromag::log::init();

    LOG_GNS_INFO("Initializing GameNetworkingSockets...");

    // Initialize GNS
    SteamDatagramErrMsg errMsg;
    if (!GameNetworkingSockets_Init(nullptr, errMsg))
    {
        SetLastError(errMsg);
        LOG_GNS_ERROR("Init failed: {}", errMsg);
        return false;
    }

    // Set up debug output
    SteamNetworkingUtils()->SetDebugOutputFunction(
        k_ESteamNetworkingSocketsDebugOutputType_Msg, DebugOutput);

    // Get the interface
    gInterface = SteamNetworkingSockets();
    if (!gInterface)
    {
        SetLastError("Failed to get ISteamNetworkingSockets interface");
        GameNetworkingSockets_Kill();
        return false;
    }

    // Reset state
    gRoomCode[0] = '\0';
    gLocalPlayerIndex = -1;
    gIsHosting = false;
    gPlayerCount = 0;

    // Clear player tracking
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
    {
        gPlayerActive[i] = false;
        gPlayerNames[i][0] = '\0';
    }

    gNetInitialized = true;
    SetState(NET_STATE_DISCONNECTED, "Initialized");
    LOG_GNS_INFO("Network initialized (GameNetworkingSockets)");
    return true;
}

void Net_Shutdown(void)
{
    if (!gNetInitialized)
        return;

    Net_CleanupSession();
    GameNetworkingSockets_Kill();
    gInterface = nullptr;

    gNetInitialized = false;
    LOG_GNS_INFO("Network shutdown");
}

bool Net_IsInitialized(void)
{
    return gNetInitialized;
}

void Net_SetSignalingServer(const char* host, uint16_t port)
{
    if (host)
    {
        strncpy(gServerHost, host, sizeof(gServerHost) - 1);
        gServerHost[sizeof(gServerHost) - 1] = '\0';
    }
    if (port > 0)
    {
        gServerPort = port;
    }
}

//==============================================================================
// CONNECTION MANAGEMENT
//==============================================================================

static bool ConnectToServer(void)
{
    LOG_GNS_INFO("Connecting to {}:{}...", gServerHost, gServerPort);

    if (gConnection != k_HSteamNetConnection_Invalid)
    {
        LOG_GNS_DEBUG("Already connected");
        return true;
    }

    // Parse server address
    SteamNetworkingIPAddr serverAddr;
    if (!serverAddr.ParseString(gServerHost))
    {
        // Try as hostname:port
        char addrStr[256];
        snprintf(addrStr, sizeof(addrStr), "%s:%d", gServerHost, gServerPort);
        if (!serverAddr.ParseString(addrStr))
        {
            SetLastError("Failed to parse server address");
            return false;
        }
    }
    else
    {
        serverAddr.m_port = gServerPort;
    }

    // Set connection options
    SteamNetworkingConfigValue_t opts[2];
    opts[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                   (void*)OnConnectionStatusChanged);
    opts[1].SetInt32(k_ESteamNetworkingConfig_TimeoutConnected, 30000); // 30 sec timeout

    // Connect
    gConnection = gInterface->ConnectByIPAddress(serverAddr, 2, opts);
    if (gConnection == k_HSteamNetConnection_Invalid)
    {
        SetLastError("Failed to create connection");
        return false;
    }

    LOG_GNS_INFO("Connection initiated: handle={}", gConnection);
    return true;
}

static void DisconnectFromServer(void)
{
    if (gConnection != k_HSteamNetConnection_Invalid)
    {
        gInterface->CloseConnection(gConnection, 0, "Disconnecting", true);
        gConnection = k_HSteamNetConnection_Invalid;
    }
}

//==============================================================================
// CONNECTION STATUS CALLBACK
//==============================================================================

static void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
{
    LOG_GNS_DEBUG("Connection status: {} -> {}",
                  info->m_eOldState, info->m_info.m_eState);

    switch (info->m_info.m_eState)
    {
        case k_ESteamNetworkingConnectionState_Connected:
        {
            LOG_GNS_INFO("Connected to server!");
            SetState(NET_STATE_WAITING_ROOM, "Connected, sending join request...");

            // Reset retry state and send initial join request
            gJoinRetryCount = 0;
            gJoinRetryTime = 0;
            SendJoinRequest();
            break;
        }

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            LOG_GNS_WARN("Connection lost: {}", info->m_info.m_szEndDebug);
            gConnection = k_HSteamNetConnection_Invalid;
            SetLastError(info->m_info.m_szEndDebug);
            SetState(NET_STATE_ERROR, "Connection lost");
            if (gDisconnectCallback)
                gDisconnectCallback(gLocalPlayerIndex);
            break;

        case k_ESteamNetworkingConnectionState_Connecting:
            SetState(NET_STATE_CONNECTING_SIGNALING, "Connecting...");
            break;

        default:
            break;
    }
}

//==============================================================================
// HOST FUNCTIONS
//==============================================================================

bool Net_CreateHost(const char* playerName)
{
    LOG_GNS_INFO("Net_CreateHost called");

    if (!gNetInitialized)
    {
        SetLastError("Network not initialized");
        return false;
    }

    if (gConnectionState != NET_STATE_DISCONNECTED)
    {
        SetLastError("Already connected");
        return false;
    }

    if (playerName)
    {
        strncpy(gLocalPlayerName, playerName, sizeof(gLocalPlayerName) - 1);
        gLocalPlayerName[sizeof(gLocalPlayerName) - 1] = '\0';
    }

    SetState(NET_STATE_CONNECTING_SIGNALING, "Connecting to server...");

    if (!ConnectToServer())
    {
        SetState(NET_STATE_ERROR, gLastError);
        return false;
    }

    gIsHosting = true;
    gLocalPlayerIndex = 0;
    gPlayerCount = 1;
    gPlayerActive[0] = true;
    strncpy(gPlayerNames[0], gLocalPlayerName, NET_PLAYER_NAME_LENGTH);

    return true;
}

const char* Net_GetRoomCode(void)
{
    return gRoomCode[0] ? gRoomCode : nullptr;
}

bool Net_IsHosting(void)
{
    return gIsHosting;
}

int Net_GetPlayerCount(void)
{
    return gPlayerCount;
}

int Net_GetP2PConnectionCount(void)
{
    return gPlayerCount > 0 ? gPlayerCount - 1 : 0;
}

bool Net_StartGame(void)
{
    if (!gIsHosting)
    {
        SetLastError("Not hosting");
        return false;
    }

    if (gConnectionState != NET_STATE_IN_LOBBY)
    {
        SetLastError("Not in lobby");
        return false;
    }

    // Send GAME_START message to server
    if (gConnection != k_HSteamNetConnection_Invalid)
    {
        GameStartMsg msg;
        msg.type = (uint8_t)MsgType::GAME_START;
        msg.playerCount = gPlayerCount;
        gInterface->SendMessageToConnection(gConnection, &msg, sizeof(msg),
            k_nSteamNetworkingSend_Reliable, nullptr);
        LOG_GNS_INFO("Sent GAME_START (playerCount={})", gPlayerCount);
    }

    SetState(NET_STATE_CONNECTED, "Game started");
    return true;
}

//==============================================================================
// CLIENT FUNCTIONS
//==============================================================================

bool Net_JoinGame(const char* roomCode, const char* playerName)
{
    if (!gNetInitialized)
    {
        SetLastError("Network not initialized");
        return false;
    }

    if (gConnectionState != NET_STATE_DISCONNECTED)
    {
        SetLastError("Already connected");
        return false;
    }

    if (!roomCode || strlen(roomCode) != NET_ROOM_CODE_LENGTH)
    {
        SetLastError("Invalid room code");
        return false;
    }

    if (playerName)
    {
        strncpy(gLocalPlayerName, playerName, sizeof(gLocalPlayerName) - 1);
        gLocalPlayerName[sizeof(gLocalPlayerName) - 1] = '\0';
    }

    strncpy(gRoomCode, roomCode, NET_ROOM_CODE_LENGTH);
    gRoomCode[NET_ROOM_CODE_LENGTH] = '\0';

    SetState(NET_STATE_CONNECTING_SIGNALING, "Connecting to server...");

    if (!ConnectToServer())
    {
        SetState(NET_STATE_ERROR, gLastError);
        return false;
    }

    gIsHosting = false;
    return true;
}

void Net_Disconnect(void)
{
    Net_CleanupSession();
}

bool Net_IsConnected(void)
{
    return gConnectionState == NET_STATE_CONNECTED ||
           gConnectionState == NET_STATE_IN_LOBBY;
}

int Net_GetLocalPlayerIndex(void)
{
    return gLocalPlayerIndex;
}

//==============================================================================
// MESSAGING
//==============================================================================

static void SendMessage(const void* data, size_t size, bool reliable)
{
    if (gConnection == k_HSteamNetConnection_Invalid || !data || size == 0)
        return;

    int flags = reliable ? k_nSteamNetworkingSend_Reliable
                         : k_nSteamNetworkingSend_UnreliableNoDelay;

    gInterface->SendMessageToConnection(gConnection, data, (uint32_t)size, flags, nullptr);
}

void Net_SendToAll(const void* data, size_t size, bool reliable)
{
    SendMessage(data, size, reliable);
}

void Net_SendToPeer(int peerIndex, const void* data, size_t size, bool reliable)
{
    (void)peerIndex;  // Server handles routing
    SendMessage(data, size, reliable);
}

void Net_SendToHost(const void* data, size_t size, bool reliable)
{
    SendMessage(data, size, reliable);
}

//==============================================================================
// TYPED SEND FUNCTIONS
//==============================================================================

void Net_SendConfig(int peerIndex, int gameMode, int age, int trackNum,
                    int playerNum, int numPlayers, int numAgesCompleted,
                    int difficulty, int tagDuration)
{
    (void)peerIndex;
    if (gConnection == k_HSteamNetConnection_Invalid) return;

    NetConfigMsg msg;
    msg.type = (uint8_t)MsgType::CONFIG;
    msg.game_mode = gameMode;
    msg.age = age;
    msg.track_num = trackNum;
    msg.player_num = playerNum;
    msg.num_players = numPlayers;
    msg.num_ages_completed = (int16_t)numAgesCompleted;
    msg.difficulty = (int16_t)difficulty;
    msg.tag_duration = (int16_t)tagDuration;

    gInterface->SendMessageToConnection(gConnection, &msg, sizeof(msg),
        k_nSteamNetworkingSend_Reliable, nullptr);
    LOG_GNS_INFO("Sent CONFIG: mode={}, player={}, numPlayers={}", gameMode, playerNum, numPlayers);
}

void Net_SendSync(int playerNum)
{
    if (gConnection == k_HSteamNetConnection_Invalid) return;

    NetSyncMsg msg;
    msg.type = (uint8_t)MsgType::SYNC;
    msg.player_num = playerNum;

    gInterface->SendMessageToConnection(gConnection, &msg, sizeof(msg),
        k_nSteamNetworkingSend_Reliable, nullptr);
}

void Net_SendHostControl(const void* data)
{
    if (gConnection == k_HSteamNetConnection_Invalid || !data) return;

    // Prepend message type byte
    // NetHostControlInfoMessageType = 342 bytes (packed struct)
    // If this size changes, update both here and in network.h
    constexpr size_t kHostControlSize = 342;

    uint8_t buffer[1 + kHostControlSize];
    buffer[0] = (uint8_t)MsgType::HOST_CONTROL;
    memcpy(buffer + 1, data, kHostControlSize);

    gInterface->SendMessageToConnection(gConnection, buffer,
        static_cast<uint32_t>(1 + kHostControlSize),
        k_nSteamNetworkingSend_UnreliableNoDelay, nullptr);
}

void Net_SendClientControl(const void* data)
{
    if (gConnection == k_HSteamNetConnection_Invalid || !data) return;

    // Prepend message type byte
    // NetClientControlInfoMessageType = 26 bytes (packed struct)
    // If this size changes, update both here and in network.h
    constexpr size_t kClientControlSize = 26;

    uint8_t buffer[1 + kClientControlSize];
    buffer[0] = (uint8_t)MsgType::CLIENT_CONTROL;
    memcpy(buffer + 1, data, kClientControlSize);

    gInterface->SendMessageToConnection(gConnection, buffer,
        static_cast<uint32_t>(1 + kClientControlSize),
        k_nSteamNetworkingSend_UnreliableNoDelay, nullptr);
}

void Net_SendVehicleType(int playerNum, int vehicleType, int sex)
{
    if (gConnection == k_HSteamNetConnection_Invalid) return;

    NetVehicleTypeMsg msg;
    msg.type = (uint8_t)MsgType::VEHICLE_TYPE;
    msg.player_num = (int16_t)playerNum;
    msg.vehicle_type = (int16_t)vehicleType;
    msg.sex = (int16_t)sex;

    gInterface->SendMessageToConnection(gConnection, &msg, sizeof(msg),
        k_nSteamNetworkingSend_Reliable, nullptr);
}

void Net_SendPlayerState(const void* playerState)
{
    if (gConnection == k_HSteamNetConnection_Invalid || !playerState) return;

    // Cast to packed struct
    const NetPlayerState* state = (const NetPlayerState*)playerState;

    // Encode with memcpy: type byte + packed struct data
    uint8_t buffer[1 + sizeof(NetPlayerState)];
    size_t msgSize = net_encode_player_state(buffer, sizeof(buffer), state);

    if (msgSize == 0)
    {
        LOG_GNS_ERROR("Failed to encode PlayerState");
        return;
    }

    // Debug log (limit spam)
    static int sSendLogCount = 0;
    if (sSendLogCount++ < 10)
    {
        LOG_GNS_DEBUG("Sending PlayerState: P{} pos=({:.0f},{:.0f},{:.0f}) size={} bytes",
                      state->player_num, state->pos_x, state->pos_y, state->pos_z, msgSize);
    }

    gInterface->SendMessageToConnection(gConnection, buffer, (uint32_t)msgSize,
        k_nSteamNetworkingSend_UnreliableNoDelay, nullptr);
}

void Net_SendWeaponEvent(int weaponType, int playerNum, int throwForward,
                         float posX, float posY, float posZ,
                         float velX, float velY, float velZ, float rotY)
{
    if (gConnection == k_HSteamNetConnection_Invalid) return;

    NetWeaponEventMsg msg;
    msg.type = (uint8_t)MsgType::WEAPON_EVENT;
    msg.weapon_type = (uint8_t)weaponType;
    msg.player_num = (uint8_t)playerNum;
    msg.throw_forward = throwForward ? 1 : 0;
    msg.pos_x = posX;
    msg.pos_y = posY;
    msg.pos_z = posZ;
    msg.vel_x = velX;
    msg.vel_y = velY;
    msg.vel_z = velZ;
    msg.rot_y = rotY;

    gInterface->SendMessageToConnection(gConnection, &msg, sizeof(msg),
        k_nSteamNetworkingSend_Reliable, nullptr);

    // Debug log
    static int sWeaponSendLogCount = 0;
    if (sWeaponSendLogCount++ < 20)
    {
        LOG_GNS_DEBUG("Sent WEAPON_EVENT: type={}, player={}, pos=({:.0f},{:.0f},{:.0f})",
                      weaponType, playerNum, posX, posY, posZ);
    }
}

//==============================================================================
// CALLBACKS
//==============================================================================

void Net_SetConnectCallback(NetConnectCallback callback)
{
    gConnectCallback = callback;
}

void Net_SetDisconnectCallback(NetDisconnectCallback callback)
{
    gDisconnectCallback = callback;
}

void Net_SetReceiveCallback(NetReceiveCallback callback)
{
    gReceiveCallback = callback;
}

void Net_SetStateChangeCallback(NetStateChangeCallback callback)
{
    gStateChangeCallback = callback;
}

void Net_SetPlayerNameCallback(Net_PlayerNameCallback callback)
{
    gPlayerNameCallback = callback;
}

void Net_SetWorldStateCallback(Net_WorldStateCallback callback)
{
    gWorldStateCallback = callback;
}

void Net_SetWeaponEventCallback(Net_WeaponEventCallback callback)
{
    gWeaponEventCallback = callback;
}

//==============================================================================
// MESSAGE PROCESSING
//==============================================================================

static void ProcessReceivedMessages(void)
{
    if (gConnection == k_HSteamNetConnection_Invalid)
        return;

    SteamNetworkingMessage_t* msgs[64];
    int numMsgs = gInterface->ReceiveMessagesOnConnection(gConnection, msgs, 64);

    for (int i = 0; i < numMsgs; i++)
    {
        SteamNetworkingMessage_t* msg = msgs[i];
        const uint8_t* data = (const uint8_t*)msg->m_pData;
        size_t size = msg->m_cbSize;

        if (size < 1)
        {
            msg->Release();
            continue;
        }

        uint8_t msgType = data[0];

        // Debug: log all incoming messages
        static int sRecvLogCount = 0;
        if (sRecvLogCount++ < 30)
        {
            LOG_GNS_DEBUG("Received msg type={}, size={}, isHosting={}", msgType, size, gIsHosting);
        }

        // Handle internal messages
        if (msgType == (uint8_t)MsgType::JOIN_RESPONSE)
        {
            if (size >= sizeof(JoinResponseMsg))
            {
                const JoinResponseMsg* jr = (const JoinResponseMsg*)data;
                if (!jr->success)
                {
                    LOG_GNS_WARN("JOIN failed: {}", jr->errorMsg);

                    // Check if this is a "room not found" error that might be a race condition
                    // Only retry for non-host clients (hosts create rooms, they don't join)
                    bool isRoomNotFound = (strstr(jr->errorMsg, "not found") != nullptr);
                    if (!gIsHosting && isRoomNotFound && gJoinRetryCount < kMaxJoinRetries)
                    {
                        // Schedule retry - don't close connection
                        RetryJoinRoom();
                    }
                    else
                    {
                        SetLastError(jr->errorMsg[0] ? jr->errorMsg : "Failed to join room");
                        SetState(NET_STATE_ERROR, gLastError);
                    }
                }
                else
                {
                    LOG_GNS_INFO("JOIN accepted, waiting for room assignment...");
                    gJoinRetryCount = 0;  // Reset retry count on success
                    gJoinRetryTime = 0;
                }
            }
            msg->Release();
            continue;
        }

        if (msgType == (uint8_t)MsgType::ROOM_ASSIGNMENT)
        {
            if (size >= sizeof(RoomAssignmentMsg))
            {
                const RoomAssignmentMsg* ra = (const RoomAssignmentMsg*)data;
                memcpy(gRoomCode, ra->roomCode, 4);
                gRoomCode[4] = '\0';
                gLocalPlayerIndex = ra->playerIndex;
                gPlayerCount = ra->playerCount;
                gIsHosting = (ra->isHost != 0);

                LOG_GNS_INFO("Room assignment: room={}, player={}, count={}, isHost={}",
                             gRoomCode, gLocalPlayerIndex, gPlayerCount, gIsHosting);

                gPlayerActive[gLocalPlayerIndex] = true;
                strncpy(gPlayerNames[gLocalPlayerIndex], gLocalPlayerName, NET_PLAYER_NAME_LENGTH);

                if (gPlayerNameCallback)
                    gPlayerNameCallback(gLocalPlayerIndex, gLocalPlayerName);

                // Send our name
                PlayerNameMsg nameMsg;
                nameMsg.type = (uint8_t)MsgType::PLAYER_NAME;
                nameMsg.playerIndex = gLocalPlayerIndex;
                strncpy(nameMsg.name, gLocalPlayerName, sizeof(nameMsg.name) - 1);
                nameMsg.name[sizeof(nameMsg.name) - 1] = '\0';
                gInterface->SendMessageToConnection(gConnection, &nameMsg, sizeof(nameMsg),
                    k_nSteamNetworkingSend_Reliable, nullptr);

                SetState(NET_STATE_IN_LOBBY, gRoomCode);

                if (gConnectCallback)
                    gConnectCallback(gLocalPlayerIndex);
            }
        }
        else if (msgType == (uint8_t)MsgType::GAME_START)
        {
            if (size >= sizeof(GameStartMsg))
            {
                const GameStartMsg* gs = (const GameStartMsg*)data;
                LOG_GNS_INFO("GAME_START received (playerCount={})", gs->playerCount);

                if (!gIsHosting)
                {
                    gPlayerCount = gs->playerCount;
                    SetState(NET_STATE_CONNECTED, "Game started by host");
                    if (gConnectCallback)
                        gConnectCallback(0);
                }
            }
        }
        else if (msgType == (uint8_t)MsgType::PLAYER_NAME)
        {
            if (size >= sizeof(PlayerNameMsg))
            {
                const PlayerNameMsg* pn = (const PlayerNameMsg*)data;
                int idx = pn->playerIndex;
                if (idx >= 0 && idx < NET_MAX_PLAYERS && idx != gLocalPlayerIndex)
                {
                    strncpy(gPlayerNames[idx], pn->name, NET_PLAYER_NAME_LENGTH - 1);
                    gPlayerNames[idx][NET_PLAYER_NAME_LENGTH - 1] = '\0';
                    gPlayerActive[idx] = true;
                    LOG_GNS_INFO("Player name: {} = '{}'", idx, pn->name);

                    if (gPlayerNameCallback)
                        gPlayerNameCallback(idx, pn->name);
                }
            }
        }
        else if (msgType == (uint8_t)MsgType::WORLD_STATE)
        {
            // Equal-players model: decode packed world state
            if (size >= 1 + sizeof(NetWorldState) && gWorldStateCallback)
            {
                NetWorldState world = {};
                size_t decoded = net_decode_world_state(data, size, &world);

                if (decoded > 0)
                {
                    // Pass decoded world state to callback
                    gWorldStateCallback(&world);

                    // Debug log (limit spam)
                    static int sWorldLogCount = 0;
                    if (sWorldLogCount++ < 10)
                    {
                        LOG_GNS_DEBUG("WORLD_STATE decoded: seq={}, time={}, players={}",
                                      world.sequence, world.server_time_ms, static_cast<int>(world.player_count));
                    }
                }
                else
                {
                    LOG_GNS_WARN("Failed to decode WorldState: invalid message");
                }
            }
        }
        else if (msgType == (uint8_t)MsgType::PING)
        {
            // Respond to server ping with pong
            if (size >= sizeof(NetPingMsg))
            {
                const NetPingMsg* ping = (const NetPingMsg*)data;

                NetPongMsg pong;
                pong.type = (uint8_t)MsgType::PONG;
                pong.server_time_ms = ping->server_time_ms;  // Echo back

                gInterface->SendMessageToConnection(gConnection, &pong, sizeof(pong),
                    k_nSteamNetworkingSend_Reliable, nullptr);

                // Debug log (very limited)
                static int sPingLogCount = 0;
                if (sPingLogCount++ < 5)
                {
                    LOG_GNS_DEBUG("Received PING, sent PONG (server_time={})", ping->server_time_ms);
                }
            }
        }
        else if (msgType == (uint8_t)MsgType::WEAPON_EVENT)
        {
            // Received weapon event from another player (relayed by server)
            if (size >= sizeof(NetWeaponEventMsg) && gWeaponEventCallback)
            {
                const NetWeaponEventMsg* weaponMsg = (const NetWeaponEventMsg*)data;

                // Don't process our own weapon events
                if (weaponMsg->player_num != gLocalPlayerIndex)
                {
                    gWeaponEventCallback(weaponMsg);

                    // Debug log
                    static int sWeaponRecvLogCount = 0;
                    if (sWeaponRecvLogCount++ < 20)
                    {
                        LOG_GNS_DEBUG("Received WEAPON_EVENT: type={}, player={}, pos=({:.0f},{:.0f},{:.0f})",
                                      weaponMsg->weapon_type, weaponMsg->player_num,
                                      weaponMsg->pos_x, weaponMsg->pos_y, weaponMsg->pos_z);
                    }
                }
            }
        }
        else if (gReceiveCallback)
        {
            // Forward game messages to callback
            gReceiveCallback(gIsHosting ? 1 : 0, data, size);
        }

        msg->Release();
    }
}

//==============================================================================
// EVENT PROCESSING
//==============================================================================

void Net_ProcessEvents(int timeoutMs)
{
    (void)timeoutMs;

    if (!gNetInitialized || !gInterface)
        return;

    // Run callbacks (processes connection status changes)
    gInterface->RunCallbacks();

    // Process received messages
    ProcessReceivedMessages();

    // Handle pending join retry
    if (gJoinRetryTime > 0 && GetTicksMs() >= gJoinRetryTime)
    {
        gJoinRetryTime = 0;
        LOG_GNS_DEBUG("Executing join retry...");
        SendJoinRequest();
    }
}

NetConnectionState Net_GetState(void)
{
    return gConnectionState;
}

const char* Net_GetLastError(void)
{
    return gLastError;
}

//==============================================================================
// CLEANUP
//==============================================================================

void Net_CleanupSession(void)
{
    DisconnectFromServer();

    gRoomCode[0] = '\0';
    gLocalPlayerIndex = -1;
    gIsHosting = false;
    gPlayerCount = 0;
    gLastError[0] = '\0';
    gJoinRetryCount = 0;
    gJoinRetryTime = 0;

    for (int i = 0; i < NET_MAX_PLAYERS; i++)
    {
        gPlayerActive[i] = false;
        gPlayerNames[i][0] = '\0';
    }

    SetState(NET_STATE_DISCONNECTED, "Cleaned up");
    LOG_GNS_INFO("Session cleaned up");
}

#endif // USE_GNS

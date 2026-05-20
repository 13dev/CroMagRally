/****************************/
/*   BACKEND_YOJIMBO.CPP    */
/* Yojimbo UDP networking   */
/* relay client             */
/****************************/

#ifdef USE_YOJIMBO

extern "C" {
#include "Backend_Network.h"
}

#include "Yojimbo_Messages.h"
#include <yojimbo.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

using namespace CroMag;
using yojimbo::Client;

//==============================================================================
// CONSTANTS
//==============================================================================

static const int kServerPort = 40000;
static const int kMaxClients = 64;
static const uint64_t kProtocolId = 0x436F726D6167ULL;  // "CroMag"

// Private key (should match server - in production, use proper key exchange)
static const uint8_t kPrivateKey[yojimbo::KeyBytes] = {
    0x60, 0x6a, 0xbe, 0x6e, 0xc9, 0x19, 0x10, 0xea,
    0x9a, 0x65, 0x62, 0xf6, 0x6f, 0x2b, 0x30, 0xe4,
    0x43, 0x71, 0xd6, 0x2c, 0xd1, 0x99, 0x27, 0x26,
    0x6b, 0x3c, 0x60, 0xf4, 0xb7, 0x15, 0xab, 0xa1
};

//==============================================================================
// INTERNAL STATE
//==============================================================================

static bool                     gNetInitialized = false;
static NetConnectionState       gConnectionState = NET_STATE_DISCONNECTED;
static char                     gLastError[256] = "";

// Server address
static char                     gServerHost[128] = NET_YOJIMBO_HOST;
static uint16_t                 gServerPort = NET_YOJIMBO_PORT;

// Yojimbo client
static GameConnectionConfig     gConfig;
static GameAdapter              gAdapter;
static yojimbo::Client*         gClient = nullptr;
static double                   gTime = 0.0;
static std::chrono::high_resolution_clock::time_point gLastTimePoint;
static bool                     gTimeInitialized = false;
static uint64_t                 gClientId = 0;

// Room/session state
static char                     gRoomCode[NET_ROOM_CODE_LENGTH + 1] = "";
static char                     gLocalPlayerName[NET_PLAYER_NAME_LENGTH] = "Player";
static int                      gLocalPlayerIndex = -1;
static bool                     gIsHosting = false;
static int                      gPlayerCount = 0;

// Player tracking
static char                     gPlayerNames[NET_MAX_PLAYERS][NET_PLAYER_NAME_LENGTH];
static bool                     gPlayerActive[NET_MAX_PLAYERS];

// Callbacks
static NetConnectCallback       gConnectCallback = nullptr;
static NetDisconnectCallback    gDisconnectCallback = nullptr;
static NetReceiveCallback       gReceiveCallback = nullptr;
static NetStateChangeCallback   gStateChangeCallback = nullptr;
static Net_PlayerNameCallback   gPlayerNameCallback = nullptr;

// Message queue for outgoing messages
struct PendingMessage
{
    int type;
    int channel;
    std::vector<uint8_t> data;
};
static std::vector<PendingMessage> gPendingMessages;
static std::mutex gMessageMutex;

//==============================================================================
// FORWARD DECLARATIONS
//==============================================================================

static void SetState(NetConnectionState state, const char* message);
static void ProcessReceivedMessages(void);

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
    printf("[Yojimbo] State changed to %d: %s\n", state, message ? message : "");

    if (gStateChangeCallback)
        gStateChangeCallback(state, message);
}

static uint64_t GenerateClientId(void)
{
    // Generate a unique client ID based on time and random
    uint64_t id = (uint64_t)time(nullptr);
    id ^= (uint64_t)rand() << 32;
    id ^= (uint64_t)rand();
    return id;
}

//==============================================================================
// NETWORK LIFECYCLE
//==============================================================================

bool Net_Initialize(void)
{
    if (gNetInitialized)
        return true;

    if (!InitializeYojimbo())
    {
        SetLastError("Failed to initialize Yojimbo");
        return false;
    }

    // Reset state
    gRoomCode[0] = '\0';
    gLocalPlayerIndex = -1;
    gIsHosting = false;
    gPlayerCount = 0;
    gTime = 0.0;
    gTimeInitialized = false;
    gClientId = GenerateClientId();

    // Clear player tracking
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
    {
        gPlayerActive[i] = false;
        gPlayerNames[i][0] = '\0';
    }

    gNetInitialized = true;
    SetState(NET_STATE_DISCONNECTED, "Initialized");
    printf("[Yojimbo] Network initialized (UDP relay)\n");
    return true;
}

void Net_Shutdown(void)
{
    if (!gNetInitialized)
        return;

    Net_CleanupSession();
    ShutdownYojimbo();

    gNetInitialized = false;
    printf("[Yojimbo] Network shutdown\n");
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
    if (gClient)
        return true;

    // Create client
    gClient = YOJIMBO_NEW(
        yojimbo::GetDefaultAllocator(),
        yojimbo::Client,
        yojimbo::GetDefaultAllocator(),
        yojimbo::Address("0.0.0.0"),
        gConfig,
        gAdapter,
        gTime
    );

    if (!gClient)
    {
        SetLastError("Failed to create Yojimbo client");
        return false;
    }

    // Resolve server address
    char serverAddress[256];
    snprintf(serverAddress, sizeof(serverAddress), "%s:%d", gServerHost, gServerPort);

    yojimbo::Address address(serverAddress);
    if (!address.IsValid())
    {
        SetLastError("Invalid server address");
        YOJIMBO_DELETE(yojimbo::GetDefaultAllocator(), Client, gClient);
        gClient = nullptr;
        return false;
    }

    // Connect using insecure connect for simplicity (production should use secure tokens)
    gClient->InsecureConnect(kPrivateKey, gClientId, address);

    printf("[Yojimbo] Connecting to %s...\n", serverAddress);
    return true;
}

static void DisconnectFromServer(void)
{
    if (gClient)
    {
        gClient->Disconnect();
        YOJIMBO_DELETE(yojimbo::GetDefaultAllocator(), Client, gClient);
        gClient = nullptr;
    }
}

//==============================================================================
// HOST FUNCTIONS
//==============================================================================

bool Net_CreateHost(const char* playerName)
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

    // Generate room code (server will assign actual one)
    // For now, use client index as room identifier
    snprintf(gRoomCode, sizeof(gRoomCode), "YOJI");

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

    SetState(NET_STATE_CONNECTED, "Game started, using UDP relay");
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

void Net_SendToAll(const void* data, size_t size, bool reliable)
{
    if (!gClient || !gClient->IsConnected() || !data || size == 0)
        return;

    int channel = reliable ? (int)GameChannel::RELIABLE : (int)GameChannel::UNRELIABLE;

    // Determine message type from first byte (type header in game protocol)
    const uint8_t* bytes = (const uint8_t*)data;
    int msgType = bytes[0];

    // Map game message type to Yojimbo message type
    yojimbo::Message* msg = nullptr;

    switch (msgType)
    {
        case 1: // kNetConfigureMessage
        {
            auto* configMsg = (ConfigMessage*)gClient->CreateMessage((int)GameMessageType::CONFIG);
            if (configMsg && size >= sizeof(int) + 9 * sizeof(int))
            {
                // Skip type byte and copy data
                // Note: This is a simplified mapping - production code should properly deserialize
                memcpy(&configMsg->gameMode, bytes + 1, sizeof(configMsg->gameMode));
            }
            msg = configMsg;
            break;
        }
        case 2: // kNetSyncMessage
        {
            auto* syncMsg = (SyncMessage*)gClient->CreateMessage((int)GameMessageType::SYNC);
            if (syncMsg && size > 1)
            {
                memcpy(&syncMsg->playerNum, bytes + 1, sizeof(syncMsg->playerNum));
            }
            msg = syncMsg;
            break;
        }
        case 3: // kNetHostControlInfoMessage
        {
            auto* hostMsg = (HostControlMessage*)gClient->CreateMessage((int)GameMessageType::HOST_CONTROL);
            if (hostMsg && size > 1)
            {
                // Copy the raw game data structure (starts with hostTimeMs now)
                size_t copySize = size - 1;
                if (copySize > sizeof(HostControlMessage) - sizeof(yojimbo::Message))
                    copySize = sizeof(HostControlMessage) - sizeof(yojimbo::Message);
                memcpy(&hostMsg->hostTimeMs, bytes + 1, copySize);
            }
            msg = hostMsg;
            channel = (int)GameChannel::UNRELIABLE;  // Always unreliable for position data
            break;
        }
        case 4: // kNetClientControlInfoMessage
        {
            auto* clientMsg = (ClientControlMessage*)gClient->CreateMessage((int)GameMessageType::CLIENT_CONTROL);
            if (clientMsg && size > 1)
            {
                size_t copySize = size - 1;
                if (copySize > sizeof(ClientControlMessage) - sizeof(yojimbo::Message))
                    copySize = sizeof(ClientControlMessage) - sizeof(yojimbo::Message);
                memcpy(&clientMsg->playerNum, bytes + 1, copySize);
            }
            msg = clientMsg;
            channel = (int)GameChannel::UNRELIABLE;  // Always unreliable for input data
            break;
        }
        case 5: // kNetPlayerCharTypeMessage
        {
            auto* vehicleMsg = (VehicleTypeMessage*)gClient->CreateMessage((int)GameMessageType::VEHICLE_TYPE);
            if (vehicleMsg && size > 1)
            {
                size_t copySize = size - 1;
                if (copySize > sizeof(VehicleTypeMessage) - sizeof(yojimbo::Message))
                    copySize = sizeof(VehicleTypeMessage) - sizeof(yojimbo::Message);
                memcpy(&vehicleMsg->playerNum, bytes + 1, copySize);
            }
            msg = vehicleMsg;
            channel = (int)GameChannel::RELIABLE;  // Reliable for vehicle selection
            break;
        }
        default:
            printf("[Yojimbo] Unknown message type: %d\n", msgType);
            return;
    }

    if (msg)
    {
        gClient->SendMessage(channel, msg);
    }
}

void Net_SendToPeer(int peerIndex, const void* data, size_t size, bool reliable)
{
    (void)peerIndex;
    // Server handles routing - send to all
    Net_SendToAll(data, size, reliable);
}

void Net_SendToHost(const void* data, size_t size, bool reliable)
{
    // Send to server, which routes to host
    Net_SendToAll(data, size, reliable);
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

//==============================================================================
// MESSAGE PROCESSING
//==============================================================================

static void ProcessReceivedMessages(void)
{
    if (!gClient)
        return;

    for (int channel = 0; channel < (int)GameChannel::COUNT; channel++)
    {
        yojimbo::Message* msg;
        while ((msg = gClient->ReceiveMessage(channel)) != nullptr)
        {
            // Convert Yojimbo message back to raw game format and call callback
            if (gReceiveCallback)
            {
                std::vector<uint8_t> buffer;
                int peerIndex = gIsHosting ? 1 : 0;  // TODO: proper peer tracking

                switch (msg->GetType())
                {
                    case (int)GameMessageType::CONFIG:
                    {
                        auto* configMsg = (ConfigMessage*)msg;
                        buffer.resize(1 + sizeof(configMsg->gameMode) * 9);
                        buffer[0] = 1;  // kNetConfigureMessage
                        memcpy(&buffer[1], &configMsg->gameMode, buffer.size() - 1);
                        break;
                    }
                    case (int)GameMessageType::SYNC:
                    {
                        auto* syncMsg = (SyncMessage*)msg;
                        buffer.resize(1 + sizeof(syncMsg->playerNum));
                        buffer[0] = 2;  // kNetSyncMessage
                        memcpy(&buffer[1], &syncMsg->playerNum, sizeof(syncMsg->playerNum));
                        break;
                    }
                    case (int)GameMessageType::HOST_CONTROL:
                    {
                        auto* hostMsg = (HostControlMessage*)msg;
                        size_t dataSize = sizeof(HostControlMessage) - sizeof(yojimbo::Message);
                        buffer.resize(1 + dataSize);
                        buffer[0] = 3;  // kNetHostControlInfoMessage
                        memcpy(&buffer[1], &hostMsg->hostTimeMs, dataSize);
                        break;
                    }
                    case (int)GameMessageType::CLIENT_CONTROL:
                    {
                        auto* clientMsg = (ClientControlMessage*)msg;
                        size_t dataSize = sizeof(ClientControlMessage) - sizeof(yojimbo::Message);
                        buffer.resize(1 + dataSize);
                        buffer[0] = 4;  // kNetClientControlInfoMessage
                        memcpy(&buffer[1], &clientMsg->playerNum, dataSize);
                        peerIndex = clientMsg->playerNum;
                        break;
                    }
                    case (int)GameMessageType::VEHICLE_TYPE:
                    {
                        auto* vehicleMsg = (VehicleTypeMessage*)msg;
                        size_t dataSize = sizeof(VehicleTypeMessage) - sizeof(yojimbo::Message);
                        buffer.resize(1 + dataSize);
                        buffer[0] = 5;  // kNetPlayerCharTypeMessage
                        memcpy(&buffer[1], &vehicleMsg->playerNum, dataSize);
                        break;
                    }
                }

                if (!buffer.empty())
                {
                    gReceiveCallback(peerIndex, buffer.data(), buffer.size());
                }
            }

            gClient->ReleaseMessage(msg);
        }
    }
}

//==============================================================================
// EVENT PROCESSING
//==============================================================================

void Net_ProcessEvents(int timeoutMs)
{
    (void)timeoutMs;

    if (!gNetInitialized || !gClient)
        return;

    // Advance time using actual elapsed time instead of fixed increment
    auto now = std::chrono::high_resolution_clock::now();
    if (!gTimeInitialized)
    {
        gLastTimePoint = now;
        gTimeInitialized = true;
    }
    double deltaTime = std::chrono::duration<double>(now - gLastTimePoint).count();
    gLastTimePoint = now;
    gTime += deltaTime;
    gClient->AdvanceTime(gTime);

    // Receive packets
    gClient->ReceivePackets();

    // Handle connection state changes
    if (gClient->IsConnecting())
    {
        if (gConnectionState != NET_STATE_CONNECTING_SIGNALING)
            SetState(NET_STATE_CONNECTING_SIGNALING, "Connecting...");
    }
    else if (gClient->IsConnected())
    {
        if (gConnectionState == NET_STATE_CONNECTING_SIGNALING ||
            gConnectionState == NET_STATE_WAITING_ROOM)
        {
            // First time connected
            if (gIsHosting)
            {
                gLocalPlayerIndex = gClient->GetClientIndex();
                SetState(NET_STATE_IN_LOBBY, gRoomCode);
            }
            else
            {
                gLocalPlayerIndex = gClient->GetClientIndex();
                gPlayerActive[gLocalPlayerIndex] = true;
                gPlayerCount++;
                strncpy(gPlayerNames[gLocalPlayerIndex], gLocalPlayerName, NET_PLAYER_NAME_LENGTH);

                if (gPlayerNameCallback)
                    gPlayerNameCallback(gLocalPlayerIndex, gLocalPlayerName);

                SetState(NET_STATE_IN_LOBBY, "Joined");
            }

            if (gConnectCallback)
                gConnectCallback(gIsHosting ? 0 : gLocalPlayerIndex);
        }
    }
    else if (gClient->IsDisconnected())
    {
        if (gConnectionState != NET_STATE_DISCONNECTED &&
            gConnectionState != NET_STATE_ERROR)
        {
            SetLastError("Disconnected from server");
            SetState(NET_STATE_ERROR, "Connection lost");

            if (gDisconnectCallback)
                gDisconnectCallback(gLocalPlayerIndex);
        }
    }
    else if (gClient->ConnectionFailed())
    {
        SetLastError("Connection failed");
        SetState(NET_STATE_ERROR, "Connection failed");
    }

    // Process received messages
    ProcessReceivedMessages();

    // Send packets
    gClient->SendPackets();
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
    gTime = 0.0;

    // Clear player tracking
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
    {
        gPlayerActive[i] = false;
        gPlayerNames[i][0] = '\0';
    }

    // Clear pending messages
    {
        std::lock_guard<std::mutex> lock(gMessageMutex);
        gPendingMessages.clear();
    }

    SetState(NET_STATE_DISCONNECTED, "Cleaned up");
    printf("[Yojimbo] Session cleaned up\n");
}

#endif // USE_YOJIMBO
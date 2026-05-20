/****************************/
/*   BACKEND_NETWORK.CPP    */
/* TCP relay networking via */
/* signaling server         */
/****************************/

extern "C" {
#include "Backend_Network.h"
}

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <mutex>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define SOCKET_CLOSE closesocket
    inline int GetSocketError() { return WSAGetLastError(); }
    inline bool IgnoreSocketError(int e) { return e == WSAEWOULDBLOCK || e == WSAENOTCONN; }
#else
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    typedef int socket_t;
    #define INVALID_SOCKET_VALUE -1
    #define SOCKET_CLOSE close
    inline int GetSocketError() { return errno; }
    inline bool IgnoreSocketError(int e) { return e == EAGAIN || e == EWOULDBLOCK || e == ENOTCONN; }
#endif

//==============================================================================
// INTERNAL STATE
//==============================================================================

static bool                     gNetInitialized = false;
static NetConnectionState       gConnectionState = NET_STATE_DISCONNECTED;
static char                     gLastError[256] = "";

// Signaling/relay server connection
static char                     gSignalingHost[128] = NET_DEFAULT_SIGNALING_HOST;
static uint16_t                 gSignalingPort = NET_SIGNALING_PORT;
static socket_t                 gServerSocket = INVALID_SOCKET_VALUE;
static std::string              gRecvBuffer;
static std::vector<std::string> gSendQueue;
static std::mutex               gSendMutex;

// Room state
static char                     gRoomCode[NET_ROOM_CODE_LENGTH + 1] = "";
static char                     gLocalPlayerName[NET_PLAYER_NAME_LENGTH] = "Player";
static int                      gLocalPlayerIndex = -1;
static bool                     gIsHosting = false;
static int                      gPlayerCount = 0;

// Player tracking for host (client names by index)
static char                     gPlayerNames[NET_MAX_PLAYERS][NET_PLAYER_NAME_LENGTH];
static bool                     gPlayerActive[NET_MAX_PLAYERS];

// Callbacks
static NetConnectCallback       gConnectCallback = nullptr;
static NetDisconnectCallback    gDisconnectCallback = nullptr;
static NetReceiveCallback       gReceiveCallback = nullptr;
static NetStateChangeCallback   gStateChangeCallback = nullptr;
static Net_PlayerNameCallback   gPlayerNameCallback = nullptr;

//==============================================================================
// FORWARD DECLARATIONS
//==============================================================================

static void SetState(NetConnectionState state, const char* message);
static void ProcessServerMessages(void);
static void SendToServer(const std::string& message);
static bool ConnectToServer(void);
static void DisconnectFromServer(void);

//==============================================================================
// BASE64 ENCODING/DECODING
//==============================================================================

static const char* kBase64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(const void* data, size_t len)
{
    const uint8_t* bytes = (const uint8_t*)data;
    std::string result;
    result.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3)
    {
        uint32_t n = (uint32_t)bytes[i] << 16;
        if (i + 1 < len) n |= (uint32_t)bytes[i + 1] << 8;
        if (i + 2 < len) n |= (uint32_t)bytes[i + 2];

        result += kBase64Chars[(n >> 18) & 0x3F];
        result += kBase64Chars[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? kBase64Chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? kBase64Chars[n & 0x3F] : '=';
    }

    return result;
}

static int Base64DecodeChar(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static std::vector<uint8_t> Base64Decode(const std::string& encoded)
{
    std::vector<uint8_t> result;
    result.reserve((encoded.length() / 4) * 3);

    uint32_t buf = 0;
    int bits = 0;

    for (char c : encoded)
    {
        if (c == '=') break;

        int val = Base64DecodeChar(c);
        if (val < 0) continue;

        buf = (buf << 6) | val;
        bits += 6;

        if (bits >= 8)
        {
            bits -= 8;
            result.push_back((uint8_t)(buf >> bits));
        }
    }

    return result;
}

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
    printf("[Net] State changed to %d: %s\n", state, message ? message : "");

    if (gStateChangeCallback)
        gStateChangeCallback(state, message);
}

static void SetSocketNonBlocking(socket_t sock)
{
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
}

//==============================================================================
// SERVER COMMUNICATION
//==============================================================================

static bool ConnectToServer(void)
{
    if (gServerSocket != INVALID_SOCKET_VALUE)
        return true;

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", gSignalingPort);

    int res = getaddrinfo(gSignalingHost, portStr, &hints, &result);
    if (res != 0)
    {
        SetLastError("Failed to resolve server");
        return false;
    }

    gServerSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (gServerSocket == INVALID_SOCKET_VALUE)
    {
        freeaddrinfo(result);
        SetLastError("Failed to create socket");
        return false;
    }

    if (connect(gServerSocket, result->ai_addr, (int)result->ai_addrlen) < 0)
    {
        freeaddrinfo(result);
        SOCKET_CLOSE(gServerSocket);
        gServerSocket = INVALID_SOCKET_VALUE;
        SetLastError("Failed to connect to server");
        return false;
    }

    freeaddrinfo(result);
    SetSocketNonBlocking(gServerSocket);
    gRecvBuffer.clear();

    printf("[Net] Connected to relay server %s:%d\n", gSignalingHost, gSignalingPort);
    return true;
}

static void DisconnectFromServer(void)
{
    if (gServerSocket != INVALID_SOCKET_VALUE)
    {
        SendToServer("LEAVE\n");

        // Flush send queue before closing
        {
            std::lock_guard<std::mutex> lock(gSendMutex);
            while (!gSendQueue.empty())
            {
                const std::string& msg = gSendQueue.front();
                send(gServerSocket, msg.c_str(), (int)msg.length(), 0);
                gSendQueue.erase(gSendQueue.begin());
            }
        }

        SOCKET_CLOSE(gServerSocket);
        gServerSocket = INVALID_SOCKET_VALUE;
    }
    gRecvBuffer.clear();
    gSendQueue.clear();
}

static void SendToServer(const std::string& message)
{
    std::lock_guard<std::mutex> lock(gSendMutex);
    gSendQueue.push_back(message);
}

static void HandleServerMessage(const std::string& message)
{
    // Don't spam logs with GAME messages (60/sec per player during gameplay)
    if (message.substr(0, 5) != "GAME:")
    {
        printf("[Net] Server: %s\n", message.c_str());
    }

    // Parse "TYPE:data" format
    size_t colonPos = message.find(':');
    std::string type = (colonPos != std::string::npos) ? message.substr(0, colonPos) : message;
    std::string data = (colonPos != std::string::npos) ? message.substr(colonPos + 1) : "";

    //--------------------------------------------------------------------------
    // ROOM - Received room code (host)
    //--------------------------------------------------------------------------
    if (type == "ROOM")
    {
        strncpy(gRoomCode, data.c_str(), NET_ROOM_CODE_LENGTH);
        gRoomCode[NET_ROOM_CODE_LENGTH] = '\0';
        gLocalPlayerIndex = 0;
        gPlayerCount = 1;

        // Mark host as active
        gPlayerActive[0] = true;
        strncpy(gPlayerNames[0], gLocalPlayerName, NET_PLAYER_NAME_LENGTH);

        SetState(NET_STATE_IN_LOBBY, gRoomCode);
    }
    //--------------------------------------------------------------------------
    // JOINED - Successfully joined room (client)
    //--------------------------------------------------------------------------
    else if (type == "JOINED")
    {
        // Format: playerIndex:hostIdentity (we ignore hostIdentity now)
        size_t sep = data.find(':');
        if (sep != std::string::npos)
        {
            gLocalPlayerIndex = atoi(data.substr(0, sep).c_str());
        }
        else
        {
            gLocalPlayerIndex = atoi(data.c_str());
        }

        // Mark ourselves as active
        gPlayerActive[gLocalPlayerIndex] = true;
        gPlayerCount++;
        strncpy(gPlayerNames[gLocalPlayerIndex], gLocalPlayerName, NET_PLAYER_NAME_LENGTH);

        // Notify callback with our own name so it's stored in gPlayerNameStrings
        if (gPlayerNameCallback)
        {
            gPlayerNameCallback(gLocalPlayerIndex, gLocalPlayerName);
        }

        printf("[Net] Joined as player %d (gPlayerCount=%d, name='%s')\n",
               gLocalPlayerIndex, gPlayerCount, gLocalPlayerName);
        SetState(NET_STATE_IN_LOBBY, "Joined room");
    }
    //--------------------------------------------------------------------------
    // PLAYER_INFO - Info about existing player (sent to joining client)
    // Format: playerIndex:playerName
    //--------------------------------------------------------------------------
    else if (type == "PLAYER_INFO")
    {
        // Parse playerIndex:playerName
        size_t sep = data.find(':');
        int playerIndex = atoi(data.c_str());
        std::string playerName = (sep != std::string::npos) ? data.substr(sep + 1) : "";

        if (playerIndex >= 0 && playerIndex < NET_MAX_PLAYERS)
        {
            gPlayerActive[playerIndex] = true;
            gPlayerCount++;
            if (!playerName.empty())
            {
                strncpy(gPlayerNames[playerIndex], playerName.c_str(), NET_PLAYER_NAME_LENGTH - 1);
                gPlayerNames[playerIndex][NET_PLAYER_NAME_LENGTH - 1] = '\0';
            }
            printf("[Net] Existing player %d: %s\n", playerIndex, playerName.c_str());

            // Notify via callback
            if (!playerName.empty() && gPlayerNameCallback)
            {
                gPlayerNameCallback(playerIndex, playerName.c_str());
            }
        }
    }
    //--------------------------------------------------------------------------
    // PLAYER_JOINED - Another player joined (host notification)
    // Format: playerIndex:playerName
    //--------------------------------------------------------------------------
    else if (type == "PLAYER_JOINED")
    {
        // Parse playerIndex:playerName
        size_t sep = data.find(':');
        int playerIndex = atoi(data.c_str());
        std::string playerName = (sep != std::string::npos) ? data.substr(sep + 1) : "";

        if (playerIndex >= 0 && playerIndex < NET_MAX_PLAYERS)
        {
            gPlayerActive[playerIndex] = true;
            if (!playerName.empty())
            {
                strncpy(gPlayerNames[playerIndex], playerName.c_str(), NET_PLAYER_NAME_LENGTH - 1);
                gPlayerNames[playerIndex][NET_PLAYER_NAME_LENGTH - 1] = '\0';
            }
        }

        gPlayerCount++;
        printf("[Net] Player %d (%s) joined (%d total)\n", playerIndex, playerName.c_str(), gPlayerCount);

        // Notify via callback
        if (!playerName.empty() && gPlayerNameCallback)
        {
            gPlayerNameCallback(playerIndex, playerName.c_str());
        }

        if (gConnectCallback)
            gConnectCallback(playerIndex);
    }
    //--------------------------------------------------------------------------
    // PLAYER_LEFT - Another player left
    //--------------------------------------------------------------------------
    else if (type == "PLAYER_LEFT")
    {
        int playerIndex = atoi(data.c_str());

        if (playerIndex >= 0 && playerIndex < NET_MAX_PLAYERS)
        {
            gPlayerActive[playerIndex] = false;
            gPlayerNames[playerIndex][0] = '\0';
        }

        gPlayerCount--;
        printf("[Net] Player %d left (%d total)\n", playerIndex, gPlayerCount);

        if (gDisconnectCallback)
            gDisconnectCallback(playerIndex);
    }
    //--------------------------------------------------------------------------
    // GAME_STARTING - Host started game (client receives this)
    //--------------------------------------------------------------------------
    else if (type == "GAME_STARTING")
    {
        printf("[Net] Game starting signal received\n");

        // Client transitions to connected state - data will flow through relay
        if (!gIsHosting)
        {
            SetState(NET_STATE_CONNECTED, "Connected to host via relay");

            if (gConnectCallback)
                gConnectCallback(0);  // Connected to host
        }
    }
    //--------------------------------------------------------------------------
    // GAME - Game data from peer
    //--------------------------------------------------------------------------
    else if (type == "GAME")
    {
        // Base64 decode and deliver to callback
        std::vector<uint8_t> decoded = Base64Decode(data);

        if (!decoded.empty() && gReceiveCallback)
        {
            // Determine peer index based on our role
            int peerIndex = gIsHosting ? 1 : 0;  // TODO: proper peer tracking for multiple clients

            // For host, we need to track which client sent this
            // For now, assume single client (peerIndex=1)
            // In a multi-client scenario, server would need to include sender info

            gReceiveCallback(peerIndex, decoded.data(), decoded.size());
        }
    }
    //--------------------------------------------------------------------------
    // ERROR - Error from server
    //--------------------------------------------------------------------------
    else if (type == "ERROR")
    {
        SetLastError(data.c_str());
        SetState(NET_STATE_ERROR, data.c_str());
    }
    //--------------------------------------------------------------------------
    // OK, PONG - Acknowledged
    //--------------------------------------------------------------------------
    else if (type == "OK" || type == "PONG")
    {
        // Acknowledged
    }
}

static void ProcessServerMessages(void)
{
    if (gServerSocket == INVALID_SOCKET_VALUE)
        return;

    // Send queued messages
    {
        std::lock_guard<std::mutex> lock(gSendMutex);
        while (!gSendQueue.empty())
        {
            const std::string& msg = gSendQueue.front();
            int sent = send(gServerSocket, msg.c_str(), (int)msg.length(), 0);
            if (sent < 0)
            {
                if (!IgnoreSocketError(GetSocketError()))
                {
                    printf("[Net] Send failed: %d\n", GetSocketError());
                }
                break;
            }
            // Don't spam logs with game messages
            if (msg.substr(0, 5) != "GAME:")
            {
                printf("[Net] Sent: %s", msg.c_str());
            }
            gSendQueue.erase(gSendQueue.begin());
        }
    }

    // Receive data
    char buffer[4096];
    int received = recv(gServerSocket, buffer, sizeof(buffer) - 1, 0);

    if (received > 0)
    {
        buffer[received] = '\0';
        gRecvBuffer += buffer;

        // Process complete messages
        size_t newline;
        while ((newline = gRecvBuffer.find('\n')) != std::string::npos)
        {
            std::string message = gRecvBuffer.substr(0, newline);
            gRecvBuffer.erase(0, newline + 1);

            // Trim \r if present
            if (!message.empty() && message.back() == '\r')
                message.pop_back();

            if (!message.empty())
                HandleServerMessage(message);
        }
    }
    else if (received == 0)
    {
        SetLastError("Server disconnected");
        SetState(NET_STATE_ERROR, "Lost connection to server");
        SOCKET_CLOSE(gServerSocket);
        gServerSocket = INVALID_SOCKET_VALUE;
    }
    else if (!IgnoreSocketError(GetSocketError()))
    {
        SetLastError("Connection error");
        SetState(NET_STATE_ERROR, "Connection error");
        SOCKET_CLOSE(gServerSocket);
        gServerSocket = INVALID_SOCKET_VALUE;
    }
}

//==============================================================================
// NETWORK LIFECYCLE
//==============================================================================

bool Net_Initialize(void)
{
    if (gNetInitialized)
        return true;

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        SetLastError("WSAStartup failed");
        return false;
    }
#endif

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
    printf("[Net] Network initialized (TCP relay)\n");
    return true;
}

void Net_Shutdown(void)
{
    if (!gNetInitialized)
        return;

    Net_CleanupSession();

#ifdef _WIN32
    WSACleanup();
#endif

    gNetInitialized = false;
    printf("[Net] Network shutdown\n");
}

bool Net_IsInitialized(void)
{
    return gNetInitialized;
}

void Net_SetSignalingServer(const char* host, uint16_t port)
{
    if (host)
    {
        strncpy(gSignalingHost, host, sizeof(gSignalingHost) - 1);
        gSignalingHost[sizeof(gSignalingHost) - 1] = '\0';
    }
    if (port > 0)
    {
        gSignalingPort = port;
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

    // Register as host
    gIsHosting = true;
    char msg[256];
    snprintf(msg, sizeof(msg), "REGISTER:%s\n", gLocalPlayerName);
    SendToServer(msg);

    SetState(NET_STATE_WAITING_ROOM, "Creating room...");
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
    // With TCP relay, connection count = player count - 1 (excluding ourselves)
    // For compatibility, report the number of active remote players
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

    // Notify server - it will tell clients game is starting
    SendToServer("START\n");

    // Host transitions to connected immediately - TCP relay is already established
    SetState(NET_STATE_CONNECTED, "Game started, using TCP relay");

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

    // Join room
    gIsHosting = false;
    char msg[256];
    snprintf(msg, sizeof(msg), "JOIN:%s:%s\n", gRoomCode, gLocalPlayerName);
    SendToServer(msg);

    SetState(NET_STATE_WAITING_ROOM, "Joining room...");
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
    (void)reliable;  // TCP is always reliable

    if (gServerSocket == INVALID_SOCKET_VALUE || !data || size == 0)
        return;

    // Encode data as base64 and send as GAME message
    std::string encoded = Base64Encode(data, size);
    std::string msg = "GAME:" + encoded + "\n";
    SendToServer(msg);
}

void Net_SendToPeer(int peerIndex, const void* data, size_t size, bool reliable)
{
    (void)peerIndex;  // TCP relay broadcasts - server handles routing

    // For now, relay server broadcasts all game messages
    // TODO: If needed, add peer-specific routing on server
    Net_SendToAll(data, size, reliable);
}

void Net_SendToHost(const void* data, size_t size, bool reliable)
{
    // Same as SendToAll - server routes to host
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
// EVENT PROCESSING
//==============================================================================

void Net_ProcessEvents(int timeoutMs)
{
    (void)timeoutMs;

    if (!gNetInitialized)
        return;

    // Process server messages (handles both signaling and game data)
    ProcessServerMessages();
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

    // Clear player tracking
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
    {
        gPlayerActive[i] = false;
        gPlayerNames[i][0] = '\0';
    }

    SetState(NET_STATE_DISCONNECTED, "Cleaned up");
    printf("[Net] Session cleaned up\n");
}

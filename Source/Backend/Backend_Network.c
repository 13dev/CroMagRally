/****************************/
/*    BACKEND_NETWORK.C     */
/* ENet-based LAN networking*/
/****************************/

#include "Backend_Network.h"
#include <enet/enet.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

//==============================================================================
// INTERNAL STATE
//==============================================================================

static bool             gNetInitialized = false;
static ENetHost*        gHost = NULL;
static ENetPeer*        gServerPeer = NULL;         // Client's connection to server
static ENetPeer*        gClientPeers[NET_MAX_PLAYERS];
static int              gClientPeerCount = 0;
static bool             gIsHosting = false;
static bool             gIsConnected = false;

// Discovery state
static int              gDiscoverySocket = -1;
static int              gBroadcastSocket = -1;
static LANGameInfo      gDiscoveredGames[NET_MAX_DISCOVERED_GAMES];
static int              gDiscoveredGameCount = 0;
static bool             gIsDiscovering = false;
static bool             gIsBroadcasting = false;

// Broadcast info (for host)
static char             gBroadcastGameName[NET_GAME_NAME_LENGTH];
static char             gBroadcastHostName[NET_PLAYER_NAME_LENGTH];
static int              gBroadcastGameMode = 0;
static float            gBroadcastTimer = 0.0f;

// Callbacks
static NetConnectCallback       gConnectCallback = NULL;
static NetDisconnectCallback    gDisconnectCallback = NULL;
static NetReceiveCallback       gReceiveCallback = NULL;

// Discovery packet magic
#define NET_DISCOVERY_MAGIC     0x43524D52  // "CRMR"

#pragma pack(push, 1)
typedef struct DiscoveryPacket
{
    uint32_t    magic;
    uint8_t     version;
    char        gameName[NET_GAME_NAME_LENGTH];
    char        hostName[NET_PLAYER_NAME_LENGTH];
    uint16_t    gamePort;
    uint8_t     currentPlayers;
    uint8_t     maxPlayers;
    uint8_t     gameMode;
} DiscoveryPacket;
#pragma pack(pop)

//==============================================================================
// HELPER FUNCTIONS
//==============================================================================

static void SetSocketNonBlocking(int sock)
{
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
}

static void CloseSocket(int sock)
{
    if (sock >= 0)
    {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
    }
}

//==============================================================================
// NETWORK LIFECYCLE
//==============================================================================

bool Net_Initialize(void)
{
    if (gNetInitialized)
        return true;

    if (enet_initialize() != 0)
    {
        fprintf(stderr, "Net_Initialize: Failed to initialize ENet\n");
        return false;
    }

    memset(gClientPeers, 0, sizeof(gClientPeers));
    memset(gDiscoveredGames, 0, sizeof(gDiscoveredGames));

    gNetInitialized = true;
    printf("Net_Initialize: ENet initialized successfully\n");
    return true;
}

void Net_Shutdown(void)
{
    if (!gNetInitialized)
        return;

    Net_CleanupSession();
    enet_deinitialize();
    gNetInitialized = false;
    printf("Net_Shutdown: ENet shutdown complete\n");
}

bool Net_IsInitialized(void)
{
    return gNetInitialized;
}

//==============================================================================
// HOST FUNCTIONS
//==============================================================================

bool Net_CreateHost(uint16_t port, int maxPlayers)
{
    if (!gNetInitialized)
        return false;

    if (gHost)
    {
        fprintf(stderr, "Net_CreateHost: Host already exists\n");
        return false;
    }

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    gHost = enet_host_create(&address, maxPlayers, NET_MAX_CHANNELS, 0, 0);
    if (!gHost)
    {
        fprintf(stderr, "Net_CreateHost: Failed to create ENet host on port %d\n", port);
        return false;
    }

    gIsHosting = true;
    gClientPeerCount = 0;
    memset(gClientPeers, 0, sizeof(gClientPeers));

    printf("Net_CreateHost: Hosting on port %d (max %d players)\n", port, maxPlayers);
    return true;
}

void Net_StartBroadcast(const char* gameName, const char* hostName, int gameMode)
{
    if (gIsBroadcasting)
        return;

    // Create UDP socket for broadcasting
#ifdef _WIN32
    gBroadcastSocket = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
    gBroadcastSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#endif
    if (gBroadcastSocket < 0)
    {
        fprintf(stderr, "Net_StartBroadcast: Failed to create socket\n");
        return;
    }

    // Enable broadcast
    int broadcast = 1;
    setsockopt(gBroadcastSocket, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast));

    // Store broadcast info
    strncpy(gBroadcastGameName, gameName, NET_GAME_NAME_LENGTH - 1);
    gBroadcastGameName[NET_GAME_NAME_LENGTH - 1] = '\0';
    strncpy(gBroadcastHostName, hostName, NET_PLAYER_NAME_LENGTH - 1);
    gBroadcastHostName[NET_PLAYER_NAME_LENGTH - 1] = '\0';
    gBroadcastGameMode = gameMode;
    gBroadcastTimer = 0.0f;

    gIsBroadcasting = true;
    printf("Net_StartBroadcast: Broadcasting game '%s' by '%s'\n", gameName, hostName);
}

void Net_StopBroadcast(void)
{
    if (!gIsBroadcasting)
        return;

    CloseSocket(gBroadcastSocket);
    gBroadcastSocket = -1;
    gIsBroadcasting = false;
    printf("Net_StopBroadcast: Stopped broadcasting\n");
}

bool Net_IsHosting(void)
{
    return gIsHosting;
}

int Net_GetConnectedClientCount(void)
{
    return gClientPeerCount;
}

//==============================================================================
// CLIENT FUNCTIONS
//==============================================================================

bool Net_Connect(uint32_t hostIP, uint16_t port)
{
    if (!gNetInitialized)
        return false;

    if (gHost)
    {
        fprintf(stderr, "Net_Connect: Already have a host/connection\n");
        return false;
    }

    // Create client host
    gHost = enet_host_create(NULL, 1, NET_MAX_CHANNELS, 0, 0);
    if (!gHost)
    {
        fprintf(stderr, "Net_Connect: Failed to create client host\n");
        return false;
    }

    ENetAddress address;
    address.host = hostIP;
    address.port = port;

    gServerPeer = enet_host_connect(gHost, &address, NET_MAX_CHANNELS, 0);
    if (!gServerPeer)
    {
        fprintf(stderr, "Net_Connect: Failed to initiate connection\n");
        enet_host_destroy(gHost);
        gHost = NULL;
        return false;
    }

    gIsHosting = false;
    gIsConnected = false;  // Will be set true on ENET_EVENT_TYPE_CONNECT

    char ipStr[32];
    Net_IPToString(hostIP, ipStr, sizeof(ipStr));
    printf("Net_Connect: Connecting to %s:%d\n", ipStr, port);
    return true;
}

void Net_Disconnect(void)
{
    if (gServerPeer)
    {
        enet_peer_disconnect(gServerPeer, 0);
        // Allow some time for disconnect to be sent
        ENetEvent event;
        while (enet_host_service(gHost, &event, 1000) > 0)
        {
            if (event.type == ENET_EVENT_TYPE_DISCONNECT)
                break;
            if (event.type == ENET_EVENT_TYPE_RECEIVE)
                enet_packet_destroy(event.packet);
        }
        enet_peer_reset(gServerPeer);
        gServerPeer = NULL;
    }

    gIsConnected = false;
    printf("Net_Disconnect: Disconnected from server\n");
}

bool Net_IsConnected(void)
{
    return gIsConnected;
}

//==============================================================================
// LAN DISCOVERY
//==============================================================================

void Net_StartDiscovery(void)
{
    if (gIsDiscovering)
        return;

    // Create UDP socket for listening
#ifdef _WIN32
    gDiscoverySocket = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
    gDiscoverySocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#endif
    if (gDiscoverySocket < 0)
    {
        fprintf(stderr, "Net_StartDiscovery: Failed to create socket\n");
        return;
    }

    // Allow reuse
    int reuse = 1;
    setsockopt(gDiscoverySocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    // Bind to discovery port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(NET_DISCOVERY_PORT);

    if (bind(gDiscoverySocket, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        fprintf(stderr, "Net_StartDiscovery: Failed to bind socket\n");
        CloseSocket(gDiscoverySocket);
        gDiscoverySocket = -1;
        return;
    }

    SetSocketNonBlocking(gDiscoverySocket);

    // Clear discovered games
    memset(gDiscoveredGames, 0, sizeof(gDiscoveredGames));
    gDiscoveredGameCount = 0;

    gIsDiscovering = true;
    printf("Net_StartDiscovery: Listening for LAN games on port %d\n", NET_DISCOVERY_PORT);
}

void Net_StopDiscovery(void)
{
    if (!gIsDiscovering)
        return;

    CloseSocket(gDiscoverySocket);
    gDiscoverySocket = -1;
    gIsDiscovering = false;
    printf("Net_StopDiscovery: Stopped listening\n");
}

int Net_GetDiscoveredGames(LANGameInfo* outGames, int maxGames)
{
    int count = 0;
    for (int i = 0; i < NET_MAX_DISCOVERED_GAMES && count < maxGames; i++)
    {
        if (gDiscoveredGames[i].isValid)
        {
            outGames[count++] = gDiscoveredGames[i];
        }
    }
    return count;
}

static void ProcessDiscoveryPacket(const DiscoveryPacket* packet, uint32_t senderIP)
{
    // Validate packet
    if (packet->magic != NET_DISCOVERY_MAGIC)
        return;
    if (packet->version != 1)
        return;

    // Check if we already have this game
    int freeSlot = -1;
    for (int i = 0; i < NET_MAX_DISCOVERED_GAMES; i++)
    {
        if (gDiscoveredGames[i].isValid)
        {
            if (gDiscoveredGames[i].hostIP == senderIP &&
                gDiscoveredGames[i].hostPort == packet->gamePort)
            {
                // Update existing entry
                gDiscoveredGames[i].currentPlayers = packet->currentPlayers;
                gDiscoveredGames[i].lastSeen = 0.0f;
                return;
            }
        }
        else if (freeSlot < 0)
        {
            freeSlot = i;
        }
    }

    // Add new entry
    if (freeSlot >= 0)
    {
        LANGameInfo* game = &gDiscoveredGames[freeSlot];
        strncpy(game->gameName, packet->gameName, NET_GAME_NAME_LENGTH - 1);
        strncpy(game->hostName, packet->hostName, NET_PLAYER_NAME_LENGTH - 1);
        game->hostIP = senderIP;
        game->hostPort = packet->gamePort;
        game->currentPlayers = packet->currentPlayers;
        game->maxPlayers = packet->maxPlayers;
        game->gameMode = packet->gameMode;
        game->lastSeen = 0.0f;
        game->isValid = true;
        gDiscoveredGameCount++;

        char ipStr[32];
        Net_IPToString(senderIP, ipStr, sizeof(ipStr));
        printf("Net_Discovery: Found game '%s' at %s:%d\n", game->gameName, ipStr, game->hostPort);
    }
}

static void SendBroadcast(void)
{
    if (!gIsBroadcasting || gBroadcastSocket < 0)
        return;

    DiscoveryPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.magic = NET_DISCOVERY_MAGIC;
    packet.version = 1;
    strncpy(packet.gameName, gBroadcastGameName, NET_GAME_NAME_LENGTH - 1);
    strncpy(packet.hostName, gBroadcastHostName, NET_PLAYER_NAME_LENGTH - 1);
    packet.gamePort = NET_GAME_PORT;
    packet.currentPlayers = (uint8_t)(gClientPeerCount + 1);  // +1 for host
    packet.maxPlayers = NET_MAX_PLAYERS;
    packet.gameMode = (uint8_t)gBroadcastGameMode;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_BROADCAST;
    addr.sin_port = htons(NET_DISCOVERY_PORT);

    sendto(gBroadcastSocket, (const char*)&packet, sizeof(packet), 0,
           (struct sockaddr*)&addr, sizeof(addr));
}

static void ReceiveDiscoveryPackets(void)
{
    if (!gIsDiscovering || gDiscoverySocket < 0)
        return;

    char buffer[256];
    struct sockaddr_in senderAddr;
    socklen_t addrLen = sizeof(senderAddr);

    while (1)
    {
        int received = recvfrom(gDiscoverySocket, buffer, sizeof(buffer), 0,
                                (struct sockaddr*)&senderAddr, &addrLen);
        if (received <= 0)
            break;

        if (received >= (int)sizeof(DiscoveryPacket))
        {
            ProcessDiscoveryPacket((const DiscoveryPacket*)buffer, senderAddr.sin_addr.s_addr);
        }
    }
}

static void UpdateDiscoveryTimeouts(float deltaTime)
{
    for (int i = 0; i < NET_MAX_DISCOVERED_GAMES; i++)
    {
        if (gDiscoveredGames[i].isValid)
        {
            gDiscoveredGames[i].lastSeen += deltaTime;
            if (gDiscoveredGames[i].lastSeen > NET_DISCOVERY_TIMEOUT)
            {
                gDiscoveredGames[i].isValid = false;
                gDiscoveredGameCount--;
            }
        }
    }
}

//==============================================================================
// MESSAGING
//==============================================================================

void Net_SendToAll(const void* data, size_t size, bool reliable)
{
    if (!gHost)
        return;

    ENetPacket* packet = enet_packet_create(data, size,
        reliable ? ENET_PACKET_FLAG_RELIABLE : 0);

    enet_host_broadcast(gHost, 0, packet);
}

void Net_SendToPeer(int peerIndex, const void* data, size_t size, bool reliable)
{
    if (!gHost || !gIsHosting)
        return;

    if (peerIndex < 0 || peerIndex >= gClientPeerCount)
        return;

    if (!gClientPeers[peerIndex])
        return;

    ENetPacket* packet = enet_packet_create(data, size,
        reliable ? ENET_PACKET_FLAG_RELIABLE : 0);

    enet_peer_send(gClientPeers[peerIndex], 0, packet);
}

void Net_SendToHost(const void* data, size_t size, bool reliable)
{
    if (!gHost || !gServerPeer || gIsHosting)
        return;

    ENetPacket* packet = enet_packet_create(data, size,
        reliable ? ENET_PACKET_FLAG_RELIABLE : 0);

    enet_peer_send(gServerPeer, 0, packet);
}

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

//==============================================================================
// EVENT PROCESSING
//==============================================================================

void Net_ProcessEvents(int timeoutMs)
{
    // Process broadcast/discovery
    static float lastTime = 0.0f;
    float currentTime = (float)enet_time_get() / 1000.0f;
    float deltaTime = (lastTime > 0.0f) ? (currentTime - lastTime) : 0.0f;
    lastTime = currentTime;

    // Send broadcast if hosting
    if (gIsBroadcasting)
    {
        gBroadcastTimer += deltaTime;
        if (gBroadcastTimer >= NET_BROADCAST_INTERVAL)
        {
            SendBroadcast();
            gBroadcastTimer = 0.0f;
        }
    }

    // Receive discovery packets
    ReceiveDiscoveryPackets();
    UpdateDiscoveryTimeouts(deltaTime);

    // Process ENet events
    if (!gHost)
        return;

    ENetEvent event;
    while (enet_host_service(gHost, &event, timeoutMs) > 0)
    {
        timeoutMs = 0;  // Only wait on first iteration

        switch (event.type)
        {
            case ENET_EVENT_TYPE_CONNECT:
            {
                if (gIsHosting)
                {
                    // New client connected
                    if (gClientPeerCount < NET_MAX_PLAYERS)
                    {
                        int peerIndex = gClientPeerCount++;
                        gClientPeers[peerIndex] = event.peer;
                        event.peer->data = (void*)(intptr_t)peerIndex;

                        char ipStr[32];
                        Net_IPToString(event.peer->address.host, ipStr, sizeof(ipStr));
                        printf("Net_ProcessEvents: Client connected from %s (peer %d)\n", ipStr, peerIndex);

                        if (gConnectCallback)
                            gConnectCallback(peerIndex);
                    }
                    else
                    {
                        // Too many players, disconnect
                        enet_peer_disconnect(event.peer, 0);
                    }
                }
                else
                {
                    // We connected to server
                    gIsConnected = true;
                    printf("Net_ProcessEvents: Connected to server\n");
                    if (gConnectCallback)
                        gConnectCallback(0);
                }
                break;
            }

            case ENET_EVENT_TYPE_DISCONNECT:
            {
                if (gIsHosting)
                {
                    int peerIndex = (int)(intptr_t)event.peer->data;
                    printf("Net_ProcessEvents: Client %d disconnected\n", peerIndex);

                    // Remove from array (shift others down)
                    for (int i = peerIndex; i < gClientPeerCount - 1; i++)
                    {
                        gClientPeers[i] = gClientPeers[i + 1];
                        gClientPeers[i]->data = (void*)(intptr_t)i;
                    }
                    gClientPeerCount--;
                    gClientPeers[gClientPeerCount] = NULL;

                    if (gDisconnectCallback)
                        gDisconnectCallback(peerIndex);
                }
                else
                {
                    gIsConnected = false;
                    gServerPeer = NULL;
                    printf("Net_ProcessEvents: Disconnected from server\n");
                    if (gDisconnectCallback)
                        gDisconnectCallback(0);
                }
                break;
            }

            case ENET_EVENT_TYPE_RECEIVE:
            {
                int peerIndex = 0;
                if (gIsHosting)
                {
                    peerIndex = (int)(intptr_t)event.peer->data;
                }

                if (gReceiveCallback)
                {
                    gReceiveCallback(peerIndex, event.packet->data, event.packet->dataLength);
                }

                enet_packet_destroy(event.packet);
                break;
            }

            default:
                break;
        }
    }

    enet_host_flush(gHost);
}

//==============================================================================
// UTILITY
//==============================================================================

void Net_IPToString(uint32_t ip, char* outBuffer, size_t bufferSize)
{
    // ENet stores IP in network byte order
    unsigned char* bytes = (unsigned char*)&ip;
    snprintf(outBuffer, bufferSize, "%d.%d.%d.%d",
             bytes[0], bytes[1], bytes[2], bytes[3]);
}

uint32_t Net_GetLocalIP(void)
{
    // This is a simplified implementation
    // In practice, you might want to enumerate interfaces
    return ENET_HOST_ANY;
}

void Net_CleanupSession(void)
{
    Net_StopBroadcast();
    Net_StopDiscovery();

    if (gServerPeer)
    {
        enet_peer_reset(gServerPeer);
        gServerPeer = NULL;
    }

    for (int i = 0; i < NET_MAX_PLAYERS; i++)
    {
        if (gClientPeers[i])
        {
            enet_peer_reset(gClientPeers[i]);
            gClientPeers[i] = NULL;
        }
    }

    if (gHost)
    {
        enet_host_destroy(gHost);
        gHost = NULL;
    }

    gIsHosting = false;
    gIsConnected = false;
    gClientPeerCount = 0;
    gDiscoveredGameCount = 0;

    memset(gDiscoveredGames, 0, sizeof(gDiscoveredGames));

    printf("Net_CleanupSession: Session cleaned up\n");
}

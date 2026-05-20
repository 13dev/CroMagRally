/****************************/
/*         MAIN.C           */
/* Signaling server for     */
/* Cro-Mag Rally P2P        */
/****************************/

#include "room.h"
#include "signaling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
    #define SHUT_RDWR SD_BOTH
#else
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

//==============================================================================
// CONFIGURATION
//==============================================================================

#define MAX_CLIENTS         64
#define RECV_BUFFER_SIZE    4096
#define CLEANUP_INTERVAL    30  // seconds

//==============================================================================
// CLIENT TRACKING
//==============================================================================

typedef struct Client
{
    socket_t    socket;
    char        recvBuffer[RECV_BUFFER_SIZE];
    int         recvLen;
    uint32_t    connectTime;
    bool        active;
} Client;

static Client gClients[MAX_CLIENTS];
static socket_t gServerSocket = INVALID_SOCKET_VALUE;
static volatile bool gRunning = true;

//==============================================================================
// HELPER FUNCTIONS
//==============================================================================

static void SetNonBlocking(socket_t sock)
{
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
}

static void SignalHandler(int sig)
{
    (void)sig;
    printf("\n[Server] Shutting down...\n");
    gRunning = false;
}

static Client* FindFreeClientSlot(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (!gClients[i].active)
            return &gClients[i];
    }
    return NULL;
}

static Client* FindClientBySocket(socket_t socket)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (gClients[i].active && gClients[i].socket == socket)
            return &gClients[i];
    }
    return NULL;
}

static void SendToClient(socket_t socket, const char* message)
{
    if (!message) return;

    size_t len = strlen(message);
    send(socket, message, (int)len, 0);
}

static void DisconnectClient(Client* client)
{
    if (!client || !client->active)
        return;

    printf("[Server] Client disconnected (socket %d)\n", (int)client->socket);

    // Before disconnecting, notify other players in the room
    Room* room = Room_FindBySocket(client->socket);
    Player* player = Room_GetPlayer(client->socket);
    if (room && player)
    {
        int playerIndex = player->playerIndex;
        printf("[Server] Notifying other players that player %d left\n", playerIndex);

        // Send PLAYER_LEFT to all other players in the room
        for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++)
        {
            if (room->players[i].active && room->players[i].socket != client->socket)
            {
                char msg[64];
                snprintf(msg, sizeof(msg), "PLAYER_LEFT:%d\n", playerIndex);
                SendToClient(room->players[i].socket, msg);
                printf("[Server] Sent PLAYER_LEFT:%d to socket %d\n", playerIndex, (int)room->players[i].socket);
            }
        }
    }

    Signaling_HandleDisconnect(client->socket);

    shutdown(client->socket, SHUT_RDWR);
    close(client->socket);

    client->socket = INVALID_SOCKET_VALUE;
    client->active = false;
    client->recvLen = 0;
}

static void ProcessClientMessage(Client* client, const char* message)
{
    // Don't spam logs with GAME messages (60/sec per player during gameplay)
    if (strncmp(message, "GAME:", 5) != 0)
    {
        printf("[Server] Received from socket %d: %s\n", (int)client->socket, message);
    }

    // Handle the message
    char* response = Signaling_HandleMessage(client->socket, message);
    if (response)
    {
        SendToClient(client->socket, response);
        free(response);
    }

    // Check for relay messages (ICE, SIGNAL, START, etc.)
    SignalingRelay relays[MAX_CLIENTS];
    int relayCount = Signaling_GetRelays(client->socket, message, relays, MAX_CLIENTS);

    for (int i = 0; i < relayCount; i++)
    {
        SendToClient(relays[i].socket, relays[i].message);
        free(relays[i].message);
    }

    // Special handling for JOIN - need to notify others AND send player list to joining client
    if (strncmp(message, "JOIN:", 5) == 0)
    {
        // Notify existing players about the new player
        SignalingRelay joinRelays[MAX_CLIENTS];
        int joinCount = Signaling_GetRelays(client->socket, "PLAYER_JOINED_EVENT", joinRelays, MAX_CLIENTS);
        for (int i = 0; i < joinCount; i++)
        {
            SendToClient(joinRelays[i].socket, joinRelays[i].message);
            free(joinRelays[i].message);
        }

        // Send player list to the joining client (so they see existing players)
        Room* room = Room_FindBySocket(client->socket);
        printf("[Server] JOIN special handling: room=%p, client socket=%d\n", (void*)room, (int)client->socket);
        if (room)
        {
            printf("[Server] Room found, playerCount=%d. Checking for other players...\n", room->playerCount);
            for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++)
            {
                if (room->players[i].active)
                {
                    printf("[Server]   Player %d: socket=%d, name='%s', active=%d\n",
                           i, (int)room->players[i].socket, room->players[i].name, room->players[i].active);
                }
                if (room->players[i].active && room->players[i].socket != client->socket)
                {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "PLAYER_INFO:%d:%s\n",
                             room->players[i].playerIndex, room->players[i].name);
                    printf("[Server] Sending to client: %s", msg);
                    SendToClient(client->socket, msg);
                }
            }
        }
        else
        {
            printf("[Server] WARNING: Room not found for client socket %d after JOIN!\n", (int)client->socket);
        }
    }
}

static void ProcessClientData(Client* client)
{
    // Look for complete messages (newline-terminated)
    char* newline;
    while ((newline = strchr(client->recvBuffer, '\n')) != NULL)
    {
        *newline = '\0';

        // Process this message
        if (client->recvBuffer[0] != '\0')
        {
            ProcessClientMessage(client, client->recvBuffer);
        }

        // Shift remaining data to start of buffer
        int processed = (int)(newline - client->recvBuffer + 1);
        int remaining = client->recvLen - processed;
        if (remaining > 0)
        {
            memmove(client->recvBuffer, newline + 1, remaining);
        }
        client->recvLen = remaining;
        client->recvBuffer[remaining] = '\0';
    }

    // Check for buffer overflow
    if (client->recvLen >= RECV_BUFFER_SIZE - 1)
    {
        printf("[Server] Client buffer overflow, disconnecting\n");
        DisconnectClient(client);
    }
}

//==============================================================================
// MAIN SERVER LOOP
//==============================================================================

static bool InitServer(int port)
{
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        fprintf(stderr, "[Server] WSAStartup failed\n");
        return false;
    }
#endif

    // Initialize subsystems
    Room_Init();
    Signaling_Init();

    // Create server socket
    gServerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (gServerSocket == INVALID_SOCKET_VALUE)
    {
        fprintf(stderr, "[Server] Failed to create socket\n");
        return false;
    }

    // Allow address reuse
    int reuse = 1;
    setsockopt(gServerSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    // Bind
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(gServerSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        fprintf(stderr, "[Server] Failed to bind to port %d\n", port);
        close(gServerSocket);
        return false;
    }

    // Listen
    if (listen(gServerSocket, 16) < 0)
    {
        fprintf(stderr, "[Server] Failed to listen\n");
        close(gServerSocket);
        return false;
    }

    SetNonBlocking(gServerSocket);

    // Initialize client slots
    memset(gClients, 0, sizeof(gClients));
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        gClients[i].socket = INVALID_SOCKET_VALUE;
    }

    printf("[Server] Listening on port %d\n", port);
    return true;
}

static void AcceptNewClients(void)
{
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);

    socket_t clientSocket = accept(gServerSocket, (struct sockaddr*)&clientAddr, &addrLen);
    if (clientSocket == INVALID_SOCKET_VALUE)
        return;

    Client* client = FindFreeClientSlot();
    if (!client)
    {
        printf("[Server] Max clients reached, rejecting connection\n");
        const char* msg = "ERROR:Server full\n";
        send(clientSocket, msg, (int)strlen(msg), 0);
        close(clientSocket);
        return;
    }

    client->socket = clientSocket;
    client->active = true;
    client->recvLen = 0;
    client->connectTime = (uint32_t)time(NULL);
    memset(client->recvBuffer, 0, sizeof(client->recvBuffer));

    SetNonBlocking(clientSocket);

    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
    printf("[Server] New client from %s (socket %d)\n", ipStr, (int)clientSocket);
}

static void ProcessClients(void)
{
    fd_set readSet;
    FD_ZERO(&readSet);

    socket_t maxFd = gServerSocket;
    FD_SET(gServerSocket, &readSet);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (gClients[i].active)
        {
            FD_SET(gClients[i].socket, &readSet);
            if (gClients[i].socket > maxFd)
                maxFd = gClients[i].socket;
        }
    }

    struct timeval timeout = { 0, 100000 };  // 100ms
    int ready = select((int)(maxFd + 1), &readSet, NULL, NULL, &timeout);

    if (ready <= 0)
        return;

    // Check for new connections
    if (FD_ISSET(gServerSocket, &readSet))
    {
        AcceptNewClients();
    }

    // Process client data
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (!gClients[i].active)
            continue;

        if (FD_ISSET(gClients[i].socket, &readSet))
        {
            char buffer[1024];
            int received = recv(gClients[i].socket, buffer, sizeof(buffer) - 1, 0);

            if (received <= 0)
            {
                DisconnectClient(&gClients[i]);
            }
            else
            {
                buffer[received] = '\0';

                // Append to client buffer
                int space = RECV_BUFFER_SIZE - gClients[i].recvLen - 1;
                int toCopy = (received < space) ? received : space;
                memcpy(gClients[i].recvBuffer + gClients[i].recvLen, buffer, toCopy);
                gClients[i].recvLen += toCopy;
                gClients[i].recvBuffer[gClients[i].recvLen] = '\0';

                ProcessClientData(&gClients[i]);
            }
        }
    }
}

static void PeriodicCleanup(void)
{
    static uint32_t lastCleanup = 0;
    uint32_t now = (uint32_t)time(NULL);

    if (now - lastCleanup < CLEANUP_INTERVAL)
        return;

    lastCleanup = now;

    Room_CleanupExpired();

    // Log stats
    int rooms, players;
    Room_GetStats(&rooms, &players);
    printf("[Server] Stats: %d active rooms, %d total players\n", rooms, players);
}

static void Shutdown(void)
{
    // Disconnect all clients
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (gClients[i].active)
        {
            DisconnectClient(&gClients[i]);
        }
    }

    if (gServerSocket != INVALID_SOCKET_VALUE)
    {
        close(gServerSocket);
        gServerSocket = INVALID_SOCKET_VALUE;
    }

#ifdef _WIN32
    WSACleanup();
#endif

    printf("[Server] Shutdown complete\n");
}

//==============================================================================
// MAIN
//==============================================================================

int main(int argc, char* argv[])
{
    int port = SIGNALING_PORT;

    // Parse command line args
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
        {
            port = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            port = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            printf("Cro-Mag Rally Signaling Server\n");
            printf("Usage: %s [-p PORT]\n", argv[0]);
            printf("  -p, --port PORT    Port to listen on (default: %d)\n", SIGNALING_PORT);
            return 0;
        }
    }

    // Setup signal handlers
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    printf("===========================================\n");
    printf("  Cro-Mag Rally Signaling Server v1.0\n");
    printf("===========================================\n\n");

    if (!InitServer(port))
    {
        return 1;
    }

    // Main loop
    while (gRunning)
    {
        ProcessClients();
        PeriodicCleanup();
    }

    Shutdown();
    return 0;
}

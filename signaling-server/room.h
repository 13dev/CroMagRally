/****************************/
/*         ROOM.H           */
/* Room management for      */
/* signaling server         */
/****************************/

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    typedef int socket_t;
    #define INVALID_SOCKET_VALUE -1
#endif

//==============================================================================
// CONSTANTS
//==============================================================================

#define ROOM_CODE_LENGTH    4       // "ABCD"
#define MAX_ROOMS           100
#define MAX_PLAYERS_PER_ROOM 6
#define ROOM_TIMEOUT_SECS   300     // 5 minutes

//==============================================================================
// DATA STRUCTURES
//==============================================================================

typedef struct Player
{
    socket_t    socket;
    uint32_t    playerIndex;        // 0 = host, 1-5 = clients
    bool        active;
    char        name[32];
    char        identity[128];      // P2P identity string for signaling
    uint32_t    lastActivity;       // Timestamp for timeout detection
} Player;

typedef struct Room
{
    char        code[ROOM_CODE_LENGTH + 1];
    Player      players[MAX_PLAYERS_PER_ROOM];
    uint32_t    playerCount;
    uint32_t    createdTime;
    bool        active;
    bool        gameStarted;
} Room;

//==============================================================================
// FUNCTIONS
//==============================================================================

// Initialize the room system
void Room_Init(void);

// Create a new room, returns room code or NULL on failure
const char* Room_Create(socket_t hostSocket, const char* hostName, const char* hostIdentity);

// Join an existing room by code
// Returns player index (1-5) on success, -1 on failure
int Room_Join(const char* code, socket_t clientSocket, const char* clientName, const char* clientIdentity);

// Find player by identity string (for signal relay)
Player* Room_FindPlayerByIdentity(const char* identity);

// Leave a room (called when socket disconnects)
void Room_Leave(socket_t socket);

// Find room by socket
Room* Room_FindBySocket(socket_t socket);

// Find room by code
Room* Room_FindByCode(const char* code);

// Get player by socket
Player* Room_GetPlayer(socket_t socket);

// Get all other players in the same room (for signaling relay)
// Returns count of players written to outPlayers
int Room_GetOtherPlayers(socket_t socket, Player** outPlayers, int maxPlayers);

// Check if socket is a host
bool Room_IsHost(socket_t socket);

// Mark game as started
void Room_MarkGameStarted(const char* code);

// Clean up timed out rooms
void Room_CleanupExpired(void);

// Get room stats for logging
void Room_GetStats(int* outActiveRooms, int* outTotalPlayers);

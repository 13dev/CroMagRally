/****************************/
/*    BACKEND_NETWORK.H     */
/* Network abstraction layer*/
/* GameNetworkingSockets    */
/****************************/

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

//==============================================================================
// CONSTANTS
//==============================================================================

#define NET_SERVER_PORT         40000
#define NET_MAX_PLAYERS         6
#define NET_ROOM_CODE_LENGTH    4           // "ABCD"
#define NET_PLAYER_NAME_LENGTH  32

// Default relay server (Fly.io)
#define NET_DEFAULT_SERVER_HOST "149.248.193.115"

//==============================================================================
// CONNECTION STATE
//==============================================================================

typedef enum NetConnectionState
{
    NET_STATE_DISCONNECTED = 0,
    NET_STATE_CONNECTING_SIGNALING,     // Connecting to relay server
    NET_STATE_WAITING_ROOM,             // Waiting for room assignment
    NET_STATE_IN_LOBBY,                 // In room, waiting for players
    NET_STATE_CONNECTING_P2P,           // Reserved (unused)
    NET_STATE_CONNECTED,                // Connected and playing
    NET_STATE_ERROR
} NetConnectionState;

//==============================================================================
// CALLBACKS
//==============================================================================

typedef void (*NetConnectCallback)(int peerIndex);
typedef void (*NetDisconnectCallback)(int peerIndex);
typedef void (*NetReceiveCallback)(int peerIndex, const void* data, size_t size);
typedef void (*NetStateChangeCallback)(NetConnectionState newState, const char* message);
typedef void (*Net_PlayerNameCallback)(int playerIndex, const char* name);
typedef void (*Net_WorldStateCallback)(const void* worldState);  // Equal-players model callback
typedef void (*Net_WeaponEventCallback)(const void* weaponEvent);  // Remote weapon thrown

//==============================================================================
// NETWORK LIFECYCLE
//==============================================================================

// Initialize the network subsystem (call once at boot)
bool Net_Initialize(void);

// Shutdown the network subsystem (call once at exit)
void Net_Shutdown(void);

// Returns true if network subsystem is initialized
bool Net_IsInitialized(void);

// Set the relay server address (call before hosting/joining)
void Net_SetSignalingServer(const char* host, uint16_t port);

//==============================================================================
// HOST FUNCTIONS
//==============================================================================

// Create a game host and connect to relay server
// Returns true if connection initiated successfully
bool Net_CreateHost(const char* playerName);

// Get the room code (valid after entering lobby)
const char* Net_GetRoomCode(void);

// Check if we are currently hosting
bool Net_IsHosting(void);

// Get number of connected players (including host)
int Net_GetPlayerCount(void);

// Get number of remote players
int Net_GetP2PConnectionCount(void);

// Start the game (host only)
bool Net_StartGame(void);

//==============================================================================
// CLIENT FUNCTIONS
//==============================================================================

// Join a game using a room code
// Returns true if join request initiated successfully
bool Net_JoinGame(const char* roomCode, const char* playerName);

// Disconnect from current game
void Net_Disconnect(void);

// Check if connected to a host
bool Net_IsConnected(void);

// Get our player index
int Net_GetLocalPlayerIndex(void);

//==============================================================================
// MESSAGING
//==============================================================================

// Send data to all connected peers (host sends to clients, client sends to host)
void Net_SendToAll(const void* data, size_t size, bool reliable);

// Send data to a specific peer by index (host only)
void Net_SendToPeer(int peerIndex, const void* data, size_t size, bool reliable);

// Send data to host (client only)
void Net_SendToHost(const void* data, size_t size, bool reliable);

//==============================================================================
// TYPED SEND FUNCTIONS
//==============================================================================

// Send game configuration
void Net_SendConfig(int peerIndex, int gameMode, int age, int trackNum,
                    int playerNum, int numPlayers, int numAgesCompleted,
                    int difficulty, int tagDuration);

// Send sync message (level prepared acknowledgment)
void Net_SendSync(int playerNum);

// Send host control info (60Hz authoritative state)
void Net_SendHostControl(const void* hostControlInfo);

// Send client control info (60Hz input)
void Net_SendClientControl(const void* clientControlInfo);

// Send vehicle type selection
void Net_SendVehicleType(int playerNum, int vehicleType, int sex);

// Equal-players model: send local player state to server
void Net_SendPlayerState(const void* playerState);

// Send weapon event (player threw/launched a weapon)
void Net_SendWeaponEvent(int weaponType, int playerNum, int throwForward,
                         float posX, float posY, float posZ,
                         float velX, float velY, float velZ, float rotY);

//==============================================================================
// CALLBACKS
//==============================================================================

void Net_SetConnectCallback(NetConnectCallback callback);
void Net_SetDisconnectCallback(NetDisconnectCallback callback);
void Net_SetReceiveCallback(NetReceiveCallback callback);
void Net_SetStateChangeCallback(NetStateChangeCallback callback);
void Net_SetPlayerNameCallback(Net_PlayerNameCallback callback);
void Net_SetWorldStateCallback(Net_WorldStateCallback callback);  // Equal-players model
void Net_SetWeaponEventCallback(Net_WeaponEventCallback callback);  // Remote weapon events

//==============================================================================
// EVENT PROCESSING
//==============================================================================

// Process network events (call every frame)
void Net_ProcessEvents(int timeoutMs);

// Get current connection state
NetConnectionState Net_GetState(void);

// Get last error message
const char* Net_GetLastError(void);

//==============================================================================
// UTILITY
//==============================================================================

// Cleanup any active connections and reset state
void Net_CleanupSession(void);

#ifdef __cplusplus
}
#endif

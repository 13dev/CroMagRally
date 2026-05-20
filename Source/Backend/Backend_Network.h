/****************************/
/*    BACKEND_NETWORK.H     */
/* Network abstraction layer*/
/* Using TCP relay server   */
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

#define NET_SIGNALING_PORT      27015       // TCP relay server port
#define NET_MAX_PLAYERS         6
#define NET_ROOM_CODE_LENGTH    4           // "ABCD"
#define NET_PLAYER_NAME_LENGTH  32

// Default signaling server (can be overridden)
#define NET_DEFAULT_SIGNALING_HOST  "cromag-signaling.fly.dev"

//==============================================================================
// CONNECTION STATE
//==============================================================================

typedef enum NetConnectionState
{
    NET_STATE_DISCONNECTED = 0,
    NET_STATE_CONNECTING_SIGNALING,     // Connecting to relay server
    NET_STATE_WAITING_ROOM,             // Host: waiting for room code / Client: joining room
    NET_STATE_IN_LOBBY,                 // In room, waiting for players
    NET_STATE_CONNECTING_P2P,           // Reserved for compatibility (unused with TCP relay)
    NET_STATE_CONNECTED,                // Connected via TCP relay
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

//==============================================================================
// NETWORK LIFECYCLE
//==============================================================================

// Initialize the network subsystem (call once at boot)
bool Net_Initialize(void);

// Shutdown the network subsystem (call once at exit)
void Net_Shutdown(void);

// Returns true if network subsystem is initialized
bool Net_IsInitialized(void);

// Set the signaling server address (call before hosting/joining)
void Net_SetSignalingServer(const char* host, uint16_t port);

//==============================================================================
// HOST FUNCTIONS
//==============================================================================

// Create a game host and register with signaling server
// Callback will be invoked with room code when ready
// Returns true if registration initiated successfully
bool Net_CreateHost(const char* playerName);

// Get the room code (valid after receiving room code callback)
const char* Net_GetRoomCode(void);

// Check if we are currently hosting
bool Net_IsHosting(void);

// Get number of connected players (including host)
int Net_GetPlayerCount(void);

// Get number of connections (for compatibility - returns player count - 1)
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
// reliable: if true, guaranteed delivery; if false, may be dropped
void Net_SendToAll(const void* data, size_t size, bool reliable);

// Send data to a specific peer by index (host only)
void Net_SendToPeer(int peerIndex, const void* data, size_t size, bool reliable);

// Send data to host (client only)
void Net_SendToHost(const void* data, size_t size, bool reliable);

//==============================================================================
// CALLBACKS
//==============================================================================

void Net_SetConnectCallback(NetConnectCallback callback);
void Net_SetDisconnectCallback(NetDisconnectCallback callback);
void Net_SetReceiveCallback(NetReceiveCallback callback);
void Net_SetStateChangeCallback(NetStateChangeCallback callback);
void Net_SetPlayerNameCallback(Net_PlayerNameCallback callback);

//==============================================================================
// EVENT PROCESSING
//==============================================================================

// Process network events (call every frame)
// timeout: milliseconds to wait for events (0 for non-blocking)
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

/****************************/
/*    BACKEND_NETWORK.H     */
/* Network abstraction layer*/
/* Using ENet for LAN play  */
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

#define NET_DISCOVERY_PORT      19670       // UDP broadcast for LAN discovery
#define NET_GAME_PORT           19671       // ENet game traffic
#define NET_MAX_PLAYERS         6
#define NET_MAX_CHANNELS        2           // 0=reliable, 1=unreliable
#define NET_GAME_NAME_LENGTH    32
#define NET_PLAYER_NAME_LENGTH  32
#define NET_BROADCAST_INTERVAL  1.0f        // Seconds between broadcasts
#define NET_DISCOVERY_TIMEOUT   5.0f        // Seconds before game entry expires

//==============================================================================
// DATA STRUCTURES
//==============================================================================

// Information about a discovered LAN game
typedef struct LANGameInfo
{
    char        gameName[NET_GAME_NAME_LENGTH];
    char        hostName[NET_PLAYER_NAME_LENGTH];
    uint32_t    hostIP;
    uint16_t    hostPort;
    uint8_t     currentPlayers;
    uint8_t     maxPlayers;
    uint8_t     gameMode;
    float       lastSeen;           // Time since last broadcast received
    bool        isValid;
} LANGameInfo;

#define NET_MAX_DISCOVERED_GAMES    16

//==============================================================================
// NETWORK LIFECYCLE
//==============================================================================

// Initialize the network subsystem (call once at boot)
bool Net_Initialize(void);

// Shutdown the network subsystem (call once at exit)
void Net_Shutdown(void);

// Returns true if network subsystem is initialized
bool Net_IsInitialized(void);

//==============================================================================
// HOST FUNCTIONS
//==============================================================================

// Create a game host that accepts connections
// Returns true on success
bool Net_CreateHost(uint16_t port, int maxPlayers);

// Start broadcasting game availability for LAN discovery
void Net_StartBroadcast(const char* gameName, const char* hostName, int gameMode);

// Stop broadcasting game availability
void Net_StopBroadcast(void);

// Check if we are currently hosting
bool Net_IsHosting(void);

// Get number of connected clients
int Net_GetConnectedClientCount(void);

//==============================================================================
// CLIENT FUNCTIONS
//==============================================================================

// Create a client and connect to a host
// Returns true if connection initiated (use Net_IsConnected to check completion)
bool Net_Connect(uint32_t hostIP, uint16_t port);

// Disconnect from current host
void Net_Disconnect(void);

// Check if connected to a host
bool Net_IsConnected(void);

//==============================================================================
// LAN DISCOVERY
//==============================================================================

// Start scanning for LAN games
void Net_StartDiscovery(void);

// Stop scanning for LAN games
void Net_StopDiscovery(void);

// Get list of discovered games (call after Net_ProcessEvents)
// Returns number of valid games found
int Net_GetDiscoveredGames(LANGameInfo* outGames, int maxGames);

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

// Callback types for receiving data
typedef void (*NetConnectCallback)(int peerIndex);
typedef void (*NetDisconnectCallback)(int peerIndex);
typedef void (*NetReceiveCallback)(int peerIndex, const void* data, size_t size);

// Set callbacks for network events
void Net_SetConnectCallback(NetConnectCallback callback);
void Net_SetDisconnectCallback(NetDisconnectCallback callback);
void Net_SetReceiveCallback(NetReceiveCallback callback);

//==============================================================================
// EVENT PROCESSING
//==============================================================================

// Process network events (call every frame)
// timeout: milliseconds to wait for events (0 for non-blocking)
void Net_ProcessEvents(int timeoutMs);

//==============================================================================
// UTILITY
//==============================================================================

// Convert IP address to string (e.g., "192.168.1.100")
void Net_IPToString(uint32_t ip, char* outBuffer, size_t bufferSize);

// Get local IP address for display
uint32_t Net_GetLocalIP(void);

// Cleanup any active connections and reset state
void Net_CleanupSession(void);

#ifdef __cplusplus
}
#endif

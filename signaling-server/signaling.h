/****************************/
/*       SIGNALING.H        */
/* ICE candidate exchange   */
/* and protocol handling    */
/****************************/

#pragma once

#include "room.h"

//==============================================================================
// PROTOCOL CONSTANTS
//==============================================================================

#define SIGNALING_PORT          27015
#define MAX_MESSAGE_SIZE        4096
#define PROTOCOL_VERSION        1

//==============================================================================
// MESSAGE TYPES
//==============================================================================

// Client -> Server
#define MSG_REGISTER            "REGISTER"      // Host creates room: REGISTER:<name>
#define MSG_JOIN                "JOIN"          // Client joins: JOIN:<code>:<name>
#define MSG_LEAVE               "LEAVE"         // Leave room: LEAVE
#define MSG_ICE                 "ICE"           // ICE candidate: ICE:<target>:<candidate>
#define MSG_SIGNAL              "SIGNAL"        // Generic signaling: SIGNAL:<target>:<data>
#define MSG_START               "START"         // Host starts game: START
#define MSG_PING                "PING"          // Keep-alive
#define MSG_GAME                "GAME"          // Game data relay: GAME:<base64_payload>

// Server -> Client
#define MSG_ROOM                "ROOM"          // Room created: ROOM:<code>
#define MSG_JOINED              "JOINED"        // Successfully joined: JOINED:<playerIndex>
#define MSG_PLAYER_INFO         "PLAYER_INFO"   // Player info: PLAYER_INFO:<playerIndex>:<name>
#define MSG_PLAYER_JOINED       "PLAYER_JOINED" // Other player joined: PLAYER_JOINED:<playerIndex>:<name>
#define MSG_PLAYER_LEFT         "PLAYER_LEFT"   // Other player left: PLAYER_LEFT:<playerIndex>
#define MSG_RELAY_ICE           "RELAY_ICE"     // ICE from peer: RELAY_ICE:<from>:<candidate>
#define MSG_RELAY_SIGNAL        "RELAY_SIGNAL"  // Signal from peer: RELAY_SIGNAL:<from>:<data>
#define MSG_GAME_STARTING       "GAME_STARTING" // Host started game
#define MSG_ERROR               "ERROR"         // Error message: ERROR:<message>
#define MSG_OK                  "OK"            // Generic success
#define MSG_PONG                "PONG"          // Keep-alive response

//==============================================================================
// FUNCTIONS
//==============================================================================

// Initialize signaling system
void Signaling_Init(void);

// Handle incoming message from a client
// Returns message to send back (caller must free), or NULL if no response needed
char* Signaling_HandleMessage(socket_t socket, const char* message);

// Build a message to notify other players in the room
// Returns list of (socket, message) pairs to send
typedef struct SignalingRelay
{
    socket_t    socket;
    char*       message;  // Caller must free
} SignalingRelay;

// Get messages to relay to other players after a state change
// Returns number of relays, fills outRelays array
int Signaling_GetRelays(socket_t socket, const char* event, SignalingRelay* outRelays, int maxRelays);

// Clean up resources for a disconnected socket
void Signaling_HandleDisconnect(socket_t socket);

// Build formatted message (caller must free result)
char* Signaling_FormatMessage(const char* type, const char* data);

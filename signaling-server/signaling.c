/****************************/
/*       SIGNALING.C        */
/* P2P signaling protocol   */
/* with identity-based relay*/
/****************************/

#include "signaling.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

//==============================================================================
// INTERNAL STATE
//==============================================================================

static bool gInitialized = false;

//==============================================================================
// HELPER FUNCTIONS
//==============================================================================

static char* AllocString(const char* str)
{
    if (!str) return NULL;
    size_t len = strlen(str);
    char* result = (char*)malloc(len + 1);
    if (result)
    {
        memcpy(result, str, len + 1);
    }
    return result;
}

static char* FormatResponse(const char* type, const char* data)
{
    char buffer[MAX_MESSAGE_SIZE];
    if (data && data[0])
    {
        snprintf(buffer, sizeof(buffer), "%s:%s\n", type, data);
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "%s\n", type);
    }
    return AllocString(buffer);
}

// Parse "COMMAND:arg1:arg2:..." format
// Returns command, fills args array (modifies message)
// For SIGNAL messages, we only split on first two colons to preserve hex payload
static const char* ParseMessage(char* message, char** args, int maxArgs, int* outArgCount, bool preservePayload)
{
    *outArgCount = 0;

    // Trim trailing whitespace
    size_t len = strlen(message);
    while (len > 0 && (message[len-1] == '\n' || message[len-1] == '\r' || message[len-1] == ' '))
    {
        message[--len] = '\0';
    }

    char* cmd = message;
    char* colonPos = strchr(message, ':');
    if (!colonPos)
        return cmd;

    *colonPos = '\0';
    char* remaining = colonPos + 1;

    for (int i = 0; i < maxArgs && *remaining; i++)
    {
        args[i] = remaining;
        (*outArgCount)++;

        // For the last arg when preserving payload, don't split further
        if (preservePayload && i == maxArgs - 1)
            break;

        colonPos = strchr(remaining, ':');
        if (!colonPos)
            break;

        *colonPos = '\0';
        remaining = colonPos + 1;
    }

    return cmd;
}

//==============================================================================
// PUBLIC FUNCTIONS
//==============================================================================

void Signaling_Init(void)
{
    gInitialized = true;
    printf("[Signaling] Initialized signaling protocol handler\n");
}

char* Signaling_HandleMessage(socket_t socket, const char* message)
{
    if (!gInitialized || !message)
        return FormatResponse(MSG_ERROR, "Server not initialized");

    // Handle HTTP health check requests from load balancers (Render, etc.)
    if (strncmp(message, "HEAD ", 5) == 0 || strncmp(message, "GET ", 4) == 0)
    {
        return AllocString("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Type: text/plain\r\n\r\nOK");
    }

    // Copy message for parsing
    char msgCopy[MAX_MESSAGE_SIZE];
    strncpy(msgCopy, message, sizeof(msgCopy) - 1);
    msgCopy[sizeof(msgCopy) - 1] = '\0';

    char* args[8];
    int argCount = 0;

    // Check if this is a SIGNAL message (need to preserve payload)
    bool isSignal = (strncmp(message, "SIGNAL:", 7) == 0);
    const char* cmd = ParseMessage(msgCopy, args, isSignal ? 2 : 8, &argCount, isSignal);

    if (!cmd)
        return FormatResponse(MSG_ERROR, "Invalid message format");

    //--------------------------------------------------------------------------
    // PING - Keep alive
    //--------------------------------------------------------------------------
    if (strcmp(cmd, MSG_PING) == 0)
    {
        Player* player = Room_GetPlayer(socket);
        if (player)
        {
            player->lastActivity = (uint32_t)time(NULL);
        }
        return FormatResponse(MSG_PONG, NULL);
    }

    //--------------------------------------------------------------------------
    // REGISTER - Host creates a new room
    // Format: REGISTER:name:identity
    //--------------------------------------------------------------------------
    if (strcmp(cmd, MSG_REGISTER) == 0)
    {
        const char* name = (argCount > 0) ? args[0] : "Host";
        const char* identity = (argCount > 1) ? args[1] : NULL;

        // Check if already in a room
        Room* existingRoom = Room_FindBySocket(socket);
        if (existingRoom)
        {
            return FormatResponse(MSG_ERROR, "Already in a room");
        }

        const char* roomCode = Room_Create(socket, name, identity);
        if (!roomCode)
        {
            return FormatResponse(MSG_ERROR, "Failed to create room");
        }

        printf("[Signaling] Host '%s' (identity: %s) created room %s\n",
               name, identity ? identity : "none", roomCode);
        return FormatResponse(MSG_ROOM, roomCode);
    }

    //--------------------------------------------------------------------------
    // JOIN - Client joins an existing room
    // Format: JOIN:roomCode:name:identity
    //--------------------------------------------------------------------------
    if (strcmp(cmd, MSG_JOIN) == 0)
    {
        if (argCount < 1)
            return FormatResponse(MSG_ERROR, "Missing room code");

        const char* roomCode = args[0];
        const char* name = (argCount > 1) ? args[1] : "Player";
        const char* identity = (argCount > 2) ? args[2] : NULL;

        // Check if already in a room
        Room* existingRoom = Room_FindBySocket(socket);
        if (existingRoom)
        {
            return FormatResponse(MSG_ERROR, "Already in a room");
        }

        int playerIndex = Room_Join(roomCode, socket, name, identity);
        if (playerIndex < 0)
        {
            return FormatResponse(MSG_ERROR, "Failed to join room");
        }

        // Get host identity to send to client
        Room* room = Room_FindByCode(roomCode);
        const char* hostIdentity = (room && room->players[0].identity[0])
                                   ? room->players[0].identity : "";

        printf("[Signaling] Player '%s' (identity: %s) joined room %s as player %d\n",
               name, identity ? identity : "none", roomCode, playerIndex);

        // Response format: playerIndex:hostIdentity
        char response[256];
        snprintf(response, sizeof(response), "%d:%s", playerIndex, hostIdentity);
        return FormatResponse(MSG_JOINED, response);
    }

    //--------------------------------------------------------------------------
    // LEAVE - Leave current room
    //--------------------------------------------------------------------------
    if (strcmp(cmd, MSG_LEAVE) == 0)
    {
        Room_Leave(socket);
        return FormatResponse(MSG_OK, NULL);
    }

    //--------------------------------------------------------------------------
    // SIGNAL - P2P signaling relay
    // Format: SIGNAL:destIdentity:hexPayload
    //--------------------------------------------------------------------------
    if (strcmp(cmd, MSG_SIGNAL) == 0)
    {
        if (argCount < 2)
            return FormatResponse(MSG_ERROR, "Missing signal parameters");

        // Signal relay is handled in Signaling_GetRelays
        return FormatResponse(MSG_OK, NULL);
    }

    //--------------------------------------------------------------------------
    // GAME - Game data relay
    // Format: GAME:base64Payload
    // Relay is handled in Signaling_GetRelays
    //--------------------------------------------------------------------------
    if (strcmp(cmd, MSG_GAME) == 0)
    {
        // Game relay is handled in Signaling_GetRelays
        // No response needed - just relay to peers
        return NULL;
    }

    //--------------------------------------------------------------------------
    // START - Host starts the game
    //--------------------------------------------------------------------------
    if (strcmp(cmd, MSG_START) == 0)
    {
        if (!Room_IsHost(socket))
        {
            return FormatResponse(MSG_ERROR, "Only host can start game");
        }

        Room* room = Room_FindBySocket(socket);
        if (room)
        {
            Room_MarkGameStarted(room->code);
            printf("[Signaling] Game starting in room %s\n", room->code);
        }

        return FormatResponse(MSG_OK, NULL);
    }

    //--------------------------------------------------------------------------
    // Unknown command
    //--------------------------------------------------------------------------
    printf("[Signaling] Unknown command: %s\n", cmd);
    return FormatResponse(MSG_ERROR, "Unknown command");
}

int Signaling_GetRelays(socket_t socket, const char* event, SignalingRelay* outRelays, int maxRelays)
{
    if (!gInitialized || !event || !outRelays || maxRelays <= 0)
        return 0;

    Room* room = Room_FindBySocket(socket);
    if (!room)
        return 0;

    Player* sender = Room_GetPlayer(socket);
    if (!sender)
        return 0;

    int relayCount = 0;

    // Copy event for parsing
    char eventCopy[MAX_MESSAGE_SIZE];
    strncpy(eventCopy, event, sizeof(eventCopy) - 1);
    eventCopy[sizeof(eventCopy) - 1] = '\0';

    // Trim whitespace
    size_t len = strlen(eventCopy);
    while (len > 0 && (eventCopy[len-1] == '\n' || eventCopy[len-1] == '\r'))
        eventCopy[--len] = '\0';

    //--------------------------------------------------------------------------
    // Handle GAME message relay
    // Host -> broadcast to all clients
    // Client -> send to host only
    //--------------------------------------------------------------------------
    if (strncmp(eventCopy, "GAME:", 5) == 0)
    {
        char* payload = eventCopy + 5;

        if (sender->playerIndex == 0)
        {
            // Host -> broadcast to all clients
            for (int i = 1; i < MAX_PLAYERS_PER_ROOM && relayCount < maxRelays; i++)
            {
                if (room->players[i].active)
                {
                    outRelays[relayCount].socket = room->players[i].socket;
                    outRelays[relayCount].message = FormatResponse(MSG_GAME, payload);
                    relayCount++;
                }
            }
        }
        else
        {
            // Client -> send to host only
            if (room->players[0].active)
            {
                outRelays[relayCount].socket = room->players[0].socket;
                outRelays[relayCount].message = FormatResponse(MSG_GAME, payload);
                relayCount++;
            }
        }
        return relayCount;
    }

    //--------------------------------------------------------------------------
    // Handle SIGNAL relay - identity-based
    //--------------------------------------------------------------------------
    if (strncmp(eventCopy, "SIGNAL:", 7) == 0)
    {
        printf("[Signaling] Processing SIGNAL from socket %d, sender=%s\n",
               (int)socket, sender ? sender->identity : "NULL");

        // Format: SIGNAL:destIdentity:hexPayload
        char* destIdentity = eventCopy + 7;
        char* colonPos = strchr(destIdentity, ':');
        if (!colonPos)
        {
            printf("[Signaling] SIGNAL parse failed: no colon after destIdentity\n");
            return 0;
        }

        *colonPos = '\0';
        char* payload = colonPos + 1;

        printf("[Signaling] Looking for identity '%s'\n", destIdentity);

        // Find destination player by identity
        Player* destPlayer = Room_FindPlayerByIdentity(destIdentity);
        printf("[Signaling] Room_FindPlayerByIdentity returned: %s\n",
               destPlayer ? destPlayer->identity : "NULL");

        if (destPlayer && destPlayer->socket != socket && relayCount < maxRelays)
        {
            // Format: SIGNAL:senderIdentity:payload
            char msg[MAX_MESSAGE_SIZE];
            snprintf(msg, sizeof(msg), "%s:%s", sender->identity, payload);

            outRelays[relayCount].socket = destPlayer->socket;
            outRelays[relayCount].message = FormatResponse(MSG_SIGNAL, msg);
            relayCount++;

            printf("[Signaling] Relaying signal from %s to %s (%zu bytes)\n",
                   sender->identity, destIdentity, strlen(payload) / 2);
        }
        else
        {
            printf("[Signaling] Relay failed: destPlayer=%p, same_socket=%d, relayCount=%d/%d\n",
                   (void*)destPlayer,
                   destPlayer ? (destPlayer->socket == socket) : 0,
                   relayCount, maxRelays);
        }
        return relayCount;
    }

    //--------------------------------------------------------------------------
    // Handle player join notification
    //--------------------------------------------------------------------------
    if (strncmp(eventCopy, "PLAYER_JOINED_EVENT", 19) == 0)
    {
        for (int i = 0; i < MAX_PLAYERS_PER_ROOM && relayCount < maxRelays; i++)
        {
            if (room->players[i].active && room->players[i].socket != socket)
            {
                char msg[128];
                snprintf(msg, sizeof(msg), "%d:%s", sender->playerIndex, sender->name);

                outRelays[relayCount].socket = room->players[i].socket;
                outRelays[relayCount].message = FormatResponse(MSG_PLAYER_JOINED, msg);
                relayCount++;
            }
        }
        return relayCount;
    }

    //--------------------------------------------------------------------------
    // Handle player leave notification
    //--------------------------------------------------------------------------
    if (strncmp(eventCopy, "PLAYER_LEFT_EVENT:", 18) == 0)
    {
        int leftPlayerIndex = atoi(eventCopy + 18);

        for (int i = 0; i < MAX_PLAYERS_PER_ROOM && relayCount < maxRelays; i++)
        {
            if (room->players[i].active)
            {
                char msg[16];
                snprintf(msg, sizeof(msg), "%d", leftPlayerIndex);

                outRelays[relayCount].socket = room->players[i].socket;
                outRelays[relayCount].message = FormatResponse(MSG_PLAYER_LEFT, msg);
                relayCount++;
            }
        }
        return relayCount;
    }

    //--------------------------------------------------------------------------
    // Handle game starting notification
    //--------------------------------------------------------------------------
    if (strcmp(eventCopy, MSG_START) == 0)
    {
        // Get host's identity
        const char* hostIdentity = room->players[0].identity;

        // Notify all players except host
        for (int i = 1; i < MAX_PLAYERS_PER_ROOM && relayCount < maxRelays; i++)
        {
            if (room->players[i].active)
            {
                outRelays[relayCount].socket = room->players[i].socket;
                outRelays[relayCount].message = FormatResponse(MSG_GAME_STARTING, hostIdentity);
                relayCount++;
            }
        }
        return relayCount;
    }

    return relayCount;
}

void Signaling_HandleDisconnect(socket_t socket)
{
    if (!gInitialized)
        return;

    Player* player = Room_GetPlayer(socket);
    if (!player)
        return;

    Room* room = Room_FindBySocket(socket);
    if (!room)
        return;

    int playerIndex = player->playerIndex;
    bool wasHost = (playerIndex == 0);

    printf("[Signaling] Player %d (%s) disconnected from room %s (was host: %s)\n",
           playerIndex, player->identity, room->code, wasHost ? "yes" : "no");

    Room_Leave(socket);
}

char* Signaling_FormatMessage(const char* type, const char* data)
{
    return FormatResponse(type, data);
}

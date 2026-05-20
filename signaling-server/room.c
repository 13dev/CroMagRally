/****************************/
/*         ROOM.C           */
/* Room management for      */
/* signaling server         */
/****************************/

#include "room.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

//==============================================================================
// INTERNAL STATE
//==============================================================================

static Room gRooms[MAX_ROOMS];
static bool gInitialized = false;

//==============================================================================
// HELPER FUNCTIONS
//==============================================================================

static uint32_t GetCurrentTime(void)
{
    return (uint32_t)time(NULL);
}

static void GenerateRoomCode(char* outCode)
{
    static const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";  // No I, O, 0, 1 to avoid confusion
    static bool seeded = false;

    if (!seeded)
    {
        srand((unsigned int)time(NULL));
        seeded = true;
    }

    for (int i = 0; i < ROOM_CODE_LENGTH; i++)
    {
        outCode[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    outCode[ROOM_CODE_LENGTH] = '\0';
}

static bool IsCodeUnique(const char* code)
{
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (gRooms[i].active && strcmp(gRooms[i].code, code) == 0)
            return false;
    }
    return true;
}

static Room* FindFreeRoom(void)
{
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (!gRooms[i].active)
            return &gRooms[i];
    }
    return NULL;
}

//==============================================================================
// PUBLIC FUNCTIONS
//==============================================================================

void Room_Init(void)
{
    memset(gRooms, 0, sizeof(gRooms));

    for (int i = 0; i < MAX_ROOMS; i++)
    {
        for (int j = 0; j < MAX_PLAYERS_PER_ROOM; j++)
        {
            gRooms[i].players[j].socket = INVALID_SOCKET_VALUE;
        }
    }

    gInitialized = true;
    printf("[Room] Initialized room system (max %d rooms, %d players/room)\n",
           MAX_ROOMS, MAX_PLAYERS_PER_ROOM);
}

const char* Room_Create(socket_t hostSocket, const char* hostName, const char* hostIdentity)
{
    if (!gInitialized)
        return NULL;

    Room* room = FindFreeRoom();
    if (!room)
    {
        printf("[Room] No free rooms available\n");
        return NULL;
    }

    // Generate unique room code
    int attempts = 0;
    do {
        GenerateRoomCode(room->code);
        attempts++;
    } while (!IsCodeUnique(room->code) && attempts < 100);

    if (attempts >= 100)
    {
        printf("[Room] Failed to generate unique room code\n");
        return NULL;
    }

    // Initialize room
    room->active = true;
    room->gameStarted = false;
    room->playerCount = 1;
    room->createdTime = GetCurrentTime();

    // Clear all player slots
    for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++)
    {
        room->players[i].socket = INVALID_SOCKET_VALUE;
        room->players[i].active = false;
        room->players[i].playerIndex = i;
        room->players[i].name[0] = '\0';
        room->players[i].identity[0] = '\0';
    }

    // Add host as player 0
    room->players[0].socket = hostSocket;
    room->players[0].active = true;
    room->players[0].lastActivity = GetCurrentTime();
    if (hostName)
    {
        strncpy(room->players[0].name, hostName, sizeof(room->players[0].name) - 1);
        room->players[0].name[sizeof(room->players[0].name) - 1] = '\0';
    }
    if (hostIdentity)
    {
        strncpy(room->players[0].identity, hostIdentity, sizeof(room->players[0].identity) - 1);
        room->players[0].identity[sizeof(room->players[0].identity) - 1] = '\0';
    }

    printf("[Room] Created room %s by %s (identity: %s)\n", room->code,
           hostName ? hostName : "Unknown", hostIdentity ? hostIdentity : "none");
    return room->code;
}

int Room_Join(const char* code, socket_t clientSocket, const char* clientName, const char* clientIdentity)
{
    if (!gInitialized || !code)
        return -1;

    Room* room = Room_FindByCode(code);
    if (!room)
    {
        printf("[Room] Room %s not found\n", code);
        return -1;
    }

    if (room->gameStarted)
    {
        printf("[Room] Room %s game already started\n", code);
        return -1;
    }

    if (room->playerCount >= MAX_PLAYERS_PER_ROOM)
    {
        printf("[Room] Room %s is full\n", code);
        return -1;
    }

    // Find free player slot (starting from 1, 0 is host)
    for (int i = 1; i < MAX_PLAYERS_PER_ROOM; i++)
    {
        if (!room->players[i].active)
        {
            room->players[i].socket = clientSocket;
            room->players[i].active = true;
            room->players[i].lastActivity = GetCurrentTime();
            if (clientName)
            {
                strncpy(room->players[i].name, clientName, sizeof(room->players[i].name) - 1);
                room->players[i].name[sizeof(room->players[i].name) - 1] = '\0';
            }
            if (clientIdentity)
            {
                strncpy(room->players[i].identity, clientIdentity, sizeof(room->players[i].identity) - 1);
                room->players[i].identity[sizeof(room->players[i].identity) - 1] = '\0';
            }
            room->playerCount++;

            printf("[Room] Player %s joined room %s as player %d (identity: %s)\n",
                   clientName ? clientName : "Unknown", code, i,
                   clientIdentity ? clientIdentity : "none");
            return i;
        }
    }

    return -1;
}

Player* Room_FindPlayerByIdentity(const char* identity)
{
    if (!gInitialized || !identity)
        return NULL;

    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (!gRooms[i].active)
            continue;

        for (int j = 0; j < MAX_PLAYERS_PER_ROOM; j++)
        {
            if (gRooms[i].players[j].active &&
                strcmp(gRooms[i].players[j].identity, identity) == 0)
            {
                return &gRooms[i].players[j];
            }
        }
    }

    return NULL;
}

void Room_Leave(socket_t socket)
{
    if (!gInitialized)
        return;

    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (!gRooms[i].active)
            continue;

        Room* room = &gRooms[i];

        for (int j = 0; j < MAX_PLAYERS_PER_ROOM; j++)
        {
            if (room->players[j].active && room->players[j].socket == socket)
            {
                printf("[Room] Player %d left room %s\n", j, room->code);

                room->players[j].socket = INVALID_SOCKET_VALUE;
                room->players[j].active = false;
                room->players[j].name[0] = '\0';
                room->playerCount--;

                // If host left, destroy the room
                if (j == 0)
                {
                    printf("[Room] Host left, destroying room %s\n", room->code);
                    room->active = false;
                    room->playerCount = 0;

                    // Mark all players as inactive
                    for (int k = 0; k < MAX_PLAYERS_PER_ROOM; k++)
                    {
                        room->players[k].active = false;
                        room->players[k].socket = INVALID_SOCKET_VALUE;
                    }
                }
                else if (room->playerCount == 0)
                {
                    // No players left, destroy room
                    printf("[Room] No players left, destroying room %s\n", room->code);
                    room->active = false;
                }

                return;
            }
        }
    }
}

Room* Room_FindBySocket(socket_t socket)
{
    if (!gInitialized)
        return NULL;

    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (!gRooms[i].active)
            continue;

        for (int j = 0; j < MAX_PLAYERS_PER_ROOM; j++)
        {
            if (gRooms[i].players[j].active && gRooms[i].players[j].socket == socket)
                return &gRooms[i];
        }
    }

    return NULL;
}

Room* Room_FindByCode(const char* code)
{
    if (!gInitialized || !code)
        return NULL;

    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (gRooms[i].active && strcmp(gRooms[i].code, code) == 0)
            return &gRooms[i];
    }

    return NULL;
}

Player* Room_GetPlayer(socket_t socket)
{
    if (!gInitialized)
        return NULL;

    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (!gRooms[i].active)
            continue;

        for (int j = 0; j < MAX_PLAYERS_PER_ROOM; j++)
        {
            if (gRooms[i].players[j].active && gRooms[i].players[j].socket == socket)
                return &gRooms[i].players[j];
        }
    }

    return NULL;
}

int Room_GetOtherPlayers(socket_t socket, Player** outPlayers, int maxPlayers)
{
    if (!gInitialized || !outPlayers || maxPlayers <= 0)
        return 0;

    Room* room = Room_FindBySocket(socket);
    if (!room)
        return 0;

    int count = 0;
    for (int i = 0; i < MAX_PLAYERS_PER_ROOM && count < maxPlayers; i++)
    {
        if (room->players[i].active && room->players[i].socket != socket)
        {
            outPlayers[count++] = &room->players[i];
        }
    }

    return count;
}

bool Room_IsHost(socket_t socket)
{
    if (!gInitialized)
        return false;

    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (gRooms[i].active &&
            gRooms[i].players[0].active &&
            gRooms[i].players[0].socket == socket)
        {
            return true;
        }
    }

    return false;
}

void Room_MarkGameStarted(const char* code)
{
    Room* room = Room_FindByCode(code);
    if (room)
    {
        room->gameStarted = true;
        printf("[Room] Game started in room %s\n", code);
    }
}

void Room_CleanupExpired(void)
{
    if (!gInitialized)
        return;

    uint32_t now = GetCurrentTime();

    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (!gRooms[i].active)
            continue;

        // Check room timeout (only if game hasn't started)
        if (!gRooms[i].gameStarted &&
            (now - gRooms[i].createdTime) > ROOM_TIMEOUT_SECS)
        {
            printf("[Room] Room %s expired after %d seconds\n",
                   gRooms[i].code, ROOM_TIMEOUT_SECS);

            gRooms[i].active = false;
            gRooms[i].playerCount = 0;

            for (int j = 0; j < MAX_PLAYERS_PER_ROOM; j++)
            {
                gRooms[i].players[j].active = false;
                gRooms[i].players[j].socket = INVALID_SOCKET_VALUE;
            }
        }
    }
}

void Room_GetStats(int* outActiveRooms, int* outTotalPlayers)
{
    int rooms = 0;
    int players = 0;

    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (gRooms[i].active)
        {
            rooms++;
            players += gRooms[i].playerCount;
        }
    }

    if (outActiveRooms) *outActiveRooms = rooms;
    if (outTotalPlayers) *outTotalPlayers = players;
}

/****************************/
/*   	  NETWORK.C	   	    */
/* (c)2000 Pangea Software  */
/* By Brian Greenstone      */
/* GNS P2P port (c)2026     */
/****************************/


/***************/
/* EXTERNALS   */
/***************/

#include "game.h"
#include "network.h"
#include "window.h"
#include "Backend_Network.h"
#include <string.h>
#include <stdio.h>

/**********************/
/*     PROTOTYPES     */
/**********************/

static void OnClientConnected(int peerIndex);
static void OnClientDisconnected(int peerIndex);
static void OnMessageReceived(int peerIndex, const void* data, size_t size);
static void OnStateChanged(NetConnectionState newState, const char* message);
static void OnPlayerName(int playerIndex, const char* name);
static void HandleNetworkMessage(int fromPeer, const void* data, size_t size);
static void PlayerUnexpectedlyLeavesGame(int playerIndex);

// Snapshot buffer functions (forward declarations)
static void StoreSnapshot(int playerIndex, NetHostControlInfoMessageType* mess);
static void ClearAllSnapshotBuffers(void);

/****************************/
/*    CONSTANTS             */
/****************************/

#define	DATA_TIMEOUT	2						// # seconds for data to timeout

// Async networking timing
static uint32_t gLastNetSendTime = 0;
static bool     gHasReceivedInitialHostData = false;    // Track if client has ever received host data

// Cached host position data for applying after physics (legacy single-frame)
static bool     gHasHostPositionData = false;
static NetHostControlInfoMessageType gCachedHostPositions;

//==============================================================================
// SNAPSHOT INTERPOLATION STATE
//==============================================================================

// Per-player snapshot for interpolation
typedef struct
{
    float posX, posY, posZ;
    float rotY;
    float velX, velY, velZ;
    float steering;
    uint32_t hostTimeMs;        // Host timestamp for this snapshot
    uint32_t localReceiveTimeMs; // Local time when received (for age calculation)
    bool valid;
} PlayerSnapshot;

// Ring buffer of snapshots per player
typedef struct
{
    PlayerSnapshot snapshots[SNAPSHOT_BUFFER_SIZE];
    int writeIndex;
    int count;
} SnapshotBuffer;

static SnapshotBuffer gSnapshotBuffers[MAX_PLAYERS];

// Clock synchronization state
static uint32_t gEstimatedRTT = 50;           // Start with 50ms estimate
static int32_t  gClockOffset = 0;             // Host clock - local clock
static uint32_t gLastClientTimeSent = 0;      // Last clientTimeMs we sent (for RTT calc)
static bool     gClockSynced = false;         // Have we completed initial clock sync?

// Per-player last client time (host tracks for echo)
static uint32_t gLastClientTime[MAX_PLAYERS];

// Network message types (internal, prepended to game messages)
enum
{
    kNetMsgType_Config = 100,
    kNetMsgType_Sync,
    kNetMsgType_HostControl,
    kNetMsgType_ClientControl,
    kNetMsgType_VehicleType,
    kNetMsgType_NullPacket,
};

// Network message header
#pragma pack(push, 1)
typedef struct
{
    uint8_t     msgType;
} NetMsgHeader;
#pragma pack(pop)

/**********************/
/*     VARIABLES      */
/**********************/

static int	gNumGatheredPlayers = 0;			// this is only used during gathering, gNumRealPlayers should be used during game!

Boolean		gNetSprocketInitialized = false;

Boolean		gIsNetworkHost = false;
Boolean		gIsNetworkClient = false;
Boolean		gNetGameInProgress = false;

void*       gNetGame = NULL;                    // Not used with GNS, kept for compatibility

Str32		gPlayerNameStrings[MAX_PLAYERS];

uint32_t	gClientSendCounter[MAX_PLAYERS];
uint32_t	gHostSendCounter;
int			gTimeoutCounter;

Boolean		gHostNetworkGame = false;
Boolean		gJoinNetworkGame = false;

// Message buffers
static NetHostControlInfoMessageType    gHostOutMess;
static NetClientControlInfoMessageType  gClientOutMess;

// Pending received messages
static bool                             gPendingConfigMessage = false;
static NetConfigMessageType             gPendingConfig;
static bool                             gPendingSyncMessage = false;
static int                              gSyncCount = 0;
static bool                             gPendingHostControlMessage = false;
static NetHostControlInfoMessageType    gPendingHostControl;
static bool                             gPendingClientControlMessage[MAX_PLAYERS];
static NetClientControlInfoMessageType  gPendingClientControl[MAX_PLAYERS];
static bool                             gPendingVehicleTypeMessage[MAX_PLAYERS];
static NetPlayerCharTypeMessage         gPendingVehicleType[MAX_PLAYERS];

// Player name for network
static char                             gLocalPlayerName[NET_PLAYER_NAME_LENGTH] = "Player";

// Status message for UI
static char                             gNetworkStatusMessage[128] = "";


/******************* INIT NETWORK MANAGER *********************/
//
// Called once at boot
//

void InitNetworkManager(void)
{
    printf("InitNetworkManager: Starting...\n");
    fflush(stdout);

    if (Net_Initialize())
    {
        gNetSprocketInitialized = true;

        // Set up callbacks
        Net_SetConnectCallback(OnClientConnected);
        Net_SetDisconnectCallback(OnClientDisconnected);
        Net_SetReceiveCallback(OnMessageReceived);
        Net_SetStateChangeCallback(OnStateChanged);
        Net_SetPlayerNameCallback(OnPlayerName);

        printf("InitNetworkManager: Network initialized successfully (GNS P2P)\n");
        fflush(stdout);
    }
    else
    {
        printf("InitNetworkManager: Failed to initialize network\n");
        fflush(stdout);
        gNetSprocketInitialized = false;
    }
}


/********************** END NETWORK GAME ******************************/
//
// Called from CleanupLevel() or when a player bails from game unexpectedly.
//

void EndNetworkGame(void)
{
    if (!gNetGameInProgress)
        return;

    // Clean up the network session
    Net_CleanupSession();

    gNetGameInProgress  = false;
    gIsNetworkHost      = false;
    gIsNetworkClient    = false;
    gNetGame            = NULL;
    gNumGatheredPlayers = 0;
    gNetworkStatusMessage[0] = '\0';

    // Reset async networking state
    gLastNetSendTime = 0;
    gHasReceivedInitialHostData = false;
    gHasHostPositionData = false;

    // Clear snapshot buffers and clock sync state
    ClearAllSnapshotBuffers();
    memset(gLastClientTime, 0, sizeof(gLastClientTime));

    printf("EndNetworkGame: Network game ended\n");
}


#pragma mark -


/****************** SETUP NETWORK HOSTING *********************/
//
// Called when this computer's user has selected to be a host for a net game.
//
// OUTPUT:  true == cancelled/error.
//

Boolean SetupNetworkHosting(void)
{
    if (!gNetSprocketInitialized)
    {
        printf("SetupNetworkHosting: Network not initialized\n");
        return true;
    }

    gHostSendCounter = 0;
    gTimeoutCounter = 0;
    memset(gClientSendCounter, 0, sizeof(gClientSendCounter));

    // Reset async networking state
    gLastNetSendTime = 0;
    gHasReceivedInitialHostData = false;
    gHasHostPositionData = false;

    // Clear snapshot buffers and clock sync state
    ClearAllSnapshotBuffers();
    memset(gLastClientTime, 0, sizeof(gLastClientTime));

    // Clear pending messages
    memset(gPendingClientControlMessage, 0, sizeof(gPendingClientControlMessage));
    memset(gPendingVehicleTypeMessage, 0, sizeof(gPendingVehicleTypeMessage));

    // Create host and register with signaling server
    if (!Net_CreateHost(gLocalPlayerName))
    {
        printf("SetupNetworkHosting: Failed to create host - %s\n", Net_GetLastError());
        snprintf(gNetworkStatusMessage, sizeof(gNetworkStatusMessage), "Failed: %s", Net_GetLastError());
        return true;
    }

    gIsNetworkHost = true;
    gIsNetworkClient = false;
    gNumGatheredPlayers = 1;  // Host counts as player 1
    gMyNetworkPlayerNum = 0;  // Host is always player 0

    // Store host's name
    snprintf((char*)gPlayerNameStrings[0], sizeof(gPlayerNameStrings[0]), "%s", gLocalPlayerName);

    snprintf(gNetworkStatusMessage, sizeof(gNetworkStatusMessage), "Connecting to server...");
    printf("SetupNetworkHosting: Registering with signaling server\n");
    return false;  // Success
}


/*************** SETUP NETWORK JOIN WITH ROOM CODE ************************/
//
// Join a game using a 4-character room code
// OUTPUT:	false == success
//			true = error
//

Boolean SetupNetworkJoinWithRoomCode(const char* roomCode)
{
    if (!gNetSprocketInitialized)
    {
        printf("SetupNetworkJoinWithRoomCode: Network not initialized\n");
        return true;
    }

    if (!roomCode || strlen(roomCode) != NET_ROOM_CODE_LENGTH)
    {
        printf("SetupNetworkJoinWithRoomCode: Invalid room code '%s'\n", roomCode ? roomCode : "NULL");
        return true;
    }

    memset(gClientSendCounter, 0, sizeof(gClientSendCounter));
    gTimeoutCounter = 0;

    // Reset async networking state
    gLastNetSendTime = 0;
    gHasReceivedInitialHostData = false;
    gHasHostPositionData = false;

    // Clear snapshot buffers and clock sync state
    ClearAllSnapshotBuffers();
    memset(gLastClientTime, 0, sizeof(gLastClientTime));

    // Clear pending messages from any previous game
    gPendingConfigMessage = false;
    gPendingHostControlMessage = false;
    memset(gPendingVehicleTypeMessage, 0, sizeof(gPendingVehicleTypeMessage));
    memset(gPendingClientControlMessage, 0, sizeof(gPendingClientControlMessage));

    // Join the game
    if (!Net_JoinGame(roomCode, gLocalPlayerName))
    {
        printf("SetupNetworkJoinWithRoomCode: Failed to join - %s\n", Net_GetLastError());
        snprintf(gNetworkStatusMessage, sizeof(gNetworkStatusMessage), "Failed: %s", Net_GetLastError());
        return true;
    }

    gIsNetworkHost = false;
    gIsNetworkClient = true;

    snprintf(gNetworkStatusMessage, sizeof(gNetworkStatusMessage), "Joining room %s...", roomCode);
    printf("SetupNetworkJoinWithRoomCode: Joining room %s\n", roomCode);

    return false;  // Success (connection initiated)
}


#pragma mark - Room State


/****************** GET NETWORK ROOM CODE *********************/

const char* GetNetworkRoomCode(void)
{
    return Net_GetRoomCode();
}


/****************** GET NETWORK STATE *********************/

NetConnectionState GetNetworkState(void)
{
    return Net_GetState();
}


/****************** GET NETWORK STATUS MESSAGE *********************/

const char* GetNetworkStatusMessage(void)
{
    return gNetworkStatusMessage;
}


#pragma mark - Network Callbacks


/****************** ON STATE CHANGED *********************/

static void OnStateChanged(NetConnectionState newState, const char* message)
{
    switch (newState)
    {
        case NET_STATE_DISCONNECTED:
            snprintf(gNetworkStatusMessage, sizeof(gNetworkStatusMessage), "Disconnected");
            break;

        case NET_STATE_CONNECTING_SIGNALING:
            snprintf(gNetworkStatusMessage, sizeof(gNetworkStatusMessage), "Connecting to server...");
            break;

        case NET_STATE_WAITING_ROOM:
            if (gIsNetworkHost)
                snprintf(gNetworkStatusMessage, sizeof(gNetworkStatusMessage), "Creating room...");
            else
                snprintf(gNetworkStatusMessage, sizeof(gNetworkStatusMessage), "Joining room...");
            break;

        case NET_STATE_IN_LOBBY:
            if (gIsNetworkHost && Net_GetRoomCode())
                snprintf(gNetworkStatusMessage, sizeof(gNetworkStatusMessage), "Room Code: %s", Net_GetRoomCode());
            else
                snprintf(gNetworkStatusMessage, sizeof(gNetworkStatusMessage), "In lobby");
            break;

        case NET_STATE_CONNECTING_P2P:
            snprintf(gNetworkStatusMessage, sizeof(gNetworkStatusMessage), "Establishing connection...");
            break;

        case NET_STATE_CONNECTED:
            snprintf(gNetworkStatusMessage, sizeof(gNetworkStatusMessage), "Connected!");
            break;

        case NET_STATE_ERROR:
            snprintf(gNetworkStatusMessage, sizeof(gNetworkStatusMessage), "Error: %s", message ? message : "Unknown");
            break;
    }

    printf("OnStateChanged: %s\n", gNetworkStatusMessage);
}


/****************** ON PLAYER NAME *********************/

static void OnPlayerName(int playerIndex, const char* name)
{
    if (playerIndex >= 0 && playerIndex < MAX_PLAYERS && name)
    {
        snprintf((char*)gPlayerNameStrings[playerIndex], sizeof(gPlayerNameStrings[0]), "%s", name);
        printf("OnPlayerName: Player %d name set to: %s\n", playerIndex, name);
    }
}


/****************** ON CLIENT CONNECTED *********************/

static void OnClientConnected(int peerIndex)
{
    if (gIsNetworkHost)
    {
        // A new client joined the lobby
        // Note: Player name is already set by OnPlayerName callback (called before this)
        int playerNum = peerIndex;  // peerIndex is already the player number from signaling
        if (playerNum < MAX_PLAYERS)
        {
            gNumGatheredPlayers = Net_GetPlayerCount();
            // Only set default name if OnPlayerName hasn't set it already
            if (gPlayerNameStrings[playerNum][0] == '\0')
            {
                snprintf((char*)gPlayerNameStrings[playerNum], sizeof(gPlayerNameStrings[0]), "Player %d", playerNum + 1);
            }
            printf("OnClientConnected: Player %d (%s) joined (total: %d)\n",
                   playerNum, gPlayerNameStrings[playerNum], gNumGatheredPlayers);
        }
    }
    else
    {
        // We connected to a host
        printf("OnClientConnected: Connected to host\n");
    }
}


/****************** ON CLIENT DISCONNECTED *********************/

static void OnClientDisconnected(int peerIndex)
{
    if (gIsNetworkHost)
    {
        int playerNum = peerIndex;
        printf("OnClientDisconnected: Player %d left\n", playerNum);

        // Clear player name from display list
        if (playerNum >= 0 && playerNum < MAX_PLAYERS)
        {
            gPlayerNameStrings[playerNum][0] = '\0';
        }

        PlayerUnexpectedlyLeavesGame(playerNum);
        gNumGatheredPlayers = Net_GetPlayerCount();
    }
    else
    {
        // Lost connection to host
        printf("OnClientDisconnected: Lost connection to host\n");
        gGameOver = true;
    }
}


/****************** ON MESSAGE RECEIVED *********************/

static void OnMessageReceived(int peerIndex, const void* data, size_t size)
{
    HandleNetworkMessage(peerIndex, data, size);
}


/****************** HANDLE NETWORK MESSAGE *********************/

static void HandleNetworkMessage(int fromPeer, const void* data, size_t size)
{
    if (size < sizeof(NetMsgHeader))
        return;

    const NetMsgHeader* header = (const NetMsgHeader*)data;
    const void* payload = (const uint8_t*)data + sizeof(NetMsgHeader);
    size_t payloadSize = size - sizeof(NetMsgHeader);

    switch (header->msgType)
    {
        case kNetMsgType_Config:
            if (payloadSize >= sizeof(NetConfigMessageType))
            {
                memcpy(&gPendingConfig, payload, sizeof(NetConfigMessageType));
                gPendingConfigMessage = true;
            }
            break;

        case kNetMsgType_Sync:
            if (payloadSize >= sizeof(NetSyncMessageType))
            {
                gSyncCount++;
                gPendingSyncMessage = true;
            }
            break;

        case kNetMsgType_HostControl:
            if (payloadSize >= sizeof(NetHostControlInfoMessageType))
            {
                memcpy(&gPendingHostControl, payload, sizeof(NetHostControlInfoMessageType));
                gPendingHostControlMessage = true;
            }
            break;

        case kNetMsgType_ClientControl:
            if (payloadSize >= sizeof(NetClientControlInfoMessageType))
            {
                NetClientControlInfoMessageType msg;
                memcpy(&msg, payload, sizeof(msg));
                int playerNum = msg.playerNum;
                if (playerNum >= 0 && playerNum < MAX_PLAYERS)
                {
                    memcpy(&gPendingClientControl[playerNum], &msg, sizeof(msg));
                    gPendingClientControlMessage[playerNum] = true;
                }
            }
            break;

        case kNetMsgType_VehicleType:
            if (payloadSize >= sizeof(NetPlayerCharTypeMessage))
            {
                NetPlayerCharTypeMessage msg;
                memcpy(&msg, payload, sizeof(msg));
                int playerNum = msg.playerNum;
                if (playerNum >= 0 && playerNum < MAX_PLAYERS)
                {
                    memcpy(&gPendingVehicleType[playerNum], &msg, sizeof(msg));
                    gPendingVehicleTypeMessage[playerNum] = true;
                }
            }
            break;

        case kNetMsgType_NullPacket:
            // Just a keepalive, ignore
            break;

        default:
            printf("HandleNetworkMessage: Unknown message type %d\n", header->msgType);
            break;
    }
}


#pragma mark - Host Gather


/********************* HOST WAIT FOR PLAYERS *********************/
//
// Returns number of gathered players
//

int HostGetGatheredPlayerCount(void)
{
    return Net_GetPlayerCount();
}


/********************* HOST GET P2P CONNECTED COUNT *********************/
//
// Returns number of P2P connected clients (for host to show connection status)
//

int HostGetP2PConnectedCount(void)
{
    return Net_GetP2PConnectionCount();
}


/********************* HOST UPDATE GATHERING *********************/
//
// Call each frame while gathering players
//

void HostUpdateGathering(void)
{
    Net_ProcessEvents(0);
    gNumGatheredPlayers = Net_GetPlayerCount();
}


/********************* HOST SEND GAME CONFIG *********************/
//
// Send game configuration to all clients when starting
// P2P connections should already be established (clients connect when joining lobby)
//

void HostSendGameConfig(void)
{
    if (!gIsNetworkHost)
        return;

    int expectedClients = gNumGatheredPlayers - 1;  // Excluding host
    int connectedClients = Net_GetP2PConnectionCount();

    printf("HostSendGameConfig: Starting game with %d/%d P2P connections\n",
           connectedClients, expectedClients);
    fflush(stdout);

    // Notify signaling server that game is starting
    Net_StartGame();

    // Wait for P2P connections - ICE negotiation can take time even on localhost
    // Increased timeout to 15 seconds to allow ICE candidate gathering to complete
    if (connectedClients < expectedClients)
    {
        uint32_t startTick = SDL_GetTicks();
        int lastReportedCount = connectedClients;
        printf("HostSendGameConfig: Waiting for P2P connections (%d/%d)...\n",
               connectedClients, expectedClients);
        fflush(stdout);

        while (connectedClients < expectedClients && (SDL_GetTicks() - startTick) < 15000)
        {
            Net_ProcessEvents(50);
            SDL_PumpEvents();  // Keep window responsive while waiting
            int newCount = Net_GetP2PConnectionCount();
            if (newCount != lastReportedCount)
            {
                printf("HostSendGameConfig: P2P connections: %d/%d\n", newCount, expectedClients);
                fflush(stdout);
                lastReportedCount = newCount;
            }
            connectedClients = newCount;
        }

        if (connectedClients < expectedClients)
        {
            printf("HostSendGameConfig: Warning: Only %d/%d clients connected after 15s\n",
                   connectedClients, expectedClients);
            fflush(stdout);
        }
    }

    printf("HostSendGameConfig: Proceeding with %d connected clients\n", connectedClients);
    fflush(stdout);

    // Small delay to ensure connections are stable
    SDL_Delay(100);
    Net_ProcessEvents(0);

    // Send config to each client
    for (int i = 1; i < gNumGatheredPlayers; i++)
    {
        uint8_t buffer[sizeof(NetMsgHeader) + sizeof(NetConfigMessageType)];
        NetMsgHeader* header = (NetMsgHeader*)buffer;
        NetConfigMessageType* config = (NetConfigMessageType*)(buffer + sizeof(NetMsgHeader));

        header->msgType = kNetMsgType_Config;
        config->gameMode = gGameMode;
        config->age = gTheAge;
        config->trackNum = gTrackNum;
        config->numPlayers = gNumGatheredPlayers;
        config->playerNum = i;  // This client's player number
        config->numAgesCompleted = gGamePrefs.tournamentProgression.numTracksCompleted;
        config->difficulty = gGamePrefs.difficulty;
        config->tagDuration = gGamePrefs.tagDuration;

        Net_SendToPeer(i, buffer, sizeof(buffer), true);
        printf("HostSendGameConfig: Sent config to player %d\n", i);
    }

    gNumRealPlayers = gNumGatheredPlayers;
    gNetGameInProgress = true;

    printf("HostSendGameConfig: Sent config to %d clients\n", gNumGatheredPlayers - 1);
}


#pragma mark - Client Wait


/********************* CLIENT WAIT FOR CONFIG *********************/
//
// Waits for host to send game configuration
// Returns true when config received, false if still waiting
//

Boolean ClientWaitForConfig(void)
{
    Net_ProcessEvents(0);

    if (gPendingConfigMessage)
    {
        // Apply configuration
        gGameMode               = gPendingConfig.gameMode;
        gTheAge                 = gPendingConfig.age;
        gTrackNum               = gPendingConfig.trackNum;
        gNumRealPlayers         = gPendingConfig.numPlayers;
        gMyNetworkPlayerNum     = gPendingConfig.playerNum;
        gGamePrefs.difficulty   = gPendingConfig.difficulty;
        gGamePrefs.tagDuration  = gPendingConfig.tagDuration;

        // Sync tournament progression from host
        if (gPendingConfig.numAgesCompleted > GetNumTracksCompletedTotal())
            SetPlayerProgression(gPendingConfig.numAgesCompleted);

        gPendingConfigMessage = false;
        gNetGameInProgress = true;

        printf("ClientWaitForConfig: Received config - player %d of %d\n",
               gMyNetworkPlayerNum, gNumRealPlayers);
        return true;
    }

    return false;
}


#pragma mark - Level Sync


/********************* HOST WAIT FOR PLAYERS TO PREPARE LEVEL *******************************/
//
// Called right before PlayArea().  This waits for the sync message from the other client players
// indicating that they are ready to start playing.
//

void HostWaitForPlayersToPrepareLevel(void)
{
    if (!gIsNetworkHost)
        return;

    printf("HostWaitForPlayersToPrepareLevel: Waiting for %d clients (gNumRealPlayers=%d)...\n",
           gNumRealPlayers - 1, gNumRealPlayers);
    fflush(stdout);

    // Reset sync state at the start
    gSyncCount = 0;
    gPendingSyncMessage = false;

    int syncedPlayers = 1;  // Host is already ready
    uint32_t startTick = SDL_GetTicks();
    uint32_t lastPrint = 0;

    while (syncedPlayers < gNumRealPlayers)
    {
        Net_ProcessEvents(10);

        if (gPendingSyncMessage)
        {
            printf("HostWaitForPlayersToPrepareLevel: Got sync message! gSyncCount=%d\n", gSyncCount);
            fflush(stdout);
            syncedPlayers = gSyncCount + 1;
            gPendingSyncMessage = false;
        }

        // Print status every 2 seconds
        uint32_t now = SDL_GetTicks();
        if (now - lastPrint > 2000)
        {
            printf("HostWaitForPlayersToPrepareLevel: Still waiting... syncedPlayers=%d/%d\n",
                   syncedPlayers, gNumRealPlayers);
            fflush(stdout);
            lastPrint = now;
        }

        // Timeout after 2 minutes
        if ((SDL_GetTicks() - startTick) > (60 * 1000 * 2))
        {
            DoFatalAlert("No response from other player(s), something has gone wrong.");
        }
    }

    // Tell all clients we're ready
    uint8_t buffer[sizeof(NetMsgHeader) + sizeof(NetSyncMessageType)];
    NetMsgHeader* header = (NetMsgHeader*)buffer;
    NetSyncMessageType* sync = (NetSyncMessageType*)(buffer + sizeof(NetMsgHeader));

    header->msgType = kNetMsgType_Sync;
    sync->playerNum = 0;

    Net_SendToAll(buffer, sizeof(buffer), true);

    gSyncCount = 0;
    printf("HostWaitForPlayersToPrepareLevel: All players ready!\n");
}


/********************* CLIENT TELL HOST LEVEL IS PREPARED *******************************/
//
// Called right before PlayArea().  Client tells host it's ready and waits for host's signal.
//

void ClientTellHostLevelIsPrepared(void)
{
    if (!gIsNetworkClient)
        return;

    printf("ClientTellHostLevelIsPrepared: Preparing to send sync (playerNum=%d)...\n", gMyNetworkPlayerNum);
    fflush(stdout);

    // Tell host we're ready
    uint8_t buffer[sizeof(NetMsgHeader) + sizeof(NetSyncMessageType)];
    NetMsgHeader* header = (NetMsgHeader*)buffer;
    NetSyncMessageType* sync = (NetSyncMessageType*)(buffer + sizeof(NetMsgHeader));

    header->msgType = kNetMsgType_Sync;
    sync->playerNum = gMyNetworkPlayerNum;

    Net_SendToHost(buffer, sizeof(buffer), true);

    printf("ClientTellHostLevelIsPrepared: Sent sync message to host, waiting for GO signal...\n");
    fflush(stdout);

    // Wait for host's ready signal
    gPendingSyncMessage = false;
    uint32_t startTick = SDL_GetTicks();
    uint32_t lastPrint = 0;

    while (!gPendingSyncMessage)
    {
        Net_ProcessEvents(10);

        // Print status every 2 seconds
        uint32_t now = SDL_GetTicks();
        if (now - lastPrint > 2000)
        {
            printf("ClientTellHostLevelIsPrepared: Still waiting for GO signal...\n");
            fflush(stdout);
            lastPrint = now;
        }

        // Timeout after 2 minutes
        if ((SDL_GetTicks() - startTick) > (60 * 1000 * 2))
        {
            DoFatalAlert("ClientTellHostLevelIsPrepared: Timeout waiting for host GO signal.");
        }
    }
    gPendingSyncMessage = false;

    printf("ClientTellHostLevelIsPrepared: Host says GO!\n");
    fflush(stdout);
}


#pragma mark - Frame Sync


/************** SEND HOST CONTROL INFO TO CLIENTS *********************/
//
// The host sends this at the beginning of each frame to all of the network clients.
//

void HostSend_ControlInfoToClients(void)
{
    if (!gIsNetworkHost)
        return;

    uint8_t buffer[sizeof(NetMsgHeader) + sizeof(NetHostControlInfoMessageType)];
    NetMsgHeader* header = (NetMsgHeader*)buffer;
    NetHostControlInfoMessageType* msg = (NetHostControlInfoMessageType*)(buffer + sizeof(NetMsgHeader));

    header->msgType = kNetMsgType_HostControl;

    // Timestamp for clock sync
    msg->hostTimeMs = SDL_GetTicks();
    // Echo back the most recent client timestamp (for RTT calculation)
    // For simplicity, echo back player 1's timestamp (first client)
    // In a more sophisticated system, each client would have their own echo
    msg->echoedClientTime = gLastClientTime[1];  // First client is player 1

    msg->frameCounter = gHostSendCounter++;
    msg->fps = gFramesPerSecond;
    msg->fpsFrac = gFramesPerSecondFrac;
    msg->randomSeed = MyRandomLong();

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        msg->controlBits[i] = gPlayerInfo[i].controlBits;
        msg->controlBitsNew[i] = gPlayerInfo[i].controlBits_New;
        msg->analogSteeringX[i] = gPlayerInfo[i].analogSteering.x;
        msg->analogSteeringY[i] = gPlayerInfo[i].analogSteering.y;

        // Pack car position state (host-authoritative)
        msg->posX[i] = gPlayerInfo[i].coord.x;
        msg->posY[i] = gPlayerInfo[i].coord.y;
        msg->posZ[i] = gPlayerInfo[i].coord.z;
        msg->steering[i] = gPlayerInfo[i].steering;

        // Get rotation and velocity from objNode if available
        ObjNode* car = gPlayerInfo[i].objNode;
        if (car)
        {
            msg->rotY[i] = car->Rot.y;
            msg->velX[i] = car->Delta.x;
            msg->velY[i] = car->Delta.y;
            msg->velZ[i] = car->Delta.z;
        }
        else
        {
            msg->rotY[i] = 0;
            msg->velX[i] = 0;
            msg->velY[i] = 0;
            msg->velZ[i] = 0;
        }

        // Race state sync
        msg->lapNum[i] = (int8_t)gPlayerInfo[i].lapNum;
        int lap = gPlayerInfo[i].lapNum;
        if (lap < 0) lap = 0;
        if (lap >= LAPS_PER_RACE) lap = LAPS_PER_RACE - 1;
        msg->currentLapTime[i] = gPlayerInfo[i].lapTimes[lap];
    }

    // Store for potential resend
    memcpy(&gHostOutMess, msg, sizeof(gHostOutMess));

    Net_SendToAll(buffer, sizeof(buffer), true);
}


/************** GET NETWORK CONTROL INFO FROM HOST *********************/
//
// The client reads this from the host at the beginning of each frame.
// NON-BLOCKING: If no data available, continues with last known state.
//

void ClientReceive_ControlInfoFromHost(void)
{
    if (!gIsNetworkClient)
        return;

    // Poll for network events (non-blocking)
    Net_ProcessEvents(0);

    // If no new data, continue with last known state
    if (!gPendingHostControlMessage)
    {
        // First frame: we need initial data, so wait briefly
        if (!gHasReceivedInitialHostData)
        {
            uint32_t startTick = SDL_GetTicks();
            while (!gPendingHostControlMessage && (SDL_GetTicks() - startTick) < 2000)
            {
                Net_ProcessEvents(10);
            }
            if (!gPendingHostControlMessage)
            {
                printf("ClientReceive_ControlInfoFromHost: Timeout waiting for initial host data\n");
                gGameOver = true;
                return;
            }
        }
        else
        {
            // No new data, use stale state (game continues smoothly)
            return;
        }
    }

    // Process received message
    NetHostControlInfoMessageType* mess = &gPendingHostControl;

    // Skip old packets
    if (mess->frameCounter < gHostSendCounter)
    {
        gPendingHostControlMessage = false;
        return;
    }

    // Allow frame skips - just update to latest received frame
    // (Previously this was a fatal error)
    if (mess->frameCounter > gHostSendCounter)
    {
        // Log the skip but continue
        // printf("ClientReceive: Skipped %u frames\n", mess->frameCounter - gHostSendCounter);
    }
    gHostSendCounter = mess->frameCounter + 1;

    //==============================================================================
    // RTT CALCULATION AND CLOCK SYNC
    //==============================================================================
    // Accept any echoed timestamp from the last 2 seconds (handles out-of-order packets)
    uint32_t now = SDL_GetTicks();
    if (mess->echoedClientTime != 0 &&
        mess->echoedClientTime <= now &&
        (now - mess->echoedClientTime) < 2000)
    {
        uint32_t rtt = now - mess->echoedClientTime;

        // Sanity check RTT (ignore absurdly high values)
        if (rtt < 1000)
        {
            // Exponential moving average for RTT (7/8 old + 1/8 new)
            gEstimatedRTT = (gEstimatedRTT * 7 + rtt) / 8;

            // Calculate clock offset: host_time = local_time + offset
            // At the time the client sent, host time was approximately (now - RTT/2) on our clock
            // So: hostTimeMs ≈ (now - RTT/2) + offset
            // Therefore: offset ≈ hostTimeMs - (now - RTT/2)
            int32_t newOffset = (int32_t)mess->hostTimeMs - (int32_t)(now - gEstimatedRTT / 2);

            // Smooth clock offset changes to avoid jumps
            if (gClockSynced)
            {
                // Gradual adjustment (7/8 old + 1/8 new)
                gClockOffset = (gClockOffset * 7 + newOffset) / 8;
            }
            else
            {
                // First sync - use directly
                gClockOffset = newOffset;
                gClockSynced = true;
                printf("ClientReceive: Clock synced! RTT=%u, offset=%d\n", gEstimatedRTT, gClockOffset);
            }
        }
    }

    gFramesPerSecond = mess->fps;
    gFramesPerSecondFrac = mess->fpsFrac;

    // Random seed check - make it a warning, not fatal
    uint32_t localSeed = MyRandomLong();
    if (localSeed != mess->randomSeed)
    {
        // Resync random seed from host instead of crashing
        SetMyRandomSeed(mess->randomSeed);
        // printf("ClientReceive: Resynced random seed\n");
    }

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        gPlayerInfo[i].controlBits = mess->controlBits[i];
        gPlayerInfo[i].controlBits_New = mess->controlBitsNew[i];
        gPlayerInfo[i].analogSteering.x = mess->analogSteeringX[i];
        gPlayerInfo[i].analogSteering.y = mess->analogSteeringY[i];

        // Sync race state for ALL players from host (host is authoritative)
        gPlayerInfo[i].lapNum = mess->lapNum[i];
        int lap = mess->lapNum[i];
        if (lap < 0) lap = 0;
        if (lap >= LAPS_PER_RACE) lap = LAPS_PER_RACE - 1;
        gPlayerInfo[i].lapTimes[lap] = mess->currentLapTime[i];
    }

    //==============================================================================
    // STORE SNAPSHOTS FOR INTERPOLATION
    //==============================================================================
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        StoreSnapshot(i, mess);
    }

    // Also keep legacy cache for fallback
    memcpy(&gCachedHostPositions, mess, sizeof(gCachedHostPositions));
    gHasHostPositionData = true;

    gTimeoutCounter = 0;
    gPendingHostControlMessage = false;
    gHasReceivedInitialHostData = true;
}


//==============================================================================
// SNAPSHOT BUFFER HELPERS
//==============================================================================

// Get estimated host time based on local clock and measured offset
static uint32_t GetEstimatedHostTime(void)
{
    return SDL_GetTicks() + gClockOffset;
}

// Store a snapshot in the ring buffer for a player
static void StoreSnapshot(int playerIndex, NetHostControlInfoMessageType* mess)
{
    if (playerIndex < 0 || playerIndex >= MAX_PLAYERS)
        return;

    SnapshotBuffer* buf = &gSnapshotBuffers[playerIndex];
    PlayerSnapshot* snap = &buf->snapshots[buf->writeIndex];

    snap->posX = mess->posX[playerIndex];
    snap->posY = mess->posY[playerIndex];
    snap->posZ = mess->posZ[playerIndex];
    snap->rotY = mess->rotY[playerIndex];
    snap->velX = mess->velX[playerIndex];
    snap->velY = mess->velY[playerIndex];
    snap->velZ = mess->velZ[playerIndex];
    snap->steering = mess->steering[playerIndex];
    snap->hostTimeMs = mess->hostTimeMs;
    snap->localReceiveTimeMs = SDL_GetTicks();
    snap->valid = true;

    buf->writeIndex = (buf->writeIndex + 1) % SNAPSHOT_BUFFER_SIZE;
    if (buf->count < SNAPSHOT_BUFFER_SIZE)
        buf->count++;
}

// Find snapshot before the given host time (for interpolation)
static PlayerSnapshot* FindSnapshotBefore(int playerIndex, uint32_t hostTime)
{
    if (playerIndex < 0 || playerIndex >= MAX_PLAYERS)
        return NULL;

    SnapshotBuffer* buf = &gSnapshotBuffers[playerIndex];
    PlayerSnapshot* best = NULL;

    for (int i = 0; i < buf->count; i++)
    {
        PlayerSnapshot* snap = &buf->snapshots[i];
        if (snap->valid && snap->hostTimeMs <= hostTime)
        {
            if (!best || snap->hostTimeMs > best->hostTimeMs)
                best = snap;
        }
    }
    return best;
}

// Find snapshot after the given host time (for interpolation)
static PlayerSnapshot* FindSnapshotAfter(int playerIndex, uint32_t hostTime)
{
    if (playerIndex < 0 || playerIndex >= MAX_PLAYERS)
        return NULL;

    SnapshotBuffer* buf = &gSnapshotBuffers[playerIndex];
    PlayerSnapshot* best = NULL;

    for (int i = 0; i < buf->count; i++)
    {
        PlayerSnapshot* snap = &buf->snapshots[i];
        if (snap->valid && snap->hostTimeMs > hostTime)
        {
            if (!best || snap->hostTimeMs < best->hostTimeMs)
                best = snap;
        }
    }
    return best;
}

// Clear all snapshot buffers (call on game start/end)
static void ClearAllSnapshotBuffers(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        gSnapshotBuffers[i].writeIndex = 0;
        gSnapshotBuffers[i].count = 0;
        for (int j = 0; j < SNAPSHOT_BUFFER_SIZE; j++)
            gSnapshotBuffers[i].snapshots[j].valid = false;
    }
    gClockSynced = false;
    gEstimatedRTT = 50;
    gClockOffset = 0;
}

/************** CLIENT APPLY HOST POSITIONS *********************/
//
// Apply host-authoritative car positions with snapshot interpolation.
// Interpolates between buffered snapshots at (hostTime - RENDER_DELAY_MS).
//

// Helper: lerp between two floats
static float LerpF(float a, float b, float t)
{
    return a + (b - a) * t;
}

// Helper: lerp angle (handles wraparound)
static float LerpAngle(float a, float b, float t)
{
    float diff = b - a;

    // Normalize to -PI to PI
    while (diff > PI) diff -= PI2;
    while (diff < -PI) diff += PI2;

    return a + diff * t;
}

// Helper: clamp float to range
static float ClampF(float val, float minVal, float maxVal)
{
    if (val < minVal) return minVal;
    if (val > maxVal) return maxVal;
    return val;
}

void ClientApplyHostPositions(void)
{
    if (!gIsNetworkClient)
        return;

    // Calculate render time: we render RENDER_DELAY_MS behind estimated host time
    // This gives us a buffer to absorb network jitter
    uint32_t renderTimeMs = GetEstimatedHostTime() - RENDER_DELAY_MS;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        ObjNode* car = gPlayerInfo[i].objNode;
        if (!car)
            continue;

        // Find two snapshots bracketing our render time
        PlayerSnapshot* before = FindSnapshotBefore(i, renderTimeMs);
        PlayerSnapshot* after = FindSnapshotAfter(i, renderTimeMs);

        float targetX, targetY, targetZ, targetRotY;
        float targetVelX, targetVelY, targetVelZ, targetSteering;

        if (before && after && after->hostTimeMs > before->hostTimeMs)
        {
            // Interpolate between two snapshots
            float t = (float)(renderTimeMs - before->hostTimeMs) /
                      (float)(after->hostTimeMs - before->hostTimeMs);
            t = ClampF(t, 0.0f, 1.0f);

            targetX = LerpF(before->posX, after->posX, t);
            targetY = LerpF(before->posY, after->posY, t);
            targetZ = LerpF(before->posZ, after->posZ, t);
            targetRotY = LerpAngle(before->rotY, after->rotY, t);
            targetVelX = LerpF(before->velX, after->velX, t);
            targetVelY = LerpF(before->velY, after->velY, t);
            targetVelZ = LerpF(before->velZ, after->velZ, t);
            targetSteering = LerpF(before->steering, after->steering, t);
        }
        else if (after)
        {
            // No older snapshot - use the oldest we have (game start or packet loss)
            targetX = after->posX;
            targetY = after->posY;
            targetZ = after->posZ;
            targetRotY = after->rotY;
            targetVelX = after->velX;
            targetVelY = after->velY;
            targetVelZ = after->velZ;
            targetSteering = after->steering;
        }
        else if (before)
        {
            // Extrapolate slightly using velocity (stale data)
            // Use velocity to predict where the car should be
            float staleMs = (float)(renderTimeMs - before->hostTimeMs);
            float staleSec = staleMs / 1000.0f;
            // Limit extrapolation to 100ms to avoid wild predictions
            if (staleSec > 0.1f) staleSec = 0.1f;

            targetX = before->posX + before->velX * staleSec;
            targetY = before->posY + before->velY * staleSec;
            targetZ = before->posZ + before->velZ * staleSec;
            targetRotY = before->rotY;
            targetVelX = before->velX;
            targetVelY = before->velY;
            targetVelZ = before->velZ;
            targetSteering = before->steering;
        }
        else
        {
            // No snapshots at all - fall back to legacy cached position if available
            if (!gHasHostPositionData)
                continue;
            targetX = gCachedHostPositions.posX[i];
            targetY = gCachedHostPositions.posY[i];
            targetZ = gCachedHostPositions.posZ[i];
            targetRotY = gCachedHostPositions.rotY[i];
            targetVelX = gCachedHostPositions.velX[i];
            targetVelY = gCachedHostPositions.velY[i];
            targetVelZ = gCachedHostPositions.velZ[i];
            targetSteering = gCachedHostPositions.steering[i];
        }

        // Calculate distance to target
        float dx = targetX - car->Coord.x;
        float dy = targetY - car->Coord.y;
        float dz = targetZ - car->Coord.z;
        float distSq = dx*dx + dy*dy + dz*dz;

        // If too far off (>500 units), snap immediately (likely teleport/respawn)
        if (distSq > 250000.0f)  // 500^2
        {
            car->Coord.x = targetX;
            car->Coord.y = targetY;
            car->Coord.z = targetZ;
            car->Rot.y = targetRotY;
            car->Delta.x = targetVelX;
            car->Delta.y = targetVelY;
            car->Delta.z = targetVelZ;
        }
        else
        {
            // With snapshot interpolation, we can apply positions more directly
            // since we're interpolating between known good states
            // Use a small lerp for final smoothing (handles any residual jitter)
            float smoothing = 0.5f;  // More aggressive since interpolation handles timing
            car->Coord.x = LerpF(car->Coord.x, targetX, smoothing);
            car->Coord.y = LerpF(car->Coord.y, targetY, smoothing);
            car->Coord.z = LerpF(car->Coord.z, targetZ, smoothing);
            car->Rot.y = LerpAngle(car->Rot.y, targetRotY, smoothing);
            car->Delta.x = LerpF(car->Delta.x, targetVelX, smoothing);
            car->Delta.y = LerpF(car->Delta.y, targetVelY, smoothing);
            car->Delta.z = LerpF(car->Delta.z, targetVelZ, smoothing);
        }

        // Sync playerInfo coord with objNode
        gPlayerInfo[i].coord.x = car->Coord.x;
        gPlayerInfo[i].coord.y = car->Coord.y;
        gPlayerInfo[i].coord.z = car->Coord.z;
        gPlayerInfo[i].steering = targetSteering;
    }
}


/************** CLIENT SEND CONTROL INFO TO HOST *********************/
//
// At the end of each frame, the client sends the new control state info to the host.
//

void ClientSend_ControlInfoToHost(void)
{
    if (!gIsNetworkClient)
        return;

    uint8_t buffer[sizeof(NetMsgHeader) + sizeof(NetClientControlInfoMessageType)];
    NetMsgHeader* header = (NetMsgHeader*)buffer;
    NetClientControlInfoMessageType* msg = (NetClientControlInfoMessageType*)(buffer + sizeof(NetMsgHeader));

    header->msgType = kNetMsgType_ClientControl;

    // Timestamp for RTT calculation
    msg->clientTimeMs = SDL_GetTicks();
    gLastClientTimeSent = msg->clientTimeMs;  // Remember for RTT calculation when echoed back

    msg->frameCounter = gClientSendCounter[gMyNetworkPlayerNum]++;
    msg->playerNum = gMyNetworkPlayerNum;
    msg->controlBits = gPlayerInfo[gMyNetworkPlayerNum].controlBits;
    msg->controlBitsNew = gPlayerInfo[gMyNetworkPlayerNum].controlBits_New;
    msg->analogSteeringX = gPlayerInfo[gMyNetworkPlayerNum].analogSteering.x;
    msg->analogSteeringY = gPlayerInfo[gMyNetworkPlayerNum].analogSteering.y;

    // Store for potential resend
    memcpy(&gClientOutMess, msg, sizeof(gClientOutMess));

    Net_SendToHost(buffer, sizeof(buffer), true);
}


/*************** HOST GET CONTROL INFO FROM CLIENTS ***********************/
//
// NON-BLOCKING: Receives any available client data without waiting.
// Clients with no new data continue using their last known input state.
//

void HostReceive_ControlInfoFromClients(void)
{
    if (!gIsNetworkHost)
        return;

    // Poll for network events (non-blocking)
    Net_ProcessEvents(0);

    // Check for received messages from each client
    for (int i = 1; i < gNumRealPlayers; i++)
    {
        if (gPendingClientControlMessage[i])
        {
            NetClientControlInfoMessageType* mess = &gPendingClientControl[i];

            // Skip old packets
            if (mess->frameCounter < gClientSendCounter[i])
            {
                gPendingClientControlMessage[i] = false;
                continue;
            }

            // Allow frame skips - just update to latest received
            // (Previously this was a fatal error)
            if (mess->frameCounter > gClientSendCounter[i])
            {
                // Log the skip but continue
                // printf("HostReceive: Player %d skipped %u frames\n", i, mess->frameCounter - gClientSendCounter[i]);
            }
            gClientSendCounter[i] = mess->frameCounter + 1;

            // Store client timestamp for RTT echo
            gLastClientTime[i] = mess->clientTimeMs;

            // Apply the new input
            gPlayerInfo[i].controlBits = mess->controlBits;
            gPlayerInfo[i].controlBits_New = mess->controlBitsNew;
            gPlayerInfo[i].analogSteering.x = mess->analogSteeringX;
            gPlayerInfo[i].analogSteering.y = mess->analogSteeringY;

            gPendingClientControlMessage[i] = false;
        }
        // If no new data for this client, their last input state persists
        // (gPlayerInfo[i].controlBits etc. remain unchanged)
    }

    gTimeoutCounter = 0;
}


#pragma mark - Vehicle Selection


/********************* PLAYER BROADCAST VEHICLE TYPE *******************************/
//
// Tell all other net players what vehicle type we want to be.
//

void PlayerBroadcastVehicleType(void)
{
    uint8_t buffer[sizeof(NetMsgHeader) + sizeof(NetPlayerCharTypeMessage)];
    NetMsgHeader* header = (NetMsgHeader*)buffer;
    NetPlayerCharTypeMessage* msg = (NetPlayerCharTypeMessage*)(buffer + sizeof(NetMsgHeader));

    header->msgType = kNetMsgType_VehicleType;
    msg->playerNum = gMyNetworkPlayerNum;
    msg->vehicleType = gPlayerInfo[gMyNetworkPlayerNum].vehicleType;
    msg->sex = gPlayerInfo[gMyNetworkPlayerNum].sex;

    if (gIsNetworkHost)
        Net_SendToAll(buffer, sizeof(buffer), true);
    else
        Net_SendToHost(buffer, sizeof(buffer), true);
}


/***************** GET VEHICLE SELECTION FROM NET PLAYERS ***********************/

void GetVehicleSelectionFromNetPlayers(void)
{
    // NOTE: Do NOT clear pending vehicle messages here!
    // Messages may have arrived while we were in character/vehicle select screens.

    printf("GetVehicleSelectionFromNetPlayers: Waiting for %d other players\n", gNumRealPlayers - 1);
    fflush(stdout);

    /*****************************/
    /* SET UP SIMPLE WAIT SCREEN */
    /*****************************/

    OGLSetupInputType viewDef;
    OGL_NewViewDef(&viewDef);
    viewDef.view.clearColor = (OGLColorRGBA) { 0, 0, 0, 1 };
    viewDef.styles.useFog = false;
    viewDef.view.pillarboxRatio = PILLARBOX_RATIO_4_3;
    viewDef.view.fontName = "rockfont";
    OGL_SetupGameView(&viewDef);

    // Create waiting message text
    NewObjectDefinitionType textDef =
    {
        .coord = {0, 50, 0},
        .scale = 0.4f,
        .slot = SPRITE_SLOT,
    };
    ObjNode* waitText = TextMesh_New(Localize(STR_WAITING_FOR_PLAYERS), kTextMeshAlignCenter, &textDef);
    waitText->ColorFilter = (OGLColorRGBA) {1, 1, 1, 1};

    // Create status text (shows count)
    textDef.coord.y = -20;
    textDef.scale = 0.25f;
    ObjNode* statusText = TextMesh_New("", kTextMeshAlignCenter, &textDef);
    statusText->ColorFilter = (OGLColorRGBA) {0.7f, 0.7f, 0.7f, 1};

    MakeFadeEvent(true);

    /*************/
    /* MAIN LOOP */
    /*************/

    int count = 1;  // We have our own info
    uint32_t startTick = SDL_GetTicks();
    uint32_t lastPrint = 0;

    while (count < gNumRealPlayers)
    {
        CalcFramesPerSecond();
        ReadKeyboard();
        Net_ProcessEvents(10);

        for (int i = 0; i < gNumRealPlayers; i++)
        {
            if (i == gMyNetworkPlayerNum)
                continue;

            if (gPendingVehicleTypeMessage[i])
            {
                NetPlayerCharTypeMessage* msg = &gPendingVehicleType[i];
                gPlayerInfo[i].vehicleType = msg->vehicleType;
                gPlayerInfo[i].sex = msg->sex;
                gPendingVehicleTypeMessage[i] = false;
                count++;

                printf("GetVehicleSelectionFromNetPlayers: Player %d chose vehicle %d\n",
                       i, msg->vehicleType);
                fflush(stdout);
            }
        }

        // Update status text
        char statusStr[64];
        SDL_snprintf(statusStr, sizeof(statusStr), "%d / %d", count, gNumRealPlayers);
        TextMesh_Update(statusStr, kTextMeshAlignCenter, statusText);

        // Print status every 2 seconds
        uint32_t now = SDL_GetTicks();
        if (now - lastPrint > 2000)
        {
            printf("GetVehicleSelectionFromNetPlayers: Still waiting... got %d/%d\n",
                   count, gNumRealPlayers);
            fflush(stdout);
            lastPrint = now;
        }

        // Timeout after 2 minutes
        if ((SDL_GetTicks() - startTick) > (60 * 1000 * 2))
        {
            DeleteAllObjects();
            OGL_DisposeGameView();
            DoFatalAlert("GetVehicleSelectionFromNetPlayers: Timeout waiting for other players.");
        }

        // Draw the waiting screen
        MoveObjects();
        OGL_DrawScene(DrawObjects);
    }

    /***********/
    /* CLEANUP */
    /***********/

    OGL_FadeOutScene(DrawObjects, MoveObjects);
    DeleteAllObjects();
    OGL_DisposeGameView();

    printf("GetVehicleSelectionFromNetPlayers: Got all vehicle selections!\n");
    fflush(stdout);
}


#pragma mark - Misc


/***************** PLAYER UNEXPECTEDLY LEAVES GAME ***********************/

static void PlayerUnexpectedlyLeavesGame(int playerIndex)
{
    if (playerIndex < 0 || playerIndex >= gNumTotalPlayers)
        return;

    // Turn into a computer player
    gPlayerInfo[playerIndex].isComputer = true;
    gPlayerInfo[playerIndex].isEliminated = true;
    gNumRealPlayers--;

    if (gNumRealPlayers <= 1)
        gGameOver = true;

    // Handle game-specific cases
    switch (gGameMode)
    {
        case GAME_MODE_TAG1:
        case GAME_MODE_TAG2:
            if (gPlayerInfo[playerIndex].isIt)
                ChooseTaggedPlayer();
            break;
    }

    printf("PlayerUnexpectedlyLeavesGame: Player %d left, converted to CPU\n", playerIndex);
}


/********************* PLAYER BROADCAST NULL PACKET *******************************/
//
// Send a dummy packet to let others know we're still active.
//

void PlayerBroadcastNullPacket(void)
{
    uint8_t buffer[sizeof(NetMsgHeader)];
    NetMsgHeader* header = (NetMsgHeader*)buffer;
    header->msgType = kNetMsgType_NullPacket;

    if (gIsNetworkHost)
        Net_SendToAll(buffer, sizeof(buffer), false);
    else if (gIsNetworkClient)
        Net_SendToHost(buffer, sizeof(buffer), false);
}


/********************* NET PROCESS EVENTS *******************************/
//
// Process network events - call this each frame
//

void NetProcessEvents(void)
{
    Net_ProcessEvents(0);
}


/********************* SET LOCAL PLAYER NAME *******************************/

void SetLocalPlayerName(const char* name)
{
    strncpy(gLocalPlayerName, name, NET_PLAYER_NAME_LENGTH - 1);
    gLocalPlayerName[NET_PLAYER_NAME_LENGTH - 1] = '\0';
}


#pragma mark - Async Networking


/********************* NET SHOULD SEND THIS FRAME *******************************/
//
// Returns true if enough time has passed to send network data.
// Used to throttle network sends to NET_TICK_RATE Hz instead of every frame.
//

Boolean NetShouldSendThisFrame(void)
{
    uint32_t now = SDL_GetTicks();
    if (now - gLastNetSendTime >= NET_SEND_INTERVAL_MS)
    {
        gLastNetSendTime = now;
        return true;
    }
    return false;
}


/********************* NET TICK HOST *******************************/
//
// Non-blocking network tick for host.
// Called every frame, but only sends at NET_TICK_RATE Hz.
//

void NetTick_Host(void)
{
    if (!gIsNetworkHost)
        return;

    // Always receive (non-blocking)
    HostReceive_ControlInfoFromClients();

    // Send at fixed rate
    if (NetShouldSendThisFrame())
    {
        HostSend_ControlInfoToClients();
    }
}


/********************* NET TICK CLIENT *******************************/
//
// Non-blocking network tick for client.
// Called every frame, but only sends at NET_TICK_RATE Hz.
//

void NetTick_Client(void)
{
    if (!gIsNetworkClient)
        return;

    // Always receive (non-blocking)
    ClientReceive_ControlInfoFromHost();

    // Send at fixed rate
    if (NetShouldSendThisFrame())
    {
        ReadKeyboard();
        GetLocalKeyState();
        ClientSend_ControlInfoToHost();
    }
}

/****************************/
/*   	  NETWORK.C	   	    */
/* (c)2000 Pangea Software  */
/* By Brian Greenstone      */
/* GNS P2P port (c)2026     */
/****************************/


/***************/
/* EXTERNALS   */
/***************/

// Standard library includes
#include <string.h>
#include <stdio.h>
#include <math.h>

// Include C++ headers from Pomme/SDL BEFORE the extern "C" block
// so their include guards prevent re-inclusion inside extern "C"
#include "Pomme.h"

// C game headers wrapped for C++ linkage
extern "C" {
#include "game.h"
#include "window.h"
}

// C++ compatible headers (already have extern "C" guards)
#include "network.h"
#include "Backend_Network.h"

// Shared network protocol (plain packed structs, no protobuf)
#include "common/net_protocol.h"

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
static void OnWorldState(const void* worldState);  // Equal-players model callback
static void OnWeaponEvent(const void* weaponEvent);  // Remote weapon event callback

// Reset function
static void ResetNetworkState(void);

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

// Equal-players model: cached world state from server (using packed struct)
static bool     gHasWorldStateData = false;
static NetWorldState gCachedWorldState;
static bool     gReceivedNewWorldStateThisFrame = false;

//==============================================================================
// SIMPLIFIED NETWORK STATE (no snapshot interpolation, no clock sync)
//==============================================================================

// Simple RTT tracking (for debug display only)
static uint32_t gEstimatedRTT = 0;

// Packet reception tracking (time-based)
static uint32_t gPacketsReceivedWindow = 0;
static uint32_t gStatsWindowStartTime = 0;
static uint32_t gLastPacketDeliveryPct = 100;
#define STATS_WINDOW_MS 2000

// Per-player last client time (host tracks for echo)
static uint32_t gLastClientTime[MAX_PLAYERS];

//==============================================================================
// DIAGNOSTIC SYSTEM (F9 to toggle recording)
//==============================================================================

#define DIAG_HISTORY_SIZE 300  // 5 seconds at 60fps

typedef struct {
    float frameDeltaMs;      // Time since last frame
    float netDeltaMs;        // Time since last network message
    float positionJump;      // Distance moved this frame (largest remote car)
    uint32_t rtt;            // Current RTT
    uint32_t packetPct;      // Packet delivery %
} DiagSample;

static DiagSample gDiagHistory[DIAG_HISTORY_SIZE];
static int gDiagIndex = 0;
static int gDiagCount = 0;
static uint32_t gLastNetMessageTime = 0;
static uint32_t gLastFrameTime = 0;
static bool gDiagEnabled = false;

// Network message types (internal, prepended to game messages)
enum
{
    kNetMsgType_Config = 100,
    kNetMsgType_Sync,
    kNetMsgType_HostControl,
    kNetMsgType_ClientControl,
    kNetMsgType_VehicleType,
    kNetMsgType_PlayerState = 105,  // Equal-players model
    kNetMsgType_WorldState = 106,   // Equal-players model
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
static bool                             gReceivedNewHostPacketThisFrame = false;  // For extrapolation logic
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
    printf("[NET] Struct sizes: HostControl=%zu, ClientControl=%zu, Config=%zu\n",
           sizeof(NetHostControlInfoMessageType),
           sizeof(NetClientControlInfoMessageType),
           sizeof(NetConfigMessageType));
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
        Net_SetWorldStateCallback(OnWorldState);  // Equal-players model
        Net_SetWeaponEventCallback(OnWeaponEvent);  // Weapon sync

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

    // Reset equal-players model state
    gHasWorldStateData = false;
    gReceivedNewWorldStateThisFrame = false;
    memset(&gCachedWorldState, 0, sizeof(NetWorldState));

    // Reset network state
    ResetNetworkState();
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

    // Reset network state
    ResetNetworkState();
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

    // Reset network state
    ResetNetworkState();
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

            // Show notification
            Notification_PlayerJoined((const char*)gPlayerNameStrings[playerNum]);
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

        // Capture name BEFORE clearing for notification
        char disconnectedName[32];
        if (playerNum >= 0 && playerNum < MAX_PLAYERS)
        {
            snprintf(disconnectedName, sizeof(disconnectedName), "%s", gPlayerNameStrings[playerNum]);
        }
        else
        {
            snprintf(disconnectedName, sizeof(disconnectedName), "Player %d", playerNum + 1);
        }

        // Show notification
        Notification_PlayerDisconnected(disconnectedName);

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
            printf("[NET] Received CONFIG message: payloadSize=%zu, expected=%zu\n",
                   payloadSize, sizeof(NetConfigMessageType));
            if (payloadSize >= sizeof(NetConfigMessageType))
            {
                memcpy(&gPendingConfig, payload, sizeof(NetConfigMessageType));
                gPendingConfigMessage = true;
                printf("[NET] CONFIG accepted: gameMode=%d, playerNum=%d, numPlayers=%d\n",
                       gPendingConfig.gameMode, gPendingConfig.playerNum, gPendingConfig.numPlayers);
            }
            else
            {
                printf("[NET] CONFIG REJECTED: payload too small!\n");
            }
            break;

        case kNetMsgType_Sync:
            printf("[NET] SYNC handler: payloadSize=%zu, expected=%zu\n",
                   payloadSize, sizeof(NetSyncMessageType));
            if (payloadSize >= sizeof(NetSyncMessageType))
            {
                gSyncCount++;
                gPendingSyncMessage = true;
                printf("[NET] SYNC accepted, gSyncCount=%d\n", gSyncCount);
            }
            else
            {
                printf("[NET] SYNC REJECTED: payload too small!\n");
            }
            break;

        case kNetMsgType_HostControl:
            printf("[NET] Received HOST_CONTROL: payloadSize=%zu, expected=%zu\n",
                   payloadSize, sizeof(NetHostControlInfoMessageType));
            if (payloadSize >= sizeof(NetHostControlInfoMessageType))
            {
                memcpy(&gPendingHostControl, payload, sizeof(NetHostControlInfoMessageType));
                gPendingHostControlMessage = true;

                // Simple time-based packet counting
                uint32_t statsNow = SDL_GetTicks();
                if (gStatsWindowStartTime == 0)
                    gStatsWindowStartTime = statsNow;

                gPacketsReceivedWindow++;

                uint32_t elapsed = statsNow - gStatsWindowStartTime;
                if (elapsed >= STATS_WINDOW_MS)
                {
                    // Expected = elapsed_time * 60Hz
                    uint32_t expectedPackets = (elapsed * NET_TICK_RATE) / 1000;
                    if (expectedPackets > 0)
                    {
                        uint32_t pct = (gPacketsReceivedWindow * 100) / expectedPackets;
                        if (pct > 100) pct = 100;
                        gLastPacketDeliveryPct = pct;
                    }
                    gStatsWindowStartTime = statsNow;
                    gPacketsReceivedWindow = 0;
                }
            }
            break;

        case kNetMsgType_ClientControl:
            printf("[NET] HOST received CLIENT_CONTROL: payloadSize=%zu, expected=%zu\n",
                   payloadSize, sizeof(NetClientControlInfoMessageType));
            if (payloadSize >= sizeof(NetClientControlInfoMessageType))
            {
                NetClientControlInfoMessageType msg;
                memcpy(&msg, payload, sizeof(msg));
                int playerNum = msg.playerNum;
                printf("[NET]   playerNum=%d, controlBits=0x%x\n", playerNum, msg.controlBits);
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
                printf("[NET] VEHICLE_TYPE handler: playerNum=%d, vehicleType=%d, sex=%d, myNum=%d\n",
                       playerNum, msg.vehicleType, msg.sex, gMyNetworkPlayerNum);
                if (playerNum >= 0 && playerNum < MAX_PLAYERS)
                {
                    memcpy(&gPendingVehicleType[playerNum], &msg, sizeof(msg));
                    gPendingVehicleTypeMessage[playerNum] = true;
                    printf("[NET] VEHICLE_TYPE stored for player %d\n", playerNum);
                }
                else
                {
                    printf("[NET] VEHICLE_TYPE REJECTED: playerNum %d out of range\n", playerNum);
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

    // Send config to each client using typed API
    for (int i = 1; i < gNumGatheredPlayers; i++)
    {
        Net_SendConfig(i,
                       gGameMode,
                       gTheAge,
                       gTrackNum,
                       i,  // This client's player number
                       gNumGatheredPlayers,
                       gGamePrefs.tournamentProgression.numTracksCompleted,
                       gGamePrefs.difficulty,
                       gGamePrefs.tagDuration);
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

    // Tell all clients we're ready using typed API
    Net_SendSync(0);

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

    // Tell host we're ready using typed API
    Net_SendSync(gMyNetworkPlayerNum);

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

    // Build message struct directly (typed API handles serialization)
    NetHostControlInfoMessageType msg = {0};

    // Timestamp for clock sync
    msg.hostTimeMs = SDL_GetTicks();
    // Echo back the most recent client timestamp (for RTT calculation)
    // For simplicity, echo back player 1's timestamp (first client)
    // In a more sophisticated system, each client would have their own echo
    msg.echoedClientTime = gLastClientTime[1];  // First client is player 1

    msg.frameCounter = gHostSendCounter++;
    msg.fps = gFramesPerSecond;
    msg.fpsFrac = gFramesPerSecondFrac;
    msg.randomSeed = MyRandomLong();

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        msg.controlBits[i] = gPlayerInfo[i].controlBits;
        msg.controlBitsNew[i] = gPlayerInfo[i].controlBits_New;
        msg.analogSteeringX[i] = gPlayerInfo[i].analogSteering.x;
        msg.analogSteeringY[i] = gPlayerInfo[i].analogSteering.y;
        msg.steering[i] = gPlayerInfo[i].steering;

        // Get position, rotation, velocity from objNode (the actual car)
        ObjNode* car = gPlayerInfo[i].objNode;
        if (car)
        {
            msg.posX[i] = car->Coord.x;
            msg.posY[i] = car->Coord.y;
            msg.posZ[i] = car->Coord.z;
            msg.rotY[i] = car->Rot.y;
            msg.velX[i] = car->Delta.x;
            msg.velY[i] = car->Delta.y;
            msg.velZ[i] = car->Delta.z;
        }
        else
        {
            msg.posX[i] = gPlayerInfo[i].coord.x;
            msg.posY[i] = gPlayerInfo[i].coord.y;
            msg.posZ[i] = gPlayerInfo[i].coord.z;
            msg.rotY[i] = 0;
            msg.velX[i] = 0;
            msg.velY[i] = 0;
            msg.velZ[i] = 0;
        }

        // Race state sync
        msg.lapNum[i] = (int8_t)gPlayerInfo[i].lapNum;
        int lap = gPlayerInfo[i].lapNum;
        if (lap < 0) lap = 0;
        if (lap >= LAPS_PER_RACE) lap = LAPS_PER_RACE - 1;
        msg.currentLapTime[i] = gPlayerInfo[i].lapTimes[lap];
    }

    // Store for potential resend
    memcpy(&gHostOutMess, &msg, sizeof(gHostOutMess));

    // Debug: log first few sends
    static int sSendCount = 0;
    if (sSendCount++ < 10)
    {
        printf("[NET] HOST sending control: player0 pos=(%.1f,%.1f,%.1f) structSize=%zu\n",
               msg.posX[0], msg.posY[0], msg.posZ[0], sizeof(msg));
    }

    // Send using typed API (no struct padding issues)
    Net_SendHostControl(&msg);
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

    // Clear new packet flag at start of each frame
    gReceivedNewHostPacketThisFrame = false;

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
            // No new data this frame - that's normal if fps > network tick rate
            return;
        }
    }

    // Process received message
    NetHostControlInfoMessageType* mess = &gPendingHostControl;

    // NOTE: Packet counting moved to receive callback (kNetMsgType_HostControl case)
    // to accurately count all received packets, even those overwritten between frames

    // Skip old packets
    if (mess->frameCounter < gHostSendCounter)
    {
        gPendingHostControlMessage = false;
        return;
    }

    // Allow frame skips - just update to latest received frame
    // (Previously this was a fatal error)
    gHostSendCounter = mess->frameCounter + 1;

    //==============================================================================
    // SIMPLE RTT CALCULATION (for debug display only)
    //==============================================================================
    uint32_t now = SDL_GetTicks();
    if (mess->echoedClientTime != 0 &&
        mess->echoedClientTime <= now &&
        (now - mess->echoedClientTime) < 2000)
    {
        uint32_t rtt = now - mess->echoedClientTime;
        if (rtt < 1000)
        {
            // Simple moving average: 3/4 old + 1/4 new
            gEstimatedRTT = (gEstimatedRTT * 3 + rtt) / 4;
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

    // Cache the latest host positions
    memcpy(&gCachedHostPositions, mess, sizeof(gCachedHostPositions));
    gHasHostPositionData = true;

    // Track network message timing for diagnostics
    gLastNetMessageTime = SDL_GetTicks();

    gTimeoutCounter = 0;
    gPendingHostControlMessage = false;
    gHasReceivedInitialHostData = true;
    gReceivedNewHostPacketThisFrame = true;  // Mark that we got a new packet
}


// Reset network state (call on game start/end)
static void ResetNetworkState(void)
{
    gEstimatedRTT = 0;
    gPacketsReceivedWindow = 0;
    gStatsWindowStartTime = 0;
    gLastPacketDeliveryPct = 100;
}

/************** CLIENT APPLY HOST POSITIONS *********************/
//
// SIMPLIFIED: Apply host-authoritative car positions with simple lerp.
// - LOCAL PLAYER: Trust local physics, snap only on huge error (>500 units)
// - REMOTE PLAYERS: Lerp toward latest received position (20% per frame)
//
// No snapshot interpolation, no clock sync, no render delay.
// Just smooth movement toward the latest known position.
//

// Helper: lerp angle (handles wraparound)
static float LerpAngle(float a, float b, float t)
{
    float diff = b - a;

    // Normalize to -PI to PI
    while (diff > PI) diff -= PI2;
    while (diff < -PI) diff += PI2;

    return a + diff * t;
}

void ClientApplyHostPositions(void)
{
    static int sLogCounter = 0;
    if (!gIsNetworkClient)
    {
        if (sLogCounter++ < 5)
            printf("[NET] ClientApplyHostPositions: Not a client (gIsNetworkClient=%d)\n", gIsNetworkClient);
        return;
    }
    if (!gHasHostPositionData)
    {
        if (sLogCounter++ < 60)
            printf("[NET] ClientApplyHostPositions: No host position data yet\n");
        return;
    }

    // Diagnostic: log what's being applied
    static int sApplyLogCount = 0;
    if (sApplyLogCount++ < 20)
    {
        printf("[NET] ClientApply: myPlayer=%d, numPlayers=%d\n",
               gMyNetworkPlayerNum, gNumRealPlayers);
        for (int i = 0; i < gNumRealPlayers; i++)
        {
            ObjNode* car = gPlayerInfo[i].objNode;
            printf("[NET]   P%d: obj=%p, cached=(%.0f,%.0f,%.0f)",
                   i, (void*)car,
                   gCachedHostPositions.posX[i],
                   gCachedHostPositions.posY[i],
                   gCachedHostPositions.posZ[i]);
            if (car)
                printf(" cur=(%.0f,%.0f,%.0f)", car->Coord.x, car->Coord.y, car->Coord.z);
            printf("\n");
        }
    }

    //==============================================================================
    // FIX 3: DIAGNOSTIC SAMPLING - measure position jump BEFORE lerp
    //==============================================================================
    float maxJumpBeforeLerp = 0;
    if (gDiagEnabled && gDiagCount < DIAG_HISTORY_SIZE)
    {
        for (int i = 0; i < gNumRealPlayers; i++)
        {
            if (i == gMyNetworkPlayerNum) continue;
            ObjNode* car = gPlayerInfo[i].objNode;
            if (!car) continue;
            float dx = gCachedHostPositions.posX[i] - car->Coord.x;
            float dz = gCachedHostPositions.posZ[i] - car->Coord.z;
            float jump = sqrtf(dx*dx + dz*dz);
            if (jump > maxJumpBeforeLerp) maxJumpBeforeLerp = jump;
        }
    }

    //==============================================================================
    // FIX 1: Frame-time-independent lerp with EMA smoothing
    // At 60 FPS (dt=0.0167), we want 20% smoothing per frame.
    // Formula: smoothing = 1 - (1 - baseSmoothing)^(dt * targetFPS)
    // Simplified: smoothing = 1 - 0.8^(dt * 60)
    //
    // We apply EMA to the smoothing factor itself to prevent jitter from
    // frame time variation (e.g., 60 FPS → 25 FPS spikes).
    //==============================================================================
    static float gSmoothedSmoothingFactor = 0.2f;  // EMA state

    float dt = gFramesPerSecondFrac;
    float targetSmoothing = 1.0f - powf(0.8f, dt * 60.0f);

    // Smooth the smoothing factor to prevent jitter from frame time variation
    gSmoothedSmoothingFactor = gSmoothedSmoothingFactor * 0.9f + targetSmoothing * 0.1f;
    float smoothing = gSmoothedSmoothingFactor;

    // Gentler clamp - max 30% correction per frame to avoid visible jitter
    if (smoothing < 0.05f) smoothing = 0.05f;
    if (smoothing > 0.30f) smoothing = 0.30f;

    //==============================================================================
    // FIX 2: Position extrapolation on missing packets
    // When no new packet arrived this frame, extrapolate the cached target
    // positions using the last known velocity. This prevents using stale data.
    //==============================================================================
    if (!gReceivedNewHostPacketThisFrame)
    {
        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            if (i == gMyNetworkPlayerNum) continue;
            // Extrapolate target position using cached velocity
            gCachedHostPositions.posX[i] += gCachedHostPositions.velX[i] * dt;
            gCachedHostPositions.posY[i] += gCachedHostPositions.velY[i] * dt;
            gCachedHostPositions.posZ[i] += gCachedHostPositions.velZ[i] * dt;
        }
    }

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        ObjNode* car = gPlayerInfo[i].objNode;
        if (!car)
            continue;

        // LOCAL PLAYER: trust local physics completely (no network correction)
        if (i == gMyNetworkPlayerNum)
        {
            continue;
        }

        // REMOTE PLAYERS: lerp toward latest position (frame-time-independent)
        car->Coord.x += (gCachedHostPositions.posX[i] - car->Coord.x) * smoothing;
        car->Coord.y += (gCachedHostPositions.posY[i] - car->Coord.y) * smoothing;
        car->Coord.z += (gCachedHostPositions.posZ[i] - car->Coord.z) * smoothing;
        car->Rot.y = LerpAngle(car->Rot.y, gCachedHostPositions.rotY[i], smoothing);

        // Sync playerInfo
        gPlayerInfo[i].coord.x = car->Coord.x;
        gPlayerInfo[i].coord.y = car->Coord.y;
        gPlayerInfo[i].coord.z = car->Coord.z;
        gPlayerInfo[i].steering = gCachedHostPositions.steering[i];
    }

    //==============================================================================
    // DIAGNOSTIC SAMPLING (when enabled via F9)
    //==============================================================================

    if (gDiagEnabled && gDiagCount < DIAG_HISTORY_SIZE)
    {
        uint32_t now = SDL_GetTicks();
        float frameDelta = gLastFrameTime ? (float)(now - gLastFrameTime) : 0;
        gLastFrameTime = now;

        DiagSample* s = &gDiagHistory[gDiagIndex];
        s->frameDeltaMs = frameDelta;
        s->netDeltaMs = gLastNetMessageTime ? (float)(now - gLastNetMessageTime) : 0;
        s->positionJump = maxJumpBeforeLerp;  // FIX 3: Use pre-lerp measurement
        s->rtt = gEstimatedRTT;
        s->packetPct = gLastPacketDeliveryPct;

        gDiagIndex = (gDiagIndex + 1) % DIAG_HISTORY_SIZE;
        gDiagCount++;
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

    // Build message struct directly (typed API handles serialization)
    NetClientControlInfoMessageType msg = {0};

    // Timestamp for RTT calculation
    msg.clientTimeMs = SDL_GetTicks();

    msg.frameCounter = gClientSendCounter[gMyNetworkPlayerNum]++;
    msg.playerNum = gMyNetworkPlayerNum;
    msg.controlBits = gPlayerInfo[gMyNetworkPlayerNum].controlBits;
    msg.controlBitsNew = gPlayerInfo[gMyNetworkPlayerNum].controlBits_New;
    msg.analogSteeringX = gPlayerInfo[gMyNetworkPlayerNum].analogSteering.x;
    msg.analogSteeringY = gPlayerInfo[gMyNetworkPlayerNum].analogSteering.y;

    // Store for potential resend
    memcpy(&gClientOutMess, &msg, sizeof(gClientOutMess));

    // Debug: log first few sends
    static int sSendCount = 0;
    if (sSendCount++ < 10)
    {
        printf("[NET] CLIENT sending control: player=%d, bits=0x%x, structSize=%zu\n",
               msg.playerNum, msg.controlBits, sizeof(msg));
    }

    // Send using typed API (no struct padding issues)
    Net_SendClientControl(&msg);
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
    // Send using typed API (no struct padding issues)
    Net_SendVehicleType(gMyNetworkPlayerNum,
                        gPlayerInfo[gMyNetworkPlayerNum].vehicleType,
                        gPlayerInfo[gMyNetworkPlayerNum].sex);
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
    viewDef.view.clearColor = OGLColorRGBA{ 0, 0, 0, 1 };
    viewDef.styles.useFog = false;
    viewDef.view.pillarboxRatio = PILLARBOX_RATIO_4_3;
    viewDef.view.fontName = "rockfont";
    OGL_SetupGameView(&viewDef);

    // Create waiting message text
    NewObjectDefinitionType textDef =
    {
        .slot = SPRITE_SLOT,
        .coord = {0, 50, 0},
        .scale = 0.4f,
    };
    ObjNode* waitText = TextMesh_New(Localize(STR_WAITING_FOR_PLAYERS), kTextMeshAlignCenter, &textDef);
    waitText->ColorFilter = OGLColorRGBA{1, 1, 1, 1};

    // Create status text (shows count)
    textDef.coord.y = -20;
    textDef.scale = 0.25f;
    ObjNode* statusText = TextMesh_New("", kTextMeshAlignCenter, &textDef);
    statusText->ColorFilter = OGLColorRGBA{0.7f, 0.7f, 0.7f, 1};

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

    // Turn into a computer player (bot takes over the car)
    gPlayerInfo[playerIndex].isComputer = true;
    // Note: Do NOT set isEliminated - that would block all control including AI
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


#pragma mark - Equal Players Model


/********************* ON WORLD STATE *******************************/
//
// Callback from Backend_GNS.cpp when WORLD_STATE message is received.
// Called from Net_ProcessEvents(). Receives already-decoded NetWorldState struct.
//

static void OnWorldState(const void* worldState)
{
    // worldState is already a decoded NetWorldState pointer from Backend_GNS.cpp
    const NetWorldState* world = (const NetWorldState*)worldState;

    // Copy the decoded world state
    memcpy(&gCachedWorldState, world, sizeof(NetWorldState));

    gHasWorldStateData = true;
    gReceivedNewWorldStateThisFrame = true;

    // Debug log (limit spam)
    static int sWorldLogCount = 0;
    if (sWorldLogCount++ < 30)
    {
        printf("[NET] OnWorldState: seq=%u, players=%d\n",
               gCachedWorldState.sequence, (int)gCachedWorldState.player_count);
        for (int j = 0; j < (int)gCachedWorldState.player_count; j++)
        {
            printf("[NET]   Recv P%d: pos=(%.0f,%.0f,%.0f)\n",
                   gCachedWorldState.players[j].player_num,
                   gCachedWorldState.players[j].pos_x,
                   gCachedWorldState.players[j].pos_y,
                   gCachedWorldState.players[j].pos_z);
        }
    }
}


/********************* SEND PLAYER STATE *******************************/
//
// Equal-players model: Send local player's position and input to server.
// All players call this - server collects states and broadcasts WORLD_STATE.
// Uses packed NetPlayerState struct.
//

void SendPlayerState(void)
{
    if (!gNetGameInProgress)
        return;

    ObjNode* car = gPlayerInfo[gMyNetworkPlayerNum].objNode;
    if (!car)
        return;

    // Don't send if car position is invalid (not yet initialized)
    // Valid track positions are typically > 1000 units from origin
    float posMag = car->Coord.x * car->Coord.x + car->Coord.z * car->Coord.z;
    if (posMag < 1000.0f * 1000.0f)
    {
        // Car not yet placed on track, skip this frame
        return;
    }

    // Build the packed player state message
    NetPlayerState state = {0};
    state.player_num = gMyNetworkPlayerNum;
    uint32_t seq = gIsNetworkHost ? gHostSendCounter++ : gClientSendCounter[gMyNetworkPlayerNum]++;
    state.sequence = seq;
    state.frame_counter = seq;  // Use sequence as frame counter for consistency
    state.control_bits = gPlayerInfo[gMyNetworkPlayerNum].controlBits;
    state.control_bits_new = gPlayerInfo[gMyNetworkPlayerNum].controlBits_New;
    state.analog_steering_x = gPlayerInfo[gMyNetworkPlayerNum].analogSteering.x;
    state.analog_steering_y = gPlayerInfo[gMyNetworkPlayerNum].analogSteering.y;

    // Position, rotation, velocity from objNode
    state.pos_x = car->Coord.x;
    state.pos_y = car->Coord.y;
    state.pos_z = car->Coord.z;
    state.rot_y = car->Rot.y;
    state.vel_x = car->Delta.x;
    state.vel_y = car->Delta.y;
    state.vel_z = car->Delta.z;

    state.steering = gPlayerInfo[gMyNetworkPlayerNum].steering;
    state.lap_num = gPlayerInfo[gMyNetworkPlayerNum].lapNum;

    int lap = gPlayerInfo[gMyNetworkPlayerNum].lapNum;
    if (lap < 0) lap = 0;
    if (lap >= LAPS_PER_RACE) lap = LAPS_PER_RACE - 1;
    state.lap_time_ms = gPlayerInfo[gMyNetworkPlayerNum].lapTimes[lap];

    // Debug log (limit spam)
    static int sSendLogCount = 0;
    if (sSendLogCount++ < 30)
    {
        printf("[NET] SendPlayerState: P%d pos=(%.0f,%.0f,%.0f) ctrl=0x%x\n",
               gMyNetworkPlayerNum, state.pos_x, state.pos_y, state.pos_z, state.control_bits);
    }

    Net_SendPlayerState(&state);
}


/********************* APPLY WORLD STATE *******************************/
//
// Equal-players model: Apply server-broadcast positions to all players.
// Similar to ClientApplyHostPositions but ALL players use this equally.
// Uses packed NetPlayerState struct.
//

void ApplyWorldState(void)
{
    if (!gHasWorldStateData)
        return;

    // Frame-time-independent lerp (same as legacy ClientApplyHostPositions)
    static float gSmoothedSmoothingFactor = 0.2f;
    float dt = gFramesPerSecondFrac;
    float targetSmoothing = 1.0f - powf(0.8f, dt * 60.0f);
    gSmoothedSmoothingFactor = gSmoothedSmoothingFactor * 0.9f + targetSmoothing * 0.1f;
    float smoothing = gSmoothedSmoothingFactor;
    if (smoothing < 0.05f) smoothing = 0.05f;
    if (smoothing > 0.30f) smoothing = 0.30f;

    // Apply each player's state from the world snapshot
    for (int i = 0; i < (int)gCachedWorldState.player_count; i++)
    {
        NetPlayerState* ps = &gCachedWorldState.players[i];

        // Use player_num from the state to find the correct player!
        int playerNum = ps->player_num;
        if (playerNum < 0 || playerNum >= gNumRealPlayers)
            continue;

        ObjNode* car = gPlayerInfo[playerNum].objNode;
        if (!car)
            continue;

        // Skip if received position is invalid (player not yet initialized)
        float recvPosMag = ps->pos_x * ps->pos_x + ps->pos_z * ps->pos_z;
        if (recvPosMag < 1000.0f * 1000.0f)
        {
            // Invalid position from server, skip this player
            continue;
        }

        // LOCAL PLAYER: trust local physics completely
        if (playerNum == gMyNetworkPlayerNum)
        {
            continue;
        }

        // REMOTE PLAYERS: apply control state
        gPlayerInfo[playerNum].controlBits = ps->control_bits;
        gPlayerInfo[playerNum].controlBits_New = ps->control_bits_new;
        gPlayerInfo[playerNum].analogSteering.x = ps->analog_steering_x;
        gPlayerInfo[playerNum].analogSteering.y = ps->analog_steering_y;

        // REMOTE PLAYERS: lerp toward server-provided position
        float targetX = ps->pos_x;
        float targetY = ps->pos_y;
        float targetZ = ps->pos_z;

        if (!gReceivedNewWorldStateThisFrame)
        {
            // Extrapolate using velocity
            targetX += ps->vel_x * dt;
            targetY += ps->vel_y * dt;
            targetZ += ps->vel_z * dt;
        }

        // Debug: log application (limit spam)
        static int sApplyLogCount = 0;
        if (sApplyLogCount++ < 30)
        {
            printf("[NET] Apply P%d (remote): target=(%.0f,%.0f,%.0f) car=(%.0f,%.0f,%.0f)\n",
                   playerNum, targetX, targetY, targetZ,
                   car->Coord.x, car->Coord.y, car->Coord.z);
        }

        car->Coord.x += (targetX - car->Coord.x) * smoothing;
        car->Coord.y += (targetY - car->Coord.y) * smoothing;
        car->Coord.z += (targetZ - car->Coord.z) * smoothing;
        car->Rot.y = LerpAngle(car->Rot.y, ps->rot_y, smoothing);

        // Sync playerInfo
        gPlayerInfo[playerNum].coord.x = car->Coord.x;
        gPlayerInfo[playerNum].coord.y = car->Coord.y;
        gPlayerInfo[playerNum].coord.z = car->Coord.z;
        gPlayerInfo[playerNum].steering = ps->steering;

        // Sync race state
        gPlayerInfo[playerNum].lapNum = ps->lap_num;
        int lap = ps->lap_num;
        if (lap < 0) lap = 0;
        if (lap >= LAPS_PER_RACE) lap = LAPS_PER_RACE - 1;
        gPlayerInfo[playerNum].lapTimes[lap] = ps->lap_time_ms;
    }

    // Clear flag after applying
    gReceivedNewWorldStateThisFrame = false;
}


/********************* NET TICK EQUAL PLAYERS *******************************/
//
// Equal-players model: All players use this tick function.
// - Send local player state to server
// - Receive world state from server
// - Apply positions equally (no host advantage)
//

void NetTick_EqualPlayers(void)
{
    if (!gNetGameInProgress)
        return;

    // Clear new packet flag at start of each frame
    gReceivedNewWorldStateThisFrame = false;

    // Always process network events (non-blocking)
    Net_ProcessEvents(0);

    // Send local player state at fixed rate
    if (NetShouldSendThisFrame())
    {
        SendPlayerState();
    }
}


#pragma mark - Debug Info


/********************* NET DEBUG FUNCTIONS *******************************/
//
// For network tuning and debugging
//

uint32_t Net_GetEstimatedRTT(void)
{
    // Host has 0 RTT to themselves
    if (gIsNetworkHost)
        return 0;
    return gEstimatedRTT;
}

int32_t Net_GetClockOffset(void)
{
    return 0;  // Clock sync removed
}

uint32_t Net_GetAdaptiveRenderDelay(void)
{
    return 0;  // Render delay removed
}

Boolean Net_IsClockSynced(void)
{
    return true;  // Clock sync removed, always "synced"
}

uint32_t Net_GetRTTJitter(void)
{
    return 0;  // Jitter tracking removed
}

uint32_t Net_GetPacketDeliveryPercent(void)
{
    // Return packet delivery rate as percentage (0-100)
    // Based on: (packets received / expected packets) over 2-second window
    // Expected = elapsed_time * NET_TICK_RATE (60Hz)
    // Host always has 100% "delivery" (they are the source)
    if (gIsNetworkHost)
        return 100;
    return gLastPacketDeliveryPct;
}


#pragma mark - Diagnostic Report System


/********************* NET DUMP DIAGNOSTIC REPORT *******************************/
//
// Writes a diagnostic report to network_diag.txt with statistics and raw samples.
//

void Net_DumpDiagnosticReport(void)
{
    FILE* f = fopen("network_diag.txt", "w");
    if (!f)
    {
        printf("[Diag] Error: Could not create network_diag.txt\n");
        return;
    }

    fprintf(f, "=== CroMagRally Network Diagnostic Report ===\n");
    fprintf(f, "Samples: %d (%.1f seconds)\n\n", gDiagCount, gDiagCount / 60.0f);

    // Calculate statistics
    float minFrame = 999, maxFrame = 0, sumFrame = 0;
    float minNet = 999, maxNet = 0, sumNet = 0;
    float maxJump = 0;
    uint32_t minRTT = 9999, maxRTT = 0;
    int rttSpikes = 0;    // RTT > 50ms
    int frameSpikes = 0;  // Frame > 25ms
    int netGaps = 0;      // Net delta > 50ms (missed 2+ packets)
    int bigJumps = 0;     // Position jump > 10 units
    int validFrames = 0;
    int validNet = 0;

    for (int i = 0; i < gDiagCount && i < DIAG_HISTORY_SIZE; i++)
    {
        DiagSample* s = &gDiagHistory[i];

        if (s->frameDeltaMs > 0)
        {
            if (s->frameDeltaMs < minFrame) minFrame = s->frameDeltaMs;
            if (s->frameDeltaMs > maxFrame) maxFrame = s->frameDeltaMs;
            sumFrame += s->frameDeltaMs;
            if (s->frameDeltaMs > 25) frameSpikes++;
            validFrames++;
        }

        if (s->netDeltaMs > 0)
        {
            if (s->netDeltaMs < minNet) minNet = s->netDeltaMs;
            if (s->netDeltaMs > maxNet) maxNet = s->netDeltaMs;
            sumNet += s->netDeltaMs;
            if (s->netDeltaMs > 50) netGaps++;
            validNet++;
        }

        if (s->positionJump > maxJump) maxJump = s->positionJump;
        if (s->positionJump > 10) bigJumps++;

        if (s->rtt < minRTT) minRTT = s->rtt;
        if (s->rtt > maxRTT) maxRTT = s->rtt;
        if (s->rtt > 50) rttSpikes++;
    }

    float avgFrame = validFrames > 0 ? sumFrame / validFrames : 0;
    float avgNet = validNet > 0 ? sumNet / validNet : 0;

    fprintf(f, "FRAME TIMING:\n");
    fprintf(f, "  Min: %.1fms  Max: %.1fms  Avg: %.1fms\n", minFrame, maxFrame, avgFrame);
    fprintf(f, "  Spikes (>25ms): %d (%.1f%%)\n\n",
            frameSpikes, gDiagCount > 0 ? 100.0f * frameSpikes / gDiagCount : 0);

    fprintf(f, "NETWORK TIMING:\n");
    fprintf(f, "  Min: %.1fms  Max: %.1fms  Avg: %.1fms\n", minNet, maxNet, avgNet);
    fprintf(f, "  Gaps (>50ms): %d (%.1f%%)\n\n",
            netGaps, gDiagCount > 0 ? 100.0f * netGaps / gDiagCount : 0);

    fprintf(f, "RTT:\n");
    fprintf(f, "  Min: %ums  Max: %ums\n", minRTT, maxRTT);
    fprintf(f, "  Spikes (>50ms): %d (%.1f%%)\n\n",
            rttSpikes, gDiagCount > 0 ? 100.0f * rttSpikes / gDiagCount : 0);

    fprintf(f, "POSITION:\n");
    fprintf(f, "  Max jump: %.1f units\n", maxJump);
    fprintf(f, "  Big jumps (>10u): %d (%.1f%%)\n\n",
            bigJumps, gDiagCount > 0 ? 100.0f * bigJumps / gDiagCount : 0);

    fprintf(f, "CURRENT STATE:\n");
    fprintf(f, "  RTT: %ums  Packet%%: %u%%\n", gEstimatedRTT, gLastPacketDeliveryPct);

    fprintf(f, "\n=== RAW SAMPLES (last 50) ===\n");
    fprintf(f, "Frame(ms) Net(ms) Jump  RTT Pkt%%\n");
    int start = gDiagCount > 50 ? gDiagCount - 50 : 0;
    for (int i = start; i < gDiagCount && i < DIAG_HISTORY_SIZE; i++)
    {
        DiagSample* s = &gDiagHistory[i];
        fprintf(f, "%6.1f %7.1f %5.1f %4u %3u\n",
                s->frameDeltaMs, s->netDeltaMs, s->positionJump, s->rtt, s->packetPct);
    }

    fclose(f);
    printf("[Diag] Report written to network_diag.txt (%d samples)\n", gDiagCount);
}


/********************* NET START DIAGNOSTICS *******************************/
//
// Starts diagnostic recording.
//

void Net_StartDiagnostics(void)
{
    gDiagIndex = 0;
    gDiagCount = 0;
    gLastNetMessageTime = 0;
    gLastFrameTime = 0;
    gDiagEnabled = true;
    printf("[Diag] Started recording (press F9 to stop and dump report)...\n");
}


/********************* NET STOP DIAGNOSTICS *******************************/
//
// Stops diagnostic recording and dumps the report.
//

void Net_StopDiagnostics(void)
{
    gDiagEnabled = false;
    Net_DumpDiagnosticReport();
}


/********************* NET IS DIAGNOSTICS ENABLED *******************************/
//
// Returns true if diagnostic recording is currently enabled.
//

Boolean Net_IsDiagnosticsEnabled(void)
{
    return gDiagEnabled;
}


#pragma mark - Weapon Synchronization


/********************* NET BROADCAST WEAPON EVENT *******************************/
//
// Called when local player throws/launches a weapon.
// Sends the event to the server which relays it to all other players.
//
void Net_BroadcastWeaponEvent(int weaponType, int playerNum, Boolean throwForward,
                              float posX, float posY, float posZ,
                              float velX, float velY, float velZ, float rotY)
{
    if (!gNetGameInProgress)
        return;

    // Only broadcast our own weapons
    if (playerNum != gMyNetworkPlayerNum)
        return;

    Net_SendWeaponEvent(weaponType, playerNum, throwForward,
                        posX, posY, posZ, velX, velY, velZ, rotY);

    printf("[NET] Broadcasting weapon event: type=%d, player=%d\n", weaponType, playerNum);
}


/********************* ON WEAPON EVENT *******************************/
//
// Callback from Backend_GNS.cpp when WEAPON_EVENT message is received.
// Creates the weapon object for the remote player.
//
static void OnWeaponEvent(const void* weaponEvent)
{
    const NetWeaponEventMsg* msg = (const NetWeaponEventMsg*)weaponEvent;

    // Ignore our own events (shouldn't happen, but safety check)
    if (msg->player_num == gMyNetworkPlayerNum)
        return;

    // Validate player number
    if (msg->player_num >= gNumRealPlayers)
        return;

    printf("[NET] OnWeaponEvent: player=%d, type=%d, forward=%d, pos=(%.0f,%.0f,%.0f)\n",
           msg->player_num, msg->weapon_type, msg->throw_forward,
           msg->pos_x, msg->pos_y, msg->pos_z);

    // Create the appropriate weapon based on type
    // These functions need the player's car object to set up properly
    short playerNum = msg->player_num;
    Boolean throwForward = msg->throw_forward != 0;

    // Call the appropriate weapon creation function
    // Note: These functions use global gCoord/gDelta, so we need to be careful
    switch (msg->weapon_type)
    {
        case 0:  // POW_TYPE_BONE
            ThrowBone(playerNum, throwForward);
            break;

        case 1:  // POW_TYPE_OIL
            ThrowOil(playerNum, throwForward);
            break;

        case 3:  // POW_TYPE_BIRDBOMB
            ThrowBirdBomb(playerNum, throwForward);
            break;

        case 7:  // POW_TYPE_FREEZE
            ThrowFreeze(playerNum, throwForward);
            break;

        // Note: Roman candle, bottle rocket, torpedo, mine are more complex
        // and would need additional work to sync properly
        // For now, we handle the basic throwable weapons

        default:
            printf("[NET] Unknown weapon type %d, ignoring\n", msg->weapon_type);
            break;
    }
}

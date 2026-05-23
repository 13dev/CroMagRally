//
// network.h
//

#pragma once

#include "main.h"
#include "Backend_Network.h"

enum
{
    kStandardMessageSize    = 256,
    kBufferSize             = 200000,
    kQElements              = 200,
    kTimeout                = 0
};

// Async networking constants
#define NET_TICK_RATE           60          // Network updates per second (60 Hz for smooth sync)
#define NET_SEND_INTERVAL_MS    (1000 / NET_TICK_RATE)

// Simple smoothing constant (no snapshot interpolation)


enum
{
    kNetConfigureMessage = 1,
    kNetSyncMessage,
    kNetHostControlInfoMessage,
    kNetClientControlInfoMessage,
    kNetPlayerCharTypeMessage,
    kNetNullPacket,

    // New equal-players model messages
    kNetPlayerStateMessage,             // Each player sends own state to server
    kNetWorldStateMessage               // Server broadcasts all player states
};


        /***************************/
        /* MESSAGE DATA STRUCTURES */
        /***************************/

// Pack network message structs to match wire protocol exactly
#pragma pack(push, 1)

        /* GAME CONFIGURATION MESSAGE */

typedef struct
{
    int32_t             gameMode;                           // game mode (tag, race, etc.)
    int32_t             age;                                // which age to play for race mode
    int32_t             trackNum;                           // which track to play for battle modes
    int32_t             playerNum;                          // this player's index
    int32_t             numPlayers;                         // # players in net game
    int16_t             numAgesCompleted;                   // pass saved game value to clients so we're all the same here
    int16_t             difficulty;                         // pass host's difficulty setting so we're in sync
    int16_t             tagDuration;                        // # minutes in tag game
}NetConfigMessageType;

        /* SYNC MESSAGE */

typedef struct
{
    int32_t             playerNum;                          // this player's index
}NetSyncMessageType;


        /* HOST CONTROL INFO MESSAGE */
        /* Now includes car state for position sync */

typedef struct
{
    // Timestamp for clock sync
    uint32_t            hostTimeMs;                         // Host's local time when snapshot was taken
    uint32_t            echoedClientTime;                   // Echo back client's timestamp for RTT calculation

    float               fps, fpsFrac;
    uint32_t            randomSeed;                         // simply used for error checking (all machines should have same seed!)
    uint32_t            controlBits[MAX_PLAYERS];
    uint32_t            controlBitsNew[MAX_PLAYERS];
    float               analogSteeringX[MAX_PLAYERS];       // X component of analog steering
    float               analogSteeringY[MAX_PLAYERS];       // Y component of analog steering
    uint32_t            frameCounter;

    // Car state for position sync (host-authoritative)
    float               posX[MAX_PLAYERS];
    float               posY[MAX_PLAYERS];
    float               posZ[MAX_PLAYERS];
    float               rotY[MAX_PLAYERS];                  // Car facing direction
    float               velX[MAX_PLAYERS];                  // For interpolation
    float               velY[MAX_PLAYERS];
    float               velZ[MAX_PLAYERS];
    float               steering[MAX_PLAYERS];              // Current steering value

    // Race state sync
    int8_t              lapNum[MAX_PLAYERS];                // Current lap number
    float               currentLapTime[MAX_PLAYERS];        // Current lap time for race timer sync
}NetHostControlInfoMessageType;


        /* CLIENT CONTROL INFO MESSAGE */

typedef struct
{
    short               playerNum;
    uint32_t            clientTimeMs;                       // Client's local time for RTT calculation
    uint32_t            controlBits;
    uint32_t            controlBitsNew;
    uint32_t            frameCounter;
    float               analogSteeringX;                    // X component of analog steering
    float               analogSteeringY;                    // Y component of analog steering
}NetClientControlInfoMessageType;


        /* PLAYER CHAR TYPE MESSAGE */

typedef struct
{
    int16_t             playerNum;
    int16_t             vehicleType;
    int16_t             sex;                                // 0 = male, 1 = female
}NetPlayerCharTypeMessage;


        /*========================================*/
        /* EQUAL PLAYERS MODEL MESSAGES          */
        /* Server collects states, broadcasts all */
        /*========================================*/

        /* PLAYER STATE MESSAGE */
        /* Each player sends their own state to server */

typedef struct
{
    int8_t              playerNum;
    uint32_t            frameCounter;
    uint32_t            controlBits;
    uint32_t            controlBitsNew;
    float               analogSteeringX;
    float               analogSteeringY;
    float               posX, posY, posZ;
    float               rotY;                               // Car facing direction
    float               velX, velY, velZ;
    float               steering;
    int8_t              lapNum;
    float               currentLapTime;
}NetPlayerStateMessage;  // ~58 bytes

        /* WORLD STATE MESSAGE */
        /* Server broadcasts all player states to everyone */

typedef struct
{
    uint32_t            serverTimeMs;
    uint8_t             playerCount;
    NetPlayerStateMessage players[MAX_PLAYERS];
}NetWorldStateMessage;

#pragma pack(pop)



//===============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// Lifecycle
void InitNetworkManager(void);
void EndNetworkGame(void);

// Host/Join setup (room code based)
Boolean SetupNetworkHosting(void);
Boolean SetupNetworkJoinWithRoomCode(const char* roomCode);

// Room state
const char* GetNetworkRoomCode(void);
NetConnectionState GetNetworkState(void);
const char* GetNetworkStatusMessage(void);

// Host gathering
int HostGetGatheredPlayerCount(void);
int HostGetP2PConnectedCount(void);
void HostUpdateGathering(void);
void HostSendGameConfig(void);

// Client waiting
Boolean ClientWaitForConfig(void);

// Level sync
void ClientTellHostLevelIsPrepared(void);
void HostWaitForPlayersToPrepareLevel(void);

// Frame sync (legacy - kept for reference, use NetTick_EqualPlayers instead)

// Vehicle selection
void PlayerBroadcastVehicleType(void);
void GetVehicleSelectionFromNetPlayers(void);

// Misc
void PlayerBroadcastNullPacket(void);
void NetProcessEvents(void);
void SetLocalPlayerName(const char* name);

// Equal-players networking model
void NetTick_EqualPlayers(void);        // All players use this - send own state, receive world state
void SendPlayerState(void);             // Send local player's position/input to server
void ApplyWorldState(void);             // Apply received world state to all players
Boolean NetShouldSendThisFrame(void);   // Returns true if it's time to send network data

// Weapon synchronization
void Net_BroadcastWeaponEvent(int weaponType, int playerNum, Boolean throwForward,
                              float posX, float posY, float posZ,
                              float velX, float velY, float velZ, float rotY);

// Player ready state (for lobby display)
Boolean Net_IsPlayerReady(int playerNum);  // Returns true if player chose vehicle
int Net_GetReadyPlayerCount(void);         // Returns count of ready players

// Debug info (for network tuning)
uint32_t Net_GetEstimatedRTT(void);         // Returns estimated round-trip time in ms
int32_t Net_GetClockOffset(void);           // Returns clock offset (host - local) in ms
uint32_t Net_GetAdaptiveRenderDelay(void);  // Returns current render delay in ms
Boolean Net_IsClockSynced(void);            // Returns true if clock has been synced
uint32_t Net_GetRTTJitter(void);            // Returns RTT jitter (std dev) in ms
uint32_t Net_GetPacketDeliveryPercent(void); // Returns packet delivery rate 0-100%

// Diagnostic report system (F9 to toggle)
void Net_StartDiagnostics(void);            // Start recording diagnostic samples
void Net_StopDiagnostics(void);             // Stop recording and dump report to file
void Net_DumpDiagnosticReport(void);        // Manually dump report without stopping
Boolean Net_IsDiagnosticsEnabled(void);     // Returns true if recording is active

// Error code handling
const char* Net_GetErrorString(int errorCode);  // Get human-readable error string

#ifdef __cplusplus
}
#endif

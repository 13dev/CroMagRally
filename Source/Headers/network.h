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


enum
{
    kNetConfigureMessage = 1,
    kNetSyncMessage,
    kNetHostControlInfoMessage,
    kNetClientControlInfoMessage,
    kNetPlayerCharTypeMessage,
    kNetNullPacket
};


        /***************************/
        /* MESSAGE DATA STRUCTURES */
        /***************************/

        /* GAME CONFIGURATION MESSAGE */

typedef struct
{
    int                 gameMode;                           // game mode (tag, race, etc.)
    int                 age;                                // which age to play for race mode
    int                 trackNum;                           // which track to play for battle modes
    long                playerNum;                          // this player's index
    long                numPlayers;                         // # players in net game
    short               numAgesCompleted;                   // pass saved game value to clients so we're all the same here
    short               difficulty;                         // pass host's difficulty setting so we're in sync
    short               tagDuration;                        // # minutes in tag game
}NetConfigMessageType;

        /* SYNC MESSAGE */

typedef struct
{
    long                playerNum;                          // this player's index
}NetSyncMessageType;


        /* HOST CONTROL INFO MESSAGE */

typedef struct
{
    float               fps, fpsFrac;
    uint32_t            randomSeed;                         // simply used for error checking (all machines should have same seed!)
    uint32_t            controlBits[MAX_PLAYERS];
    uint32_t            controlBitsNew[MAX_PLAYERS];
    float               analogSteeringX[MAX_PLAYERS];       // X component of analog steering
    float               analogSteeringY[MAX_PLAYERS];       // Y component of analog steering
    uint32_t            frameCounter;
}NetHostControlInfoMessageType;


        /* CLIENT CONTROL INFO MESSAGE */

typedef struct
{
    short               playerNum;
    uint32_t            controlBits;
    uint32_t            controlBitsNew;
    uint32_t            frameCounter;
    float               analogSteeringX;                    // X component of analog steering
    float               analogSteeringY;                    // Y component of analog steering
}NetClientControlInfoMessageType;


        /* PLAYER CHAR TYPE MESSAGE */

typedef struct
{
    short               playerNum;
    short               vehicleType;
    short               sex;                                // 0 = male, 1 = female
}NetPlayerCharTypeMessage;



//===============================================================================

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

// Frame sync
void HostSend_ControlInfoToClients(void);
void ClientSend_ControlInfoToHost(void);
void ClientReceive_ControlInfoFromHost(void);
void HostReceive_ControlInfoFromClients(void);

// Vehicle selection
void PlayerBroadcastVehicleType(void);
void GetVehicleSelectionFromNetPlayers(void);

// Misc
void PlayerBroadcastNullPacket(void);
void NetProcessEvents(void);
void SetLocalPlayerName(const char* name);

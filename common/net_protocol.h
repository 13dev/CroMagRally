//==============================================================================
// net_protocol.h
// Shared network protocol definitions for CroMagRally client and server.
// Plain packed C++ structs - no protobuf dependency.
//
// Conventions (C++ Core Guidelines + GNS Style):
// - Enum.3: Use enum class over plain enum
// - Con.5: Use constexpr for compile-time constants
// - NL.10: Prefer underscore_style names
// - C.2: Use struct for POD data with no invariants
// - GNS: Use #pragma pack(push, 1) for wire format structs
//==============================================================================

#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

#ifdef __cplusplus
#include <cstdint>
#include <cstddef>
#include <cstring>
#else
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

//==============================================================================
// CONSTANTS (Con.5: constexpr in C++, #define in C)
//==============================================================================

#ifdef __cplusplus
constexpr int kNetMaxPlayers = 6;
constexpr int kNetRoomCodeLength = 4;
constexpr int kNetPlayerNameLength = 32;
constexpr uint16_t kNetServerPort = 40000;
constexpr int kNetTickRateHz = 60;
#else
#define kNetMaxPlayers 6
#define kNetRoomCodeLength 4
#define kNetPlayerNameLength 32
#define kNetServerPort 40000
#define kNetTickRateHz 60
#endif

//==============================================================================
// PLAYER STATUS (for lobby ready tracking)
//==============================================================================

#ifdef __cplusplus
enum class NetPlayerStatus : uint8_t
{
    disconnected = 0,
    connecting   = 1,
    in_lobby     = 2,   // Connected, waiting to choose vehicle
    ready        = 3,   // Vehicle chosen, ready to play
    loading      = 4,   // Loading level
    in_game      = 5,
    dropped      = 6
};
#else
typedef enum {
    NET_PLAYER_DISCONNECTED = 0,
    NET_PLAYER_CONNECTING   = 1,
    NET_PLAYER_IN_LOBBY     = 2,
    NET_PLAYER_READY        = 3,
    NET_PLAYER_LOADING      = 4,
    NET_PLAYER_IN_GAME      = 5,
    NET_PLAYER_DROPPED      = 6
} NetPlayerStatus;
#endif

//==============================================================================
// ERROR CODES (for structured error handling)
//==============================================================================

#ifdef __cplusplus
enum class NetErrorCode : uint8_t
{
    none = 0,
    timeout_config,         // Timeout waiting for game config
    timeout_sync,           // Timeout waiting for sync
    timeout_vehicle,        // Timeout waiting for vehicle selection
    player_dropped,         // Player disconnected during game
    connection_lost,        // Lost connection to server/host
    invalid_state,          // Invalid state machine transition
    protocol_mismatch,      // Protocol version mismatch
    room_full,              // Room has max players
    room_not_found,         // Room code not found
    invalid_message,        // Malformed message received
    bounds_error            // Array bounds violation prevented
};
#else
typedef enum {
    NET_ERROR_NONE = 0,
    NET_ERROR_TIMEOUT_CONFIG,
    NET_ERROR_TIMEOUT_SYNC,
    NET_ERROR_TIMEOUT_VEHICLE,
    NET_ERROR_PLAYER_DROPPED,
    NET_ERROR_CONNECTION_LOST,
    NET_ERROR_INVALID_STATE,
    NET_ERROR_PROTOCOL_MISMATCH,
    NET_ERROR_ROOM_FULL,
    NET_ERROR_ROOM_NOT_FOUND,
    NET_ERROR_INVALID_MESSAGE,
    NET_ERROR_BOUNDS_ERROR
} NetErrorCode;
#endif

//==============================================================================
// MESSAGE TYPES (Enum.3: enum class in C++, plain enum in C)
//==============================================================================

#ifdef __cplusplus
enum class NetMsgType : uint8_t
{
    // Game messages (100-199)
    config          = 100,
    sync            = 101,
    host_control    = 102,  // Host -> Clients: control state during gameplay
    client_control  = 103,  // Client -> Host: control state during gameplay
    vehicle_type    = 104,
    player_state    = 105,
    world_state     = 106,

    // Keep-alive messages (110-119)
    ping            = 110,  // Server -> Client: keep connection alive
    pong            = 111,  // Client -> Server: response to ping

    // Game events (120-129)
    weapon_event    = 120,  // Player threw/launched a weapon

    // Server messages (200-255)
    room_assignment = 200,
    game_start      = 201,
    player_name     = 202,
    join_request    = 203,
    join_response   = 204
};
#else
// C version - plain enum with NET_MSG_ prefix
enum NetMsgType
{
    NET_MSG_CONFIG          = 100,
    NET_MSG_SYNC            = 101,
    NET_MSG_HOST_CONTROL    = 102,
    NET_MSG_CLIENT_CONTROL  = 103,
    NET_MSG_VEHICLE_TYPE    = 104,
    NET_MSG_PLAYER_STATE    = 105,
    NET_MSG_WORLD_STATE     = 106,
    NET_MSG_PING            = 110,
    NET_MSG_PONG            = 111,
    NET_MSG_WEAPON_EVENT    = 120,
    NET_MSG_ROOM_ASSIGNMENT = 200,
    NET_MSG_GAME_START      = 201,
    NET_MSG_PLAYER_NAME     = 202,
    NET_MSG_JOIN_REQUEST    = 203,
    NET_MSG_JOIN_RESPONSE   = 204
};
#endif

//==============================================================================
// WIRE FORMAT STRUCTS (GNS style: #pragma pack(push, 1))
//==============================================================================

#pragma pack(push, 1)

// Player state - sent by each player to server (62 bytes packed)
typedef struct NetPlayerState
{
    uint8_t     player_num;         // 0-5
    uint32_t    sequence;           // Incrementing counter for ordering
    uint32_t    frame_counter;
    uint32_t    control_bits;
    uint32_t    control_bits_new;
    float       analog_steering_x;
    float       analog_steering_y;
    float       pos_x;
    float       pos_y;
    float       pos_z;
    float       rot_y;
    float       vel_x;
    float       vel_y;
    float       vel_z;
    float       steering;
    int8_t      lap_num;
    float       lap_time_ms;
} NetPlayerState;

// World state - broadcast by server to all players (381 bytes packed)
// 4 + 4 + 1 + (62 * 6) = 9 + 372 = 381
typedef struct NetWorldState
{
    uint32_t        sequence;
    uint32_t        server_time_ms;
    uint8_t         player_count;   // 1-6
    NetPlayerState  players[kNetMaxPlayers];
} NetWorldState;

#ifdef __cplusplus
static_assert(sizeof(NetPlayerState) == 62, "NetPlayerState must be 62 bytes");
static_assert(sizeof(NetWorldState) == 381, "NetWorldState must be 381 bytes");
#endif

// Game config - host to clients at start
typedef struct NetConfigMsg
{
    uint8_t     type;               // NetMsgType::config
    int32_t     game_mode;
    int32_t     age;
    int32_t     track_num;
    int32_t     player_num;
    int32_t     num_players;
    int16_t     num_ages_completed;
    int16_t     difficulty;
    int16_t     tag_duration;
} NetConfigMsg;

// Sync message - level ready acknowledgment
typedef struct NetSyncMsg
{
    uint8_t     type;               // NetMsgType::sync
    int32_t     player_num;
} NetSyncMsg;

// Vehicle type - character selection
typedef struct NetVehicleTypeMsg
{
    uint8_t     type;               // NetMsgType::vehicle_type
    int16_t     player_num;
    int16_t     vehicle_type;
    int16_t     sex;
} NetVehicleTypeMsg;

// Join request - client to server
typedef struct NetJoinRequestMsg
{
    uint8_t     type;               // NetMsgType::join_request
    char        room_code[kNetRoomCodeLength];
    char        player_name[kNetPlayerNameLength];
} NetJoinRequestMsg;

// Join response - server to client
typedef struct NetJoinResponseMsg
{
    uint8_t     type;               // NetMsgType::join_response
    uint8_t     success;
    char        error_msg[64];
} NetJoinResponseMsg;

// Room assignment - server to client
typedef struct NetRoomAssignmentMsg
{
    uint8_t     type;               // NetMsgType::room_assignment
    char        room_code[kNetRoomCodeLength];
    int32_t     player_index;
    int32_t     player_count;
    int32_t     is_host;
} NetRoomAssignmentMsg;

// Game start - host to server, server to clients
typedef struct NetGameStartMsg
{
    uint8_t     type;               // NetMsgType::game_start
    int32_t     player_count;
} NetGameStartMsg;

// Player name broadcast
typedef struct NetPlayerNameMsg
{
    uint8_t     type;               // NetMsgType::player_name
    int32_t     player_index;
    char        name[31];
} NetPlayerNameMsg;

// Ping message - server to client
typedef struct NetPingMsg
{
    uint8_t     type;               // NetMsgType::ping
    uint32_t    server_time_ms;     // Server's timestamp
} NetPingMsg;

// Pong message - client to server
typedef struct NetPongMsg
{
    uint8_t     type;               // NetMsgType::pong
    uint32_t    server_time_ms;     // Echo back server's timestamp
} NetPongMsg;

// Weapon event - player threw/launched a weapon
typedef struct NetWeaponEventMsg
{
    uint8_t     type;               // NetMsgType::weapon_event
    uint8_t     weapon_type;        // POW_TYPE_BONE, POW_TYPE_OIL, etc.
    uint8_t     player_num;         // Who threw/launched it (0-5)
    uint8_t     throw_forward;      // 1 = forward, 0 = backward
    float       pos_x;              // Weapon spawn position
    float       pos_y;
    float       pos_z;
    float       vel_x;              // Weapon velocity
    float       vel_y;
    float       vel_z;
    float       rot_y;              // Car rotation at time of throw
} NetWeaponEventMsg;

#pragma pack(pop)

//==============================================================================
// ENCODE/DECODE HELPERS (inline, no separate .c file needed)
// C++ only - C code should use memcpy directly
//==============================================================================

#ifdef __cplusplus

inline size_t net_encode_player_state(uint8_t* buffer, size_t buf_size,
                                       const NetPlayerState* state)
{
    constexpr size_t msg_size = 1 + sizeof(NetPlayerState);
    if (buf_size < msg_size) return 0;

    buffer[0] = static_cast<uint8_t>(NetMsgType::player_state);
    std::memcpy(buffer + 1, state, sizeof(NetPlayerState));
    return msg_size;
}

inline size_t net_decode_player_state(const uint8_t* buffer, size_t buf_size,
                                       NetPlayerState* out_state)
{
    constexpr size_t msg_size = 1 + sizeof(NetPlayerState);
    if (buf_size < msg_size) return 0;
    if (buffer[0] != static_cast<uint8_t>(NetMsgType::player_state)) return 0;

    std::memcpy(out_state, buffer + 1, sizeof(NetPlayerState));
    return msg_size;
}

inline size_t net_encode_world_state(uint8_t* buffer, size_t buf_size,
                                      const NetWorldState* world)
{
    constexpr size_t msg_size = 1 + sizeof(NetWorldState);
    if (buf_size < msg_size) return 0;

    buffer[0] = static_cast<uint8_t>(NetMsgType::world_state);
    std::memcpy(buffer + 1, world, sizeof(NetWorldState));
    return msg_size;
}

inline size_t net_decode_world_state(const uint8_t* buffer, size_t buf_size,
                                      NetWorldState* out_world)
{
    constexpr size_t msg_size = 1 + sizeof(NetWorldState);
    if (buf_size < msg_size) return 0;
    if (buffer[0] != static_cast<uint8_t>(NetMsgType::world_state)) return 0;

    std::memcpy(out_world, buffer + 1, sizeof(NetWorldState));
    return msg_size;
}

inline NetMsgType net_get_msg_type(const uint8_t* buffer)
{
    return static_cast<NetMsgType>(buffer[0]);
}

#endif // __cplusplus

#ifdef __cplusplus
}
#endif

#endif // NET_PROTOCOL_H

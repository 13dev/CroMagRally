#pragma once

#include <cstdint>
#include <cstring>

// Shared network protocol (plain packed structs, no protobuf)
#include "common/net_protocol.h"

namespace relay {

//==============================================================================
// CONFIGURATION
//==============================================================================

constexpr int kServerPort = 40000;
constexpr int kMaxClients = 64;
constexpr int kMaxRooms = 100;
constexpr int kMaxPlayersPerRoom = 6;
constexpr int kTickRateHz = 120;
constexpr int kTickIntervalUs = 1'000'000 / kTickRateHz;
constexpr int kStatsIntervalSec = 60;

// Room code characters (excludes ambiguous: 0, O, I, 1)
constexpr char kRoomCodeChars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr int kRoomCodeLength = 4;

//==============================================================================
// MESSAGE TYPES
// Must match client-side Protocol (Backend_GNS.cpp)
// Note: Using NetMsgType from common/net_protocol.h, but keeping MsgType
// for backwards compatibility with existing server code.
//==============================================================================

enum class MsgType : uint8_t
{
    // Game messages (100-199)
    CONFIG          = 100,
    SYNC            = 101,
    HOST_CONTROL    = 102,  // Unused - kept for protocol compatibility
    CLIENT_CONTROL  = 103,  // Unused - kept for protocol compatibility
    VEHICLE_TYPE    = 104,
    PLAYER_STATE    = 105,  // Each player sends own state to server
    WORLD_STATE     = 106,  // Server broadcasts all player states

    // Keep-alive messages (110-119)
    PING            = 110,  // Server -> Client: keep connection alive
    PONG            = 111,  // Client -> Server: response to ping

    // Game events (120-129)
    WEAPON_EVENT    = 120,  // Player threw/launched a weapon

    // Server messages (200-255)
    ROOM_ASSIGNMENT = 200,
    GAME_START      = 201,
    PLAYER_NAME     = 202,
    JOIN_REQUEST    = 203,  // Client -> Server: room code + player name
    JOIN_RESPONSE   = 204,  // Server -> Client: success/failure
};

//==============================================================================
// WIRE PROTOCOL MESSAGES
// Server-specific messages that don't need to be in common/
//==============================================================================

#pragma pack(push, 1)

struct RoomAssignmentMsg
{
    uint8_t  type = static_cast<uint8_t>(MsgType::ROOM_ASSIGNMENT);
    char     roomCode[kRoomCodeLength];
    int32_t  playerIndex;
    int32_t  playerCount;
    int32_t  isHost;

    void setRoomCode(const char* code) { std::memcpy(roomCode, code, kRoomCodeLength); }
};

struct GameStartMsg
{
    uint8_t  type = static_cast<uint8_t>(MsgType::GAME_START);
    int32_t  playerCount;
};

struct PlayerNameMsg
{
    uint8_t  type = static_cast<uint8_t>(MsgType::PLAYER_NAME);
    int32_t  playerIndex;
    char     name[31];
};

struct JoinRequestMsg
{
    uint8_t  type = static_cast<uint8_t>(MsgType::JOIN_REQUEST);
    char     roomCode[kRoomCodeLength];  // Empty (all zeros) = create new room (host)
    char     playerName[32];
};

struct JoinResponseMsg
{
    uint8_t  type = static_cast<uint8_t>(MsgType::JOIN_RESPONSE);
    uint8_t  success;
    char     errorMsg[64];
};

#pragma pack(pop)

//==============================================================================
// MESSAGE ROUTING HELPERS
//==============================================================================

inline bool isReliableMessage(MsgType type)
{
    switch (type)
    {
        case MsgType::CONFIG:
        case MsgType::SYNC:
        case MsgType::VEHICLE_TYPE:
        case MsgType::ROOM_ASSIGNMENT:
        case MsgType::GAME_START:
        case MsgType::PLAYER_NAME:
        case MsgType::JOIN_REQUEST:
        case MsgType::JOIN_RESPONSE:
            return true;
        default:
            return false;
    }
}

} // namespace relay

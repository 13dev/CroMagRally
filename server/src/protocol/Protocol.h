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
constexpr int kMaxMessageSize = 4096;       // Maximum allowed message size
constexpr int kPingIntervalMs = 5000;       // Keep-alive ping interval

// Room code characters (excludes ambiguous: 0, O, I, 1)
constexpr char kRoomCodeChars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr int kRoomCodeLength = 4;

//==============================================================================
// MESSAGE TYPES
// Using NetMsgType from common/net_protocol.h as the single source of truth.
// MsgType is a convenience alias with UPPER_CASE values for server code style.
//==============================================================================

// Message type enum - alias to common protocol with server naming convention
enum class MsgType : uint8_t
{
    // Game messages (100-199)
    CONFIG          = static_cast<uint8_t>(NetMsgType::config),
    SYNC            = static_cast<uint8_t>(NetMsgType::sync),
    HOST_CONTROL    = static_cast<uint8_t>(NetMsgType::host_control),
    CLIENT_CONTROL  = static_cast<uint8_t>(NetMsgType::client_control),
    VEHICLE_TYPE    = static_cast<uint8_t>(NetMsgType::vehicle_type),
    PLAYER_STATE    = static_cast<uint8_t>(NetMsgType::player_state),
    WORLD_STATE     = static_cast<uint8_t>(NetMsgType::world_state),

    // Keep-alive messages (110-119)
    PING            = static_cast<uint8_t>(NetMsgType::ping),
    PONG            = static_cast<uint8_t>(NetMsgType::pong),

    // Game events (120-129)
    WEAPON_EVENT    = static_cast<uint8_t>(NetMsgType::weapon_event),

    // Server messages (200-255)
    ROOM_ASSIGNMENT = static_cast<uint8_t>(NetMsgType::room_assignment),
    GAME_START      = static_cast<uint8_t>(NetMsgType::game_start),
    PLAYER_NAME     = static_cast<uint8_t>(NetMsgType::player_name),
    JOIN_REQUEST    = static_cast<uint8_t>(NetMsgType::join_request),
    JOIN_RESPONSE   = static_cast<uint8_t>(NetMsgType::join_response),
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

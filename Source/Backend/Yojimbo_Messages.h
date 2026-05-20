/****************************/
/*   YOJIMBO_MESSAGES.H     */
/* Message definitions for  */
/* Yojimbo UDP networking   */
/****************************/

#pragma once

#ifdef USE_YOJIMBO

#include <yojimbo.h>

// Forward declare network message types from network.h
// We can't include network.h directly here due to C/C++ mixing
#define MAX_PLAYERS 6

namespace CroMag {

//==============================================================================
// CHANNEL CONFIGURATION
//==============================================================================

enum class GameChannel
{
    UNRELIABLE = 0,     // Position updates, inputs (60 Hz)
    RELIABLE = 1,       // Config, sync, vehicle selection
    COUNT = 2
};

//==============================================================================
// MESSAGE TYPES
//==============================================================================

enum class GameMessageType
{
    CONFIG = 0,
    SYNC,
    HOST_CONTROL,
    CLIENT_CONTROL,
    VEHICLE_TYPE,
    COUNT
};

//==============================================================================
// HOST CONTROL MESSAGE (60 Hz, Unreliable)
// Host -> All Clients: Car positions, velocities, and control inputs
//==============================================================================

struct HostControlMessage : public yojimbo::Message
{
    // Timestamp for clock sync
    uint32_t hostTimeMs;          // Host's local time when snapshot was taken
    uint32_t echoedClientTime;    // Echo back client's timestamp for RTT calculation

    // FPS timing
    float fps;
    float fpsFrac;

    // Synchronization
    uint32_t randomSeed;
    uint32_t frameCounter;

    // Control inputs for all players
    uint32_t controlBits[MAX_PLAYERS];
    uint32_t controlBitsNew[MAX_PLAYERS];
    float analogSteeringX[MAX_PLAYERS];
    float analogSteeringY[MAX_PLAYERS];

    // Car state (host-authoritative)
    float posX[MAX_PLAYERS];
    float posY[MAX_PLAYERS];
    float posZ[MAX_PLAYERS];
    float rotY[MAX_PLAYERS];
    float velX[MAX_PLAYERS];
    float velY[MAX_PLAYERS];
    float velZ[MAX_PLAYERS];
    float steering[MAX_PLAYERS];

    // Race state sync
    int8_t lapNum[MAX_PLAYERS];
    float currentLapTime[MAX_PLAYERS];

    HostControlMessage()
        : hostTimeMs(0), echoedClientTime(0),
          fps(0), fpsFrac(0), randomSeed(0), frameCounter(0),
          controlBits{0}, controlBitsNew{0}, analogSteeringX{0}, analogSteeringY{0},
          posX{0}, posY{0}, posZ{0}, rotY{0}, velX{0}, velY{0}, velZ{0}, steering{0},
          lapNum{0}, currentLapTime{0}
    {
    }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bits(stream, hostTimeMs, 32);
        serialize_bits(stream, echoedClientTime, 32);
        serialize_float(stream, fps);
        serialize_float(stream, fpsFrac);
        serialize_bits(stream, randomSeed, 32);
        serialize_bits(stream, frameCounter, 32);

        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            serialize_bits(stream, controlBits[i], 32);
            serialize_bits(stream, controlBitsNew[i], 32);
            serialize_float(stream, analogSteeringX[i]);
            serialize_float(stream, analogSteeringY[i]);
            serialize_float(stream, posX[i]);
            serialize_float(stream, posY[i]);
            serialize_float(stream, posZ[i]);
            serialize_float(stream, rotY[i]);
            serialize_float(stream, velX[i]);
            serialize_float(stream, velY[i]);
            serialize_float(stream, velZ[i]);
            serialize_float(stream, steering[i]);
            serialize_int(stream, lapNum[i], -10, 10);
            serialize_float(stream, currentLapTime[i]);
        }

        return true;
    }

    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS();
};

//==============================================================================
// CLIENT CONTROL MESSAGE (60 Hz, Unreliable)
// Client -> Host: Input controls only
//==============================================================================

struct ClientControlMessage : public yojimbo::Message
{
    int16_t playerNum;
    uint32_t clientTimeMs;        // Client's local time for RTT calculation
    uint32_t frameCounter;
    uint32_t controlBits;
    uint32_t controlBitsNew;
    float analogSteeringX;
    float analogSteeringY;

    ClientControlMessage()
        : playerNum(0), clientTimeMs(0), frameCounter(0), controlBits(0), controlBitsNew(0),
          analogSteeringX(0), analogSteeringY(0)
    {
    }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, playerNum, 0, MAX_PLAYERS);
        serialize_bits(stream, clientTimeMs, 32);
        serialize_bits(stream, frameCounter, 32);
        serialize_bits(stream, controlBits, 32);
        serialize_bits(stream, controlBitsNew, 32);
        serialize_float(stream, analogSteeringX);
        serialize_float(stream, analogSteeringY);
        return true;
    }

    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS();
};

//==============================================================================
// CONFIG MESSAGE (Reliable)
// Host -> Client: Game configuration at start
//==============================================================================

struct ConfigMessage : public yojimbo::Message
{
    int32_t gameMode;
    int32_t age;
    int32_t trackNum;
    int32_t playerNum;
    int32_t numPlayers;
    int16_t numAgesCompleted;
    int16_t difficulty;
    int16_t tagDuration;

    ConfigMessage()
        : gameMode(0), age(0), trackNum(0), playerNum(0), numPlayers(0),
          numAgesCompleted(0), difficulty(0), tagDuration(0)
    {
    }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, gameMode, 0, 16);
        serialize_int(stream, age, 0, 16);
        serialize_int(stream, trackNum, 0, 32);
        serialize_int(stream, playerNum, 0, MAX_PLAYERS);
        serialize_int(stream, numPlayers, 1, MAX_PLAYERS);
        serialize_int(stream, numAgesCompleted, 0, 100);
        serialize_int(stream, difficulty, 0, 4);
        serialize_int(stream, tagDuration, 0, 60);
        return true;
    }

    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS();
};

//==============================================================================
// SYNC MESSAGE (Reliable)
// Bidirectional: Level sync acknowledgment
//==============================================================================

struct SyncMessage : public yojimbo::Message
{
    int32_t playerNum;

    SyncMessage() : playerNum(0) {}

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, playerNum, 0, MAX_PLAYERS);
        return true;
    }

    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS();
};

//==============================================================================
// VEHICLE TYPE MESSAGE (Reliable)
// Broadcast: Player's vehicle selection
//==============================================================================

struct VehicleTypeMessage : public yojimbo::Message
{
    int16_t playerNum;
    int16_t vehicleType;
    int16_t sex;

    VehicleTypeMessage() : playerNum(0), vehicleType(0), sex(0) {}

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, playerNum, 0, MAX_PLAYERS);
        serialize_int(stream, vehicleType, 0, 32);
        serialize_int(stream, sex, 0, 1);
        return true;
    }

    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS();
};

//==============================================================================
// MESSAGE FACTORY
//==============================================================================

YOJIMBO_MESSAGE_FACTORY_START(GameMessageFactory, (int)GameMessageType::COUNT);
    YOJIMBO_DECLARE_MESSAGE_TYPE((int)GameMessageType::CONFIG, ConfigMessage);
    YOJIMBO_DECLARE_MESSAGE_TYPE((int)GameMessageType::SYNC, SyncMessage);
    YOJIMBO_DECLARE_MESSAGE_TYPE((int)GameMessageType::HOST_CONTROL, HostControlMessage);
    YOJIMBO_DECLARE_MESSAGE_TYPE((int)GameMessageType::CLIENT_CONTROL, ClientControlMessage);
    YOJIMBO_DECLARE_MESSAGE_TYPE((int)GameMessageType::VEHICLE_TYPE, VehicleTypeMessage);
YOJIMBO_MESSAGE_FACTORY_FINISH();

//==============================================================================
// ADAPTER
//==============================================================================

class GameAdapter : public yojimbo::Adapter
{
public:
    yojimbo::MessageFactory* CreateMessageFactory(yojimbo::Allocator& allocator) override
    {
        return YOJIMBO_NEW(allocator, GameMessageFactory, allocator);
    }
};

//==============================================================================
// CONNECTION CONFIG
//==============================================================================

struct GameConnectionConfig : public yojimbo::ClientServerConfig
{
    GameConnectionConfig()
    {
        numChannels = (int)GameChannel::COUNT;

        // Channel 0: Unreliable for frequent position/input updates
        channel[(int)GameChannel::UNRELIABLE].type = yojimbo::CHANNEL_TYPE_UNRELIABLE_UNORDERED;
        channel[(int)GameChannel::UNRELIABLE].maxMessagesPerPacket = 64;

        // Channel 1: Reliable for config, sync, vehicle selection
        channel[(int)GameChannel::RELIABLE].type = yojimbo::CHANNEL_TYPE_RELIABLE_ORDERED;
        channel[(int)GameChannel::RELIABLE].maxMessagesPerPacket = 16;

        // Connection settings
        timeout = 10;  // 10 second timeout

        // Disable network simulator in release
#ifndef _DEBUG
        networkSimulator = false;
#endif
    }
};

} // namespace CroMag

#endif // USE_YOJIMBO
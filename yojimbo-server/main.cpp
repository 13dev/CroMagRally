/****************************/
/*    YOJIMBO RELAY SERVER  */
/* UDP relay server for     */
/* CroMagRally multiplayer  */
/****************************/

#include <yojimbo.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <ctime>
#include <vector>
#include <string>
#include <map>

//==============================================================================
// CONFIGURATION
//==============================================================================

static const int kServerPort = 40000;
static const int kMaxClients = 64;
static const int kMaxRooms = 100;
static const int kMaxPlayersPerRoom = 6;
static const uint64_t kProtocolId = 0x436F726D6167ULL;  // "CroMag"

// Same private key as client (in production, use proper key management)
static const uint8_t kPrivateKey[yojimbo::KeyBytes] = {
    0x60, 0x6a, 0xbe, 0x6e, 0xc9, 0x19, 0x10, 0xea,
    0x9a, 0x65, 0x62, 0xf6, 0x6f, 0x2b, 0x30, 0xe4,
    0x43, 0x71, 0xd6, 0x2c, 0xd1, 0x99, 0x27, 0x26,
    0x6b, 0x3c, 0x60, 0xf4, 0xb7, 0x15, 0xab, 0xa1
};

//==============================================================================
// MESSAGE TYPES (Must match client)
//==============================================================================

#define MAX_PLAYERS 6

namespace CroMag {

enum class GameChannel
{
    UNRELIABLE = 0,
    RELIABLE = 1,
    COUNT = 2
};

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
// MESSAGE DEFINITIONS
//==============================================================================

struct HostControlMessage : public yojimbo::Message
{
    float fps, fpsFrac;
    uint32_t randomSeed, frameCounter;
    uint32_t controlBits[MAX_PLAYERS];
    uint32_t controlBitsNew[MAX_PLAYERS];
    float analogSteeringX[MAX_PLAYERS];
    float analogSteeringY[MAX_PLAYERS];
    float posX[MAX_PLAYERS], posY[MAX_PLAYERS], posZ[MAX_PLAYERS];
    float rotY[MAX_PLAYERS];
    float velX[MAX_PLAYERS], velY[MAX_PLAYERS], velZ[MAX_PLAYERS];
    float steering[MAX_PLAYERS];

    HostControlMessage()
        : fps(0), fpsFrac(0), randomSeed(0), frameCounter(0),
          controlBits{0}, controlBitsNew{0}, analogSteeringX{0}, analogSteeringY{0},
          posX{0}, posY{0}, posZ{0}, rotY{0}, velX{0}, velY{0}, velZ{0}, steering{0}
    {}

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
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
        }
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS();
};

struct ClientControlMessage : public yojimbo::Message
{
    int16_t playerNum;
    uint32_t frameCounter;
    uint32_t controlBits, controlBitsNew;
    float analogSteeringX, analogSteeringY;

    ClientControlMessage()
        : playerNum(0), frameCounter(0), controlBits(0), controlBitsNew(0),
          analogSteeringX(0), analogSteeringY(0)
    {}

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, playerNum, 0, MAX_PLAYERS);
        serialize_bits(stream, frameCounter, 32);
        serialize_bits(stream, controlBits, 32);
        serialize_bits(stream, controlBitsNew, 32);
        serialize_float(stream, analogSteeringX);
        serialize_float(stream, analogSteeringY);
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS();
};

struct ConfigMessage : public yojimbo::Message
{
    int32_t gameMode, age, trackNum, playerNum, numPlayers;
    int16_t numAgesCompleted, difficulty, tagDuration;

    ConfigMessage()
        : gameMode(0), age(0), trackNum(0), playerNum(0), numPlayers(0),
          numAgesCompleted(0), difficulty(0), tagDuration(0)
    {}

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

struct VehicleTypeMessage : public yojimbo::Message
{
    int16_t playerNum, vehicleType, sex;
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
// CONNECTION CONFIG
//==============================================================================

struct GameConnectionConfig : public yojimbo::ClientServerConfig
{
    GameConnectionConfig()
    {
        numChannels = (int)GameChannel::COUNT;
        channel[(int)GameChannel::UNRELIABLE].type = yojimbo::CHANNEL_TYPE_UNRELIABLE_UNORDERED;
        channel[(int)GameChannel::UNRELIABLE].maxMessagesPerPacket = 64;
        channel[(int)GameChannel::RELIABLE].type = yojimbo::CHANNEL_TYPE_RELIABLE_ORDERED;
        channel[(int)GameChannel::RELIABLE].maxMessagesPerPacket = 16;
        timeout = 10;
        networkSimulator = false;
    }
};

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

} // namespace CroMag

using namespace CroMag;
using yojimbo::Server;

//==============================================================================
// ROOM MANAGEMENT
//==============================================================================

struct Room
{
    char code[5];
    int hostClientIndex;
    int clientIndices[kMaxPlayersPerRoom];
    int playerCount;
    bool active;
    bool gameStarted;

    Room()
    {
        memset(code, 0, sizeof(code));
        hostClientIndex = -1;
        playerCount = 0;
        active = false;
        gameStarted = false;
        for (int i = 0; i < kMaxPlayersPerRoom; i++)
            clientIndices[i] = -1;
    }
};

static Room gRooms[kMaxRooms];
static int gClientRoom[kMaxClients];  // Maps client index to room index

static void InitRooms()
{
    for (int i = 0; i < kMaxRooms; i++)
        gRooms[i] = Room();
    for (int i = 0; i < kMaxClients; i++)
        gClientRoom[i] = -1;
}

static Room* FindRoomByClient(int clientIndex)
{
    int roomIndex = gClientRoom[clientIndex];
    if (roomIndex >= 0 && roomIndex < kMaxRooms && gRooms[roomIndex].active)
        return &gRooms[roomIndex];
    return nullptr;
}

static Room* CreateRoom(int hostClientIndex)
{
    for (int i = 0; i < kMaxRooms; i++)
    {
        if (!gRooms[i].active)
        {
            Room& room = gRooms[i];
            room.active = true;
            room.hostClientIndex = hostClientIndex;
            room.clientIndices[0] = hostClientIndex;
            room.playerCount = 1;
            room.gameStarted = false;

            // Generate room code
            static const char chars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
            for (int j = 0; j < 4; j++)
                room.code[j] = chars[rand() % (sizeof(chars) - 1)];
            room.code[4] = '\0';

            gClientRoom[hostClientIndex] = i;
            printf("[Room] Created room %s with host client %d\n", room.code, hostClientIndex);
            return &room;
        }
    }
    return nullptr;
}

static int JoinRoom(const char* code, int clientIndex)
{
    for (int i = 0; i < kMaxRooms; i++)
    {
        if (gRooms[i].active && strcmp(gRooms[i].code, code) == 0)
        {
            Room& room = gRooms[i];
            if (room.playerCount >= kMaxPlayersPerRoom)
                return -1;

            for (int j = 0; j < kMaxPlayersPerRoom; j++)
            {
                if (room.clientIndices[j] == -1)
                {
                    room.clientIndices[j] = clientIndex;
                    room.playerCount++;
                    gClientRoom[clientIndex] = i;
                    printf("[Room] Client %d joined room %s (player %d)\n",
                           clientIndex, room.code, j);
                    return j;
                }
            }
        }
    }
    return -1;
}

static void LeaveRoom(int clientIndex)
{
    Room* room = FindRoomByClient(clientIndex);
    if (!room)
        return;

    for (int i = 0; i < kMaxPlayersPerRoom; i++)
    {
        if (room->clientIndices[i] == clientIndex)
        {
            room->clientIndices[i] = -1;
            room->playerCount--;
            printf("[Room] Client %d left room %s (%d players remain)\n",
                   clientIndex, room->code, room->playerCount);
            break;
        }
    }

    gClientRoom[clientIndex] = -1;

    // If host left, destroy room
    if (clientIndex == room->hostClientIndex)
    {
        printf("[Room] Host left, destroying room %s\n", room->code);
        for (int i = 0; i < kMaxPlayersPerRoom; i++)
        {
            if (room->clientIndices[i] >= 0)
                gClientRoom[room->clientIndices[i]] = -1;
        }
        *room = Room();
    }
    else if (room->playerCount <= 0)
    {
        printf("[Room] Room %s now empty, destroying\n", room->code);
        *room = Room();
    }
}

//==============================================================================
// RELAY SERVER
//==============================================================================

class RelayServer
{
public:
    RelayServer(const yojimbo::Address& address)
        : m_time(0.0)
    {
        m_server = YOJIMBO_NEW(
            yojimbo::GetDefaultAllocator(),
            yojimbo::Server,
            yojimbo::GetDefaultAllocator(),
            kPrivateKey,
            address,
            m_config,
            m_adapter,
            m_time
        );

        m_server->Start(kMaxClients);
        printf("[Server] Started on %s:%d (max %d clients)\n",
               address.ToString(m_addressBuffer, sizeof(m_addressBuffer)),
               address.GetPort(), kMaxClients);
    }

    ~RelayServer()
    {
        m_server->Stop();
        YOJIMBO_DELETE(yojimbo::GetDefaultAllocator(), Server, m_server);
    }

    void Update()
    {
        m_server->AdvanceTime(m_time);
        m_server->ReceivePackets();

        // Handle client connections/disconnections
        for (int client = 0; client < kMaxClients; client++)
        {
            if (m_server->IsClientConnected(client))
            {
                if (!m_clientConnected[client])
                {
                    OnClientConnect(client);
                    m_clientConnected[client] = true;
                }
            }
            else if (m_clientConnected[client])
            {
                OnClientDisconnect(client);
                m_clientConnected[client] = false;
            }
        }

        // Process and relay messages
        for (int client = 0; client < kMaxClients; client++)
        {
            if (!m_server->IsClientConnected(client))
                continue;

            for (int ch = 0; ch < (int)GameChannel::COUNT; ch++)
            {
                yojimbo::Message* msg;
                while ((msg = m_server->ReceiveMessage(client, ch)) != nullptr)
                {
                    RelayMessage(client, msg, (GameChannel)ch);
                    m_server->ReleaseMessage(client, msg);
                }
            }
        }

        m_server->SendPackets();
        m_time += 1.0 / 60.0;
    }

    bool IsRunning() const
    {
        return m_server->IsRunning();
    }

private:
    void OnClientConnect(int clientIndex)
    {
        printf("[Server] Client %d connected\n", clientIndex);

        // Auto-create room for first client (host)
        // In a full implementation, you'd have a separate signaling protocol
        if (FindRoomByClient(clientIndex) == nullptr)
        {
            // Check if this is a late joiner - try to assign to existing room
            // For now, just create a new room (host)
            CreateRoom(clientIndex);
        }
    }

    void OnClientDisconnect(int clientIndex)
    {
        printf("[Server] Client %d disconnected\n", clientIndex);
        LeaveRoom(clientIndex);
    }

    void RelayMessage(int fromClient, yojimbo::Message* msg, GameChannel channel)
    {
        Room* room = FindRoomByClient(fromClient);
        if (!room)
            return;

        if (fromClient == room->hostClientIndex)
        {
            // Host -> broadcast to all other clients
            for (int i = 0; i < kMaxPlayersPerRoom; i++)
            {
                int dest = room->clientIndices[i];
                if (dest >= 0 && dest != fromClient && m_server->IsClientConnected(dest))
                {
                    auto* clone = CloneMessage(dest, msg);
                    if (clone)
                        m_server->SendMessage(dest, (int)channel, clone);
                }
            }
        }
        else
        {
            // Client -> host only
            if (m_server->IsClientConnected(room->hostClientIndex))
            {
                auto* clone = CloneMessage(room->hostClientIndex, msg);
                if (clone)
                    m_server->SendMessage(room->hostClientIndex, (int)channel, clone);
            }
        }
    }

    yojimbo::Message* CloneMessage(int clientIndex, yojimbo::Message* src)
    {
        // Create a new message of the same type and copy the data
        yojimbo::Message* dest = m_server->CreateMessage(clientIndex, src->GetType());
        if (!dest)
            return nullptr;

        // Copy message data based on type (can't use assignment - yojimbo::Message has private operator=)
        switch (src->GetType())
        {
            case (int)GameMessageType::HOST_CONTROL:
            {
                auto* s = (HostControlMessage*)src;
                auto* d = (HostControlMessage*)dest;
                d->fps = s->fps;
                d->fpsFrac = s->fpsFrac;
                d->randomSeed = s->randomSeed;
                d->frameCounter = s->frameCounter;
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    d->controlBits[i] = s->controlBits[i];
                    d->controlBitsNew[i] = s->controlBitsNew[i];
                    d->analogSteeringX[i] = s->analogSteeringX[i];
                    d->analogSteeringY[i] = s->analogSteeringY[i];
                    d->posX[i] = s->posX[i];
                    d->posY[i] = s->posY[i];
                    d->posZ[i] = s->posZ[i];
                    d->rotY[i] = s->rotY[i];
                    d->velX[i] = s->velX[i];
                    d->velY[i] = s->velY[i];
                    d->velZ[i] = s->velZ[i];
                    d->steering[i] = s->steering[i];
                }
                break;
            }
            case (int)GameMessageType::CLIENT_CONTROL:
            {
                auto* s = (ClientControlMessage*)src;
                auto* d = (ClientControlMessage*)dest;
                d->playerNum = s->playerNum;
                d->frameCounter = s->frameCounter;
                d->controlBits = s->controlBits;
                d->controlBitsNew = s->controlBitsNew;
                d->analogSteeringX = s->analogSteeringX;
                d->analogSteeringY = s->analogSteeringY;
                break;
            }
            case (int)GameMessageType::CONFIG:
            {
                auto* s = (ConfigMessage*)src;
                auto* d = (ConfigMessage*)dest;
                d->gameMode = s->gameMode;
                d->age = s->age;
                d->trackNum = s->trackNum;
                d->playerNum = s->playerNum;
                d->numPlayers = s->numPlayers;
                d->numAgesCompleted = s->numAgesCompleted;
                d->difficulty = s->difficulty;
                d->tagDuration = s->tagDuration;
                break;
            }
            case (int)GameMessageType::SYNC:
            {
                auto* s = (SyncMessage*)src;
                auto* d = (SyncMessage*)dest;
                d->playerNum = s->playerNum;
                break;
            }
            case (int)GameMessageType::VEHICLE_TYPE:
            {
                auto* s = (VehicleTypeMessage*)src;
                auto* d = (VehicleTypeMessage*)dest;
                d->playerNum = s->playerNum;
                d->vehicleType = s->vehicleType;
                d->sex = s->sex;
                break;
            }
        }

        return dest;
    }

    GameConnectionConfig m_config;
    GameAdapter m_adapter;
    yojimbo::Server* m_server;
    double m_time;
    bool m_clientConnected[kMaxClients] = {false};
    char m_addressBuffer[256];
};

//==============================================================================
// MAIN
//==============================================================================

static bool gRunning = true;

void SignalHandler(int signal)
{
    (void)signal;
    gRunning = false;
    printf("\n[Server] Shutting down...\n");
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    printf("CroMagRally Yojimbo Relay Server\n");
    printf("================================\n");

    // Initialize random seed
    srand((unsigned int)time(nullptr));

    // Set up signal handler
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // Initialize Yojimbo
    if (!InitializeYojimbo())
    {
        printf("[Server] Failed to initialize Yojimbo\n");
        return 1;
    }

    // Initialize rooms
    InitRooms();

    // Get port from environment (for Fly.io)
    int port = kServerPort;
    const char* portEnv = getenv("PORT");
    if (portEnv)
        port = atoi(portEnv);

    // Create server
    yojimbo::Address address("0.0.0.0", port);
    RelayServer server(address);

    // Main loop
    printf("[Server] Running...\n");
    while (gRunning && server.IsRunning())
    {
        server.Update();
        yojimbo_sleep(1.0 / 60.0);
    }

    // Cleanup
    ShutdownYojimbo();
    printf("[Server] Shutdown complete\n");

    return 0;
}

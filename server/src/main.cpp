/**
 * CroMagRally Relay Server
 *
 * UDP relay server using GameNetworkingSockets.
 * Handles room matchmaking and message routing between game clients.
 */

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>

#include "relay/Server.h"
#include "protocol/Protocol.h"

namespace {

volatile bool g_running = true;

void signalHandler(int)
{
    g_running = false;
}

void debugOutput(ESteamNetworkingSocketsDebugOutputType type, const char* msg)
{
    const char* prefix = "";
    switch (type)
    {
        case k_ESteamNetworkingSocketsDebugOutputType_Error:   prefix = "ERROR"; break;
        case k_ESteamNetworkingSocketsDebugOutputType_Warning: prefix = "WARN";  break;
        default: return; // Only log errors and warnings
    }
    std::printf("[GNS %s] %s\n", prefix, msg);
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    // Disable stdout buffering for container logging
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::printf("CroMagRally Relay Server\n");
    std::printf("========================\n");
    std::printf("[Server] Build: %s %s\n", __DATE__, __TIME__);
    std::printf("[Server] System time: %lld\n", static_cast<long long>(std::time(nullptr)));

    // Initialize RNG
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    // Signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Initialize GameNetworkingSockets
    SteamDatagramErrMsg errMsg;
    if (!GameNetworkingSockets_Init(nullptr, errMsg))
    {
        std::printf("[Server] Failed to init GNS: %s\n", errMsg);
        return 1;
    }

    SteamNetworkingUtils()->SetDebugOutputFunction(
        k_ESteamNetworkingSocketsDebugOutputType_Warning,
        debugOutput);

    // Get port from environment
    int port = relay::kServerPort;
    if (const char* portEnv = std::getenv("PORT"))
    {
        port = std::atoi(portEnv);
    }

    // Start server
    relay::Server server;
    if (!server.start(static_cast<uint16_t>(port)))
    {
        GameNetworkingSockets_Kill();
        return 1;
    }

    std::printf("[Server] Running at %dHz tick rate\n", relay::kTickRateHz);

    // Main loop
    auto lastStats = std::chrono::steady_clock::now();

    while (g_running)
    {
        server.update();

        // Log stats periodically
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastStats).count();
        if (elapsed >= relay::kStatsIntervalSec)
        {
            auto stats = server.getStats();
            std::printf("[Server] Status: %d rooms, %d players\n",
                        stats.activeRooms, stats.totalPlayers);
            lastStats = now;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(relay::kTickIntervalUs));
    }

    std::printf("\n[Server] Shutting down...\n");
    server.stop();
    GameNetworkingSockets_Kill();
    std::printf("[Server] Shutdown complete\n");

    return 0;
}

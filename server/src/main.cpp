/**
 * CroMagRally Relay Server
 *
 * UDP relay server using GameNetworkingSockets.
 * Handles room matchmaking and message routing between game clients.
 */

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>

#include "relay/Server.h"
#include "protocol/Protocol.h"
#include "common/log.h"

namespace {

volatile bool g_running = true;

void signalHandler(int)
{
    g_running = false;
}

void debugOutput(ESteamNetworkingSocketsDebugOutputType type, const char* msg)
{
    switch (type)
    {
        case k_ESteamNetworkingSocketsDebugOutputType_Error:
            LOG_GNS_ERROR("{}", msg);
            break;
        case k_ESteamNetworkingSocketsDebugOutputType_Warning:
            LOG_GNS_WARN("{}", msg);
            break;
        default:
            // Only log errors and warnings
            break;
    }
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    // Disable stdout buffering for container logging
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // Initialize logging
    cromag::log::init();

    LOG_SERVER_INFO("CroMagRally Relay Server");
    LOG_SERVER_INFO("========================");
    LOG_SERVER_INFO("Build: {} {}", __DATE__, __TIME__);
    LOG_SERVER_INFO("System time: {}", static_cast<long long>(std::time(nullptr)));

    // Initialize RNG
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    // Signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Initialize GameNetworkingSockets
    SteamDatagramErrMsg errMsg;
    if (!GameNetworkingSockets_Init(nullptr, errMsg))
    {
        LOG_SERVER_ERROR("Failed to init GNS: {}", errMsg);
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

    LOG_SERVER_INFO("Running at {}Hz tick rate", relay::kTickRateHz);

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
            LOG_SERVER_INFO("Status: {} rooms, {} players", stats.activeRooms, stats.totalPlayers);
            lastStats = now;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(relay::kTickIntervalUs));
    }

    LOG_SERVER_INFO("Shutting down...");
    server.stop();
    GameNetworkingSockets_Kill();
    LOG_SERVER_INFO("Shutdown complete");

    return 0;
}

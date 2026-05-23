//==============================================================================
// log.h
// Structured logging for CroMagRally using spdlog.
//
// Usage:
//   LOG_NET_INFO("Player {} synced", player_num);
//   LOG_GNS_WARN("Connection timeout: {}", error_msg);
//   LOG_SERVER_ERROR("Failed to bind: {}", port);
//   LOG_ROOM_INFO("{} joined room {}", conn, room_code);
//
// Initialize at startup:
//   cromag::log::init();
//
// Conventions (C++ Core Guidelines NL.10):
// - Named loggers for subsystems: NET, GNS, SERVER, ROOM
// - Lazy evaluation (no overhead when level disabled)
// - Thread-safe by default
// - fmt-style {} placeholders
//==============================================================================

#ifndef CROMAG_LOG_H
#define CROMAG_LOG_H

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace cromag::log {

//------------------------------------------------------------------------------
// Named loggers for each subsystem
//------------------------------------------------------------------------------

inline spdlog::logger& net()
{
    static auto logger = spdlog::stdout_color_mt("NET");
    return *logger;
}

inline spdlog::logger& gns()
{
    static auto logger = spdlog::stdout_color_mt("GNS");
    return *logger;
}

inline spdlog::logger& server()
{
    static auto logger = spdlog::stdout_color_mt("SERVER");
    return *logger;
}

inline spdlog::logger& room()
{
    static auto logger = spdlog::stdout_color_mt("ROOM");
    return *logger;
}

//------------------------------------------------------------------------------
// Initialization
//------------------------------------------------------------------------------

inline void init()
{
    // Pattern: [HH:MM:SS.mmm] [LOGGER] [LEVEL] message
    spdlog::set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::info);

    // Pre-create all loggers to avoid race conditions
    net();
    gns();
    server();
    room();
}

inline void set_level(spdlog::level::level_enum level)
{
    spdlog::set_level(level);
}

inline void enable_debug()
{
    spdlog::set_level(spdlog::level::debug);
}

} // namespace cromag::log

//==============================================================================
// Convenience macros - wrap logger calls with lazy evaluation
//==============================================================================

// NET - Game-level network layer (network.cpp)
#define LOG_NET_TRACE(...)   SPDLOG_LOGGER_TRACE(&cromag::log::net(), __VA_ARGS__)
#define LOG_NET_DEBUG(...)   SPDLOG_LOGGER_DEBUG(&cromag::log::net(), __VA_ARGS__)
#define LOG_NET_INFO(...)    SPDLOG_LOGGER_INFO(&cromag::log::net(), __VA_ARGS__)
#define LOG_NET_WARN(...)    SPDLOG_LOGGER_WARN(&cromag::log::net(), __VA_ARGS__)
#define LOG_NET_ERROR(...)   SPDLOG_LOGGER_ERROR(&cromag::log::net(), __VA_ARGS__)

// GNS - GameNetworkingSockets backend layer (Backend_GNS.cpp)
#define LOG_GNS_TRACE(...)   SPDLOG_LOGGER_TRACE(&cromag::log::gns(), __VA_ARGS__)
#define LOG_GNS_DEBUG(...)   SPDLOG_LOGGER_DEBUG(&cromag::log::gns(), __VA_ARGS__)
#define LOG_GNS_INFO(...)    SPDLOG_LOGGER_INFO(&cromag::log::gns(), __VA_ARGS__)
#define LOG_GNS_WARN(...)    SPDLOG_LOGGER_WARN(&cromag::log::gns(), __VA_ARGS__)
#define LOG_GNS_ERROR(...)   SPDLOG_LOGGER_ERROR(&cromag::log::gns(), __VA_ARGS__)

// SERVER - Relay server main/connection handling (main.cpp, Server.cpp)
#define LOG_SERVER_TRACE(...) SPDLOG_LOGGER_TRACE(&cromag::log::server(), __VA_ARGS__)
#define LOG_SERVER_DEBUG(...) SPDLOG_LOGGER_DEBUG(&cromag::log::server(), __VA_ARGS__)
#define LOG_SERVER_INFO(...)  SPDLOG_LOGGER_INFO(&cromag::log::server(), __VA_ARGS__)
#define LOG_SERVER_WARN(...)  SPDLOG_LOGGER_WARN(&cromag::log::server(), __VA_ARGS__)
#define LOG_SERVER_ERROR(...) SPDLOG_LOGGER_ERROR(&cromag::log::server(), __VA_ARGS__)

// ROOM - Room/player state management (Room.cpp)
#define LOG_ROOM_TRACE(...)  SPDLOG_LOGGER_TRACE(&cromag::log::room(), __VA_ARGS__)
#define LOG_ROOM_DEBUG(...)  SPDLOG_LOGGER_DEBUG(&cromag::log::room(), __VA_ARGS__)
#define LOG_ROOM_INFO(...)   SPDLOG_LOGGER_INFO(&cromag::log::room(), __VA_ARGS__)
#define LOG_ROOM_WARN(...)   SPDLOG_LOGGER_WARN(&cromag::log::room(), __VA_ARGS__)
#define LOG_ROOM_ERROR(...)  SPDLOG_LOGGER_ERROR(&cromag::log::room(), __VA_ARGS__)

#endif // CROMAG_LOG_H

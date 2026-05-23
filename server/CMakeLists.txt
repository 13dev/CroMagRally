cmake_minimum_required(VERSION 3.21)

project(relay-server
    VERSION 1.0.0
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#------------------------------------------------------------------------------
# FETCH GAMENETWORKINGSOCKETS
#------------------------------------------------------------------------------

include(FetchContent)

FetchContent_Declare(
    GameNetworkingSockets
    URL https://github.com/ValveSoftware/GameNetworkingSockets/archive/refs/tags/v1.5.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(GAMENETWORKINGSOCKETS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GAMENETWORKINGSOCKETS_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(GameNetworkingSockets)

#------------------------------------------------------------------------------
# SERVER EXECUTABLE
#------------------------------------------------------------------------------

set(SERVER_SOURCES
    src/main.cpp
    src/relay/Server.cpp
    src/relay/Room.cpp
)

add_executable(${PROJECT_NAME} ${SERVER_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${gamenetworkingsockets_SOURCE_DIR}/include
)

# Note: STEAMNETWORKINGSOCKETS_STANDALONELIB is defined by GNS v1.5+ headers

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Try to find spdlog via vcpkg/system, fallback to FetchContent for Docker builds
find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
    message(STATUS "spdlog not found via find_package, fetching from GitHub...")
    FetchContent_Declare(
        spdlog
        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(spdlog)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    GameNetworkingSockets
    Threads::Threads
    spdlog::spdlog
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

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
    URL https://github.com/ValveSoftware/GameNetworkingSockets/archive/refs/tags/v1.4.1.tar.gz
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

target_compile_definitions(${PROJECT_NAME} PRIVATE
    STEAMNETWORKINGSOCKETS_STANDALONELIB
)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    GameNetworkingSockets
    Threads::Threads
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

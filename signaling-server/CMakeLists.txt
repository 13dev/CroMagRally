cmake_minimum_required(VERSION 3.16)

project(CroMagRally-SignalingServer
    VERSION 1.0.0
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)

#------------------------------------------------------------------------------
# EXECUTABLE TARGET
#------------------------------------------------------------------------------

add_executable(signaling-server
    main.c
    room.c
    room.h
    signaling.c
    signaling.h
)

#------------------------------------------------------------------------------
# PLATFORM-SPECIFIC
#------------------------------------------------------------------------------

if(WIN32)
    target_link_libraries(signaling-server PRIVATE ws2_32)
    target_compile_definitions(signaling-server PRIVATE
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
endif()

if(NOT MSVC)
    target_compile_options(signaling-server PRIVATE
        -Wall
        -Wextra
        -Werror=return-type
    )
endif()

#------------------------------------------------------------------------------
# INSTALL
#------------------------------------------------------------------------------

install(TARGETS signaling-server
    RUNTIME DESTINATION bin
)

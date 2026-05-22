# FindSDL3.cmake
# This module is used when SDL3 is built via FetchContent
# It short-circuits to use the already-available target

if(TARGET SDL3::SDL3 OR TARGET SDL3::SDL3-static)
    set(SDL3_FOUND TRUE)

    # Create SDL3::SDL3 alias if only SDL3-static exists
    if(NOT TARGET SDL3::SDL3 AND TARGET SDL3::SDL3-static)
        add_library(SDL3::SDL3 ALIAS SDL3-static)
    endif()

    message(STATUS "FindSDL3: Using existing SDL3 target from FetchContent")
else()
    # Fall back to normal config-mode search
    find_package(SDL3 CONFIG)
endif()

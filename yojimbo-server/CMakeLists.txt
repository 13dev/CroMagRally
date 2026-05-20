cmake_minimum_required(VERSION 3.21)

project(yojimbo-server
    VERSION 1.0.0
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 17)

#------------------------------------------------------------------------------
# FETCH YOJIMBO AND DEPENDENCIES
#------------------------------------------------------------------------------

include(FetchContent)

FetchContent_Declare(yojimbo
    GIT_REPOSITORY https://github.com/mas-bandwidth/yojimbo.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)
FetchContent_Declare(netcode
    GIT_REPOSITORY https://github.com/mas-bandwidth/netcode.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)
FetchContent_Declare(reliable
    GIT_REPOSITORY https://github.com/mas-bandwidth/reliable.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)
FetchContent_Declare(tlsf
    GIT_REPOSITORY https://github.com/mattconte/tlsf.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(yojimbo netcode reliable tlsf)

#------------------------------------------------------------------------------
# FIND LIBSODIUM
#------------------------------------------------------------------------------

find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)

#------------------------------------------------------------------------------
# BUILD TLSF AS STATIC LIBRARY
#------------------------------------------------------------------------------

add_library(tlsf_lib STATIC ${tlsf_SOURCE_DIR}/tlsf.c)
target_include_directories(tlsf_lib PUBLIC ${tlsf_SOURCE_DIR})

#------------------------------------------------------------------------------
# BUILD YOJIMBO AS STATIC LIBRARY
#------------------------------------------------------------------------------

# Create a directory structure that matches yojimbo's expected include paths
set(YOJIMBO_TLSF_INCLUDE_DIR "${CMAKE_BINARY_DIR}/yojimbo-deps-include")
file(MAKE_DIRECTORY "${YOJIMBO_TLSF_INCLUDE_DIR}/tlsf")
file(CREATE_LINK "${tlsf_SOURCE_DIR}/tlsf.h" "${YOJIMBO_TLSF_INCLUDE_DIR}/tlsf/tlsf.h" COPY_ON_ERROR SYMBOLIC)
file(CREATE_LINK "${tlsf_SOURCE_DIR}/tlsf.c" "${YOJIMBO_TLSF_INCLUDE_DIR}/tlsf/tlsf.c" COPY_ON_ERROR SYMBOLIC)

file(GLOB YOJIMBO_SOURCES ${yojimbo_SOURCE_DIR}/source/*.cpp)

add_library(yojimbo_lib STATIC ${YOJIMBO_SOURCES})
target_include_directories(yojimbo_lib PUBLIC
    ${yojimbo_SOURCE_DIR}/include
    ${yojimbo_SOURCE_DIR}/netcode
    ${yojimbo_SOURCE_DIR}/reliable
    ${yojimbo_SOURCE_DIR}/serialize
    ${netcode_SOURCE_DIR}
    ${reliable_SOURCE_DIR}
    ${YOJIMBO_TLSF_INCLUDE_DIR}
    ${SODIUM_INCLUDE_DIRS}
)
target_compile_definitions(yojimbo_lib PRIVATE
    NETCODE_ENABLE_TESTS=0
    RELIABLE_ENABLE_TESTS=0
)
target_link_libraries(yojimbo_lib PUBLIC ${SODIUM_LIBRARIES} tlsf_lib)

#------------------------------------------------------------------------------
# BUILD NETCODE AS STATIC LIBRARY
#------------------------------------------------------------------------------

add_library(netcode_lib STATIC ${netcode_SOURCE_DIR}/netcode.c)
target_include_directories(netcode_lib PUBLIC ${netcode_SOURCE_DIR} ${SODIUM_INCLUDE_DIRS})
target_compile_definitions(netcode_lib PRIVATE NETCODE_ENABLE_TESTS=0)
target_link_libraries(netcode_lib PUBLIC ${SODIUM_LIBRARIES})

#------------------------------------------------------------------------------
# BUILD RELIABLE AS STATIC LIBRARY
#------------------------------------------------------------------------------

add_library(reliable_lib STATIC ${reliable_SOURCE_DIR}/reliable.c)
target_include_directories(reliable_lib PUBLIC ${reliable_SOURCE_DIR})
target_compile_definitions(reliable_lib PRIVATE RELIABLE_ENABLE_TESTS=0)

#------------------------------------------------------------------------------
# BUILD SERVER EXECUTABLE
#------------------------------------------------------------------------------

add_executable(${PROJECT_NAME} main.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${yojimbo_SOURCE_DIR}/include
    ${netcode_SOURCE_DIR}
    ${reliable_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    yojimbo_lib
    netcode_lib
    reliable_lib
)

# Linux-specific
if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# Install
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

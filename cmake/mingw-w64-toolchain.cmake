# MinGW-w64 toolchain for cross-compiling Windows binaries from Linux
# Works on both Fedora (dnf) and Ubuntu (apt) with mingw-w64

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Find the MinGW compiler (works for both Fedora and Ubuntu)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Standard MinGW sysroot paths
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)

# Fedora-specific: Add mingw64 package paths
# Fedora uses /usr/x86_64-w64-mingw32/sys-root/mingw for libraries
if(EXISTS /usr/x86_64-w64-mingw32/sys-root/mingw)
    list(APPEND CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32/sys-root/mingw)
    # OpenSSL and Protobuf from Fedora mingw64-* packages
    set(OPENSSL_ROOT_DIR /usr/x86_64-w64-mingw32/sys-root/mingw CACHE PATH "OpenSSL root")
endif()

# CI/vcpkg: If MINGW_VCPKG_ROOT is set, add it to search paths
if(DEFINED ENV{MINGW_VCPKG_ROOT})
    list(APPEND CMAKE_FIND_ROOT_PATH $ENV{MINGW_VCPKG_ROOT})
    list(APPEND CMAKE_PREFIX_PATH $ENV{MINGW_VCPKG_ROOT})
    set(OPENSSL_ROOT_DIR $ENV{MINGW_VCPKG_ROOT} CACHE PATH "OpenSSL root")
endif()

# Search behavior for cross-compilation
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)  # Use host programs (cmake, ninja, protoc)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)   # Only use target libraries
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)   # Only use target headers
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)   # Only use target packages

# Static linking for standalone executables
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")

# Ensure .exe extension
set(CMAKE_EXECUTABLE_SUFFIX ".exe")

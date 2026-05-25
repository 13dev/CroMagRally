#!/bin/bash
# Cro-Mag Rally Build Script (Meson)
# Usage: ./build.sh [release|debug|debug-sanitize] [run|clean|deps]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_TYPE="${1:-debug}"
ACTION="${2:-}"

# Show dependency information
show_deps() {
    echo "=== Required Dependencies ==="
    echo ""
    echo "The following libraries must be installed on your system:"
    echo "  - SDL3 (development files)"
    echo "  - spdlog (development files)"
    echo "  - GameNetworkingSockets (development files)"
    echo "  - OpenGL (usually pre-installed)"
    echo ""
    echo "=== Installation Commands ==="
    echo ""
    echo "Fedora/RHEL:"
    echo "  sudo dnf install meson ninja-build SDL3-devel spdlog-devel"
    echo "  # GameNetworkingSockets: build from source"
    echo ""
    echo "Ubuntu/Debian:"
    echo "  sudo apt install meson ninja-build libspdlog-dev"
    echo "  # SDL3: Use PPA or build from source"
    echo "  # GameNetworkingSockets: build from source"
    echo ""
    echo "Arch Linux:"
    echo "  sudo pacman -S meson ninja sdl3 spdlog"
    echo "  # GameNetworkingSockets: AUR or build from source"
    echo ""
    echo "macOS (Homebrew):"
    echo "  brew install meson ninja sdl3 spdlog"
    echo "  # GameNetworkingSockets: build from source"
    echo ""
    exit 0
}

# Handle deps action
if [ "$ACTION" = "deps" ] || [ "$BUILD_TYPE" = "deps" ]; then
    show_deps
fi

case "$BUILD_TYPE" in
    release)
        BUILD_DIR="build-release"
        MESON_ARGS="-Dbuildtype=release"
        ;;
    debug)
        BUILD_DIR="build-debug"
        MESON_ARGS="-Dbuildtype=debug"
        ;;
    debug-sanitize)
        BUILD_DIR="build-debug-sanitize"
        MESON_ARGS="-Dbuildtype=debug -Dsanitize=true"
        ;;
    *)
        echo "Cro-Mag Rally Build Script"
        echo ""
        echo "Usage: $0 [preset] [action]"
        echo ""
        echo "Presets:"
        echo "  debug          - Debug build (default)"
        echo "  release        - Release build"
        echo "  debug-sanitize - Debug with ASAN/UBSAN"
        echo ""
        echo "Actions:"
        echo "  (none) - Build only"
        echo "  run    - Build and run the game"
        echo "  clean  - Remove build directory"
        echo "  deps   - Show required dependencies"
        echo ""
        echo "Examples:"
        echo "  $0              # Build debug"
        echo "  $0 debug run    # Build and run debug"
        echo "  $0 release      # Build release"
        echo "  $0 deps         # Show dependencies"
        exit 1
        ;;
esac

# Handle clean action
if [ "$ACTION" = "clean" ]; then
    echo "Cleaning $BUILD_DIR..."
    rm -rf "$BUILD_DIR"
    exit 0
fi

# Auto-install deps with Conan if conanfile.py exists
if [ -f "conanfile.py" ] && command -v conan &> /dev/null; then
    case "$BUILD_TYPE" in
        release) CONAN_BUILD_TYPE="Release" ;;
        *) CONAN_BUILD_TYPE="Debug" ;;
    esac

    if [ ! -f "$BUILD_DIR/conan_meson_native.ini" ]; then
        echo "Installing dependencies with Conan..."
        conan install . --output-folder="$BUILD_DIR" \
            --build=missing \
            -s build_type="$CONAN_BUILD_TYPE" \
            -s compiler.cppstd=gnu20 \
            -s compiler.version=14
    fi
fi

# Configure if needed (check for build.ninja to detect if meson has run)
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    echo "Configuring $BUILD_TYPE build..."
    NATIVE_FILE=""
    if [ -f "$BUILD_DIR/conan_meson_native.ini" ]; then
        NATIVE_FILE="--native-file $BUILD_DIR/conan_meson_native.ini"
    fi
    if ! meson setup $MESON_ARGS $NATIVE_FILE "$BUILD_DIR"; then
        echo ""
        echo "=== Configuration Failed ==="
        echo "Run '$0 deps' to see required dependencies."
        exit 1
    fi
fi

# Build
echo "Building $BUILD_TYPE..."
meson compile -C "$BUILD_DIR"

# Symlink Data directory to build output (for development)
if [ ! -L "$BUILD_DIR/Data" ] && [ ! -d "$BUILD_DIR/Data" ]; then
    echo "Symlinking Data directory..."
    ln -sf "$SCRIPT_DIR/Data" "$BUILD_DIR/Data"
fi

# Run if requested
if [ "$ACTION" = "run" ]; then
    echo "Running CroMagRally..."
    cd "$BUILD_DIR"
    ./CroMagRally
fi

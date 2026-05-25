#!/bin/bash
# Cro-Mag Rally Relay Server Build Script (Meson)
# Usage: ./build.sh [release|debug] [run|clean|docker|deploy|logs|ssh]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_TYPE="${1:-debug}"
ACTION="${2:-}"

case "$BUILD_TYPE" in
    release)
        BUILD_DIR="build-release"
        MESON_ARGS="-Dbuildtype=release"
        ;;
    debug)
        BUILD_DIR="build-debug"
        MESON_ARGS="-Dbuildtype=debug"
        ;;
    *)
        echo "Cro-Mag Rally Relay Server Build Script"
        echo ""
        echo "Usage: $0 [preset] [action]"
        echo ""
        echo "Presets:"
        echo "  debug   - Debug build (default)"
        echo "  release - Release build"
        echo ""
        echo "Actions:"
        echo "  (none)  - Build only"
        echo "  run     - Build and run server"
        echo "  clean   - Remove build directory"
        echo "  docker  - Build Docker image"
        echo "  deploy  - Deploy to Fly.io"
        echo "  logs    - View Fly.io logs"
        echo "  ssh     - SSH into Fly.io instance"
        echo ""
        echo "Examples:"
        echo "  $0                  # Build debug"
        echo "  $0 debug run        # Build and run"
        echo "  $0 release          # Build release"
        echo "  $0 release deploy   # Deploy to Fly.io"
        exit 1
        ;;
esac

case "$ACTION" in
    clean)
        echo "Cleaning $BUILD_DIR..."
        rm -rf "$BUILD_DIR"
        exit 0
        ;;
    docker)
        echo "Building Docker image..."
        docker build -f Dockerfile -t relay-server ..
        exit 0
        ;;
    deploy)
        echo "Deploying to Fly.io..."
        cd ..
        fly ips list | grep -q "v4" || fly ips allocate-v4
        fly deploy --force-machines
        exit 0
        ;;
    logs)
        cd .. && fly logs
        exit 0
        ;;
    ssh)
        cd .. && fly ssh console
        exit 0
        ;;
esac

# Auto-install deps with Conan if conanfile.py exists in parent
if [ -f "../conanfile.py" ] && command -v conan &> /dev/null; then
    case "$BUILD_TYPE" in
        release) CONAN_BUILD_TYPE="Release" ;;
        *) CONAN_BUILD_TYPE="Debug" ;;
    esac

    if [ ! -f "$BUILD_DIR/conan_meson_native.ini" ]; then
        echo "Installing dependencies with Conan..."
        conan install .. --output-folder="$BUILD_DIR" \
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
    meson setup $MESON_ARGS $NATIVE_FILE "$BUILD_DIR"
fi

# Build
echo "Building $BUILD_TYPE..."
meson compile -C "$BUILD_DIR"

# Run if requested
if [ "$ACTION" = "run" ]; then
    echo "Running relay-server..."
    ./"$BUILD_DIR"/relay-server
fi

#!/bin/bash
# CroMagRally Build Script
# Uses CMake presets with Ninja generator

set -e

PRESET="${1:-debug}"
ACTION="${2:-build}"

cd "$(dirname "$0")"

case "$ACTION" in
    configure)
        echo "Configuring with preset: $PRESET"
        cmake --preset "$PRESET"
        ;;
    build)
        echo "Building with preset: $PRESET"
        cmake --preset "$PRESET"
        cmake --build --preset "$PRESET" --parallel
        ;;
    clean)
        echo "Cleaning preset: $PRESET"
        rm -rf "out/$PRESET"
        ;;
    run)
        echo "Running $PRESET build..."
        ./out/$PRESET/CroMagRally
        ;;

    run3)
        echo "Running 3 instances of $PRESET build..."
        ./out/$PRESET/CroMagRally 2>&1 | tee client1.log &
        ./out/$PRESET/CroMagRally 2>&1 | tee client2.log &
        ./out/$PRESET/CroMagRally 2>&1 | tee client3.log &
        wait
        ;;
    list)
        echo "Available presets:"
        cmake --list-presets
        ;;
    *)
        echo "Usage: $0 [preset] [action]"
        echo ""
        echo "Presets:"
        echo "  debug              - Debug build (default)"
        echo "  release            - Release build"
        echo "  debug-sanitize     - Debug with ASAN/UBSAN"
        echo "  mingw-release      - Cross-compile Windows from Linux"
        echo ""
        echo "Actions: configure, build (default), clean, run, list"
        echo ""
        echo "Examples:"
        echo "  $0                    # Build debug"
        echo "  $0 release            # Build release"
        echo "  $0 debug run          # Build and run debug"
        echo "  $0 debug clean        # Clean debug build"
        exit 1
        ;;
esac

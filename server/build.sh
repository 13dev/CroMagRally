#!/bin/bash
# CroMagRally Relay Server Build Script
set -e

PRESET="${1:-debug}"
ACTION="${2:-build}"

cd "$(dirname "$0")"

case "$ACTION" in
    configure)
        cmake --preset "$PRESET"
        ;;
    build)
        cmake --preset "$PRESET"
        cmake --build --preset "$PRESET" --parallel
        ;;
    clean)
        rm -rf "out/$PRESET"
        ;;
    run)
        ./out/$PRESET/relay-server
        ;;
    docker)
        # Build from project root to include common/
        docker build -f Dockerfile -t relay-server ..
        ;;
    deploy)
        # Allocate IPv4 if needed, then deploy from project root
        cd ..
        fly ips list | grep -q "v4" || fly ips allocate-v4
        fly deploy --force-machines
        ;;
    logs)
        cd .. && fly logs
        ;;
    ssh)
        cd .. && fly ssh console
        ;;
    *)
        echo "Usage: $0 [preset] [action]"
        echo ""
        echo "Presets: debug (default), release, release-static"
        echo ""
        echo "Actions:"
        echo "  build   - Configure and build (default)"
        echo "  clean   - Remove build directory"
        echo "  run     - Run local server"
        echo "  docker  - Build Docker image"
        echo "  deploy  - Deploy to Fly.io (force)"
        echo "  logs    - View Fly.io logs"
        echo "  ssh     - SSH into Fly.io instance"
        echo ""
        echo "Examples:"
        echo "  $0                 # Build debug"
        echo "  $0 release         # Build release"
        echo "  $0 release deploy  # Deploy to Fly.io"
        exit 1
        ;;
esac

# Building Cro-Mag Rally

This project uses the [Meson](https://mesonbuild.com/) build system with [Conan](https://conan.io/) for dependency management.

## Prerequisites

### Required Tools

- **Meson** 1.0+ and **Ninja**
- **Conan** 2.0+ (package manager)
- **OpenGL** (usually pre-installed)

### Installing Conan

```bash
pip install conan
conan profile detect  # One-time setup
```

### Alternative: System Dependencies

If you prefer system packages instead of Conan, install these manually:

#### Fedora/RHEL

```bash
sudo dnf install meson ninja-build SDL3-devel spdlog-devel mesa-libGL-devel
# GameNetworkingSockets: see below
```

#### Ubuntu/Debian

```bash
sudo apt install meson ninja-build libspdlog-dev libgl1-mesa-dev
# SDL3: Use PPA or build from source (not yet in Ubuntu repos)
# GameNetworkingSockets: see below
```

#### Arch Linux

```bash
sudo pacman -S meson ninja sdl3 spdlog mesa
# GameNetworkingSockets: AUR (gamenetworkingsockets) or build from source
```

#### macOS (Homebrew)

```bash
brew install meson ninja sdl3 spdlog
# GameNetworkingSockets: see below
```

#### Building GameNetworkingSockets (if not using Conan)

```bash
git clone --depth 1 --branch v1.5.1 https://github.com/ValveSoftware/GameNetworkingSockets.git
cd GameNetworkingSockets
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF
cmake --build build --parallel
sudo cmake --install build
sudo ldconfig  # Linux only
```

## Building the Game

Use the provided build script:

```bash
./build.sh debug          # Debug build
./build.sh release        # Release build
./build.sh debug run      # Build and run
./build.sh debug-sanitize # Debug with ASAN/UBSAN
./build.sh deps           # Show dependency info
```

Or use Meson directly:

```bash
meson setup build-release -Dbuildtype=release
meson compile -C build-release
```

The game executable is built in the build directory. A symlink to `Data/` is created automatically.

## Building the Relay Server

The relay server enables online multiplayer:

```bash
cd server
./build.sh debug          # Debug build
./build.sh release        # Release build
./build.sh debug run      # Build and run locally
./build.sh release docker # Build Docker image
./build.sh release deploy # Deploy to Fly.io
```

## Build Options

Meson options can be set with `-D`:

| Option | Default | Description |
|--------|---------|-------------|
| `buildtype` | `debugoptimized` | Build type: `debug`, `release`, `debugoptimized` |
| `sanitize` | `false` | Enable ASAN/UBSAN sanitizers |
| `sdl_static` | `false` | Statically link SDL3 |

Example:

```bash
meson setup build -Dbuildtype=release -Dsanitize=true
```

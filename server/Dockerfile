# Build stage
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y \
    cmake \
    ninja-build \
    gcc \
    g++ \
    git \
    pkg-config \
    libssl-dev \
    libprotobuf-dev \
    protobuf-compiler \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build/server
COPY server/CMakeLists.txt server/CMakePresets.json ./
COPY server/src/ src/
COPY common/ ../common/

RUN cmake --preset release && cmake --build --preset release

# Runtime stage
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    libssl3 \
    libprotobuf32 \
    && rm -rf /var/lib/apt/lists/*

# Copy GNS shared library
COPY --from=builder /build/server/out/release/bin/libGameNetworkingSockets.so /usr/local/lib/
RUN ldconfig

COPY --from=builder /build/server/out/release/relay-server /usr/local/bin/

EXPOSE 40000/udp

CMD ["relay-server"]

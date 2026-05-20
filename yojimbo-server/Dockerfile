# Build stage
FROM debian:bookworm-slim AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    cmake \
    build-essential \
    git \
    pkg-config \
    libsodium-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy server source
WORKDIR /build
COPY . .

# Build server
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel

# Runtime stage
FROM debian:bookworm-slim

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libsodium23 \
    && rm -rf /var/lib/apt/lists/*

# Copy built server
COPY --from=builder /build/build/yojimbo-server /usr/local/bin/

# Expose UDP port
EXPOSE 40000/udp

# Run server
CMD ["yojimbo-server"]

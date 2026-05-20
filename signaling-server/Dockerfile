FROM alpine:3.19 AS builder

RUN apk add --no-cache build-base cmake

WORKDIR /app
COPY . .

RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

FROM alpine:3.19

RUN apk add --no-cache libstdc++

WORKDIR /app
COPY --from=builder /app/build/signaling-server .

EXPOSE 27015

CMD ["./signaling-server", "-p", "27015"]

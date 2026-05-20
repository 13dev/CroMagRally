# Cro-Mag Rally Signaling Server

TCP relay server for Cro-Mag Rally multiplayer games.

## Features

- Room-based matchmaking with 4-character codes
- Player join/leave notifications
- Game data relay between host and clients
- Cross-platform (Linux/Windows)

## Local Build

```bash
mkdir build && cd build
cmake ..
make
./signaling-server -p 27015
```

## Docker

```bash
docker build -t cromag-signaling .
docker run -p 27015:27015 cromag-signaling
```

## Deploy to Cloud

### Fly.io (Recommended - Free tier with TCP support)

```bash
cd signaling-server
fly auth login
fly launch
fly deploy
```

### Render.com

1. Push to GitHub
2. Go to [Render Dashboard](https://dashboard.render.com)
3. New > Blueprint > Connect your repo
4. Render will detect `render.yaml` and deploy

**Note:** Render expects HTTP health checks. You may need to suspend health checks in the dashboard for TCP-only servers.

### Railway.app

```bash
cd signaling-server
railway login
railway init
railway up
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| PORT | 27015 | Server port |

## Protocol

Messages are newline-terminated text:

- `REGISTER:<name>:<id>` - Host creates room
- `JOIN:<code>:<name>:<id>` - Client joins room
- `START` - Host starts game
- `GAME:<base64>` - Game data relay
- `PING` / `PONG` - Keepalive

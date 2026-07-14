# src/net — networking (milestone M1)

Reserved for the network layer, added in **M1** (see `docs/ROADMAP.md`).

Planned units:

- **`ApiClient`** — `QNetworkAccessManager` wrapper for the REST API
  (`POST /api/rooms`, `GET /api/rooms/<code>`, later auth/avatars/personal
  rooms). Holds the `QNetworkCookieJar` that carries the `meetup_session`
  cookie into both HTTP and the WebSocket handshake.
- **`SignalingClient`** — `QWebSocket` wrapper for the JSON control protocol
  (`join` → `join_ok`, `participant_*`, `chat`, `state`, `screen`, `ping`),
  including the 8-attempt reconnect policy.
- **`Protocol.h`** — binary media framing (pack 11-byte header / unpack
  15-byte header) and the media type/flag/codec constants. These constants
  live only in the client — the server treats media payloads as opaque.

Connection rule: `wss://<host>/ws` when the server origin is https, otherwise
`ws://<host>:9000`; REST rides the same scheme+host.

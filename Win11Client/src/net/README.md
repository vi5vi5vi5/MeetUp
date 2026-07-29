# src/net — networking (milestone M1)

Reserved for the network layer, added in **M1** (see `docs/ROADMAP.md`).

Planned units:

- **`ApiClient`** — `QNetworkAccessManager` wrapper for the REST API
  (`POST /api/rooms`, `GET /api/rooms/<code>`, later auth/avatars/personal
  rooms). Holds the `QNetworkCookieJar` that carries the `meetup_session`
  cookie into both HTTP and the WebSocket handshake.
- **`SignalingClient`** — room state and the JSON control protocol
  (`join` → `join_ok`, `participant_*`, `chat`, `state`, `screen`, `ping`),
  including the 8-attempt reconnect policy. Lives on the GUI thread; owns the
  transport below and talks to it with queued calls.
- **`SignalingLink`** — the `QWebSocket` itself, on its **own thread**. Incoming
  binary frames are split at the socket: the audio band (`AUDIO_CODED`,
  `SCREEN_AUDIO`) goes to the audio thread, the video bands (`VIDEO_*`,
  `SCREEN_*`) to the two decode threads, and only service frames
  (`KEYFRAME_REQ`) to the GUI thread. The split exists because the GUI thread
  stalls — a window resize runs Windows' modal size loop and blocks Qt Quick on
  every frame — and media must not wait behind that.
- **`Protocol.h`** — binary media framing (pack 11-byte header / unpack
  15-byte header) and the media type/flag/codec constants. These constants
  live only in the client — the server treats media payloads as opaque.
- **`ChatImages`** — chat pictures: a store of JPEG bytes plus the QML image
  provider behind `image://chatimg/<id>`. Bytes rather than decoded `QImage`s
  because the server keeps up to 24 pictures in room history — expanded to
  pixels that would be ~180 MB, as JPEG about ten — so decoding happens in
  `requestImage`, straight to the size actually being drawn. Every `put()`
  mints a **new** id on purpose: when a key is entered late and the feed is
  re-read, the URL has to change or QML serves the previous (empty) result from
  its cache. `packChatImage` is the send side — the same ladder the web uses
  (1600/1024/720 × quality 85/70/55 until it fits 480 000 base64 chars, against
  the server's 600 000 cap), run off the GUI thread because decoding a phone
  photo takes hundreds of milliseconds.

Connection rule: `wss://<host>/ws` when the server origin is https, otherwise
`ws://<host>:9000`; REST rides the same scheme+host.

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
- **`ChatModel`** — the chat feed as a real `QAbstractListModel`, **newest
  message at index 0**. The reversed order pairs with
  `verticalLayoutDirection: BottomToTop` on the QML side, and the pairing is
  the whole point: index 0 draws at the bottom of the screen, so "keep the
  newest in view" stops being a computation and becomes a *position* — new
  rows arrive at the edge the view is already pinned to, and `ListView` holds
  it there by itself.
  The alternative (natural order, follow the bottom manually) cannot be made
  reliable here. Message heights vary with text wrapping and images, and
  `QQuickListView` only knows the real height of delegates it has created —
  the rest are averaged, so `contentHeight` is an *estimate* and everything
  computed on top of it ("am I at the bottom?", "scroll to the bottom") drifts
  with the length of the conversation. A stand with forty variable-height
  messages failed that approach in 4 of 9 scenarios; the flipped list passes
  all 9 with no scroll-handling code at all.
  Two further properties matter for not disturbing a reader: live messages
  arrive as an **insert** (never a reset), and re-reading with a new E2E key
  emits **`dataChanged`** — a reset would throw the reader back to the newest
  message at the exact moment they typed the key.
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

- **`UpdateChecker`** (QML: `Updates`) — client self-update from **GitHub
  Releases**, not from the MeetUp server: the server is upgraded on its own
  schedule and its version answers a different question, plus GitHub's CDN
  serves the 46 MB archive instead of someone's home box. The repo is a CMake
  cache variable (`UPDATE_REPO`), so a fork updates from the fork; empty
  disables checking outright.

  The install rests on one Windows property: a running `.exe` and loaded
  `.dll`s **cannot be deleted but can be renamed**. So the old files move to
  `<name>.old-<timestamp>`, the new ones unpack into their place, the app
  restarts, and the next launch sweeps the tails (`sweepOldFiles`, called from
  `main` before anything else — during the swap they were still held by the
  departing process). No separate `updater.exe` exists or is needed. Verified
  against a live process: delete fails, rename succeeds, the replacement lands
  on the freed name, and the tail clears once the process exits. Replacement is
  all-or-nothing — a half-updated folder is a broken program with no obvious
  way back — so renames and copies are tracked and rolled back on any failure.

  Two details that look like trivia and are not. **The release tag is
  `Win11Client_1.05`**, so the version cannot be read by stripping a leading
  `v`: the first number in that string is the `11` of `Win11`. `versionFromTag`
  takes the *last* dotted group instead — product names have no dots, versions
  do. And **`tar.exe` is invoked by absolute path** (`%SystemRoot%\System32`),
  because it must be the bsdtar that ships with Windows and understands zip;
  the GNU tar that arrives with Git for Windows may well come first in `PATH`
  and answers "This does not look like a tar archive".

  There is deliberately no manifest signature. Integrity rests on HTTPS to
  `api.github.com` and to the file, on refusing to follow a redirect anywhere
  but GitHub, and on the size matching what the API promised.

  Two entry points, both one button walking through every step (check →
  download → restart), because which state the program is in is not the user's
  problem: the pill beside the wordmark on the home screen, and **Settings →
  About**, which is reachable *from a call*. That second one is why
  `SignalingClient::restartArgs` exists — restarting mid-conference has to come
  back to the same room, and that means name, server, `#k=` key and, for a
  locked room, the password. It is assembled in C++ because all of it already
  lives there and the password then never enters QML at all. An empty list
  means "not in a room", and the caller falls back to just the server address.

- **`../Cli`** (QML: `Cli`) — what to open on launch, serving two purposes at
  once: a shortcut that drops straight into a room
  (`--server`, `--room`, `--name`, `--key`, `--phrase`, or a whole invite link
  as a positional argument), and the return trip after an update, which
  restarts the client with where it was. Order of precedence: an explicit room
  wins over any stored account session; failing that, a server alone just
  switches there and signs in with the saved cookie; anything else passed
  without either is an error stated on screen, because a shortcut that silently
  opens the wrong thing is worse than one that explains itself. An empty
  command line is an ordinary launch and says nothing.

  **A locked room** needs `--password`. Without it the shortcut still gets all
  the way to the gate card and stops there asking for it, which is correct but
  not what a shortcut promises. The server only demands the password *after* we
  have introduced ourselves, so it is submitted on the phase change to `gate`,
  not at entry — and exactly once, via `takePassword()`, which hands it over and
  forgets it. Once matters twice over: a wrong password would otherwise loop
  forever (the server just says `gate` again), and a password meant for one room
  would silently be offered to the next one entered in the same session.

  Arguments are parsed with `parse()` rather than `process()`: the latter kills
  the process on an unknown key after printing to a console a GUI program may
  not have, leaving a person staring at nothing instead of a window. The room
  path goes through `LinkController::openAs`, which already knows how to switch
  servers, enter as a guest and pick up `#k=` — the CLI just assembles the link.
  The E2E key in an argument is visible in the process list; that is the
  accepted price for a shortcut that opens an encrypted room without typing the
  phrase, and the key still never touches disk.

Connection rule: `wss://<host>/ws` when the server origin is https, otherwise
`ws://<host>:9000`; REST rides the same scheme+host.

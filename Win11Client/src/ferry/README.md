# src/ferry — Ferry in the chat (receiving and sending)

A Ferry link (`https://<any host>/t/<id>#<key>`) dropped into a MeetUp chat
turns into a card under the message: what the volume is, **Accept**, progress,
**Delivered**, **Open folder**. Old clients see the same message as a plain
link — the MeetUp server is not involved at all (its chat only carries text and
images), the card is built entirely on the client.

The protocol is Ferry's own (`docs/protocol.md` in the Ferry repo); this is a
third receiver next to the CLI and the web page, written after
`Server/web/assets/js/receiver.js`.

## Pieces

- **Ferry core** — `third_party/ferry`, fetched by `scripts/fetch-deps.ps1` at a
  pinned commit (`$FerryCommit`; bumping it re-fetches by itself). Chunks,
  manifest, links, BLAKE3, JSON — the very same code the CLI and the relay use.
  Point `FERRY_DIR` at your own Ferry checkout to work on both at once.
- **`FerryCrypto.cpp`** — `core/Crypto.h` on Windows CNG instead of OpenSSL
  (the `ferry-crypto` target is never built), plus the CLI's own
  `Cli/Password.cpp` and reference Argon2 for password-mode links.
- **`FerryVolume`** — what the relay says about a transfer
  (`GET /api/transfers/<id>`, burns nothing) and opening it with the key:
  manifest, hash list against its root, layout. Also refusal texts for every
  error code (same wording as Ferry's own "Errors" board).
- **`FerryDisk`** — the volume as one byte space over a single file or a tree;
  files of a tree are created lazily, written unbuffered (a chunk can be asked
  for by another receiver right after it lands).
- **`FerryReceiver`** — the protocol engine, on the `ferry-io` thread:
  challenge → `hello` with what we already have → chunks in any order, each
  checked against its BLAKE3 → `have` reports → re-encrypting chunks for
  latecomers (`serve`) → rename `<name>.ferry-part` to `<name>` at the end.
  Streamed hashes, reconnects with a rejoin token, resume after a restart.
- **`FerryTransfer`** — one per link for the whole app run, the state the card
  binds to (`fog` → `offer` → `receiving` → `done`, plus password, missing key,
  waiting for approval, failures).
- **`FerryHub`** (QML: `Ferry`) — recognizes links, hands out transfers, owns
  the I/O thread and the settings (Settings → «Файлы»).

## Sending

The paperclip in the chat is now a menu — an image as before, files or a
folder through Ferry — and dropping files on the chat does the same (a single
image still goes as an ordinary chat image). Both open the **send sheet**
(`qml/FerrySendSheet.qml`): what goes (with what will be skipped and why),
uses, lifetime, concurrency, «ask whom to let in», «serve without me»,
password instead of key, relay. The link goes to the chat as an ordinary
message as soon as the relay accepts the offer — hashes keep being computed in
the background (streamed hashes). In the sender's own chat the message shows
the **sender card**: hashing progress, receivers with their roles, where the
bytes come from, the door (approve / deny / let everyone in, also as a toast
over the call), conditions on the fly, the owner link, «close access», and a
receipt at the end.

- **`FerryCargo`** — the manifest from what was picked: one file, one folder
  or several things (a `ferry-YYYY-MM-DD` tree), same rules as Ferry's CLI and
  web; shortcuts, unreadable files and names that would not survive on the
  other side are listed as skipped, before the link exists.
- **`FerrySender`** — the protocol, after `send.worker.js`: draft, offer,
  hash segments, serving exactly what the relay asks for (live wave first, up
  to three tenths for backfill), owner commands, returning after a dropped
  connection (`resume_owner`). Hashing runs in its own thread (`FerryHasher`).
- **`FerryDraft` / `FerrySend`** — what the sheet and the sender card bind to.

Sending lives while the app is open — leaving the call does not stop it; the
home screen shows «Ferry · раздаётся N тома», and closing the app while nobody
has the whole volume yet asks first.

Not done yet: picking a sending back up after a restart (the card then shows
the receiver side of one's own link).

## Privacy rules the card keeps

- **Unknown relays are not contacted** until the person clicks «Показать
  сведения»: asking a relay about a transfer reveals your address to it, and
  anyone can drop a link into a chat. Trusted without asking: the relay from the
  MeetUp server's `[ferry] relay` (`ferry_relay` in `/api/config`) and relays the
  person ticked «Доверять этому релею дальше» for.
- The key never leaves the client and is **never written to disk** — it lives in
  the link, i.e. in the chat history. The state file
  (`%LOCALAPPDATA%\MeetUp\MeetUp\ferry\recv-*.json`) holds only the map of
  received chunks, paths and the rejoin token.
- Seeding what you downloaded: by default **while you are in the call**
  (leaving the call disconnects the seed); «пока открыто приложение» or «не
  раздавать» in settings.

## Checking it

None of these are part of the normal build:

```
cmake --build out/build/release --target ferry-core-test ferry-cng-test ferry-recv-tool ferry-send-tool
```

- `ferry-core-test` — Ferry's own core self-tests, run against `FerryCrypto.cpp`.
- `ferry-cng-test` — known answers taken from OpenSSL (`node:crypto`), Argon2id
  included.
- `ferry-recv-tool <link> <dir> [--password p] [--seed s] [--stop-after bytes]` —
  the same receiver without the UI, for checking against a real relay and
  `ferry send` (single file, tree, password, streamed hashes, resume, seeding a
  latecomer after the sender left were all verified this way).
- `ferry-send-tool <relay> <path>... [--password p] [--uses n] [--leave-open]
  [--approve-all] [--seconds n] [--revoke]` — the same sender without the UI.
  Its volumes were received byte for byte by our receiver and by Ferry's own
  `receiver.js` (WebCrypto, JS BLAKE3) run in node; approval, password, several
  loose files, a latecomer served by a seed after the sender left, and revoke
  were all checked this way.

Build these from PowerShell, not from Git Bash: there the console code page is
OEM, Ninja does not recognise the localized `/showIncludes` prefix, and header
dependencies silently stop being tracked.

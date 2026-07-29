# src/crypto — end-to-end encryption (milestone M5)

Byte-for-byte compatible with the web client
(`Server/web/assets/meetup-media.js`) — people sit in the same room from a
browser and from this app, and one byte of drift means "they cannot hear each
other", not "slightly worse interop".

- **`E2eCipher`** — the primitives, thread-safe and shared by every media
  thread (audio, two encode threads, two decode threads, chat on the GUI).
  - Key derivation: **PBKDF2-HMAC-SHA256**, 150 000 iterations, salt
    `"meetup-e2e-v1|" + roomCode`, 32-byte key — or a raw key from a
    `#k=<base64url>` invite fragment.
  - **AES-256-GCM** seal/open of media payloads and chat strings.
    IV = 4-byte per-key random prefix + 8-byte LE counter; AAD = `[type, codec]`
    for media, `[250,0]` for chat text (`[251,0]` for chat images, which this
    client does not send yet); 16-byte tag appended. Only the payload is
    encrypted — the frame header stays cleartext, the server needs it to relay.
    The counter is **atomic**: lanes seal from different threads, and a repeated
    IV under one key destroys GCM outright.
  - Chat rides as `"🔒e2e:<base64url(iv|ct|tag)>"` — an ordinary message string
    as far as the server is concerned.
- **`E2eController`** (QML: `Crypto`) — where the key comes from and when it
  goes away: phrase → PBKDF2 (off the GUI thread, it is deliberately slow),
  `#k=` from the invite link, building an invite link *with* the key, and
  dropping the key when the room is left. The key is **never written to disk** —
  the same choice the web client makes with `sessionStorage`.
- Peer "locked by key" state after 3 consecutive decrypt failures, merged across
  a participant's lanes in `VideoEngine::isLocked` (tile badge) and per-message
  in the chat.

No new external dependency: AES-GCM and PBKDF2 come from **Windows CNG**
(`bcrypt.lib`), which ships with the OS. OpenSSL was the original plan, but this
client is Windows-only by design (WASAPI, Desktop Duplication, PrintWindow), and
hauling ~5 MB into the release archive for one cipher mode the system already
provides is a poor trade.

Interop is verified against the real WebCrypto API rather than by inspection: a
cross-check probe runs the web client's own key-derivation and seal/open code in
Node against this implementation, both directions, for media, chat and link
keys.

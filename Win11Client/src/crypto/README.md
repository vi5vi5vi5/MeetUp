# src/crypto — end-to-end encryption (milestone M5)

Reserved for the E2E layer (see `docs/ROADMAP.md`).

- Key derivation: **PBKDF2-HMAC-SHA256**, 150 000 iterations, salt
  `"meetup-e2e-v1|" + roomCode`, 32-byte key — or a raw key from a
  `#k=<base64url>` invite fragment.
- **AES-256-GCM** seal/open of media payloads and chat/image strings.
  IV = 4-byte random prefix + 8-byte LE counter; AAD = `[type, codec]` for
  media, `[250,0]` for chat text, `[251,0]` for chat images; 16-byte tag
  appended. Only the payload is encrypted — the frame header stays cleartext.
- Peer "locked by key" state after 3 consecutive decrypt failures.

External dep introduced here (MSVC, via vcpkg): **OpenSSL 3** (libcrypto,
EVP API) — required for AES-GCM, which Qt does not expose. PBKDF2 itself can
use `QMessageAuthenticationCode`, but GCM needs OpenSSL.

# src/media — capture, codecs, playback (milestones M2–M4, M8)

The media pipeline (see `docs/ROADMAP.md`).

- **M2 Audio** (`AudioEngine` + `AudioWorker`) — `QAudioSource` capture
  (48 kHz mono) → libopus 20 ms frames → v2 packets; receive → libopus decode →
  jitter buffer → mix → `QAudioSink`. All of it runs on a dedicated **audio
  thread** (`AudioWorker`), fed straight from the socket thread; `AudioEngine`
  stays on the GUI thread and only decides *when* to capture and play.
  The jitter buffer starts at a 3-chunk (60 ms) prebuffer and adapts: an
  underrun is concealed with Opus PLC and widens the buffer by 2 chunks
  (up to 160 ms), which decays back after 5 s of clean playback.

  **Noise suppression** (`Denoiser`, on top of RNNoise) sits on the capture
  path, and its position in `onCaptured()` is load-bearing: it runs *before*
  the sensitivity gain, because the network was trained on natural levels and
  samples cranked to 200 % throw off its band estimates. The 960-sample frame
  maps onto RNNoise as exactly two 480-sample blocks, so nothing is re-buffered
  and no resampling happens: it is written for 48 kHz mono, which is already
  the conference format.

  The same call also returns a **speech probability, and that — not the RMS —
  drives the "speaking" highlight.** The old level threshold was measuring the
  wrong quantity: loudness is not speech, so it both missed quiet sentences
  (below the threshold) and accepted any loud enough thud. Two thresholds
  rather than one, because a single one flickers at the boundary on every
  breath between words: the upper one lights the highlight, the lower one only
  sustains it. The level meter stays on RMS — it answers "how loud", which is
  a different and honestly-answered question. With suppression switched off
  there is no probability to read, and the old level threshold is what remains.

  Cost, measured over 60 s of audio on this machine: **0.27 ms per 10 ms frame**
  (0.55 ms per conference frame, ~2.7 % of one core) and **+10 ms** of
  algorithmic delay from the 50 %-overlapped analysis window — single-digit
  percent against a pipeline that already holds 60–160 ms of jitter buffer plus
  ~60 ms in the sink. Suppression measured on synthetic noise: −34 dB on white
  noise, −35 dB on noise with keyboard-like transients, and a fan-style tonal
  hum driven to silence. The state is recurrent, so one instance must see one
  uninterrupted sample stream — it lives and dies with the capture, and the
  screen-audio band deliberately has none of this (that band carries music).

  `OPUS_SET_DTX` is on, and only makes sense *together* with the denoiser:
  before it there is no silence in a real room for DTX to detect. The frame is
  still transmitted when Opus shrinks it to a single byte — dropping it would
  punch a hole at the receiver, and a hole there widens the jitter buffer by
  2 chunks, which would pay for the saved bandwidth in conversation delay.
- **M3 Video receive** (`VideoEngine` + `VideoRecvWorker` + `VideoDecoder`) —
  FFmpeg (libavcodec) decode of H.264/HEVC/VP8/VP9/AV1, keyframe and
  `KEYFRAME_REQ` handling, render into tiles via `QVideoSink`. Decoding is
  **receive-wider-than-send on purpose**: VP9 and AV1 have no encoder in either
  ladder below, but a browser or a future client may well send them, and
  refusing a stream we can decode would be a self-inflicted blackout.
  Where the GPU offers it, decode goes through **D3D11VA** on one device shared
  by every decoder (`sharedD3d11Device`); `get_format` asks for
  `AV_PIX_FMT_D3D11` and silently accepts a software format when the driver
  will not take the stream. Decode and `sws_scale` run on
  **two decode threads** (one per band, same reason as the send side), fed
  straight from the socket thread; only the finished `QVideoFrame` crosses to
  the GUI thread, because `QVideoSink` belongs to the tile.

  **A/V sync holds both bands, each against its own clock.** Audio reaches the
  ear later than video reaches the eye — jitter prebuffer plus the sink's own
  queue, roughly 60–120 ms — so a frame that has overtaken its sound waits in
  `Peer::holdQ` until `AudioWorker`'s playhead reaches its timestamp. The screen
  band used to be exempt on the grounds that a desktop has no lips; that was
  wrong once screen audio existed, because `SCREEN_AUDIO` travels through the
  same jitter buffer while the picture did not, and a video played inside a
  shared window drifted exactly as far as a face would. So `AudioWorker`
  publishes **two** playhead maps (voice and screen) and the video engine picks
  by band. The cursor does not pay for this: the screen playhead exists only
  while screen audio is actually flowing, and otherwise reads 0, which paints
  immediately as before. Queue depth is 16 frames for screen against 12 for
  camera — the hold is a duration, and 60 fps fits fewer milliseconds into the
  same frame count than 24 fps does; the cap stays counted in frames rather
  than milliseconds because a 4K NV12 frame is 12 MB and "1200 ms at 60 fps"
  would be most of a gigabyte.
- **M4 Video send** (`VideoEngine` + `VideoEncoder`) — `QCamera` → sws_scale →
  encoder, even dimensions, keyframe every ~72 frames + forced on
  `KEYFRAME_REQ` / `participant_joined` → v2 packets, with backpressure (drop
  video frames above 1.5 MB queued in the socket). Self tile gets the raw
  camera preview.

  **There is no codec setting, in either band.** There was one, and removing it
  was a measurement result: a full sweep of every codec this build can encode,
  at 720p/1080p/1440p/4K, produced the same top answer on every resolution, and
  the only reason to move off it is a receiver saying it cannot decode — which
  the program learns before the user could. What replaces the setting is a
  **ladder per band**, walked downward only by `Proto::CODEC_UNSUPPORTED`
  (`VideoEncoder::open`), and reset to the top on the next `join_ok` because the
  next room holds different people:
  - **screen: HEVC → H.264 → VP8**, hardware first. HEVC costs the same to
    encode as H.264 on this card (2.4 vs 2.2 ms at 1440p) and a third of the
    bandwidth at equal quality (4158 vs 10832 kbit/s at 1080p). H.264 is the
    compatibility rung — no browser lacks it, and plenty lack HEVC.
  - **camera: VP9 → H.264 → VP8**, software throughout. VP9 is the fastest
    software encoder measured (3.6 ms at 1080p against openh264's 16.3) and
    browsers decode it. Hardware is withheld deliberately, see below.

  A complaint is only acted on when the codec named in it is the one *this*
  band is currently sending (`m_scrCodec` / `m_camCodec`): the server fans
  `CODEC_UNSUPPORTED` out to every sender, and without that check one
  participant's missing HEVC would demote a second presenter who was already
  on H.264.

  One measured trap is worth repeating because it cost the screen band its
  frame rate for a long time: **`scenario=display_remoting` must not be set on
  `h264_mf`.** It costs 17–30 ms per frame — identically at 720p and at 4K, so
  it is not work — and buys nothing: same bitrate (5687 vs 5687 kbit/s) and same
  PSNR (40.02 vs 40.05 dB). Thirty milliseconds of nothing is a 33 fps ceiling
  at any resolution. On `hevc_mf` the same option is free (1.4 ms) and stays.
  The rate control is `pc_vbr`, not `cbr`: CBR pads a static screen up to the
  target regardless of content (3414 vs 714 kbit/s measured on a still 1080p
  desktop).

  Three more things about the hardware path are load-bearing, and all three
  were established by probe rather than by reading docs:
  - **NV12 is mandatory.** `h264_mf` advertises `yuv420p` in its pixel-format
    list and then fails `avcodec_open2` with it. So `VideoEncoder::pixFmt()`
    exists and both `sws_scale` and the cursor blend handle either layout.
  - **The pipeline is two frames deep**, so a packet leaving `encode()` belongs
    to the frame fed two calls earlier. `Packet::ptsMs` carries the real one;
    stamping the header with the current frame's time would put a timestamp two
    frames into the future on every packet.
  - **The camera ladder is software-only**, and that is the reason it has its
    own ladder at all. Those two frames of pipeline are ~83 ms at 24 fps, which
    for a face goes straight into lip-sync drift, and there is nothing to win:
    720p on the CPU already encodes in single-digit milliseconds.
  `hw_encoding=1` means the open *fails* without a GPU encoder and the candidate
  list falls through to the next rung — deliberate, since MF's own software
  encoder is slower than openh264, and getting it silently would be worse than
  not getting MF at all. Candidate de-duplication is therefore **by encoder
  name**, not by `Proto::CODEC_*`: hardware and software H.264 share a protocol
  codec byte, and de-duplicating by that would have dropped the fallback.

  **AV1 is absent from both ladders** although this card encodes it. It decodes
  at 18–20 ms per frame at *every* resolution — 720p costs what 4K costs — which
  is past the whole 60 fps budget on its own, and its encoder buffers ~18 frames
  deep. The decoder stays wired up for interop; the encoder is gone.

  Two invariants hold on the send path, and both exist because breaking them
  cost the screen band half its frames:
  **Screen capture is ours** (`ScreenCapturer`, on top of Windows.Graphics
  Capture) rather than `QScreenCapture`, which delivered 27 fps on a 60 Hz
  monitor with no way to influence that, and 61 % of those frames were
  byte-identical to the previous one. WGC measured 50 fps on the same 4K screen,
  captures a **single window** (DXGI Desktop Duplication cannot at all), and
  draws the cursor itself via `IsCursorCaptureEnabled` — which is why the
  client-side cursor compositing is gone entirely. There is deliberately **no Qt
  fallback**: the target is Windows 11, and older systems are served by the web
  client. Two consequences worth knowing:
  - WGC delivers a frame **only when the compositor repainted something**, so on
    a still screen the stream would simply stop. `VideoEngine` re-sends the last
    frame on the pacer's schedule; a repeat costs a handful of bytes and keeps
    the hardware encoder's two-frame pipeline moving.
  - A **minimised window** produces no frames at all. `ScreenCapturer` watches
    for that and reports it, so viewers get a sentence instead of a silently
    frozen stage.

  **(1) no media payload crosses the GUI thread.** Encoded packets go from the
  encode thread straight to the socket thread (`packetReady` →
  `SignalingLink::sendBinary`); only byte counts visit the GUI thread, for
  `MediaStats`. **(2) the engine learns that a worker is free by reading an
  atomic** (`VideoSendWorker::busy()`), not by waiting for a signal — the GUI
  thread blocks while Qt Quick syncs with the render thread, and a worker that
  reported its freedom through that queue sat idle for the duration.
  Also on this path: `sws_scale` is built by hand rather than through
  `sws_getCachedContext` (the latter takes no thread count, and BGRA 4K →
  YUV420P is 12–20 ms single-threaded, more than a whole 60 fps frame budget),
  it uses area averaging when downscaling (bilinear samples 2×2 regardless of
  ratio, which turns shrunken text to mush), and the encoder is opened with the
  **achieved** frame rate, not the preset's wish — rate control divides the
  bitrate by the rate it was told, so claiming 60 while delivering 20 gave every
  real frame a third of the bits it had coming.
- **M8 Settings** (`../MediaSettings`) — device selection (mic/cam/speakers),
  playback volume and mic sensitivity (0–200 %), send-quality presets
  (low/med/high for camera resolution+bitrate+fps and Opus bitrate), persisted
  in QSettings and applied live by both engines.

- **UI sounds** (`UiSounds`, QML `Sfx`) — ten short WAVs in
  `../../resources/sounds`, played through `QSoundEffect` into the **selected**
  output device (the pool is rebuilt when `MediaSettings::outId` changes, so a
  notification never lands in a different pair of headphones than the call).
  One family derived from a single tine sample: events differ by note count,
  interval and direction rather than by timbre, and levels are baked into the
  files (`message` loudest, `peer-leave` quietest) — nothing is mixed at
  runtime. Policy lives in the catalogue in `UiSounds.cpp`: *notifications*
  (`message`, `peer-join`, `peer-leave`) go silent while the conference sound
  is off, *actions* (toggles, share, join) always play, and a per-sound minimum
  gap collapses bursts. Context the C++ side cannot know stays at the call site
  in `ConferenceScreen.qml`: sounds are armed ~1.2 s after entering a room (so
  `join_ok` and reconnects don't fire a volley), peer sounds stop above six
  participants, and an incoming message is silent while the chat is visible in
  a focused window.

- **M8 Settings** also owns the voice-processing switches in `SettingsAudio.qml`.
  Only noise suppression is wired; **echo cancellation and auto-gain are still
  `soon: true`**, and that is not laziness about a "better denoiser". An echo
  canceller is a different problem: it needs a second, reference signal (what
  went to the speakers), an estimate of the round-trip delay, and non-linear
  post-processing of the residual. Realistically that means WebRTC's AEC3.
  The architecture is already in its favour — `mixOneFrame()` is the reference,
  `sinkQueuedMs()` is the delay, and both live on the same thread as capture,
  which is where naive AEC integrations usually come apart. Note that AEC must
  run **before** the denoiser: suppression is non-linear and an adaptive filter
  placed after it stops converging.

External deps introduced here: FFmpeg (avcodec/swscale with `openh264` + `vpx`
features) and libopus, both MSVC via vcpkg.

**RNNoise is fetched, not committed.** vcpkg does have a port, but it is
autotools-built and does not claim Windows at all, while the library itself has
no dependencies whatsoever — so it lives in `third_party/`, which is
`.gitignore`d in full and populated by `scripts/fetch-deps.ps1`. Nobody has to
remember to run it: `configure.ps1` calls it, and every road (build, deploy)
goes through configure. It costs ~6 s on a cold tree and 0.1 s afterwards.
The reason for the whole arrangement is one file: the model weights
(`src/rnnoise_data.c`) are ~75 MB of generated C, which is forty times the size
of this entire client and would sit in git history forever. They are not in the
upstream repository either — the script downloads them from media.xiph.org by
the hash in `model_version`, which doubles as the SHA-256 checksum, and pins the
RNNoise commit so a build is reproducible a year from now.

One build detail is deliberate: the SIMD path is chosen at **runtime**
(`RNN_ENABLE_X86_RTCD`, SSE4.1/AVX2 in their own translation units) rather than
by a global `/arch:` flag, which would produce a binary that refuses to start on
machines without AVX2. The dispatcher is worth it, at 0.47 → 0.27 ms per frame.

UI sounds need no dependency beyond Qt Multimedia — `QSoundEffect` plays
uncompressed PCM WAV only, which is what the files are (48 kHz, mono, 16-bit).

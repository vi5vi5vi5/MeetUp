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
- **M3 Video receive** (`VideoEngine` + `VideoRecvWorker` + `VideoDecoder`) —
  FFmpeg (libavcodec) decode of H.264/VP8/VP9, keyframe and `KEYFRAME_REQ`
  handling, render into tiles via `QVideoSink`. Decode and `sws_scale` run on
  **two decode threads** (one per band, same reason as the send side), fed
  straight from the socket thread; only the finished `QVideoFrame` crosses to
  the GUI thread, because `QVideoSink` belongs to the tile.
- **M4 Video send** (`VideoEngine` + `VideoEncoder`) — `QCamera` →
  YUV420P (sws_scale) → H.264 Annex B (libopenh264) or VP8 (libvpx), even
  dimensions, keyframe every ~72 frames + forced on `KEYFRAME_REQ` /
  `participant_joined` → v2 packets, with backpressure (drop video frames above
  1.5 MB queued in the socket). Self tile gets the raw camera preview.

  **The screen band encodes on the GPU**, via `h264_mf` (Media Foundation) with
  `scenario=display_remoting`, `rate_control=cbr`, `hw_encoding=1`; on this
  machine's AMD card that is 2.5 ms per 1080p frame against libopenh264's 13.4 ms
  average and 48 ms worst case — the worst case being what actually broke a
  60 fps budget. Three things about it are load-bearing, and all three were
  established by probe rather than by reading docs:
  - **NV12 is mandatory.** `h264_mf` advertises `yuv420p` in its pixel-format
    list and then fails `avcodec_open2` with it. So `VideoEncoder::pixFmt()`
    exists and both `sws_scale` and the cursor blend handle either layout.
  - **The pipeline is two frames deep**, so a packet leaving `encode()` belongs
    to the frame fed two calls earlier. `Packet::ptsMs` carries the real one;
    stamping the header with the current frame's time would put a timestamp two
    frames into the future on every packet.
  - **The camera deliberately stays on libopenh264** (`allowHardware` is set
    only for `SCREEN_CODED`). Those two frames of pipeline are ~66 ms at 30 fps,
    which for a face goes straight into lip-sync drift, and there is nothing to
    win: 720p on the CPU already encodes in 3–5 ms.
  `hw_encoding=1` means the open *fails* without a GPU encoder and the candidate
  list falls through to libopenh264 — deliberate, since MF's own software
  encoder is slower than openh264, and getting it silently would be worse than
  not getting MF at all. Candidate de-duplication is therefore **by encoder
  name**, not by `Proto::CODEC_*`: hardware and software H.264 share a protocol
  codec byte, and de-duplicating by that would have dropped the fallback.

  Two invariants hold on the send path, and both exist because breaking them
  cost the screen band half its frames:
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

External deps introduced here (MSVC, via vcpkg): FFmpeg (avcodec/swscale with
`openh264` + `vpx` features) and libopus. UI sounds need no dependency beyond
Qt Multimedia — `QSoundEffect` plays uncompressed PCM WAV only, which is what
the files are (48 kHz, mono, 16-bit).

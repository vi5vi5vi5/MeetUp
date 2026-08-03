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
  drives the "speaking" highlight** for the local tile. The old level threshold
  was measuring the wrong quantity: loudness is not speech, so it both missed
  quiet sentences (below the threshold) and accepted any loud enough thud. Two
  thresholds rather than one, because a single one flickers at the boundary on
  every breath between words: the upper one lights the highlight, the lower one
  only sustains it. The level meter stays on RMS — it answers "how loud", which
  is a different and honestly-answered question.

  **Where no probability is available, `SpeechGate` answers instead**: for
  remote participants (their audio arrives encoded, and running every one of
  them through RNNoise would cost a percent of a core per person) and for
  ourselves when suppression is switched off. It does not compare loudness to a
  fixed number — that lies in both directions at once, deaf to a quiet person
  on a distant mic and lit permanently by a noisy room — but to *that speaker's
  own* noise floor, estimated as the **minimum frame over a 2 s window**. The
  minimum specifically: speech is intermittent, its gaps *are* the floor,
  whereas an average would be dragged along by the speech itself.

  That last part was arrived at the hard way, and the note is here so nobody
  re-derives it: the first implementation used exponential smoothing that only
  rose while silent — a guard against a long sentence pulling the floor up to
  itself. Scenario testing showed the guard turns into a permanent latch. Let
  the background rise at once (an air conditioner starts), the gate says
  "speech", and that same "speech" forbids the floor from ever following, so it
  never recovers. A windowed minimum has no such state; the cost is a bounded
  ~4 s of false highlight after an abrupt background change, which is the right
  trade against *forever*.

  On the receive path one more signal is free and better than any measurement
  of ours: a payload of **two bytes or fewer is Opus DTX** — the sender's own
  verdict on the sender's own signal. The floor keeps being fed from those
  frames anyway, since they decode to comfort noise, which is exactly the level
  of that person's room.

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

  **Auto-gain** (`AutoGain`) is the next stage, after suppression and *instead
  of* the sensitivity slider — never on top of it, since the slider is what
  displays the result and multiplying the two would close a feedback loop
  through the UI. It answers a different question than the denoiser: not "is
  this voice", but "is it the loudness the far end should get". Target is a
  fixed −20 dBFS on the envelope, which measures out at ≈ −23 dBov of
  long-term speech; it is deliberately not a setting, being exactly the knob
  nobody knows how to turn and everybody can get wrong.

  Three parts, all three load-bearing:
  - The **slow part** walks the speech level to the target, and walks
    asymmetrically: ~2 s upward (haste is audible as background swelling into
    the gaps between phrases) against ~0.2 s downward (delay is audible as
    clipping, which nothing downstream can undo). It moves in the **log
    domain**, so a given coefficient means a fixed number of dB per second
    regardless of where the gain currently sits; in linear gain the same
    coefficient would crawl near unity and bolt near 8×.

    "Speech level" here is an **envelope** — a recent maximum with a slow decay
    — and never the RMS of the current frame. This is the one mistake that
    broke the class outright, and the note is here so nobody re-derives it: the
    first version was driven by per-frame RMS and crept to +17 dB on live
    speech within twenty seconds. Inside a phrase the quiet frames *outnumber*
    the loud ones — word tails, breaths, gaps between words — and the same VAD
    marks them as speech, since its lower threshold exists precisely to stop
    the highlight flickering on every breath. Each of those frames honestly
    reports "20 dB short", and although the descent is nine times faster than
    the climb, there are enough of them to drag the equilibrium to the ceiling.
    A recent maximum has no such bias: it answers "how loudly is this person
    speaking", not "how loud is this particular 20 ms". The envelope is
    additionally only updated on frames that clear the noise floor by 12 dB —
    the VAD flag alone would let a long stretch of speech-without-voice pull it
    down, and the gain up after it.
  - The **limiter** holds the peak. Gain raised for a quiet talker turns a
    sudden laugh into Int16 overflow, so the frame's peak is measured *before*
    the frame is amplified — 20 ms of look-ahead that costs nothing, because
    the frame is already buffered whole. Both ends of the per-sample gain ramp
    are clamped to what that peak allows, which makes the saturation in the
    apply loop a guard rather than a mechanism. Attack is instant, release
    ~0.1 s: an instant release would make the limiter itself the pumping.
  - The **noise floor** caps how far the gain may rise (so a quiet room is not
    pulled up until every fly is audible) and sets the bar a frame must clear
    to count as voice at all. It is the minimum over a 150-frame window, taken
    across **every** frame rather than only the pauses — and that distinction
    is the whole point. Measuring it only where "there is no speech" makes it
    depend on the speech detector, and it is needed precisely where the
    detector is wrong: with suppression off and a window open, `SpeechGate`
    read the street as speech, so pauses never occurred, the floor was never
    measured, both limits switched off, and the gain climbed to the ceiling
    amplifying the street — seen in the field at 1500 %. That configuration is
    no longer reachable (auto-gain now requires suppression, below), but
    nothing here got simpler: RNNoise is wrong too, just less often. Same
    mistake as the first `SpeechGate`: "only update when the detector says X"
    breaks exactly when the detector says X wrongly. Speech is intermittent enough
    that a 3–6 s minimum lands in a gap between words anyway. The cap never
    goes below unity: a noisy room is a reason not to amplify, not a reason to
    make the person quieter.

    Until the first window closes there is no floor — a minimum over three
    frames is not a noise estimate — so for that first second **the gain is
    held** at whatever it started from. That second is the only time both
    limits are off, and quiet room noise is indistinguishable from a very
    quiet person; letting the gain move would buy a 2.4× swell that the floor
    then takes straight back.

  The gain is applied as a **linear ramp inside the frame**, not as a step at
  the frame boundary; fifty steps a second are audible as a rasp.

  **The speech decision is what makes the whole thing possible**, and it is
  already computed one line above for the highlight — RNNoise's probability
  when suppression is on, `SpeechGate` when it is off. Without it auto-gain
  reads a pause as "too quiet" and spends two seconds hoisting the noise floor
  into the foreground; the gain is therefore frozen whenever speech is absent.
  This is also why the level meter is measured twice per reported frame: once
  before gain (that is what the gate and the AGC are asking about) and once
  after (that is what the far end actually receives).

  **Auto-gain requires noise suppression** and its switch is disabled without
  it (`MediaSettings::autoGain` returns the choice *and* the permission). Not a
  dependency for tidiness: with a live room floor the noise ceiling pins the
  gain near unity, so the feature would switch on and do nothing, and a switch
  that does nothing is worse than one that is visibly unavailable. The stored
  choice is not overwritten — turning suppression back on restores auto-gain
  with it. The cost of the rule is the clean-room case: someone with a quiet
  room and a good mic who dislikes what RNNoise does to timbre now cannot have
  auto-gain alone.

  On the UI side the slider **becomes an indicator** while auto-gain is on:
  non-interactive, driven by `MediaSettings::agcSensitivity`. That value is
  runtime-only and never reaches QSettings — the gain is recomputed every 20 ms,
  and persisting it would be fifty disk writes a second to store the reading of
  a voltmeter. It is reported at ~10 Hz from the audio thread and rounded to the
  slider's own 5 % step, and it reports the **slow part only**: a handle
  following the limiter would show tremor instead of sensitivity. The scale
  stays 0–200 %, so past 2× the handle sits pinned at the right edge while the
  caption keeps printing the true figure.
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

  **Both hand-off queues are bounded on purpose, and the bound is a setting.**
  A frame crosses two thread boundaries by Qt's event queue — socket thread →
  decode thread on the way in, WGC pool thread → GUI thread on the way out — and
  a Qt event queue has no limit at all. Whenever the far side is slower than the
  stream, that queue *is* the backlog, and it never drains: switch the screen
  share from AV1 back to HEVC and the viewer keeps grinding through queued AV1
  for another half minute. The queue cannot be emptied after the fact, so the
  decision is made before a frame is posted, on the producing thread —
  `VideoRecvWorker::offer()` inbound, the same in-flight counter around
  `onScreenCapFrame` outbound. With buffering on (default) it always accepts;
  with buffering off it accepts only when the previous frame is done and drops
  the rest on the spot, which also raises a gap flag so the decoder asks for a
  keyframe instead of rendering deltas onto a picture it never saw. Both
  switches live in settings (`rxBuffer` / `txBuffer`), with a manual flush
  beside each: flushing bumps a generation counter, so everything already
  queued is recognised as stale and thrown away undecoded.

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
  immediately as before.

  **Overflow paints, it does not drop.** The queue is capped by a memory budget
  (`kHoldBudgetBytes`, 64 MB per peer per band — a 4K NV12 frame is 12 MB, so
  "16 frames" means wildly different things at 720p and at 4K) and by wall
  duration (`kSyncHoldMaxMs`). When either cap is hit, the head goes **to the
  sink**, not to the bin. Dropping it was a trap that cost the whole screen
  share: the head is the frame due next, so once the hold exceeded the queue's
  capacity — 267 ms at 60 fps with the old 16-frame cap — *every* frame was
  evicted before it ripened and the picture stopped entirely, while bytes kept
  arriving at full bitrate and the sender reported 0 % drops. Now the cap is a
  ceiling on compensation rather than a cliff: past it the picture runs with
  the sync error the queue could not absorb, which is the right trade. A stalled
  playhead is covered too — `drainHeld` releases anything held longer than
  `kSyncHoldMaxMs` regardless of what the audio clock says.
- **M4 Video send** (`VideoEngine` + `VideoEncoder`) — `QCamera` → sws_scale →
  encoder, even dimensions, keyframe every 3 s (camera) or 30 s (screen) plus
  forced on `KEYFRAME_REQ` / `participant_joined` → v2 packets, with
  backpressure (drop video frames above 1.5 MB queued in the socket). Self tile
  gets the raw camera preview.

  **The cadence is a clock, not a frame count**, and `KEYFRAME_REQ` names its
  band. Both are the same lesson from opposite ends: a screen keyframe is a
  quarter-megabyte burst, so who triggers it and how often has to be
  predictable. Counting frames made the period depend on the rate — "every 900
  frames" is 15 s at 60 fps and a minute at 15. And a bandless request meant one
  stalled camera receiver asking once a second fired a *screen* keyframe too,
  which congested the link, stalled the audio behind it, made that receiver
  worse, and had it ask again. The band travels as one byte of the payload;
  an empty payload still means "both bands", which is what the web client sends
  and what it understands.

  **The codec is chosen automatically, and can be overridden.** The automatic
  answer is a **ladder per band**, walked downward only by
  `Proto::CODEC_UNSUPPORTED` (`VideoEncoder::open`), and reset to the top on the
  next `join_ok` because the next room holds different people:
  - **screen: HEVC → H.264 → VP8**, hardware first. HEVC costs the same to
    encode as H.264 on this card (2.4 vs 2.2 ms at 1440p) and a third of the
    bandwidth at equal quality (4158 vs 10832 kbit/s at 1080p). H.264 is the
    compatibility rung — no browser lacks it, and plenty lack HEVC.
  - **camera: VP9 → H.264 → VP8**, software throughout. VP9 is the fastest
    software encoder measured (3.6 ms at 1080p against openh264's 16.3) and
    browsers decode it. Hardware is withheld deliberately, see below.

  The override sits in front of that ladder as a **rung zero**: the encoder
  named in `MediaSettings::screenCodec` / `camCodec` is tried first, and if it
  will not open — or a receiver complains about it — the ladder carries on
  exactly as before. So the setting is a preference, never a way to end up with
  no picture at all. The list offered in settings comes from `probeCodecs()`,
  which actually opens each candidate at 640×360 on a throwaway MTA thread
  (Media Foundation answers nowhere else, and the screen thread must not stall
  mid-share for the third of a second this costs). Hardware and software
  variants of the same codec are separate entries, because the difference
  between them is the whole point of asking.

  A complaint is only acted on when the codec named in it is the one *this*
  band is currently sending (`m_scrCodec` / `m_camCodec`): the server fans
  `CODEC_UNSUPPORTED` out to every sender, and without that check one
  participant's missing HEVC would demote a second presenter who was already
  on H.264. Complaints now also come from the receive side for a codec that is
  *understood but not affordable* — a software decoder eating more than 70 % of
  the frame interval for 1.5 s straight asks the sender to step down, since
  falling behind by a growing margin looks exactly like not decoding at all.

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
  Noise suppression and auto-gain are wired; **echo cancellation is still
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

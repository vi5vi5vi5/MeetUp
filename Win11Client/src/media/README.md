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
- **M8 Settings** (`../MediaSettings`) — device selection (mic/cam/speakers),
  playback volume and mic sensitivity (0–200 %), send-quality presets
  (low/med/high for camera resolution+bitrate+fps and Opus bitrate), persisted
  in QSettings and applied live by both engines.

External deps introduced here (MSVC, via vcpkg): FFmpeg (avcodec/swscale with
`openh264` + `vpx` features) and libopus.

# src/media — capture, codecs, playback (milestones M2–M4)

Reserved for the media pipeline (see `docs/ROADMAP.md`).

- **M2 Audio** — `QAudioSource` capture (48 kHz mono) → libopus 20 ms frames →
  v2 packets; receive → libopus decode → jitter buffer (3-chunk prebuffer) →
  `QAudioSink`.
- **M3 Video receive** — FFmpeg (libavcodec) decode of H.264/VP8/VP9, keyframe
  and `KEYFRAME_REQ` handling, render into tiles.
- **M4 Video send** — `QCamera` → encode (H.264 Annex B or VP8, even
  dimensions, keyframe every ~72 frames) → v2 packets, with backpressure
  (drop video above 1.5 MB queued).

External deps introduced here (MSVC, via vcpkg): FFmpeg (avcodec/avutil/
swscale/swresample) and libopus.

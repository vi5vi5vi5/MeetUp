# src/media — capture, codecs, playback (milestones M2–M4, M8)

The media pipeline (see `docs/ROADMAP.md`).

- **M2 Audio** (`AudioEngine`) — `QAudioSource` capture (48 kHz mono) → libopus
  20 ms frames → v2 packets; receive → libopus decode → jitter buffer (3-chunk
  prebuffer) → mix → `QAudioSink`.
- **M3 Video receive** (`VideoEngine` + `VideoDecoder`) — FFmpeg (libavcodec)
  decode of H.264/VP8/VP9, keyframe and `KEYFRAME_REQ` handling, render into
  tiles via `QVideoSink`.
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

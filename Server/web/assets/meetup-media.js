"use strict";
// MeetUpMedia — медиадвижок конференции: захват, кодеки, приём и E2E.
//
// Отправка: видео VideoEncoder (H.264/VP8/VP9 — первый поддержанный) до
// 720p, аудио AudioEncoder (Opus 48 кГц/32 кбит). Браузеру без WebCodecs —
// фолбэк на прежний формат: JPEG-кадры 10 к/с и PCM 16 кГц.
//
// Протокол v2 (бинарь через WebSocket, сервер прозрачно вставляет sender
// после первого байта и НЕ трогает остальное):
//   клиент шлёт:    [type:1][flags:1][codec:1][ts:8 LE][payload]
//   клиент получает:[type:1][sender:4 LE][flags:1][codec:1][ts:8 LE][payload]
// ts: для кодек-чанков — chunk.timestamp (мкс), для legacy — Date.now() (мс).
//
// E2E: AES-256-GCM поверх payload (WebCrypto), ключ на сервер не уходит.
// Формат шифрованного payload: [iv:12][ciphertext+tag]; AAD = [type, codec].
// Заголовок остаётся открытым: серверу нужен type, приёмнику — codec и
// флаг keyframe до расшифровки.
(function () {

  const MSG = { VIDEO_JPEG: 1, AUDIO_PCM: 2, VIDEO_CODED: 3, AUDIO_CODED: 4, KEYFRAME_REQ: 6 };
  const FLAG_KEYFRAME = 1;
  const FLAG_ENCRYPTED = 2;
  const CHAT_AAD_TYPE = 250;              // «тип» для шифрования текста чата

  const CODEC_BY_ID = { 1: "vp8", 2: "avc1.42E01F", 3: "vp09.00.10.08", 4: "opus" };
  const VIDEO_TRY = [
    { id: 2, codec: "avc1.42E01F", extra: { avc: { format: "annexb" } } },  // аппаратный почти везде
    { id: 1, codec: "vp8", extra: {} },
    { id: 3, codec: "vp09.00.10.08", extra: {} },
  ];
  const OPUS_ID = 4;

  const AUDIO_RATE = 48000;               // AudioContext форсируется на 48к (родной рейт Opus)
  const AUDIO_FRAME = 960;                // 20 мс
  const PCM_RATE = 16000;                 // legacy-фолбэк
  const JPEG_INTERVAL_MS = 100;           // legacy ~10 к/с
  const KEY_EVERY_FRAMES = 72;            // keyframe раз в ~3 с при 24 к/с
  const MAX_BUFFERED = 1.5e6;             // затык сети: пропускаем кадры

  // ---- Захват микрофона: копит кванты по 128 в чанки 960 (20 мс) ----
  const CAPTURE_WORKLET = `
    class MuCapture extends AudioWorkletProcessor {
      constructor() { super(); this.buf = new Float32Array(${AUDIO_FRAME}); this.n = 0; }
      process(inputs) {
        const ch = inputs[0] && inputs[0][0];
        if (!ch) return true;
        let i = 0;
        while (i < ch.length) {
          const take = Math.min(${AUDIO_FRAME} - this.n, ch.length - i);
          this.buf.set(ch.subarray(i, i + take), this.n);
          this.n += take; i += take;
          if (this.n === ${AUDIO_FRAME}) {
            const out = this.buf.slice(0);
            this.port.postMessage(out, [out.buffer]);
            this.n = 0;
          }
        }
        return true;
      }
    }
    registerProcessor("mu-capture", MuCapture);`;

  // ---- Плеер: очередь чанков + предбуфер 60 мс, дроп при лаге >240 мс ----
  const PLAYER_WORKLET = `
    class MuPlayer extends AudioWorkletProcessor {
      constructor() {
        super();
        this.queue = []; this.cur = null; this.off = 0; this.priming = true;
        this.port.onmessage = (e) => {
          this.queue.push(e.data);
          if (this.queue.length > 12) this.queue.splice(0, this.queue.length - 6);
        };
      }
      process(_in, outputs) {
        const out = outputs[0] && outputs[0][0];
        if (!out) return true;
        if (this.priming) {
          if (this.queue.length < 3) return true;   // тишина, копим предбуфер
          this.priming = false;
        }
        let i = 0;
        while (i < out.length) {
          if (!this.cur) {
            this.cur = this.queue.shift();
            this.off = 0;
            if (!this.cur) { this.priming = true; break; }   // недобор — копим заново
          }
          const take = Math.min(out.length - i, this.cur.length - this.off);
          out.set(this.cur.subarray(this.off, this.off + take), i);
          i += take; this.off += take;
          if (this.off >= this.cur.length) this.cur = null;
        }
        return true;
      }
    }
    registerProcessor("mu-player", MuPlayer);`;

  // ---- base64url ----
  function u8ToB64(u8) {
    let s = "";
    for (let i = 0; i < u8.length; i++) s += String.fromCharCode(u8[i]);
    return btoa(s).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
  }
  function b64ToU8(b64) {
    const s = atob(b64.replace(/-/g, "+").replace(/_/g, "/"));
    const u8 = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) u8[i] = s.charCodeAt(i);
    return u8;
  }

  // ---- E2E: ключи ----
  async function deriveKeyFromPhrase(phrase, roomCode) {
    const enc = new TextEncoder();
    const base = await crypto.subtle.importKey("raw", enc.encode(phrase), "PBKDF2", false, ["deriveKey"]);
    return crypto.subtle.deriveKey(
      { name: "PBKDF2", hash: "SHA-256", iterations: 150000,
        salt: enc.encode("meetup-e2e-v1|" + roomCode) },
      base, { name: "AES-GCM", length: 256 }, true, ["encrypt", "decrypt"]);
  }
  async function importKeyB64(b64) {
    const raw = b64ToU8(b64);
    if (raw.length !== 32) throw new Error("bad key length");
    return crypto.subtle.importKey("raw", raw, { name: "AES-GCM" }, true, ["encrypt", "decrypt"]);
  }
  async function exportKeyB64(key) {
    return u8ToB64(new Uint8Array(await crypto.subtle.exportKey("raw", key)));
  }
  async function randomKey() {
    return importKeyB64(u8ToB64(crypto.getRandomValues(new Uint8Array(32))));
  }

  // IV = [случайный префикс сессии:4][счётчик:8] — уникален в пределах ключа.
  function makeCipher(key) {
    const prefix = crypto.getRandomValues(new Uint8Array(4));
    let counter = 0n;
    return {
      async seal(type, codecId, u8) {
        const iv = new Uint8Array(12);
        iv.set(prefix, 0);
        new DataView(iv.buffer).setBigUint64(4, counter++, true);
        const aad = new Uint8Array([type, codecId]);
        const ct = await crypto.subtle.encrypt({ name: "AES-GCM", iv, additionalData: aad }, key, u8);
        const out = new Uint8Array(12 + ct.byteLength);
        out.set(iv, 0);
        out.set(new Uint8Array(ct), 12);
        return out;
      },
      async open(type, codecId, u8) {
        if (u8.length < 12 + 16) return null;
        try {
          const pt = await crypto.subtle.decrypt(
            { name: "AES-GCM", iv: u8.subarray(0, 12), additionalData: new Uint8Array([type, codecId]) },
            key, u8.subarray(12));
          return new Uint8Array(pt);
        } catch (e) {
          return null;   // не тот ключ или порченые данные
        }
      },
    };
  }

  function packV2(type, flags, codecId, tsBig, payload) {
    const out = new Uint8Array(11 + payload.length);
    const dv = new DataView(out.buffer);
    dv.setUint8(0, type);
    dv.setUint8(1, flags);
    dv.setUint8(2, codecId);
    dv.setBigUint64(3, tsBig, true);
    out.set(payload, 11);
    return out.buffer;
  }

  function create(opts) {
    // opts: send(buf), buffered()->число, micOn()->bool,
    //       onSelfSpeaking(), onSpeaking(id), onFrameActivity(id), onLocked(id, bool)
    const st = {
      ctx: null, masterGain: null, workletsReady: null,
      volume: 1, sens: 1, micLevel: 0,
      cipher: null,
      // отправка
      videoChoice: undefined,     // undefined = детект идёт, null = только legacy
      audioCoded: false,
      capEl: null, capToken: 0, videoActive: false, isScreen: false,
      venc: null, vencW: 0, vencH: 0, vframes: 0, keyNext: false, lastForceAt: 0,
      aenc: null, aTs: 0,
      micSrc: null, capNode: null,
      jpegCanvas: null, lastJpegAt: 0, jpegBusy: false,
      sendChains: { a: Promise.resolve(), v: Promise.resolve() },
      lastKeyReqAt: 0,
      // приём
      peers: new Map(),           // id -> peer
      sinks: new Map(),           // id -> <canvas>
      sinkRefs: new Map(),
      lastFrameAt: new Map(),
      stats: { videoCodec: "", audioCodec: "" },
    };

    // ---------- Детект кодеков ----------

    const detect = (async () => {
      st.videoChoice = null;
      if (typeof VideoEncoder !== "undefined" && typeof VideoDecoder !== "undefined") {
        for (const c of VIDEO_TRY) {
          try {
            const r = await VideoEncoder.isConfigSupported(videoCfg(c, 1280, 720, false));
            if (r.supported) { st.videoChoice = c; break; }
          } catch (e) { /* кодек неизвестен браузеру */ }
        }
      }
      if (typeof AudioEncoder !== "undefined" && typeof AudioDecoder !== "undefined") {
        try {
          const r = await AudioEncoder.isConfigSupported(
            { codec: "opus", sampleRate: AUDIO_RATE, numberOfChannels: 1, bitrate: 32000 });
          st.audioCoded = !!r.supported;
        } catch (e) { st.audioCoded = false; }
      }
      st.stats.videoCodec = st.videoChoice ? st.videoChoice.codec : "jpeg";
      st.stats.audioCodec = st.audioCoded ? "opus" : "pcm";
    })();

    function videoCfg(choice, w, h, screen) {
      return Object.assign({
        codec: choice.codec, width: w, height: h,
        bitrate: screen ? 1500000 : 1200000,
        framerate: screen ? 15 : 24,
        latencyMode: "realtime",
      }, choice.extra);
    }

    // ---------- Аудиографа ----------

    async function ensureAudio() {
      if (st.ctx) return st.workletsReady;
      const AC = window.AudioContext || window.webkitAudioContext;
      if (!AC) return null;
      // 48 кГц — родной рейт Opus; захват и воспроизведение в одном контексте.
      try { st.ctx = new AC({ sampleRate: AUDIO_RATE }); }
      catch (e) { st.ctx = new AC(); }
      st.masterGain = st.ctx.createGain();
      st.masterGain.gain.value = st.volume;
      st.masterGain.connect(st.ctx.destination);
      if (st.ctx.state === "suspended") {
        // Autoplay-policy: без жеста звук не стартует (например, после F5).
        const resume = () => {
          st.ctx.resume();
          document.removeEventListener("pointerdown", resume);
        };
        document.addEventListener("pointerdown", resume);
      }
      st.workletsReady = (async () => {
        if (!st.ctx.audioWorklet) return null;
        const mod = new Blob([CAPTURE_WORKLET + "\n" + PLAYER_WORKLET], { type: "application/javascript" });
        const url = URL.createObjectURL(mod);
        try { await st.ctx.audioWorklet.addModule(url); } finally { URL.revokeObjectURL(url); }
        return true;
      })();
      return st.workletsReady;
    }

    // ---------- Отправка: микрофон ----------

    async function startMic(stream) {
      await ensureAudio();
      await detect;
      if (!st.ctx) return;
      if (st.micSrc) { try { st.micSrc.disconnect(); } catch (e) {} }
      st.micSrc = st.ctx.createMediaStreamSource(stream);
      if (!st.capNode && (await st.workletsReady)) {
        st.capNode = new AudioWorkletNode(st.ctx, "mu-capture",
          { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1] });
        // Узел должен быть в графе до destination, иначе не тикает — глушим нулём.
        const mute = st.ctx.createGain();
        mute.gain.value = 0;
        st.capNode.connect(mute).connect(st.ctx.destination);
        st.capNode.port.onmessage = (e) => onMicChunk(e.data);
      }
      if (st.capNode) st.micSrc.connect(st.capNode);
    }

    function onMicChunk(f32) {
      let sum = 0, n = 0;
      for (let i = 0; i < f32.length; i += 8) { const v = f32[i] * st.sens; sum += v * v; n++; }
      st.micLevel = n ? Math.sqrt(sum / n) : 0;
      if (!opts.micOn()) return;
      if (st.micLevel > 0.02) opts.onSelfSpeaking();

      if (st.audioCoded) {
        if (!ensureAudioEncoder()) return;
        if (st.aenc.encodeQueueSize > 4) return;   // сеть/CPU не успевают — дропаем
        const buf = st.sens === 1 ? f32 : withGain(f32, st.sens);
        const ad = new AudioData({ format: "f32", sampleRate: AUDIO_RATE,
                                   numberOfFrames: f32.length, numberOfChannels: 1,
                                   timestamp: st.aTs, data: buf });
        st.aTs += Math.round(f32.length * 1e6 / AUDIO_RATE);
        try { st.aenc.encode(ad); } catch (e) { st.aenc = null; }
        ad.close();
      } else {
        // Legacy: 48к -> 16к усреднением по 3 + Int16.
        const out = new Int16Array(Math.floor(f32.length / 3));
        for (let i = 0; i < out.length; i++) {
          const j = i * 3;
          const v = Math.max(-1, Math.min(1, (f32[j] + f32[j + 1] + f32[j + 2]) / 3 * st.sens));
          out[i] = v < 0 ? v * 0x8000 : v * 0x7FFF;
        }
        enqueueSend(MSG.AUDIO_PCM, 0, 0, BigInt(Date.now()), new Uint8Array(out.buffer), "a");
      }
    }

    function withGain(f32, g) {
      const out = new Float32Array(f32.length);
      for (let i = 0; i < f32.length; i++) out[i] = Math.max(-1, Math.min(1, f32[i] * g));
      return out;
    }

    function ensureAudioEncoder() {
      if (st.aenc) return true;
      try {
        st.aenc = new AudioEncoder({
          output: (chunk) => {
            const u8 = new Uint8Array(chunk.byteLength);
            chunk.copyTo(u8);
            enqueueSend(MSG.AUDIO_CODED, 0, OPUS_ID,
                        BigInt(Math.max(0, Math.round(chunk.timestamp || 0))), u8, "a");
          },
          error: () => { st.aenc = null; },
        });
        st.aenc.configure({ codec: "opus", sampleRate: AUDIO_RATE, numberOfChannels: 1, bitrate: 32000 });
        return true;
      } catch (e) {
        st.aenc = null;
        st.audioCoded = false;   // не вышло — до конца сессии шлём PCM
        return false;
      }
    }

    // ---------- Отправка: видео ----------

    function startVideo(stream, o) {
      stopVideo();
      st.isScreen = !!(o && o.screen);
      st.videoActive = true;
      const v = st.capEl || (st.capEl = document.createElement("video"));
      v.muted = true; v.playsInline = true; v.autoplay = true;
      v.srcObject = stream;
      const p = v.play();
      if (p && p.catch) p.catch(() => {});
      const token = ++st.capToken;
      if ("requestVideoFrameCallback" in HTMLVideoElement.prototype) {
        const step = () => {
          if (st.capToken !== token || !st.videoActive) return;
          captureTick(v);
          v.requestVideoFrameCallback(step);
        };
        v.requestVideoFrameCallback(step);
      } else {
        const timer = setInterval(() => {
          if (st.capToken !== token || !st.videoActive) { clearInterval(timer); return; }
          captureTick(v);
        }, 1000 / 24);
      }
    }

    function stopVideo() {
      st.videoActive = false;
      st.capToken++;
      if (st.venc) { try { st.venc.close(); } catch (e) {} st.venc = null; }
      if (st.capEl) st.capEl.srcObject = null;
    }

    function captureTick(v) {
      if (!v.videoWidth || v.readyState < 2) return;
      if (st.videoChoice === undefined) return;          // детект кодеков ещё идёт
      if (st.videoChoice === null) { legacyJpegTick(v); return; }
      if (opts.buffered() > MAX_BUFFERED) return;        // сеть не успевает — пропуск кадра

      const w = v.videoWidth & ~1, h = v.videoHeight & ~1;
      if (!st.venc || st.vencW !== w || st.vencH !== h) {
        if (st.venc) { try { st.venc.close(); } catch (e) {} }
        const choice = st.videoChoice;
        try {
          st.venc = new VideoEncoder({
            output: (chunk) => {
              const u8 = new Uint8Array(chunk.byteLength);
              chunk.copyTo(u8);
              enqueueSend(MSG.VIDEO_CODED, chunk.type === "key" ? FLAG_KEYFRAME : 0, choice.id,
                          BigInt(Math.max(0, Math.round(chunk.timestamp || 0))), u8, "v");
            },
            error: () => { st.venc = null; },   // пересоздастся следующим кадром
          });
          st.venc.configure(videoCfg(choice, w, h, st.isScreen));
          st.vencW = w; st.vencH = h; st.vframes = 0;
        } catch (e) {
          st.venc = null;
          st.videoChoice = null;                // WebCodecs сломан — legacy до конца сессии
          st.stats.videoCodec = "jpeg";
          return;
        }
      }
      if (st.venc.encodeQueueSize > 2) return;  // энкодер захлебнулся — пропуск

      let frame;
      try {
        frame = new VideoFrame(v, { timestamp: Math.round(performance.now() * 1000) });
      } catch (e) {
        return;   // кадр ещё не готов
      }
      const key = st.keyNext || st.vframes % KEY_EVERY_FRAMES === 0;
      st.keyNext = false;
      st.vframes++;
      try { st.venc.encode(frame, { keyFrame: key }); } catch (e) { st.venc = null; }
      frame.close();
    }

    function legacyJpegTick(v) {
      const now = Date.now();
      if (now - st.lastJpegAt < JPEG_INTERVAL_MS || st.jpegBusy) return;
      if (opts.buffered() > MAX_BUFFERED) return;
      st.lastJpegAt = now;
      const c = st.jpegCanvas || (st.jpegCanvas = document.createElement("canvas"));
      const vw = v.videoWidth || 480, vh = v.videoHeight || 360;
      const scale = Math.min(480 / vw, 360 / vh, 1);
      const cw = Math.max(2, Math.round(vw * scale)), ch = Math.max(2, Math.round(vh * scale));
      if (c.width !== cw || c.height !== ch) { c.width = cw; c.height = ch; }
      c.getContext("2d").drawImage(v, 0, 0, cw, ch);
      st.jpegBusy = true;
      c.toBlob((blob) => {
        st.jpegBusy = false;
        if (!blob || !st.videoActive) return;
        blob.arrayBuffer().then((ab) => {
          enqueueSend(MSG.VIDEO_JPEG, 0, 0, BigInt(Date.now()), new Uint8Array(ab), "v");
        });
      }, "image/jpeg", 0.7);
    }

    function forceKeyframe() {
      const now = Date.now();
      if (now - st.lastForceAt < 500) return;
      st.lastForceAt = now;
      st.keyNext = true;
    }

    function requestKeyframe() {
      const now = Date.now();
      if (now - st.lastKeyReqAt < 1000) return;
      st.lastKeyReqAt = now;
      opts.send(packV2(MSG.KEYFRAME_REQ, 0, 0, 0n, new Uint8Array(0)));
    }

    // Отправка с шифрованием; цепочка на канал сохраняет порядок чанков
    // (crypto.subtle асинхронный и мог бы переставить их местами).
    function enqueueSend(type, flags, codecId, tsBig, payload, lane) {
      const cipher = st.cipher;
      st.sendChains[lane] = st.sendChains[lane].then(async () => {
        let body = payload, fl = flags;
        if (cipher) {
          body = await cipher.seal(type, codecId, payload);
          fl |= FLAG_ENCRYPTED;
        }
        opts.send(packV2(type, fl, codecId, tsBig, body));
      }).catch(() => {});
    }

    // ---------- Приём ----------

    function ensurePeer(id) {
      let p = st.peers.get(id);
      if (!p) {
        p = { chains: { a: Promise.resolve(), v: Promise.resolve() },
              vdec: null, vcodec: 0, awaitKey: true,
              adec: null, player: null,
              pcmCursor: 0, fails: 0, locked: false };
        st.peers.set(id, p);
      }
      return p;
    }

    function onBinary(buffer) {
      if (buffer.byteLength < 15) return;
      const dv = new DataView(buffer);
      const type = dv.getUint8(0);
      const sender = dv.getUint32(1, true);
      const flags = dv.getUint8(5);
      const codecId = dv.getUint8(6);
      const ts = dv.getBigUint64(7, true);
      if (type === MSG.KEYFRAME_REQ) { forceKeyframe(); return; }
      const payload = new Uint8Array(buffer, 15);
      const lane = (type === MSG.AUDIO_PCM || type === MSG.AUDIO_CODED) ? "a" : "v";
      const peer = ensurePeer(sender);
      peer.chains[lane] = peer.chains[lane]
        .then(() => handleMedia(peer, sender, type, flags, codecId, ts, payload))
        .catch(() => {});
    }

    async function handleMedia(peer, sender, type, flags, codecId, ts, payload) {
      let body = payload;
      if (flags & FLAG_ENCRYPTED) {
        if (!st.cipher) { setLocked(peer, sender, true); return; }
        body = await st.cipher.open(type, codecId, payload);
        if (!body) {
          if (++peer.fails >= 3) setLocked(peer, sender, true);
          return;
        }
      }
      peer.fails = 0;
      setLocked(peer, sender, false);

      switch (type) {
        case MSG.VIDEO_CODED: decodeVideo(peer, sender, flags, codecId, ts, body); break;
        case MSG.VIDEO_JPEG:  paintJpeg(sender, body); break;
        case MSG.AUDIO_CODED: decodeAudio(peer, codecId, ts, body, sender); break;
        case MSG.AUDIO_PCM:   playPcm(peer, sender, body); break;
      }
    }

    function setLocked(peer, sender, locked) {
      if (peer.locked === locked) return;
      peer.locked = locked;
      opts.onLocked(sender, locked);
    }

    function decodeVideo(peer, sender, flags, codecId, ts, body) {
      if (!CODEC_BY_ID[codecId]) return;
      if (!peer.vdec || peer.vcodec !== codecId) {
        if (peer.vdec) { try { peer.vdec.close(); } catch (e) {} }
        if (typeof VideoDecoder === "undefined") return;
        peer.vdec = new VideoDecoder({
          output: (frame) => paintFrame(sender, frame),
          error: () => { peer.vdec = null; peer.awaitKey = true; requestKeyframe(); },
        });
        try { peer.vdec.configure({ codec: CODEC_BY_ID[codecId], optimizeForLatency: true }); }
        catch (e) { peer.vdec = null; return; }
        peer.vcodec = codecId;
        peer.awaitKey = true;
      }
      const isKey = !!(flags & FLAG_KEYFRAME);
      if (peer.awaitKey && !isKey) { requestKeyframe(); return; }   // ждём опорный кадр
      peer.awaitKey = false;
      try {
        peer.vdec.decode(new EncodedVideoChunk({
          type: isKey ? "key" : "delta", timestamp: Number(ts), data: body }));
      } catch (e) {
        try { peer.vdec.close(); } catch (e2) {}
        peer.vdec = null;
        peer.awaitKey = true;
        requestKeyframe();
      }
    }

    function paintFrame(sender, frame) {
      st.lastFrameAt.set(sender, Date.now());
      const canvas = st.sinks.get(sender);
      if (canvas) {
        const w = frame.displayWidth, h = frame.displayHeight;
        if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
        canvas.getContext("2d").drawImage(frame, 0, 0, w, h);
      }
      frame.close();
      opts.onFrameActivity(sender);
    }

    function paintJpeg(sender, body) {
      const blob = new Blob([body], { type: "image/jpeg" });
      createImageBitmap(blob).then((bmp) => {
        st.lastFrameAt.set(sender, Date.now());
        const canvas = st.sinks.get(sender);
        if (canvas) {
          if (canvas.width !== bmp.width || canvas.height !== bmp.height) {
            canvas.width = bmp.width; canvas.height = bmp.height;
          }
          canvas.getContext("2d").drawImage(bmp, 0, 0);
        }
        bmp.close();
        opts.onFrameActivity(sender);
      }).catch(() => {});
    }

    function decodeAudio(peer, codecId, ts, body, sender) {
      if (codecId !== OPUS_ID || typeof AudioDecoder === "undefined") return;
      if (!peer.adec) {
        try {
          peer.adec = new AudioDecoder({
            output: (ad) => playDecoded(peer, sender, ad),
            error: () => { peer.adec = null; },
          });
          peer.adec.configure({ codec: "opus", sampleRate: AUDIO_RATE, numberOfChannels: 1 });
        } catch (e) { peer.adec = null; return; }
      }
      try {
        peer.adec.decode(new EncodedAudioChunk({ type: "key", timestamp: Number(ts), data: body }));
      } catch (e) {
        try { peer.adec.close(); } catch (e2) {}
        peer.adec = null;
      }
    }

    async function playDecoded(peer, sender, ad) {
      const n = ad.numberOfFrames;
      const f32 = new Float32Array(n);
      try {
        // Моно: planar и interleaved совпадают; формат зависит от браузера.
        if (ad.format === "s16" || ad.format === "s16-planar") {
          const i16 = new Int16Array(n);
          ad.copyTo(i16, { planeIndex: 0 });
          for (let i = 0; i < n; i++) f32[i] = i16[i] / 32768;
        } else {
          ad.copyTo(f32, { planeIndex: 0 });
        }
      } catch (e) { ad.close(); return; }
      ad.close();

      let sum = 0, m = 0;
      for (let i = 0; i < n; i += 16) { sum += f32[i] * f32[i]; m++; }
      if (m && Math.sqrt(sum / m) > 0.02) opts.onSpeaking(sender);

      if (!st.ctx || st.ctx.state !== "running" || !(await st.workletsReady)) return;
      if (!peer.player) {
        peer.player = new AudioWorkletNode(st.ctx, "mu-player",
          { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [1] });
        peer.player.connect(st.masterGain);
      }
      peer.player.port.postMessage(f32, [f32.buffer]);
    }

    // Legacy PCM 16 кГц: планирование BufferSource по курсору (как раньше).
    function playPcm(peer, sender, body) {
      const samples = Math.floor(body.byteLength / 2);
      if (!samples || !st.ctx || st.ctx.state !== "running") return;
      const i16 = new Int16Array(body.buffer, body.byteOffset, samples);
      let sum = 0, m = 0;
      for (let i = 0; i < samples; i += 4) { const v = i16[i] / 32768; sum += v * v; m++; }
      if (m && Math.sqrt(sum / m) > 0.02) opts.onSpeaking(sender);

      const buf = st.ctx.createBuffer(1, samples, PCM_RATE);
      const ch = buf.getChannelData(0);
      for (let i = 0; i < samples; i++) ch[i] = i16[i] / 32768;
      const now = st.ctx.currentTime;
      if (peer.pcmCursor < now + 0.06) peer.pcmCursor = now + 0.06;
      if (peer.pcmCursor > now + 0.7) peer.pcmCursor = now + 0.1;
      const src = st.ctx.createBufferSource();
      src.buffer = buf;
      src.connect(st.masterGain);
      src.start(peer.pcmCursor);
      peer.pcmCursor += buf.duration;
    }

    // ---------- Плитки: ref-колбэки для <canvas> ----------

    function videoSinkRef(id) {
      let cb = st.sinkRefs.get(id);
      if (!cb) {
        cb = (el) => {
          if (el) st.sinks.set(id, el);
          else st.sinks.delete(id);
        };
        st.sinkRefs.set(id, cb);
      }
      return cb;
    }

    // ---------- Управление ----------

    function dropPeer(id) {
      const p = st.peers.get(id);
      if (p) {
        if (p.vdec) { try { p.vdec.close(); } catch (e) {} }
        if (p.adec) { try { p.adec.close(); } catch (e) {} }
        if (p.player) { try { p.player.disconnect(); } catch (e) {} }
        st.peers.delete(id);
      }
      st.lastFrameAt.delete(id);
    }

    function clearPeers() {
      for (const id of Array.from(st.peers.keys())) dropPeer(id);
    }

    function stop() {
      stopVideo();
      clearPeers();
      if (st.aenc) { try { st.aenc.close(); } catch (e) {} st.aenc = null; }
      if (st.micSrc) { try { st.micSrc.disconnect(); } catch (e) {} }
      if (st.ctx) { const p = st.ctx.close(); if (p && p.catch) p.catch(() => {}); }
    }

    return {
      ensureAudio, startMic, startVideo, stopVideo, stop,
      onBinary, forceKeyframe, requestKeyframe,
      videoSinkRef, dropPeer, clearPeers,
      lastFrameAt: st.lastFrameAt,
      micLevel: () => st.micLevel,
      setVolume: (v) => { st.volume = v; if (st.masterGain) st.masterGain.gain.value = v; },
      setSensitivity: (v) => { st.sens = v; },
      setSinkId: (id) => {
        if (st.ctx && st.ctx.setSinkId) {
          const p = st.ctx.setSinkId(id);
          if (p && p.catch) p.catch(() => {});
        }
      },
      setKey: (key) => {
        st.cipher = key ? makeCipher(key) : null;
        st.peers.forEach((p) => { p.fails = 0; });
        forceKeyframe();
      },
      encrypted: () => !!st.cipher,
      // Текст чата: "🔒e2e:<base64url(iv|ct)>" — сервер хранит его как обычный текст.
      sealText: async (text) => {
        if (!st.cipher) return null;
        return "🔒e2e:" + u8ToB64(await st.cipher.seal(CHAT_AAD_TYPE, 0, new TextEncoder().encode(text)));
      },
      openText: async (s) => {
        if (typeof s !== "string" || !s.startsWith("🔒e2e:")) return { encrypted: false, text: s };
        if (!st.cipher) return { encrypted: true, text: null };
        let pt = null;
        try { pt = await st.cipher.open(CHAT_AAD_TYPE, 0, b64ToU8(s.slice(6))); } catch (e) {}
        return { encrypted: true, text: pt ? new TextDecoder().decode(pt) : null };
      },
      stats: () => ({ videoCodec: st.stats.videoCodec, audioCodec: st.stats.audioCodec,
                      encrypted: !!st.cipher }),
      codecsReady: detect,
    };
  }

  window.MeetUpMedia = {
    create: (opts) => {
      const engine = create(opts);
      window.MeetUpMedia.lastEngine = engine;   // для отладки и автотестов
      return engine;
    },
    deriveKeyFromPhrase, importKeyB64, exportKeyB64, randomKey,
  };
})();

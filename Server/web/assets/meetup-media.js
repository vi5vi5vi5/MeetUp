"use strict";
// MeetUpMedia — медиадвижок конференции: захват, кодеки, приём и E2E.
//
// Отправка: видео VideoEncoder (H.264/VP8/VP9 — первый поддержанный) до
// 720p, аудио AudioEncoder (Opus 48 кГц/32 кбит). Браузеру без WebCodecs —
// фолбэк на прежний формат: JPEG-кадры 10 к/с и PCM 16 кГц.
// Демонстрация экрана — отдельный поток (типы SCREEN_*) параллельно камере.
//
// Протокол v2 (бинарь через WebSocket, сервер прозрачно вставляет sender
// после первого байта и НЕ трогает остальное):
//   клиент шлёт:    [type:1][flags:1][codec:1][ts:8 LE][payload]
//   клиент получает:[type:1][sender:4 LE][flags:1][codec:1][ts:8 LE][payload]
// ts — Date.now() (мс, стенные часы отправителя) на всех дорожках: единая
// шкала у аудио и видео нужна для синхронизации губ на приёме. Метку из
// пакета приёмник отдаёт декодеру как PTS (chunk.timestamp — отдельная,
// внутренняя шкала энкодера, наружу не уходит).
//
// E2E: AES-256-GCM поверх payload (WebCrypto), ключ на сервер не уходит.
// Формат шифрованного payload: [iv:12][ciphertext+tag]; AAD = [type, codec].
// Заголовок остаётся открытым: серверу нужен type, приёмнику — codec и
// флаг keyframe до расшифровки.
(function () {

  // Камера и демонстрация экрана — независимые потоки: у экрана свои типы,
  // чтобы приёмник держал два декодера и рисовал их в разные места.
  const MSG = { VIDEO_JPEG: 1, AUDIO_PCM: 2, VIDEO_CODED: 3, AUDIO_CODED: 4,
                SCREEN_CODED: 5, KEYFRAME_REQ: 6, SCREEN_JPEG: 7 };
  const FLAG_KEYFRAME = 1;
  const FLAG_ENCRYPTED = 2;
  const CHAT_AAD_TYPE = 250;              // «тип» для шифрования текста чата
  const IMAGE_AAD_TYPE = 251;             // …и картинок чата

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

  // ---- Плеер: очередь чанков + предбуфер 60 мс, дроп при лаге >160 мс.
  // Раз в ~170 мс шлёт наружу глубину буфера (в сэмплах) — по ней приёмник
  // придерживает видеокадры, чтобы губы совпадали со звуком. ----
  const PLAYER_WORKLET = `
    class MuPlayer extends AudioWorkletProcessor {
      constructor() {
        super();
        this.queue = []; this.cur = null; this.off = 0; this.priming = true;
        this.ticks = 0;
        this.port.onmessage = (e) => {
          this.queue.push(e.data);
          if (this.queue.length > 8) this.queue.splice(0, this.queue.length - 4);
        };
      }
      depth() {
        let d = this.cur ? this.cur.length - this.off : 0;
        for (const q of this.queue) d += q.length;
        return d;
      }
      process(_in, outputs) {
        const out = outputs[0] && outputs[0][0];
        if (!out) return true;
        if (++this.ticks % 64 === 0) this.port.postMessage(this.depth());
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
  // Обычный base64 (не url-safe) — для data:-URL картинок чата.
  function u8ToStdB64(u8) {
    let s = "";
    for (let i = 0; i < u8.length; i++) s += String.fromCharCode(u8[i]);
    return btoa(s);
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

  // Пресеты качества отправки (выбор в настройках конференции). Разрешение
  // камеры задаёт страница при захвате — сюда приходит уже готовый кадр,
  // здесь только битрейты кодеков и целевой fps.
  const QUALITY = {
    cam:    { low:  { bitrate: 400000,  framerate: 15 },
              med:  { bitrate: 1200000, framerate: 24 },
              high: { bitrate: 2500000, framerate: 30 } },
    screen: { low:  { bitrate: 600000,  framerate: 5 },
              med:  { bitrate: 1500000, framerate: 15 },
              high: { bitrate: 4000000, framerate: 30 } },
    audio:  { low: 16000, med: 32000, high: 64000 },
  };

  function create(opts) {
    // opts: send(buf), buffered()->число, micOn()->bool,
    //       onSelfSpeaking(), onSpeaking(id), onFrameActivity(id), onLocked(id, bool)
    const st = {
      quality: { cam: "med", screen: "med", audio: "med" },
      ctx: null, masterGain: null, workletsReady: null,
      volume: 1, sens: 1, micLevel: 0,
      cipher: null,
      // отправка
      videoChoice: undefined,     // undefined = детект идёт, null = только legacy
      audioCoded: false,
      lastForceAt: 0,
      aenc: null, aTs: 0,
      micSrc: null, capNode: null,
      sendChains: { a: Promise.resolve(), v: Promise.resolve(), s: Promise.resolve() },
      lastKeyReqAt: 0,
      // приём
      peers: new Map(),           // id -> peer
      sinks: new Map(),           // id -> <canvas> камеры
      sinkRefs: new Map(),
      screenSinks: new Map(),     // id -> <canvas> демонстрации экрана
      screenSinkRefs: new Map(),
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
      const kind = screen ? "screen" : "cam";
      const q = QUALITY[kind][st.quality[kind]];
      return Object.assign({
        codec: choice.codec, width: w, height: h,
        bitrate: q.bitrate,
        framerate: q.framerate,
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
            // Метка — стенные часы отправителя (мс): у видео та же шкала,
            // приёмник синхронизирует картинку со звуком по этим меткам.
            enqueueSend(MSG.AUDIO_CODED, 0, OPUS_ID, BigInt(Date.now()), u8, "a");
          },
          error: () => { st.aenc = null; },
        });
        st.aenc.configure({ codec: "opus", sampleRate: AUDIO_RATE, numberOfChannels: 1,
                            bitrate: QUALITY.audio[st.quality.audio] });
        return true;
      } catch (e) {
        st.aenc = null;
        st.audioCoded = false;   // не вышло — до конца сессии шлём PCM
        return false;
      }
    }

    // ---------- Отправка: видео ----------
    // Камера и экран — два одинаково устроенных, но независимых отправителя:
    // демонстрация не выключает камеру и наоборот.

    function makeVideoSender(screen) {
      const lane = screen ? "s" : "v";
      const codedType = screen ? MSG.SCREEN_CODED : MSG.VIDEO_CODED;
      const jpegType = screen ? MSG.SCREEN_JPEG : MSG.VIDEO_JPEG;
      // Legacy-JPEG: камера — мелко и часто, экран — крупно и редко,
      // иначе текст на демонстрации нечитаем.
      const jpegMaxW = screen ? 1280 : 480, jpegMaxH = screen ? 720 : 360;
      const jpegIntervalMs = screen ? 500 : JPEG_INTERVAL_MS;

      const s = { el: null, token: 0, active: false, reader: null, stream: null,
                  onElement: false,
                  venc: null, w: 0, h: 0, frames: 0, keyNext: false,
                  jpegCanvas: null, lastJpegAt: 0, jpegBusy: false };

      // Кадры берём ПРЯМО С ТРЕКА через MediaStreamTrackProcessor. Скрытый
      // <video> вне DOM браузер деприоритезирует: он не держится живого края
      // потока и копит задержку — картинка уезжала от звука на секунды.
      // Элемент остаётся фолбэком там, где процессора нет (Safari/Firefox),
      // и для legacy-JPEG, которому нужен именно элемент.
      function start(stream) {
        stop();
        s.active = true;
        s.stream = stream;
        const token = ++s.token;
        // Ждём детект кодеков: без WebCodecs идём только элементом (JPEG).
        detect.then(() => {
          if (s.token !== token || !s.active) return;
          const track = stream.getVideoTracks ? stream.getVideoTracks()[0] : null;
          if (st.videoChoice && track && typeof MediaStreamTrackProcessor !== "undefined")
            pumpDirect(track, token);
          else
            startElement(stream, token);
        });
      }

      function startElement(stream, token) {
        s.onElement = true;
        const v = s.el || (s.el = document.createElement("video"));
        v.muted = true; v.playsInline = true; v.autoplay = true;
        v.srcObject = stream;
        const p = v.play();
        if (p && p.catch) p.catch(() => {});
        if ("requestVideoFrameCallback" in HTMLVideoElement.prototype) {
          const step = () => {
            if (s.token !== token || !s.active) return;
            tick(v);
            v.requestVideoFrameCallback(step);
          };
          v.requestVideoFrameCallback(step);
        } else {
          const timer = setInterval(() => {
            if (s.token !== token || !s.active) { clearInterval(timer); return; }
            tick(v);
          }, 1000 / 24);
        }
      }

      // Насос кадров с трека: каждый элемент потока — готовый VideoFrame.
      async function pumpDirect(track, token) {
        let reader;
        try {
          reader = new MediaStreamTrackProcessor({ track }).readable.getReader();
        } catch (e) {
          startElement(s.stream || new MediaStream([track]), token);
          return;
        }
        s.reader = reader;
        for (;;) {
          let res;
          try { res = await reader.read(); } catch (e) { break; }
          if (res.done) break;
          const frame = res.value;
          if (s.token !== token || !s.active) { frame.close(); break; }
          directFrame(frame, token);
        }
        try { reader.releaseLock(); } catch (e) {}
        if (s.reader === reader) s.reader = null;
      }

      function stop() {
        s.active = false;
        s.token++;
        s.stream = null;
        s.onElement = false;
        if (s.reader) { try { s.reader.cancel(); } catch (e) {} s.reader = null; }
        if (s.venc) { try { s.venc.close(); } catch (e) {} s.venc = null; }
        if (s.el) s.el.srcObject = null;
      }

      // Энкодер под текущий размер кадра. false — WebCodecs сломался.
      function ensureEncoder(w, h) {
        if (s.venc && s.w === w && s.h === h) return true;
        if (s.venc) { try { s.venc.close(); } catch (e) {} }
        const choice = st.videoChoice;
        try {
          s.venc = new VideoEncoder({
            output: (chunk) => {
              const u8 = new Uint8Array(chunk.byteLength);
              chunk.copyTo(u8);
              // Стенные часы (мс) — общая шкала с аудио, см. ensureAudioEncoder.
              enqueueSend(codedType, chunk.type === "key" ? FLAG_KEYFRAME : 0, choice.id,
                          BigInt(Date.now()), u8, lane);
            },
            error: () => { s.venc = null; },   // пересоздастся следующим кадром
          });
          s.venc.configure(videoCfg(choice, w, h, screen));
          s.w = w; s.h = h; s.frames = 0;
          return true;
        } catch (e) {
          s.venc = null;
          st.videoChoice = null;                // WebCodecs сломан — legacy до конца сессии
          st.stats.videoCodec = "jpeg";
          return false;
        }
      }

      function emitFrame(frame) {
        const key = s.keyNext || s.frames % KEY_EVERY_FRAMES === 0;
        s.keyNext = false;
        s.frames++;
        try { s.venc.encode(frame, { keyFrame: key }); } catch (e) { s.venc = null; }
      }

      function directFrame(frame, token) {
        if (!st.videoChoice) {                  // WebCodecs отвалился на ходу
          frame.close();
          if (s.active && !s.onElement && s.stream) {
            if (s.reader) { try { s.reader.cancel(); } catch (e) {} s.reader = null; }
            startElement(s.stream, token);      // дальше — JPEG через элемент
          }
          return;
        }
        const w = frame.displayWidth & ~1, h = frame.displayHeight & ~1;
        if (w >= 2 && h >= 2 && opts.buffered() <= MAX_BUFFERED
            && ensureEncoder(w, h) && s.venc.encodeQueueSize <= 2)
          emitFrame(frame);
        frame.close();
      }

      function tick(v) {
        if (!v.videoWidth || v.readyState < 2) return;
        if (st.videoChoice === undefined) return;          // детект кодеков ещё идёт
        if (st.videoChoice === null) { legacyTick(v); return; }
        if (opts.buffered() > MAX_BUFFERED) return;        // сеть не успевает — пропуск кадра

        const w = v.videoWidth & ~1, h = v.videoHeight & ~1;
        if (!ensureEncoder(w, h)) return;
        if (s.venc.encodeQueueSize > 2) return;  // энкодер захлебнулся — пропуск

        let frame;
        try {
          frame = new VideoFrame(v, { timestamp: Math.round(performance.now() * 1000) });
        } catch (e) {
          return;   // кадр ещё не готов
        }
        emitFrame(frame);
        frame.close();
      }

      function legacyTick(v) {
        const now = Date.now();
        if (now - s.lastJpegAt < jpegIntervalMs || s.jpegBusy) return;
        if (opts.buffered() > MAX_BUFFERED) return;
        s.lastJpegAt = now;
        const c = s.jpegCanvas || (s.jpegCanvas = document.createElement("canvas"));
        const vw = v.videoWidth || jpegMaxW, vh = v.videoHeight || jpegMaxH;
        const scale = Math.min(jpegMaxW / vw, jpegMaxH / vh, 1);
        const cw = Math.max(2, Math.round(vw * scale)), ch = Math.max(2, Math.round(vh * scale));
        if (c.width !== cw || c.height !== ch) { c.width = cw; c.height = ch; }
        c.getContext("2d").drawImage(v, 0, 0, cw, ch);
        s.jpegBusy = true;
        c.toBlob((blob) => {
          s.jpegBusy = false;
          if (!blob || !s.active) return;
          blob.arrayBuffer().then((ab) => {
            enqueueSend(jpegType, 0, 0, BigInt(Date.now()), new Uint8Array(ab), lane);
          });
        }, "image/jpeg", 0.7);
      }

      // Смена качества на лету: энкодер закрывается, следующий кадр создаст
      // его заново с новым пресетом и сразу пошлёт опорный кадр.
      function resetEncoder() {
        if (s.venc) { try { s.venc.close(); } catch (e) {} s.venc = null; }
        s.keyNext = true;
      }

      return { start, stop, resetEncoder,
               forceKey: () => { if (s.active) s.keyNext = true; } };
    }

    const camSender = makeVideoSender(false);
    const screenSender = makeVideoSender(true);

    function forceKeyframe() {
      const now = Date.now();
      if (now - st.lastForceAt < 500) return;
      st.lastForceAt = now;
      camSender.forceKey();
      screenSender.forceKey();
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
        p = { chains: { a: Promise.resolve(), v: Promise.resolve(), s: Promise.resolve() },
              cam: { dec: null, codec: 0, awaitKey: true },
              scr: { dec: null, codec: 0, awaitKey: true },
              adec: null, player: null,
              // Синхронизация губ: часы звука (метка чанка + когда встал в
              // буфер), глубина буфера плеера и придержанные видеокадры.
              aClock: null, aDepth: 0, frameQ: [], drainTimer: null,
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
      const lane = (type === MSG.AUDIO_PCM || type === MSG.AUDIO_CODED) ? "a"
                 : (type === MSG.SCREEN_CODED || type === MSG.SCREEN_JPEG) ? "s" : "v";
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
        case MSG.VIDEO_CODED:  decodeVideo(peer, peer.cam, st.sinks, false, sender, flags, codecId, ts, body); break;
        case MSG.SCREEN_CODED: decodeVideo(peer, peer.scr, st.screenSinks, true, sender, flags, codecId, ts, body); break;
        case MSG.VIDEO_JPEG:   paintJpeg(st.sinks, false, sender, body); break;
        case MSG.SCREEN_JPEG:  paintJpeg(st.screenSinks, true, sender, body); break;
        case MSG.AUDIO_CODED:  decodeAudio(peer, codecId, ts, body, sender); break;
        case MSG.AUDIO_PCM:    playPcm(peer, sender, body); break;
      }
    }

    function setLocked(peer, sender, locked) {
      if (peer.locked === locked) return;
      peer.locked = locked;
      opts.onLocked(sender, locked);
    }

    // sub — peer.cam или peer.scr: у камеры и экрана свои декодеры и синки.
    function decodeVideo(peer, sub, sinks, isScreen, sender, flags, codecId, ts, body) {
      if (!CODEC_BY_ID[codecId]) return;
      if (!sub.dec || sub.codec !== codecId) {
        if (sub.dec) { try { sub.dec.close(); } catch (e) {} }
        if (typeof VideoDecoder === "undefined") return;
        sub.dec = new VideoDecoder({
          output: (frame) => paintFrame(peer, sinks, isScreen, sender, frame),
          error: () => { sub.dec = null; sub.awaitKey = true; requestKeyframe(); },
        });
        try { sub.dec.configure({ codec: CODEC_BY_ID[codecId], optimizeForLatency: true }); }
        catch (e) { sub.dec = null; return; }
        sub.codec = codecId;
        sub.awaitKey = true;
      }
      const isKey = !!(flags & FLAG_KEYFRAME);
      if (sub.awaitKey && !isKey) { requestKeyframe(); return; }   // ждём опорный кадр
      sub.awaitKey = false;
      try {
        sub.dec.decode(new EncodedVideoChunk({
          type: isKey ? "key" : "delta", timestamp: Number(ts), data: body }));
      } catch (e) {
        try { sub.dec.close(); } catch (e2) {}
        sub.dec = null;
        sub.awaitKey = true;
        requestKeyframe();
      }
    }

    // Где сейчас «играющий» звук пира в шкале часов отправителя (мс):
    // метка чанка, вставшего в буфер, минус глубина буфера, плюс прошедшее
    // время. null — звука нет или он устарел (мик выключен) — видео не ждёт.
    function audioPlayhead(peer) {
      if (!peer.aClock) return null;
      const age = performance.now() - peer.aClock.at;
      if (age > 700) return null;
      return peer.aClock.ts - peer.aDepth / (AUDIO_RATE / 1000) + age;
    }

    function paintFrame(peer, sinks, isScreen, sender, frame) {
      // Синхронизация губ: звук доходит до ушей позже картинки (джиттер-буфер),
      // поэтому кадр камеры, обогнавший звук, придерживается до его метки.
      // Экран не придерживаем: там важна отзывчивость, не губы. Верхняя
      // граница отсекает бессмыслицу (другая шкала у старого клиента, скачок
      // часов) — тогда рисуем сразу, как раньше.
      if (!isScreen && peer) {
        const ph = audioPlayhead(peer);
        const lead = ph == null ? 0 : frame.timestamp - ph;
        if (lead > 30 && lead < 1200) {
          peer.frameQ.push(frame);
          if (peer.frameQ.length > 12) peer.frameQ.shift().close();
          drainFramesLater(peer, sinks, sender, lead);
          return;
        }
      }
      drawFrame(sinks, isScreen, sender, frame);
    }

    function drainFramesLater(peer, sinks, sender, delayMs) {
      if (peer.drainTimer) return;
      peer.drainTimer = setTimeout(() => {
        peer.drainTimer = null;
        const ph = audioPlayhead(peer);
        while (peer.frameQ.length) {
          const f = peer.frameQ[0];
          const lead = ph == null ? 0 : f.timestamp - ph;
          if (lead > 30) { drainFramesLater(peer, sinks, sender, lead); return; }
          peer.frameQ.shift();
          drawFrame(sinks, false, sender, f);
        }
      }, Math.max(15, Math.min(delayMs, 250)));
    }

    function drawFrame(sinks, isScreen, sender, frame) {
      const canvas = sinks.get(sender);
      if (canvas) {
        const w = frame.displayWidth, h = frame.displayHeight;
        if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
        canvas.getContext("2d").drawImage(frame, 0, 0, w, h);
      }
      frame.close();
      if (isScreen) {
        if (opts.onScreenFrame) opts.onScreenFrame(sender);
      } else {
        st.lastFrameAt.set(sender, Date.now());
        opts.onFrameActivity(sender);
      }
    }

    function paintJpeg(sinks, isScreen, sender, body) {
      const blob = new Blob([body], { type: "image/jpeg" });
      createImageBitmap(blob).then((bmp) => {
        const canvas = sinks.get(sender);
        if (canvas) {
          if (canvas.width !== bmp.width || canvas.height !== bmp.height) {
            canvas.width = bmp.width; canvas.height = bmp.height;
          }
          canvas.getContext("2d").drawImage(bmp, 0, 0);
        }
        bmp.close();
        if (isScreen) {
          if (opts.onScreenFrame) opts.onScreenFrame(sender);
        } else {
          st.lastFrameAt.set(sender, Date.now());
          opts.onFrameActivity(sender);
        }
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
      const adTs = ad.timestamp;   // метка чанка = Date.now() отправителя (мс)
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
        // Плеер репортит глубину буфера — по ней считается audioPlayhead.
        peer.player.port.onmessage = (e) => { peer.aDepth = e.data; };
        peer.player.connect(st.masterGain);
      }
      peer.aClock = { ts: adTs, at: performance.now() };
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

    function sinkRefFor(sinks, refs, id) {
      let cb = refs.get(id);
      if (!cb) {
        cb = (el) => {
          if (el) sinks.set(id, el);
          else sinks.delete(id);
        };
        refs.set(id, cb);
      }
      return cb;
    }

    // ---------- Управление ----------

    function dropPeer(id) {
      const p = st.peers.get(id);
      if (p) {
        if (p.cam.dec) { try { p.cam.dec.close(); } catch (e) {} }
        if (p.scr.dec) { try { p.scr.dec.close(); } catch (e) {} }
        if (p.adec) { try { p.adec.close(); } catch (e) {} }
        if (p.player) { try { p.player.disconnect(); } catch (e) {} }
        if (p.drainTimer) clearTimeout(p.drainTimer);
        for (const f of p.frameQ) { try { f.close(); } catch (e) {} }
        st.peers.delete(id);
      }
      st.lastFrameAt.delete(id);
    }

    function clearPeers() {
      for (const id of Array.from(st.peers.keys())) dropPeer(id);
    }

    function stop() {
      camSender.stop();
      screenSender.stop();
      clearPeers();
      if (st.aenc) { try { st.aenc.close(); } catch (e) {} st.aenc = null; }
      if (st.micSrc) { try { st.micSrc.disconnect(); } catch (e) {} }
      if (st.ctx) { const p = st.ctx.close(); if (p && p.catch) p.catch(() => {}); }
    }

    return {
      ensureAudio, startMic, stop,
      startVideo: (stream) => camSender.start(stream),
      stopVideo: () => camSender.stop(),
      startScreen: (stream) => screenSender.start(stream),
      stopScreen: () => screenSender.stop(),
      onBinary, forceKeyframe, requestKeyframe,
      videoSinkRef: (id) => sinkRefFor(st.sinks, st.sinkRefs, id),
      screenSinkRef: (id) => sinkRefFor(st.screenSinks, st.screenSinkRefs, id),
      dropPeer, clearPeers,
      lastFrameAt: st.lastFrameAt,
      micLevel: () => st.micLevel,
      setVolume: (v) => { st.volume = v; if (st.masterGain) st.masterGain.gain.value = v; },
      setSensitivity: (v) => { st.sens = v; },
      // Качество отправки: "cam" | "screen" | "audio" -> "low" | "med" | "high".
      // Применяется на лету — энкодер пересоздаётся со следующим чанком.
      setQuality: (kind, level) => {
        if (!QUALITY[kind] || !(level in QUALITY[kind]) || st.quality[kind] === level) return;
        st.quality[kind] = level;
        if (kind === "audio") {
          if (st.aenc) { try { st.aenc.close(); } catch (e) {} st.aenc = null; }
        } else {
          (kind === "screen" ? screenSender : camSender).resetEncoder();
        }
      },
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
      // Картинка чата: на входе/выходе обычный base64 JPEG; шифрованная —
      // тот же формат "🔒e2e:<base64url(iv|ct)>", что и текст.
      sealImage: async (b64) => {
        if (!st.cipher) return null;
        return "🔒e2e:" + u8ToB64(await st.cipher.seal(IMAGE_AAD_TYPE, 0, b64ToU8(b64)));
      },
      openImage: async (s) => {
        if (typeof s !== "string" || !s) return { encrypted: false, src: null };
        if (!s.startsWith("🔒e2e:")) return { encrypted: false, src: "data:image/jpeg;base64," + s };
        if (!st.cipher) return { encrypted: true, src: null };
        let pt = null;
        try { pt = await st.cipher.open(IMAGE_AAD_TYPE, 0, b64ToU8(s.slice(6))); } catch (e) {}
        return { encrypted: true, src: pt ? "data:image/jpeg;base64," + u8ToStdB64(pt) : null };
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

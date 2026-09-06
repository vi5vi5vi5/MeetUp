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
                SCREEN_CODED: 5, KEYFRAME_REQ: 6, SCREEN_JPEG: 7,
                // Звук того, что показывает ведущий (видео, музыка) — отдельной
                // полосой от его голоса. Opus 48 кГц моно, кадр 20 мс, та же
                // шкала меток. Отдельная полоса нужна не для порядка: голос и
                // фонограмма идут от ОДНОГО отправителя, а складывать их в один
                // Opus-декодер нельзя — у него состояние потока. И по этой
                // полосе не считают губы и не зажигают «говорит»: громкая
                // музыка не означает, что человек говорит.
                SCREEN_AUDIO: 8,
                // «Не понимаю такой кодек» — шлём мы, когда кадр пришёл с
                // кодеком, которого браузер не открывает. Отправитель по нему
                // сам вернётся на H.264. Без этого ответа он не узнает, что
                // часть участников его не видит: у нас пусто, а у него всё
                // хорошо. Раскладка: codec — непонятый байт, payload — один
                // байт с типом полосы (VIDEO_CODED или SCREEN_CODED).
                CODEC_UNSUPPORTED: 9 };
  const FLAG_KEYFRAME = 1;
  const FLAG_ENCRYPTED = 2;
  const CHAT_AAD_TYPE = 250;              // «тип» для шифрования текста чата
  const IMAGE_AAD_TYPE = 251;             // …и картинок чата

  const H264_ID = 2;
  const CODEC_NAME = { 1: "VP8", 2: "H.264", 3: "VP9", 4: "Opus", 5: "HEVC", 6: "AV1" };

  // Что мы умеем ПРИНИМАТЬ. Список намеренно шире того, что мы отправляем:
  // шлём узко (все поймут), принимаем всё, что вытянет браузер. Десктопный
  // клиент по умолчанию ведёт демонстрацию в HEVC, а камеру в VP9 — без этих
  // строк чужая демонстрация в браузере просто не появлялась бы, пока не
  // сработает жалоба CODEC_UNSUPPORTED и отправитель не опустится на H.264.
  //
  // H.264 объявляем уровнем 5.2 (…34): иначе кадр крупнее 720p (а экран
  // именно такой) декодер не принял бы. См. avcCodec ниже про уровни.
  // У HEVC и AV1 по нескольку написаний — какое поймёт браузер, заранее не
  // известно, поэтому пробуем по очереди. Описания (description) не шлём:
  // без него WebCodecs ждёт Annex B, а это ровно то, что даёт ffmpeg.
  const VIDEO_DECODE = {
    1: ["vp8"],
    2: ["avc1.42E034"],
    3: ["vp09.00.10.08"],
    5: ["hvc1.1.6.L153.B0", "hev1.1.6.L153.B0"],
    6: ["av01.0.12M.08", "av01.0.08M.08"],
  };

  // Проба «умеет ли браузер это декодировать» — один раз на кодек, результат
  // кэшируется вместе с рабочей строкой. Проверяем ЗАРАНЕЕ, а не по факту
  // ошибки: configure() на неподдержанном кодеке падает не всегда синхронно,
  // и без пробы приёмник крутился бы в цикле «создать декодер — получить
  // ошибку — создать заново».
  const decodeProbe = new Map();
  function decoderCodecFor(id) {
    if (decodeProbe.has(id)) return decodeProbe.get(id);
    const p = (async () => {
      const list = VIDEO_DECODE[id];
      if (!list || typeof VideoDecoder === "undefined") return null;
      for (const codec of list) {
        try {
          const r = await VideoDecoder.isConfigSupported({ codec, optimizeForLatency: true });
          if (r && r.supported) return codec;
        } catch (e) { /* такой строки кодека браузер не знает вовсе */ }
      }
      return null;
    })();
    decodeProbe.set(id, p);
    return p;
  }

  // Что мы ОТПРАВЛЯЕМ. Здесь выбора нет и не должно быть: H.264 понимают все,
  // и почти везде он кодируется видеокартой. VP8 — не альтернатива, а
  // аварийный выход для браузера, где H.264 недоступен (часть сборок Firefox
  // без системных кодеков): там иначе не было бы видео вообще.
  //
  // VP9 из отправки убран намеренно, хотя браузеры его умеют. Ручка «кодек»
  // в вебе — иллюзия управления: настоящий выбор всё равно делает WebCodecs
  // за нас, а каждый лишний кодек в отправке — это ещё один способ оказаться
  // непонятым частью комнаты. Принимаем при этом по-прежнему всё, что
  // вытянет браузер (см. VIDEO_DECODE): шлём узко, принимаем широко.
  const VIDEO_TRY = [
    { id: 2, codec: "avc1.42E01F", extra: { avc: { format: "annexb" } } },
    { id: 1, codec: "vp8", extra: {} },
  ];

  // H.264 задаёт «уровень» (level_idc) — потолок разрешения и битрейта потока.
  // Аппаратный (и программный openh264) энкодер соблюдает его строго: кадр
  // крупнее потолка он не берёт и асинхронно падает через error-колбэк.
  // avc1.42E01F из VIDEO_TRY — это уровень 3.1 (максимум 720p): камеру (≤720p)
  // тянет, а демонстрацию экрана в родном разрешении (1080p/1440p/4K) — нет.
  // Поэтому уровень в строке кодека подбираем под конкретный размер кадра.
  // VP8/VP9 (libvpx) уровень не проверяют — оттого экран в Firefox (там VP8)
  // работал, а в Chrome (там H.264) молча не отправлялся.
  const AVC_LEVELS = [           // [level_idc, MaxFS (МБ 16×16), MaxMBPS]
    [0x1f, 3600,  108000],       // 3.1 — 720p30
    [0x20, 5120,  216000],       // 3.2
    [0x28, 8192,  245760],       // 4.0 — 1080p30
    [0x2a, 8704,  522240],       // 4.2 — 1080p60
    [0x32, 22080, 589824],       // 5.0 — 1440p30
    [0x33, 36864, 983040],       // 5.1 — 2160p30
    [0x34, 36864, 2073600],      // 5.2 — 2160p60
  ];
  function avcCodec(w, h, fps) {
    const mbs = Math.ceil(w / 16) * Math.ceil(h / 16);
    const mbps = mbs * (fps || 30);
    let id = 0x34;               // выше 5.2 не поднимаемся (потолок ~4K)
    for (const lvl of AVC_LEVELS) {
      if (mbs <= lvl[1] && mbps <= lvl[2]) { id = lvl[0]; break; }
    }
    return "avc1.42E0" + id.toString(16).padStart(2, "0").toUpperCase();
  }
  const OPUS_ID = 4;

  const AUDIO_RATE = 48000;               // AudioContext форсируется на 48к (родной рейт Opus)
  const AUDIO_FRAME = 960;                // 20 мс
  const PCM_RATE = 16000;                 // legacy-фолбэк
  const JPEG_INTERVAL_MS = 100;           // legacy ~10 к/с
  const KEY_EVERY_FRAMES = 72;            // keyframe раз в ~3 с при 24 к/с

  // Затор в сокете. Все полосы — голос, камера, экран — едут через ОДИН
  // WebSocket поверх TCP, поэтому очередь в сокете задерживает и звук: пока
  // не уйдёт опорный кадр экрана в сотни килобайт, голос стоит за ним.
  // Отсюда асимметрия: экран прижимаем раньше и роняем первым, у камеры
  // запас больше, а голос не трогаем никогда.
  //
  //   easeAt — с этого размера очереди понижаем битрейт (плавно);
  //   dropAt — с этого просто не отдаём кадр энкодеру (обрыв, край).
  const PRESSURE = {
    cam:    { easeAt: 400e3,  dropAt: 1.5e6 },
    screen: { easeAt: 200e3,  dropAt: 350e3 },
  };
  const RATE_MIN = 0.25;                  // ниже четверти пресета не опускаемся

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
      // Множитель битрейта под состояние сети: 1 — пресет целиком, ниже —
      // сеть не тянет. Держится отдельно по полосам, потому что и давят на
      // них по-разному (см. PRESSURE).
      rate: { cam: 1, screen: 1 },
      ctx: null, masterGain: null, screenGain: null, workletsReady: null,
      volume: 1, screenVolume: 1, sens: 1, micLevel: 0,
      // «Не слышу вас»: глушим на общем узле, а декодеры и плееры продолжают
      // работать. Иначе встали бы и картинка (её придерживают по часам
      // звука), и подсветка «говорит» — а человек всего лишь убрал звук.
      deafened: false,
      peerVolume: new Map(),      // id -> множитель громкости участника
      cipher: null,
      // отправка
      videoChoice: undefined,     // undefined = детект идёт, null = только legacy
      videoOk: [],                // ВСЕ кодеки, которыми этот браузер умеет кодировать
      // Куда жалоба получателя опустила полосу. null — идём общим выбором.
      forcedCam: null, forcedScreen: null,
      audioCoded: false,
      lastForceAt: 0,
      aenc: null, aTs: 0,
      micSrc: null, capNode: null,
      // Звук демонстрации на отправке: свой источник, свой кодер, своя шкала.
      scrSrc: null, scrMono: null, scrCap: null, scrMute: null,
      scrAenc: null, scrTs: 0,
      sendChains: { a: Promise.resolve(), v: Promise.resolve(),
                    s: Promise.resolve(), sa: Promise.resolve() },
      lastKeyReqAt: 0,
      lastScreenAudioAt: 0,
      // Счётчики для раздела «Диагностика». Считаем сырые байты кадров:
      // накладные расходы WebSocket и TCP сюда не входят, поэтому число
      // всегда чуть меньше того, что покажет системный монитор трафика.
      // «Потерь пакетов» здесь нет и не будет — транспорт TCP.
      meter: { rxBytes: 0, txBytes: 0, rxFrames: 0, txFrames: 0,
               at: 0, lastRx: 0, lastTx: 0, lastRxF: 0, lastTxF: 0 },
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
      st.videoOk = [];
      if (typeof VideoEncoder !== "undefined" && typeof VideoDecoder !== "undefined") {
        // Перебираем весь список, а не останавливаемся на первом годном:
        // откат по жалобе получателя должен знать, доступен ли H.264 вообще.
        // Без этого в браузере без H.264 (Firefox на части сборок Linux)
        // жалоба переключила бы отправку на кодек, которого там нет, и видео
        // пропало бы совсем — вместо того чтобы просто остаться как есть.
        for (const c of VIDEO_TRY) {
          try {
            const r = await VideoEncoder.isConfigSupported(videoCfg(c, 1280, 720, false));
            if (r.supported) st.videoOk.push(c);
          } catch (e) { /* кодек неизвестен браузеру */ }
        }
        st.videoChoice = st.videoOk[0] || null;
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
      // H.264 — уровень под размер кадра (иначе экран >720p не кодируется).
      const codec = choice.codec.startsWith("avc1.") ? avcCodec(w, h, q.framerate) : choice.codec;
      return Object.assign({
        codec, width: w, height: h,
        // Пресет — это потолок, а не обещание: сколько уедет на самом деле,
        // решает состояние сети (см. pace ниже).
        bitrate: Math.max(80000, Math.round(q.bitrate * st.rate[kind])),
        framerate: q.framerate,
        latencyMode: "realtime",
      }, choice.extra);
    }

    // ---------- Аудиографа ----------

    function masterLevel() { return st.deafened ? 0 : st.volume; }

    // Узел громкости конкретного участника: его голос идёт через него, а не
    // прямо в общий. Личная громкость нужна ровно там, где общая не помогает:
    // один говорит шёпотом, другой кричит.
    function peerOut(peer, id) {
      if (!peer.gain) {
        peer.gain = st.ctx.createGain();
        const v = st.peerVolume.get(id);
        peer.gain.gain.value = v == null ? 1 : v;
        peer.gain.connect(st.masterGain);
      }
      return peer.gain;
    }

    async function ensureAudio() {
      if (st.ctx) return st.workletsReady;
      const AC = window.AudioContext || window.webkitAudioContext;
      if (!AC) return null;
      // 48 кГц — родной рейт Opus; захват и воспроизведение в одном контексте.
      try { st.ctx = new AC({ sampleRate: AUDIO_RATE }); }
      catch (e) { st.ctx = new AC(); }
      st.masterGain = st.ctx.createGain();
      st.masterGain.gain.value = masterLevel();
      st.masterGain.connect(st.ctx.destination);
      // Громкость звука демонстрации — ручка СЛУШАТЕЛЯ, а не ведущего: иначе
      // один человек решал бы за всю комнату, насколько громко играет его
      // фонограмма. Отдельный узел перед общим, чтобы общая громкость и
      // «не слышу вас» продолжали работать поверх.
      st.screenGain = st.ctx.createGain();
      st.screenGain.gain.value = st.screenVolume;
      st.screenGain.connect(st.masterGain);
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

    // ---------- Отправка: звук демонстрации ----------
    // Браузер отдаёт его вместе с картинкой, когда человек отметил «Поделиться
    // звуком» в окне выбора (Chrome и Edge умеют, Firefox и Safari — нет).
    // Никакой отдельной настройки не заводим: галочка в системном окне и есть
    // согласие, а вторая галочка в наших настройках означала бы, что человек
    // разрешил, а мы всё равно не отправили.
    //
    // Дальше — полностью отдельный от голоса тракт: свой кодер (у Opus
    // состояние потока, делить его нельзя), свои метки времени и полоса "sa",
    // чтобы фонограмма не вставала в очередь за голосом.

    async function startScreenAudio(stream) {
      stopScreenAudio();
      const track = stream && stream.getAudioTracks ? stream.getAudioTracks()[0] : null;
      if (!track) return false;
      await ensureAudio();
      await detect;
      if (!st.ctx || !st.audioCoded || !(await st.workletsReady)) return false;

      st.scrSrc = st.ctx.createMediaStreamSource(new MediaStream([track]));
      // Экран почти всегда отдаёт стерео, а протокол требует моно. Сведение
      // делает сам граф: узел с явным channelCount = 1 микширует каналы по
      // правилам Web Audio, складывать их руками не нужно.
      st.scrMono = st.ctx.createGain();
      st.scrMono.channelCount = 1;
      st.scrMono.channelCountMode = "explicit";
      st.scrMono.channelInterpretation = "speakers";

      st.scrCap = new AudioWorkletNode(st.ctx, "mu-capture",
        { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1] });
      // Узел обязан быть в графе до destination, иначе не тикает; выход
      // глушим — свою же фонограмму возвращать себе в уши незачем.
      st.scrMute = st.ctx.createGain();
      st.scrMute.gain.value = 0;
      st.scrCap.connect(st.scrMute).connect(st.ctx.destination);
      st.scrSrc.connect(st.scrMono).connect(st.scrCap);
      st.scrCap.port.onmessage = (e) => onScreenAudioChunk(e.data);
      return true;
    }

    function onScreenAudioChunk(f32) {
      if (!ensureScreenAudioEncoder()) return;
      if (st.scrAenc.encodeQueueSize > 4) return;   // не успеваем — роняем кадр
      const ad = new AudioData({ format: "f32", sampleRate: AUDIO_RATE,
                                 numberOfFrames: f32.length, numberOfChannels: 1,
                                 timestamp: st.scrTs, data: f32 });
      st.scrTs += Math.round(f32.length * 1e6 / AUDIO_RATE);
      try { st.scrAenc.encode(ad); } catch (e) { st.scrAenc = null; }
      ad.close();
    }

    function ensureScreenAudioEncoder() {
      if (st.scrAenc) return true;
      try {
        st.scrAenc = new AudioEncoder({
          output: (chunk) => {
            const u8 = new Uint8Array(chunk.byteLength);
            chunk.copyTo(u8);
            enqueueSend(MSG.SCREEN_AUDIO, 0, OPUS_ID, BigInt(Date.now()), u8, "sa");
          },
          error: () => { st.scrAenc = null; },
        });
        // 64 кбит/с против голосовых 32: это музыка и звук видео, и экономить
        // на них нечего — демонстрацию со звуком включают именно ради них.
        st.scrAenc.configure({ codec: "opus", sampleRate: AUDIO_RATE,
                               numberOfChannels: 1, bitrate: 64000 });
        return true;
      } catch (e) {
        st.scrAenc = null;
        return false;
      }
    }

    function stopScreenAudio() {
      if (st.scrCap) {
        try { st.scrCap.port.onmessage = null; st.scrCap.disconnect(); } catch (e) {}
        st.scrCap = null;
      }
      for (const key of ["scrMono", "scrMute", "scrSrc"]) {
        if (st[key]) { try { st[key].disconnect(); } catch (e) {} st[key] = null; }
      }
      if (st.scrAenc) { try { st.scrAenc.close(); } catch (e) {} st.scrAenc = null; }
    }

    // ---------- Отправка: видео ----------
    // Камера и экран — два одинаково устроенных, но независимых отправителя:
    // демонстрация не выключает камеру и наоборот.

    function makeVideoSender(screen) {
      const kind = screen ? "screen" : "cam";
      const press = PRESSURE[kind];
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
                  lastPaceAt: 0,
                  jpegCanvas: null, lastJpegAt: 0, jpegBusy: false };

      // Подстройка под сеть. Раньше здесь был один порог: очередь в сокете
      // переросла полтора мегабайта — кадр выброшен. Это обрыв, а не
      // регулирование: полтора мегабайта на медленном канале — уже несколько
      // секунд задержки, и всё это время картинка стоит, а голос опаздывает
      // за ней. Теперь сначала плавно снижаем битрейт (сеть перестаёт
      // захлёбываться, картинка живая, просто мягче), и только на самом краю
      // роняем кадр.
      //
      // Вверх возвращаемся втрое медленнее, чем вниз: провал заметен сразу и
      // реагировать надо быстро, а вот радоваться освободившемуся каналу
      // рано — стоит дёрнуться, и затор повторится.
      function pace() {
        const now = Date.now();
        if (now - s.lastPaceAt < 1000) return;
        s.lastPaceAt = now;

        const buf = opts.buffered();
        const cur = st.rate[kind];
        let next = cur;
        if (buf > press.easeAt) next = Math.max(RATE_MIN, cur - 0.2);
        else if (buf < press.easeAt / 4) next = Math.min(1, cur + 0.07);
        if (Math.abs(next - cur) < 0.02) return;

        st.rate[kind] = next;
        // Меняем битрейт на живом энкодере, без пересоздания: состояние
        // потока сохраняется, а следующий кадр делаем опорным — новый битрейт
        // должен вступить в силу с картинки, а не с середины серии дельт.
        if (s.venc && s.venc.state === "configured" && s.w) {
          const choice = choiceFor(screen);
          try {
            if (choice) { s.venc.configure(videoCfg(choice, s.w, s.h, screen)); s.keyNext = true; }
          } catch (e) {
            resetEncoder();   // не принял — пересоздадим со следующим кадром
          }
        }
      }

      // Кадры берём ПРЯМО С ТРЕКА через MediaStreamTrackProcessor. Скрытый
      // <video> вне DOM браузер деприоритезирует: он не держится живого края
      // потока и копит задержку — картинка уезжала от звука на секунды.
      // Элемент остаётся фолбэком там, где процессора нет (Safari/Firefox),
      // и для legacy-JPEG, которому нужен именно элемент.
      function start(stream) {
        stop();
        s.active = true;
        s.stream = stream;
        // Новый захват — новая ситуация: прошлый зажим мог остаться от сети,
        // которой давно нет. Если сеть всё ещё плохая, pace() вернёт зажим
        // через секунду; а вот стартовать заведомо мягкой картинкой и ждать
        // десять секунд подъёма — хуже.
        st.rate[kind] = 1;
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
        const choice = choiceFor(screen);
        if (!choice) return false;
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
        st.meter.txFrames++;
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
        pace();
        const w = frame.displayWidth & ~1, h = frame.displayHeight & ~1;
        if (w >= 2 && h >= 2 && opts.buffered() <= press.dropAt
            && ensureEncoder(w, h) && s.venc.encodeQueueSize <= 2)
          emitFrame(frame);
        frame.close();
      }

      function tick(v) {
        if (!v.videoWidth || v.readyState < 2) return;
        if (st.videoChoice === undefined) return;          // детект кодеков ещё идёт
        if (st.videoChoice === null) { legacyTick(v); return; }
        pace();
        if (opts.buffered() > press.dropAt) return;        // сеть не успевает — пропуск кадра

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
        if (opts.buffered() > press.dropAt) return;
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

    // Кодек полосы. Обычно это общий выбор детекта, но жалоба получателя
    // опускает КОНКРЕТНУЮ полосу на H.264 и держит её там до конца сессии:
    // раз кто-то в комнате не разобрал наш кадр, возвращаться к тому же
    // кодеку смысла нет.
    function choiceFor(screen) {
      return (screen ? st.forcedScreen : st.forcedCam) || st.videoChoice;
    }

    // Кто-то в комнате не смог разобрать наш кадр (тип 9). Сервер разослал
    // жалобу всем, поэтому первым делом проверяем, что она вообще про нас.
    //
    // Молчать здесь нельзя: у пожаловавшегося просто пусто, у нас — всё
    // хорошо, и без сообщения обе стороны так и не поймут, что происходит.
    function onCodecComplaint(codecId, payload) {
      const isScreen = payload.length > 0 && payload[0] === MSG.SCREEN_CODED;
      const cur = choiceFor(isScreen);
      if (!cur || cur.id !== codecId) return;         // жалуются не на наш поток

      let switched = false;
      if (cur.id !== H264_ID) {
        // Только среди тех, которыми этот браузер РЕАЛЬНО умеет кодировать.
        const h264 = st.videoOk.filter((c) => c.id === H264_ID)[0];
        if (h264) {
          if (isScreen) st.forcedScreen = h264; else st.forcedCam = h264;
          (isScreen ? screenSender : camSender).resetEncoder();
          switched = true;
        }
      }
      if (opts.onCodecComplaint)
        opts.onCodecComplaint({ isScreen: isScreen, switched: switched,
                                codec: CODEC_NAME[codecId] || ("код " + codecId) });
    }

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
        st.meter.txBytes += body.length + 11;
        opts.send(packV2(type, fl, codecId, tsBig, body));
      }).catch(() => {});
    }

    // ---------- Приём ----------

    function ensurePeer(id) {
      let p = st.peers.get(id);
      if (!p) {
        p = { chains: { a: Promise.resolve(), v: Promise.resolve(),
                        s: Promise.resolve(), sa: Promise.resolve() },
              cam: { dec: null, codec: 0, awaitKey: true },
              scr: { dec: null, codec: 0, awaitKey: true },
              // Голос и звук демонстрации приходят от одного отправителя, но
              // это два независимых потока Opus: у декодера состояние, и в
              // один их складывать нельзя.
              voice: { dec: null, player: null },
              scrAudio: { dec: null, player: null },
              gain: null,                   // личная громкость этого участника
              // Синхронизация губ: часы звука (метка чанка + когда встал в
              // буфер), глубина буфера плеера и придержанные видеокадры.
              aClock: null, aDepth: 0, frameQ: [], drainTimer: null,
              pcmCursor: 0, fails: 0, locked: false };
        st.peers.set(id, p);
      }
      return p;
    }

    function onBinary(buffer) {
      st.meter.rxBytes += buffer.byteLength;
      if (buffer.byteLength < 15) return;
      const dv = new DataView(buffer);
      const type = dv.getUint8(0);
      const sender = dv.getUint32(1, true);
      const flags = dv.getUint8(5);
      const codecId = dv.getUint8(6);
      const ts = dv.getBigUint64(7, true);
      if (type === MSG.KEYFRAME_REQ) { forceKeyframe(); return; }
      // Служебные кадры разбираем до E2E: они и не шифруются — сервер должен
      // уметь их релеить, а мы понимать без ключа.
      if (type === MSG.CODEC_UNSUPPORTED) {
        onCodecComplaint(codecId, new Uint8Array(buffer, 15));
        return;
      }
      const payload = new Uint8Array(buffer, 15);
      const lane = type === MSG.SCREEN_AUDIO ? "sa"
                 : (type === MSG.AUDIO_PCM || type === MSG.AUDIO_CODED) ? "a"
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

      // decodeVideo ждёт пробу декодера — без await кадры обгоняли бы друг
      // друга и опорный кадр мог прийти позже дельты, которая его ждёт.
      switch (type) {
        case MSG.VIDEO_CODED:  await decodeVideo(peer, peer.cam, st.sinks, false, sender, flags, codecId, ts, body); break;
        case MSG.SCREEN_CODED: await decodeVideo(peer, peer.scr, st.screenSinks, true, sender, flags, codecId, ts, body); break;
        case MSG.VIDEO_JPEG:   paintJpeg(st.sinks, false, sender, body); break;
        case MSG.SCREEN_JPEG:  paintJpeg(st.screenSinks, true, sender, body); break;
        case MSG.AUDIO_CODED:  decodeAudio(peer, peer.voice, false, codecId, ts, body, sender); break;
        case MSG.SCREEN_AUDIO: decodeAudio(peer, peer.scrAudio, true, codecId, ts, body, sender); break;
        case MSG.AUDIO_PCM:    playPcm(peer, sender, body); break;
      }
    }

    function setLocked(peer, sender, locked) {
      if (peer.locked === locked) return;
      peer.locked = locked;
      opts.onLocked(sender, locked);
    }

    // sub — peer.cam или peer.scr: у камеры и экрана свои декодеры и синки.
    // Пожаловаться отправителю на кодек, которого мы не понимаем. Не чаще
    // раза в две секунды: кадры идут потоком, а сообщение нужно одно.
    function complainCodec(codecId, isScreen) {
      const now = Date.now();
      if (now - (st.lastCodecComplaintAt || 0) < 2000) return;
      st.lastCodecComplaintAt = now;
      opts.send(packV2(MSG.CODEC_UNSUPPORTED, 0, codecId, 0n,
                       new Uint8Array([isScreen ? MSG.SCREEN_CODED : MSG.VIDEO_CODED])));
    }

    async function decodeVideo(peer, sub, sinks, isScreen, sender, flags, codecId, ts, body) {
      if (!sub.dec || sub.codec !== codecId) {
        // Кодек нам незнаком, или браузер его не открывает (нет аппаратного
        // декодера). Для отправителя это одно и то же: пусть шлёт H.264.
        const codec = await decoderCodecFor(codecId);
        if (!codec) { complainCodec(codecId, isScreen); return; }
        if (sub.dec) { try { sub.dec.close(); } catch (e) {} }
        sub.dec = new VideoDecoder({
          output: (frame) => paintFrame(peer, sinks, isScreen, sender, frame),
          error: () => { sub.dec = null; sub.awaitKey = true; requestKeyframe(); },
        });
        try { sub.dec.configure({ codec, optimizeForLatency: true }); }
        catch (e) { sub.dec = null; complainCodec(codecId, isScreen); return; }
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
      st.meter.rxFrames++;
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

    // sub — peer.voice или peer.scrAudio: у голоса и фонограммы демонстрации
    // свои декодер и плеер, хотя отправитель у них один.
    function decodeAudio(peer, sub, isScreen, codecId, ts, body, sender) {
      if (codecId !== OPUS_ID || typeof AudioDecoder === "undefined") return;
      if (!sub.dec) {
        try {
          sub.dec = new AudioDecoder({
            output: (ad) => playDecoded(peer, sub, isScreen, sender, ad),
            error: () => { sub.dec = null; },
          });
          sub.dec.configure({ codec: "opus", sampleRate: AUDIO_RATE, numberOfChannels: 1 });
        } catch (e) { sub.dec = null; return; }
      }
      try {
        sub.dec.decode(new EncodedAudioChunk({ type: "key", timestamp: Number(ts), data: body }));
      } catch (e) {
        try { sub.dec.close(); } catch (e2) {}
        sub.dec = null;
      }
    }

    async function playDecoded(peer, sub, isScreen, sender, ad) {
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

      if (isScreen) {
        // «Звук демонстрации идёт» — факт о ПОТОКЕ, а не о наших динамиках,
        // поэтому отмечаем его до проверки аудиографа. Иначе при
        // заблокированном автозвуке (обновили страницу и ни разу не кликнули)
        // человек не увидел бы даже намёка, что звук есть, — и не понял бы,
        // что надо кликнуть по странице.
        st.lastScreenAudioAt = Date.now();
        if (opts.onScreenAudio) opts.onScreenAudio(sender);
      } else {
        // «Говорит» — только по голосу. Громкая фонограмма демонстрации не
        // означает, что человек говорит, и подсвечивать его плитку по ней
        // значило бы врать (см. §5.3 методички).
        let sum = 0, m = 0;
        for (let i = 0; i < n; i += 16) { sum += f32[i] * f32[i]; m++; }
        if (m && Math.sqrt(sum / m) > 0.02) opts.onSpeaking(sender);
      }

      if (!st.ctx || st.ctx.state !== "running" || !(await st.workletsReady)) return;
      if (!sub.player) {
        sub.player = new AudioWorkletNode(st.ctx, "mu-player",
          { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [1] });
        if (isScreen) {
          sub.player.connect(st.screenGain);   // своя громкость у слушателя
        } else {
          // Плеер репортит глубину буфера — по ней считается audioPlayhead.
          sub.player.port.onmessage = (e) => { peer.aDepth = e.data; };
          sub.player.connect(peerOut(peer, sender));
        }
      }
      // Часы для синхронизации губ ведёт только голос: под фонограмму
      // придерживать картинку не нужно и вредно. И ставим их здесь, а не
      // выше: если звук не играет, видео не должно его ждать.
      if (!isScreen) peer.aClock = { ts: adTs, at: performance.now() };
      sub.player.port.postMessage(f32, [f32.buffer]);
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
      src.connect(peerOut(peer, sender));   // личная громкость и у legacy-PCM
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
        for (const sub of [p.voice, p.scrAudio]) {
          if (sub.dec) { try { sub.dec.close(); } catch (e) {} }
          if (sub.player) { try { sub.player.disconnect(); } catch (e) {} }
        }
        if (p.gain) { try { p.gain.disconnect(); } catch (e) {} }
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
      stopScreenAudio();
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
      stopScreen: () => { screenSender.stop(); stopScreenAudio(); },
      // Звук демонстрации. Возвращает true, если браузер отдал звуковую
      // дорожку и мы её действительно повезём.
      startScreenAudio, stopScreenAudio,
      // Идёт ли звук демонстрации прямо сейчас — по нему показывают ручку
      // громкости на сцене: пока звука нет, ручке там делать нечего.
      screenAudioLive: () => Date.now() - st.lastScreenAudioAt < 2000,
      onBinary, forceKeyframe, requestKeyframe,
      videoSinkRef: (id) => sinkRefFor(st.sinks, st.sinkRefs, id),
      screenSinkRef: (id) => sinkRefFor(st.screenSinks, st.screenSinkRefs, id),
      dropPeer, clearPeers,
      lastFrameAt: st.lastFrameAt,
      micLevel: () => st.micLevel,
      setVolume: (v) => { st.volume = v; if (st.masterGain) st.masterGain.gain.value = masterLevel(); },
      setScreenVolume: (v) => { st.screenVolume = v; if (st.screenGain) st.screenGain.gain.value = v; },
      // «Не слышу вас». Глушим только выход: очереди по-прежнему вычерпываются,
      // поэтому ни картинка, ни подсветка «говорит» не встают.
      setDeafened: (on) => {
        st.deafened = !!on;
        if (st.masterGain) st.masterGain.gain.value = masterLevel();
      },
      // Личная громкость участника (1 — как есть). Помнится и для тех, кто
      // сейчас молчит: узел создаётся при первом же его звуке.
      setPeerVolume: (id, v) => {
        st.peerVolume.set(id, v);
        const p = st.peers.get(id);
        if (p && p.gain) p.gain.gain.value = v;
      },
      peerVolume: (id) => {
        const v = st.peerVolume.get(id);
        return v == null ? 1 : v;
      },
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
      // Что этот браузер умеет ДЕКОДИРОВАТЬ. Для раздела «О программе»:
      // объясняет, почему у одного участника чужая демонстрация идёт как
      // есть, а у другого отправитель спускается на H.264.
      decodeOk: async () => {
        const out = [];
        for (const id of Object.keys(VIDEO_DECODE)) {
          if (await decoderCodecFor(Number(id))) out.push(CODEC_NAME[id] || id);
        }
        return out;
      },
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
      // Скорости за время с прошлого вызова. Считать «ровно за секунду» не
      // нужно и вредно: интервал таймера плавает, а деление на реальный
      // промежуток даёт честное число при любом дрожании.
      metrics: () => {
        const m = st.meter;
        const now = Date.now();
        const dt = m.at ? (now - m.at) / 1000 : 0;
        const out = { rxTotal: m.rxBytes, txTotal: m.txBytes,
                      rxKbps: 0, txKbps: 0, rxFps: 0, txFps: 0 };
        if (dt > 0.2) {
          out.rxKbps = Math.round((m.rxBytes - m.lastRx) * 8 / dt / 1000);
          out.txKbps = Math.round((m.txBytes - m.lastTx) * 8 / dt / 1000);
          out.rxFps = Math.round((m.rxFrames - m.lastRxF) / dt * 10) / 10;
          out.txFps = Math.round((m.txFrames - m.lastTxF) / dt * 10) / 10;
          m.at = now; m.lastRx = m.rxBytes; m.lastTx = m.txBytes;
          m.lastRxF = m.rxFrames; m.lastTxF = m.txFrames;
        } else if (!m.at) {
          m.at = now;
        }
        return out;
      },
      stats: () => {
        const cam = choiceFor(false), scr = choiceFor(true);
        return { videoCodec: st.stats.videoCodec, audioCodec: st.stats.audioCodec,
                 camCodec: cam ? (CODEC_NAME[cam.id] || cam.codec) : "JPEG",
                 screenCodec: scr ? (CODEC_NAME[scr.id] || scr.codec) : "JPEG",
                 // Чем этот браузер умеет кодировать — пригодится в разделе
                 // диагностики и объясняет, почему откат возможен не всегда.
                 encodeOk: st.videoOk.map((c) => CODEC_NAME[c.id] || c.codec),
                 // Насколько сеть прижала каждую полосу (1 — не прижала).
                 camRate: st.rate.cam, screenRate: st.rate.screen,
                 buffered: opts.buffered(),
                 screenAudio: !!st.scrAenc,
                 encrypted: !!st.cipher };
      },
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

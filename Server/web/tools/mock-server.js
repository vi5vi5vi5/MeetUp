#!/usr/bin/env node
"use strict";
// ============================================================
//  Стенд для разработки интерфейса конференции.
//
//  Настоящий сервер — это Qt/C++ (../..), собирается docker'ом и на Windows
//  под рукой его обычно нет. А посмотреть, как выглядит комната с людьми,
//  демонстрацией и чатом, нужно постоянно. Этот файл поднимает ровно
//  столько, чтобы клиент дошёл до эфира: раздачу файлов и WebSocket, который
//  отвечает на join, ping, state, screen и chat.
//
//  Он НЕ часть продукта: лежит в tools/, в dist и в образ не попадает.
//  Медиа он не разбирает вовсе — бинарные кадры просто рассылает остальным,
//  как настоящий сервер (вставляя sender после первого байта).
//
//  Запуск:  node tools/mock-server.js [порт]
//
//  Порт по умолчанию — 9000, и это не случайное число: страница по http
//  всегда идёт на ws://<хост>:9000 (см. wsUrl в meetup-common.js). Открыв
//  стенд на другом порту, вы получите работающую вёрстку и мёртвый сокет.
// ============================================================

const http = require("http");
const fs = require("fs");
const path = require("path");
const crypto = require("crypto");

const PORT = Number(process.argv[2]) || 9000;
const ROOT = path.resolve(__dirname, "..", "dist");

const MIME = {
  ".html": "text/html; charset=utf-8", ".js": "application/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8", ".json": "application/json; charset=utf-8",
  ".svg": "image/svg+xml", ".png": "image/png", ".jpg": "image/jpeg",
  ".ico": "image/x-icon", ".woff2": "font/woff2",
};

// ---- Комната ----------------------------------------------------------
// Двое «живых» собеседников, которых мы придумали, и все, кто реально
// подключился к стенду. Их id занимают числа с 10, чтобы не путаться.
const FAKES = [
  { id: 2, name: "Анна Кольцова", mic: true, cam: false },
  { id: 3, name: "Борис", mic: false, cam: false },
];
const HISTORY = [
  { sender_id: 2, sender_name: "Анна Кольцова", text: "Всем привет! Начинаем через минуту.",
    timestamp_ms: Date.now() - 300000 },
  { sender_id: 3, sender_name: "Борис", text: "Я на месте, слышно хорошо.",
    timestamp_ms: Date.now() - 240000 },
];
const clients = new Set();
let nextId = 10;
let screenId = null;

function send(c, obj) { writeFrame(c.socket, 1, Buffer.from(JSON.stringify(obj), "utf8")); }
function broadcast(obj, except) {
  for (const c of clients) if (c !== except && c.joined) send(c, obj);
}

function onJson(c, msg) {
  switch (msg.type) {
    case "join":
      c.name = String(msg.name || "Гость").slice(0, 64);
      c.mic = !!msg.mic; c.cam = !!msg.cam;
      c.joined = true;
      console.log("  join: " + c.name + " (mic=" + c.mic + " cam=" + c.cam + ")");
      send(c, { type: "join_ok", sender_id: c.id,
                participants: FAKES.concat(
                  Array.from(clients).filter(o => o !== c && o.joined)
                       .map(o => ({ id: o.id, name: o.name, mic: o.mic, cam: o.cam }))),
                history: HISTORY,
                room_title: "Стенд разработчика",
                ...(screenId != null ? { screen_id: screenId } : {}) });
      broadcast({ type: "participant_joined", id: c.id, name: c.name, mic: c.mic, cam: c.cam }, c);
      break;
    case "ping":
      send(c, { type: "pong", t: msg.t });
      break;
    case "state":
      c.mic = !!msg.mic; c.cam = !!msg.cam;
      broadcast({ type: "participant_state", id: c.id, mic: c.mic, cam: c.cam }, c);
      break;
    case "screen":
      if (msg.on) {
        if (screenId != null && screenId !== c.id) { send(c, { type: "error", reason: "screen_busy" }); break; }
        screenId = c.id;
      } else if (screenId === c.id) {
        screenId = null;
      }
      for (const o of clients) if (o.joined) send(o, { type: "screen", id: c.id, on: msg.on });
      break;
    case "chat": {
      const out = { type: "chat", sender_id: c.id, sender_name: c.name,
                    text: msg.text, timestamp_ms: Date.now() };
      if (msg.image) out.image = msg.image;
      for (const o of clients) if (o.joined) send(o, out);
      break;
    }
    default:
      send(c, { type: "error", reason: "unknown_type" });
  }
}

// Бинарь: как настоящий сервер — вставляем sender (4 байта LE) после первого
// байта и рассылаем остальным, содержимое не трогаем.
function onBinary(c, buf) {
  if (buf.length < 9) return;
  const out = Buffer.alloc(buf.length + 4);
  out[0] = buf[0];
  out.writeUInt32LE(c.id, 1);
  buf.copy(out, 5, 1);
  for (const o of clients) if (o !== c && o.joined) writeFrame(o.socket, 2, out);
}

// ---- Минимальный WebSocket (RFC 6455) ---------------------------------
// Без зависимостей: у проекта их нет и здесь заводить незачем.

function writeFrame(socket, opcode, payload) {
  const len = payload.length;
  let header;
  if (len < 126) {
    header = Buffer.alloc(2);
    header[1] = len;
  } else if (len < 65536) {
    header = Buffer.alloc(4);
    header[1] = 126;
    header.writeUInt16BE(len, 2);
  } else {
    header = Buffer.alloc(10);
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(len), 2);
  }
  header[0] = 0x80 | opcode;
  try { socket.write(Buffer.concat([header, payload])); } catch (e) {}
}

function attach(socket, client) {
  let buf = Buffer.alloc(0);
  socket.on("data", (chunk) => {
    buf = Buffer.concat([buf, chunk]);
    for (;;) {
      if (buf.length < 2) return;
      const opcode = buf[0] & 0x0f;
      const masked = (buf[1] & 0x80) !== 0;
      let len = buf[1] & 0x7f;
      let off = 2;
      if (len === 126) { if (buf.length < 4) return; len = buf.readUInt16BE(2); off = 4; }
      else if (len === 127) { if (buf.length < 10) return; len = Number(buf.readBigUInt64BE(2)); off = 10; }
      const maskLen = masked ? 4 : 0;
      if (buf.length < off + maskLen + len) return;
      const mask = masked ? buf.subarray(off, off + 4) : null;
      const body = Buffer.from(buf.subarray(off + maskLen, off + maskLen + len));
      if (mask) for (let i = 0; i < body.length; i++) body[i] ^= mask[i & 3];
      buf = buf.subarray(off + maskLen + len);

      if (opcode === 8) { socket.end(); return; }
      if (opcode === 9) { writeFrame(socket, 10, body); continue; }
      if (opcode === 1) {
        try { onJson(client, JSON.parse(body.toString("utf8"))); }
        catch (e) { send(client, { type: "error", reason: "invalid_json" }); }
      } else if (opcode === 2) {
        onBinary(client, body);
      }
    }
  });
  const drop = () => {
    clients.delete(client);
    if (screenId === client.id) {
      screenId = null;
      broadcast({ type: "screen", id: client.id, on: false });
    }
    if (client.joined) broadcast({ type: "participant_left", id: client.id });
  };
  socket.on("close", drop);
  socket.on("error", drop);
}

// Замер раскладки для headless-прогонов (см. ?probe=1 ниже).
const PROBE = `<script>
setTimeout(function () {
  var d = document.documentElement, out = {};
  function box(sel) {
    var e = document.querySelector(sel); if (!e) return null;
    var r = e.getBoundingClientRect();
    return [Math.round(r.left), Math.round(r.right), Math.round(r.width)];
  }
  out.vw = innerWidth;
  out.scrollW = d.scrollWidth;
  out.overflow = d.scrollWidth - d.clientWidth;
  out.conf = box(".conf");
  out.stage = box(".stage");
  out.grid = box(".grid");
  out.dock = box(".dockbar");
  out.side = box(".side");
  out.fab = box(".chat-fab");
  out.cols = getComputedStyle(document.querySelector(".conf")).gridTemplateColumns;
  out.gridCols = document.querySelector(".grid") ? getComputedStyle(document.querySelector(".grid")).gridTemplateColumns : null;
  out.tiles = document.querySelectorAll(".grid > div").length;
  var widest = null, max = 0;
  document.querySelectorAll("*").forEach(function (e) {
    var r = e.getBoundingClientRect();
    if (r.right > max) { max = r.right; widest = e.className || e.tagName; }
  });
  out.widest = [String(widest).slice(0, 40), Math.round(max)];
  document.title = "PROBE " + JSON.stringify(out);
}, 3500);
</scr` + `ipt>`;

// ---- HTTP -------------------------------------------------------------

const server = http.createServer((req, res) => {
  const url = req.url.split("?")[0];
  // Аккаунтов на стенде нет: конференция должна честно уйти на гейт «имя».
  if (url.startsWith("/api/")) {
    res.writeHead(404, { "Content-Type": "application/json" });
    res.end('{"error":"no_api_on_mock"}');
    return;
  }
  // Служебный вход: кладёт имя в sessionStorage (оттуда его берёт
  // conference.html) и уходит в комнату. Нужен, чтобы снимать конференцию
  // headless-браузером — гейт «представьтесь» руками там не пройти.
  // Живёт только в стенде, в продукт не попадает.
  if (url === "/dev-enter.html") {
    const q = new URLSearchParams(req.url.split("?")[1] || "");
    const name = (q.get("name") || "Игорь").slice(0, 64);
    const room = (q.get("room") || "DEV").slice(0, 64);
    const json = JSON.stringify;
    res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
    res.end("<!doctype html><meta charset=utf-8><script>"
      + "sessionStorage.setItem('meetup.name'," + json(name) + ");"
      + "location.replace('conference.html?room=' + encodeURIComponent(" + json(room) + ")"
      + (q.get("probe") ? " + '&probe=1'" : "")
      + (q.get("open") ? " + '&open=" + encodeURIComponent(q.get("open")) + "'" : "") + ");"
      + "</scr" + "ipt>");
    return;
  }

  const rel = url === "/" ? "/login.html" : url;
  const file = path.join(ROOT, path.normalize(rel).replace(/^[\\/]+/, ""));
  if (!file.startsWith(ROOT) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    res.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
    res.end("404");
    return;
  }
  // ?probe=1 — дописать к странице замер раскладки. Headless-браузер не даёт
  // выполнить свой скрипт, а --dump-dom отдаёт разметку, поэтому результат
  // кладём в <title>: так его видно и в дампе, и глазами.
  // ?open=<раздел> — открыть настройки на нужном разделе. Тоже ради снимков:
  // headless-браузеру некому нажать шестерёнку.
  const openOn = /[?&]open=([^&]*)/.exec(req.url);
  if ((/[?&]probe=1(&|$)/.test(req.url) || openOn) && file.endsWith(".html")) {
    let html = fs.readFileSync(file, "utf8");
    if (/[?&]probe=1(&|$)/.test(req.url)) html = html.replace("</body>", PROBE + "</body>");
    if (openOn) {
      const want = decodeURIComponent(openOn[1]);
      html = html.replace("</body>", "<script>setTimeout(function () {"
        + "var d = document.querySelector('.dockbar button[aria-label=\"Настройки\"]');"
        + "if (d) d.click();"
        + "setTimeout(function () {"
        + "  var want = " + JSON.stringify(want) + ";"
        + "  if (!want || want === '1') return;"
        + "  var i = [].slice.call(document.querySelectorAll('.set-item'))"
        + "      .filter(function (b) { return b.textContent.trim() === want; })[0];"
        + "  if (i) i.click();"
        + "}, 400);"
        + "}, 2500);</scr" + "ipt></body>");
    }
    res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
    res.end(html);
    return;
  }
  res.writeHead(200, { "Content-Type": MIME[path.extname(file)] || "application/octet-stream" });
  fs.createReadStream(file).pipe(res);
});

server.on("upgrade", (req, socket) => {
  const key = req.headers["sec-websocket-key"];
  if (!key) { socket.destroy(); return; }
  const accept = crypto.createHash("sha1")
    .update(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest("base64");
  socket.write("HTTP/1.1 101 Switching Protocols\r\n"
    + "Upgrade: websocket\r\nConnection: Upgrade\r\n"
    + "Sec-WebSocket-Accept: " + accept + "\r\n\r\n");
  socket.setNoDelay(true);
  const client = { socket, id: nextId++, name: "", mic: false, cam: false, joined: false };
  clients.add(client);
  attach(socket, client);
});

server.listen(PORT, "127.0.0.1", () => {
  console.log("Стенд: http://127.0.0.1:" + PORT + "/conference.html?room=DEV");
  console.log("Раздаётся " + ROOT + " (не забудьте node tools/build.js)");
  console.log("В комнате уже сидят: " + FAKES.map(f => f.name).join(", "));
});

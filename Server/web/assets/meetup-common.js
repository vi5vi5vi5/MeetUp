"use strict";
// Общие помощники страниц MeetUp: адрес WebSocket, HTTP API, имя пользователя.
(function () {
  var WS_PORT = 9000; // при https WS идёт через nginx (/ws) — порт не нужен

  // Страница по https обязана использовать wss — TLS терминирует nginx,
  // который проксирует /ws на WS-порт сервера (см. proxy/nginx.conf).
  // По http (локальная разработка без прокси) коннектимся напрямую на порт.
  function wsUrl() {
    return location.protocol === "https:"
        ? "wss://" + location.host + "/ws"
        : "ws://" + location.hostname + ":" + WS_PORT;
  }

  // fetch-обёртка: JSON-ответ, сетевые ошибки не бросаются, а дают status 0.
  // body (объект) сериализуется в JSON; кука сессии ходит сама (same-origin).
  function api(method, path, body) {
    var opts = { method: method, headers: { "Accept": "application/json" } };
    if (body !== undefined) {
      opts.headers["Content-Type"] = "application/json";
      opts.body = JSON.stringify(body);
    }
    return fetch(path, opts)
      .then(function (resp) {
        return resp.json()
          .catch(function () { return null; })
          .then(function (body) { return { ok: resp.ok, status: resp.status, body: body }; });
      })
      .catch(function () { return { ok: false, status: 0, body: null }; });
  }

  // --- Портрет сервера -------------------------------------------------------
  // Что на этом сервере разрешено (GET /api/config). Страницы здесь отдельные,
  // поэтому ответ кладём в sessionStorage: без кэша переход со входа на
  // регистрацию моргал бы кнопками, которых сервер не даёт.
  //
  // Правило на неизвестное: отсутствующее поле — РАЗРЕШАЮЩЕЕ. Так новая
  // страница продолжает работать со старым сервером, который про эти поля
  // ещё не знает.
  var CONFIG_KEY = "meetup.config";
  var configPromise = null;

  // Синхронно: то, что известно прямо сейчас (кэш прошлой страницы или пусто).
  // Годится для первой отрисовки — обновит serverConfig().
  function serverConfigNow() {
    try { return JSON.parse(sessionStorage.getItem(CONFIG_KEY)) || {}; }
    catch (e) { return {}; }
  }

  // Сколько миллисекунд идёт круг до сервера и обратно. Меряем на самой
  // лёгкой ручке — портрете сервера; кэш браузера обходим сами, иначе второй
  // замер показал бы ноль и соврал.
  function pingServer() {
    const t0 = performance.now();
    return fetch("/api/config?t=" + t0, { cache: "no-store" })
      .then(() => Math.round(performance.now() - t0))
      .catch(() => -1);
  }

  function serverConfig() {
    if (!configPromise) {
      configPromise = api("GET", "/api/config").then(function (resp) {
        // Сервер не ответил (старая версия, сеть) — работаем на том, что есть.
        if (!resp.ok || !resp.body) return serverConfigNow();
        try { sessionStorage.setItem(CONFIG_KEY, JSON.stringify(resp.body)); } catch (e) {}
        return resp.body;
      });
    }
    return configPromise;
  }

  // Имя пользователя между страницами (лобби -> конференция).
  var NAME_KEY = "meetup.name";
  function savedName() {
    try { return sessionStorage.getItem(NAME_KEY) || ""; } catch (e) { return ""; }
  }
  function saveName(name) {
    try { sessionStorage.setItem(NAME_KEY, name); } catch (e) { /* приватный режим */ }
  }

  // --- Аккаунты --------------------------------------------------------------
  // Сессия живёт в HttpOnly-куке meetup_session — JS её не видит и не хранит.
  // Ответы: { ok, status, body }; при ошибке body.error — машинный код
  // ("login_taken", "wrong_credentials", ...), текст подбирает страница.
  function authLogin(login, password) {
    return api("POST", "/api/auth/login", { login: login, password: password });
  }
  function authRegister(login, password, displayName) {
    return api("POST", "/api/auth/register",
               { login: login, password: password, display_name: displayName });
  }
  function authLogout() { return api("POST", "/api/auth/logout"); }
  function authMe() { return api("GET", "/api/me"); }
  function updateMe(patch) { return api("PATCH", "/api/me", patch); }

  // --- Личная комната владельца ----------------------------------------------
  // body.room: { code, title, password, online, participants }. Ошибки:
  // "no_room", "invalid_code", "code_taken", "room_exists", "invalid_title".
  // Комнат у человека может быть несколько (сколько — говорит сервер в
  // rooms.max). Все операции адресуют конкретную комнату по её id.
  //
  // Ручки без номера (/api/me/room) на сервере остались, но нужны они не нам:
  // по ним ходят скачанные раньше десктопные клиенты, которые знают ровно про
  // одну комнату. Веб-клиент раздаётся вместе с сервером и всегда одной с ним
  // версии, поэтому здесь сразу новые адреса.
  function myRooms() { return api("GET", "/api/me/rooms"); }
  function createMyRoom(data) { return api("POST", "/api/me/rooms", data); }
  function updateMyRoom(id, patch) { return api("PATCH", "/api/me/rooms/" + id, patch); }
  function deleteMyRoom(id) { return api("DELETE", "/api/me/rooms/" + id); }
  function closeMyRoom(id) { return api("POST", "/api/me/rooms/" + id + "/close"); }
  function roomAliases(id) { return api("GET", "/api/me/rooms/" + id + "/aliases"); }
  function createRoomAlias(id, data) { return api("POST", "/api/me/rooms/" + id + "/aliases", data); }
  function updateRoomAlias(id, aliasId, patch) {
    return api("PATCH", "/api/me/rooms/" + id + "/aliases/" + aliasId, patch);
  }
  function deleteRoomAlias(id, aliasId) {
    return api("DELETE", "/api/me/rooms/" + id + "/aliases/" + aliasId);
  }

  // --- «Добавить на главный экран» -------------------------------------------
  // Регистрируем service worker: с ним страница ставится на телефон как
  // приложение (свой значок, запуск без адресной строки) и не перекачивает
  // ассеты при каждом заходе. Требует защищённого контекста — по http
  // (локальная разработка) браузер его не даст, и это нормально.
  function registerWorker() {
    if (!("serviceWorker" in navigator) || !window.isSecureContext) return;
    window.addEventListener("load", function () {
      navigator.serviceWorker.register("/sw.js").catch(function () {
        // Не зарегистрировался — сайт просто работает как раньше.
      });
    });
  }
  registerWorker();

  window.MeetUp = {
    wsUrl: wsUrl,
    api: api,
    serverConfig: serverConfig,
    pingServer: pingServer,
    serverConfigNow: serverConfigNow,
    savedName: savedName,
    saveName: saveName,
    authLogin: authLogin,
    authRegister: authRegister,
    authLogout: authLogout,
    authMe: authMe,
    updateMe: updateMe,
    myRooms: myRooms,
    createMyRoom: createMyRoom,
    updateMyRoom: updateMyRoom,
    deleteMyRoom: deleteMyRoom,
    closeMyRoom: closeMyRoom,
    roomAliases: roomAliases,
    createRoomAlias: createRoomAlias,
    updateRoomAlias: updateRoomAlias,
    deleteRoomAlias: deleteRoomAlias,
  };
})();

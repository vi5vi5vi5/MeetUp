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

  window.MeetUp = {
    wsUrl: wsUrl,
    api: api,
    savedName: savedName,
    saveName: saveName,
    authLogin: authLogin,
    authRegister: authRegister,
    authLogout: authLogout,
    authMe: authMe,
    updateMe: updateMe,
  };
})();

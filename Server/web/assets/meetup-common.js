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
  function api(method, path) {
    return fetch(path, { method: method, headers: { "Accept": "application/json" } })
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

  // --- Заглушки входа по аккаунту -------------------------------------------
  // Сервер отвечает 501 not_implemented. Когда появится реальная авторизация,
  // здесь будут передаваться креды и обрабатываться токен/сессия.
  // TODO(auth): вход и регистрация по аккаунту.
  function authLogin(login, password) { return api("POST", "/api/auth/login"); }
  function authRegister(login, password) { return api("POST", "/api/auth/register"); }

  window.MeetUp = {
    wsUrl: wsUrl,
    api: api,
    savedName: savedName,
    saveName: saveName,
    authLogin: authLogin,
    authRegister: authRegister,
  };
})();

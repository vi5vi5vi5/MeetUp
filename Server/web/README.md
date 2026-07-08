# web/

Веб-клиент MeetUp (дизайн «Prism Minimal», React + JSX, компилируется
Babel'ем прямо в браузере — сборка не нужна). Отдаётся встроенным
`HttpFileServer`; точка входа `/` — это `login.html`.

## Страницы

| Файл | Назначение |
|---|---|
| `login.html` | Вход по аккаунту (`POST /api/auth/login`; живая сессия в куке ведёт в лобби автоматически) либо «Войти без аккаунта» → `anon_lobby.html`. |
| `register.html` | Регистрация: логин, отображаемое имя, пароль с повтором (`POST /api/auth/register`); после успеха — сразу в лобби с сессией в куке. |
| `anon_lobby.html` | Лобби анонимного пользователя: имя + код комнаты, либо «Создать трансляцию» (`POST /api/rooms`, код генерирует сервер). У вошедших по аккаунту имя уже подставлено. |
| `conference.html?room=<код>` | Конференция: плитки с видео/заглушками и пагинацией, чат с историей, кнопка «поделиться ссылкой». Открытая по ссылке без имени показывает гейт «Представьтесь». Настройки (шестерёнка в доке): выбор микрофона/камеры/динамиков, громкость воспроизведения и чувствительность микрофона — хранятся в `localStorage`, ключ `meetup.av`. На мобильном вместо демонстрации экрана в доке кнопка настроек. |
| `index.html` | Старый тестовый клиент (legacy). Join теперь требует существующую комнату, поэтому для проверок сперва создайте её через `POST /api/rooms`. |

## assets/

Общие ресурсы страниц (в браузере кэшируются, см. Cache-Control в
`HttpFileServer`):

- `react.js`, `react-dom.js`, `babel.js` — React 18 (dev-сборки) и
  @babel/standalone для JSX без шага сборки;
- `meetup-ds.js` — дизайн-система (Card, Button, VideoTile, ControlDock…),
  глобал `window.MeetUpDesignSystem_2584b5`;
- `meetup-common.js` — помощники страниц: адрес WebSocket, fetch-обёртка
  `/api` (JSON-тело, кука сессии ходит сама), имя пользователя в
  sessionStorage, вызовы аккаунтов (`authLogin/authRegister/authMe/…`);
- `theme.css` — дизайн-токены (цвета/типографика/отступы/радиусы),
  тёмная тема по умолчанию, светлая через `[data-theme="light"]`;
- `fonts.css` + `fonts/*.woff2` — Manrope, Space Grotesk, Unbounded
  (self-hosted, без походов в Google Fonts).

К WebSocket клиент подключается в зависимости от схемы страницы:
по https — `wss://<host>/ws` (через nginx из [`../proxy`](../proxy/README.md)),
по http (локальная разработка) — напрямую `ws://<host>:9000`.

Протокол и HTTP API описаны в [`../docs/meetup-server-spec.md`](../docs/meetup-server-spec.md).

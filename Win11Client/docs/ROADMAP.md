# MeetUp — десктопный клиент для Windows 11 · Roadmap

Нативный клиент MeetUp на **Qt6 / QML** под Windows 11. Совместим по протоколу с
эталонным веб-клиентом (`../Server/web/`) и сервером-релеем. В отличие от веба
(один «толстый» HTML на мобильку и десктоп сразу) — это отдельное десктопное
приложение: только Win11, только клавиатура/мышь, полный экран.

> Полная спецификация протокола — в методичке сервера:
> [`../../Server/docs/desktop-client-guide.md`](../../Server/docs/desktop-client-guide.md).
> Этот файл — карта нашей реализации и порядок работ.

---

## Что мы строим

- **Тулчейн:** MSVC 2022 + Qt 6.11.1 (`msvc2022_64`), CMake + Ninja, пресеты
  `Debug-x64` / `Release-x64`.
- **Архитектура:** тонкий C++-бэкенд (сеть, медиа, крипто) ↔ QML-фронтенд.
  Вся логика кодеков/шифрования/отрисовки — на клиенте; сервер лишь
  ретранслирует.
- **Дизайн:** «Prism Minimal» — тёмная near-monochrome тема с единственным
  акцентом acid-lime `#c6ff3d`, шрифты Unbounded / Manrope / Space Grotesk,
  Lucide-иконки. Токены 1:1 с веб-клиентом (`Server/web/assets/theme.css`),
  собраны в `qml/Theme.qml`.
- **Тест-сервер по умолчанию:** `meetup.linkpc.net` (82.40.57.187), прод за
  nginx с Let's Encrypt → штатная TLS-валидация; `wss://<host>/ws` + REST на
  том же origin.

---

## Вехи

| Веха | Что даёт | Новые зависимости | Статус |
|---|---|---|---|
| **M0** Фундамент и UI-каркас | Qt Quick-приложение, тема, компоненты, экраны лобби/конференции на мок-данных, шрифты, скрипты сборки | только Qt (Core, Gui, Qml, Quick) | ✅ готово |
| **M1** Сеть: комнаты, join, чат | Создание/проверка комнаты по HTTP, WS-`join`, живой список участников, текстовый чат; переподключение | Qt WebSockets | ⬜ |
| **M2** Аудио | Захват 48 кГц mono → libopus 20 мс → пакеты v2; приём → декод → джиттер-буфер → `QAudioSink` | libopus, Qt Multimedia | ⬜ |
| **M3** Приём видео | FFmpeg-декод H.264/VP8/VP9, keyframe/`KEYFRAME_REQ`, отрисовка в плитки | FFmpeg (avcodec/avutil/swscale) | ⬜ |
| **M4** Отправка видео | `QCamera` → энкод (H.264 Annex B или VP8) → пакеты, backpressure | FFmpeg (энкодер) | ⬜ |
| **M5** E2E-шифрование | Ключ из фразы/`#k=`-ссылки, AES-256-GCM seal/open медиа+чата+картинок, состояние «заблокировано ключом» | OpenSSL 3 (libcrypto) | ⬜ |
| **M6** Аккаунты | login/register, сессия в `QNetworkCookieJar`, аватарки, личные комнаты + вход по паролю, alias-ссылки | — | ⬜ |
| **M7** Демонстрация экрана | `QScreenCapture` → второй поток (`SCREEN_CODED`), протокол `screen`/`screen_busy`/`screen_id`, отдельная плитка | — | ⬜ |
| **M8** Полировка | Смена устройств, картинки в чате, RTT-индикатор (`ping`), индикаторы «говорит» (RMS), пресеты качества, инсталлятор | — | ⬜ |

Порядок неслучаен (совпадает с §10 методички): аудио раньше видео — оно проще
и сразу даёт «живую» связь; отправка видео после приёма — можно смотреть на
вещающий веб-клиент, пока своя отправка не готова.

---

## M0 — что уже сделано

```
Win11Client/
  docs/ROADMAP.md              ← этот файл
  CMakeLists.txt               Qt Quick app (qt_add_qml_module, модуль MeetUp)
  CMakePresets.json / CMakeUserPresets.json
  src/main.cpp                 точка входа: QGuiApplication + QQmlApplicationEngine
  src/net|media|crypto/        README-заготовки под M1–M5
  qml/
    Main.qml                   окно + StackView (Lobby ↔ Conference)
    Theme.qml   (singleton)    все дизайн-токены + переключение тёмной/светлой
    MockData.qml (singleton)   мок-участники и сообщения
    components/                AppIcon, AppButton, IconButton, Field, AppInput,
                               Card, Badge, VideoTile, ChatMessage, ControlDock
    screens/                   LobbyScreen, ConferenceScreen
  resources/
    fonts/                     Unbounded / Manrope / Space Grotesk (TTF)
    icons/                     19 Lucide SVG (mic, video, screen, phone-off, …)
  scripts/                     configure.ps1 · build.ps1 · run.ps1 · deploy.ps1
```

Экран лобби (hero + карточка входа) и экран конференции (адаптивная сетка
плиток, плавающий док управления, панель чата/участников) работают на
мок-данных: кнопки mic/cam/screen переключают локальный вид, тумблер темы
меняет палитру вживую, «завершить» возвращает в лобби. **Сети и медиа пока нет.**

---

## Архитектура (целевая)

```
┌──────────────── QML (qml/) ────────────────┐
│  Screens ← Components ← Theme (singleton)   │
│        ▲ properties/signals ▼               │
├─────────────── C++ backend ────────────────┤
│  AppController        общий стейт для QML   │
│  net/  ApiClient · SignalingClient · Protocol
│  media/ AudioEngine · VideoDecoder · VideoEncoder · ScreenCapture
│  crypto/ E2eCipher (PBKDF2 + AES-256-GCM)   │
└─────────────────────────────────────────────┘
        ▲ HTTP (QNetworkAccessManager)  ▲ WS (QWebSocket)
        └──────────── MeetUp server ────┘
```

C++-объекты выставляются в QML через `setContextProperty` / регистрацию типов;
списки участников и сообщений — через `QAbstractListModel` (заменят нынешние
`MockData`-массивы).

---

## Карта протокола (кратко; детали — в методичке)

- **HTTP API:** `POST /api/rooms` → `{room:<24 симв.>}`; `GET /api/rooms/<код>`
  (проверка/пароль/`personal`); аккаунты `/api/auth/*`, `/api/me*`,
  `/api/me/room*`. Сессия — кука `meetup_session` (HttpOnly), едет и в
  WS-хендшейке.
- **WS JSON:** →`join`/`chat`/`state`/`screen`/`ping`;
  ←`join_ok`/`participant_joined|left|state`/`chat`/`screen`/`pong`/`error`.
- **WS бинарь (медиа v2), little-endian:** отправка — заголовок **11 байт**
  `[type][flags][codec][ts:8]`; приём — **15 байт** (сервер вставил
  `sender:4`). `ts` = `Date.now()` мс (общая шкала аудио/видео!). Типы:
  `VIDEO_JPEG=1, AUDIO_PCM=2, VIDEO_CODED=3, AUDIO_CODED=4, SCREEN_CODED=5,
  KEYFRAME_REQ=6, SCREEN_JPEG=7`. Флаги: `KEYFRAME=1, ENCRYPTED=2`. Кодеки:
  `vp8=1, h264=2 (Annex B!), vp9=3, opus=4`. **Эти константы — контракт
  клиента; сервер медиа не разбирает.**
- **Подключение:** https-origin → `wss://<host>/ws` (nginx проксирует на 9000);
  http-origin → `ws://<host>:9000` напрямую. Локально: HTTP 80 / WS 9000.

### Чек-лист подводных камней (держать под рукой на M1+)

- [ ] Всё числовое в бинаре — little-endian; отправка 11 байт, приём 15.
- [ ] `timestamp` — `Date.now()` в **мс**, одна шкала аудио и видео.
- [ ] H.264 — только Annex B, SPS/PPS в каждом keyframe, Baseline, без B-кадров.
- [ ] Размеры видеокадра — чётные; keyframe каждые ~72 кадра + по `KEYFRAME_REQ`.
- [ ] После пересоздания декодера — ждать keyframe, слать `KEYFRAME_REQ` (1/с).
- [ ] E2E: IV уникален (префикс+счётчик), AAD `[type,codec]`, tag в конце,
      заголовок не шифруется; PBKDF2-**SHA256** 150k, соль `"meetup-e2e-v1|"+код`.
- [ ] base64url **без паддинга** для `#k=` и чата; чат-текст AAD-тип 250,
      картинка 251; префикс `🔒e2e:` сравнивать по байтам UTF-8.
- [ ] Ключ восстановить **до** обработки `join_ok` (иначе история не расшифруется).
- [ ] `sender_id` анонима меняется на каждом join — не кэшировать между сессиями.
- [ ] Backpressure: >1.5 МБ в сокете — дропать видеокадры (аудио продолжать).
- [ ] `session_replaced`/`room_closed`/`room_not_found`/`alias_forbidden` —
      фатальны (не реконнектиться); `wrong_password`/`room_offline` — ждать/спросить.

---

## Сборка и запуск

```powershell
# из папки Win11Client
scripts\configure.ps1 -Config Debug   # cmake --preset Debug-x64
scripts\build.ps1     -Config Debug
scripts\run.ps1       -Config Debug    # собрать и запустить (Qt DLL берутся из кита)
scripts\deploy.ps1                     # чистая самодостаточная сборка -> dist\Release
scripts\deploy.ps1 -Portable           # +MSVC-рантайм для переноса на «голый» ПК
scripts\clean.ps1  [-All]              # удалить out\ (и dist\ с -All)
```

Две выходные папки, не путать:

- **`out\build\<cfg>\`** — *дерево сборки* CMake/Ninja: объектники, кэш, autogen,
  qmlcache. Там же лежит `Win11Client.exe`, но рядом гора промежуточных файлов —
  это нормально, папка в `.gitignore`. Запускать оттуда напрямую не стоит: exe
  не самодостаточен (нужны Qt DLL из кита; `run.ps1` подставляет их через PATH).
- **`dist\<cfg>\`** — *чистая самодостаточная сборка* (`deploy.ps1`): только
  `Win11Client.exe` + минимально нужный Qt-рантайм. Весь QML, шрифты и иконки
  уже вшиты в exe (Qt-ресурсы), снаружи только DLL. Запускается двойным кликом
  и переносится на другой ПК. Lean-режим ≈ 48 МБ (без software-OpenGL,
  d3dcompiler, D3D12-компилятора шейдеров и установщика VC++); `-Portable`
  добавляет VC++-рантайм для машин без него.

Полностью статический одиночный exe (без DLL вовсе) потребовал бы отдельной
статической сборки Qt — вне текущего кита; при необходимости добавим позже.

Требования: Qt 6.11.1 `msvc2022_64` (модули Core, Gui, Qml, Quick,
QuickControls2; с M1 — WebSockets, с M2 — Multimedia), CMake ≥ 3.21, Ninja,
MSVC 2022. Путь к Qt задаётся в `CMakeUserPresets.json` (`QTDIR`).

Зависимости старших вех (FFmpeg, libopus, OpenSSL) под MSVC подключим через
**vcpkg** (manifest-режим) на соответствующих этапах.

---

## Дизайн-токены (шпаргалка)

| | dark | | | dark |
|---|---|---|---|---|
| bg | `#0e0e10` | | accent | `#c6ff3d` |
| surface | `#161619` | | onAccent | `#10120a` |
| surface-2 | `#1e1e22` | | text | `#f2f2f4` |
| surface-3 | `#26262b` | | text-muted | `#9a9aa2` |
| border | `#26262b` | | danger | `#ff5470` |

Радиусы: card 16 · lg 14 · md 12 · sm 10 · xs 7 · pill 999. Отступы: grid gap
10, inline 8, card 24, panel 20, stage 26. Всё — в `qml/Theme.qml`; там же живёт
светлая тема и переключатель `Theme.toggle()`.

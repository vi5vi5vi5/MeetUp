# MeetUp — сервер

WebSocket-relay сервер конференций (Qt6, C++20) + примитивный веб-клиент для тестирования.
Сервер не декодирует медиа — только пересылает сжатые кадры участникам одной комнаты (SFU).

`C++20` · `Qt6 (Core, Network, WebSockets)` · `CMake + Ninja` · `MinGW`

## Что уже работает

- WebSocket-relay: `join` / `join_ok` / `participant_joined` / `participant_left`
- Текстовый чат с временными метками
- Бинарный релей медиа внутри комнаты; сервер не знает формат полезной
  нагрузки: клиенты шлют видео до 720p (WebCodecs: H.264/VP8/VP9) и Opus-звук,
  фолбэк для старых браузеров — JPEG/PCM
- Сквозное шифрование по желанию участников: AES-256-GCM на клиенте, ключ из
  фразы или ссылки `#k=…`; сервер ретранслирует и хранит только шифротекст
- Раздача веб-клиента по HTTP (без Python — встроенный `HttpFileServer`)
- Аккаунты: регистрация, вход, сессии в HttpOnly-куке, смена отображаемого
  имени (`/api/auth/*`, `/api/me`); хранение пока InMemory — до перезапуска

## Структура проекта

Каждая папка документирована отдельно — здесь только карта и ссылки.
Разбиение по образцу [MedFlow](https://github.com/vi5vi5vi5/medflow):
модели / интерфейсы хранилищ / сервисы.

| Папка / файл | Что внутри | Подробности |
|---|---|---|
| `main.cpp` | Точка входа: разбор аргументов, сборка зависимостей, запуск серверов | — |
| `models/` | Структуры данных: пользователь, сессия | [`models/README.md`](models/README.md) |
| `interface/` | Доступ к данным: `Abstract` (контракты) → `InMemory` (сейчас) / `sqlite` (позже) | [`interface/README.md`](interface/README.md) |
| `services/` | Бизнес-логика: аккаунты (дальше — конференции, чат) | [`services/README.md`](services/README.md) |
| `core/` | Модель времени выполнения: сессии, комнаты, реестр комнат | [`core/README.md`](core/README.md) |
| `network/` | Входные точки: WebSocket-relay, HTTP (статика + JSON API) | [`network/README.md`](network/README.md) |
| `web/` | Веб-клиент | [`web/README.md`](web/README.md) |
| `build.ps1` / `run.ps1` | Сборка и запуск одной командой | — |
| `Dockerfile` | Multi-stage сборка образа (Qt6 из пакетов Debian) | — |
| `proxy/` | nginx-прокси: https/wss с самоподписанным сертификатом | [`proxy/README.md`](proxy/README.md) |
| `docker-compose.yml` | Запуск связки приложение + прокси | — |
| `tools/` | Деплой/обновление в Docker (`update.sh` / `update.ps1`) | [`tools/README.md`](tools/README.md) |

Слои зависят строго сверху вниз:

```
main.cpp  →  network/  →  services/  →  interface/Abstract  →  InMemory | sqlite
                │             │                │
                └──► core/    └──► models/ ◄───┘
```

## Требования

- Qt 6.11 (MinGW-сборка) + модуль **WebSockets**
- Бандл-инструменты Qt: MinGW 13.1, CMake, Ninja

Пути к Qt зашиты в `build.ps1` — поправь, если Qt лежит в другом месте.

## Сборка

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

Результат — самодостаточный `build\MeetUpServer.exe` (нужные Qt/MinGW DLL кладутся рядом).

## Запуск

```powershell
powershell -ExecutionPolicy Bypass -File run.ps1
```

Порты по умолчанию:

| Назначение | Порт | Переопределить |
|---|---|---|
| WebSocket-relay | **9000** | `--ws-port N` |
| HTTP (веб-клиент) | **80** | `--http-port N` |

> Порт 8080 из спеки на этой машине занят Steam (remote-debugging держит `127.0.0.1:8080`),
> поэтому WS по умолчанию на 9000.

## Развёртывание в Docker (https)

Камера в браузере работает только в secure context — `https://` или
`localhost`. Значит, с чужих устройств страница обязана открываться по
**https**, а WebSocket — по **wss** (страница https не имеет права открыть
`ws://`). Это делает nginx-прокси из [`proxy/`](proxy/README.md): терминирует
TLS и разводит трафик внутрь. Без домена сертификат самоподписанный —
браузер один раз предупредит: «Дополнительно → Перейти на сайт», это ожидаемо.
С доменом выпускается доверенный сертификат Let's Encrypt (см. ниже).

```
браузер ──► https://IP/    ──► proxy:443 ──► app:80    (веб-клиент)
        └─► wss://IP/ws    ──► proxy:443 ──► app:9000  (WebSocket-relay)
```

Запуск на сервере (нужен только `docker`, Qt в образе ставится из пакетов Debian):

```bash
docker compose up -d --build
```

Затем открой **https://<IP-сервера>/**. Порт 80 отдаёт редирект на https.
Переопределить порты хоста: `HTTPS_PORT=8443 HTTP_PORT=8081 docker compose up -d --build`.

### С доменом (доверенный сертификат Let's Encrypt)

Если домен указывает на сервер и порты 80/443 доступны из интернета, задай
`DOMAIN` — прокси сам выпустит и будет продлевать сертификат Let's Encrypt:

```bash
DOMAIN=meetup.linkpc.net LETSENCRYPT_EMAIL=you@mail.com docker compose up -d --build
```

Браузер больше не предупреждает. Без `DOMAIN` поведение прежнее (самоподписанный).
В режиме с доменом `HTTP_PORT` переопределять нельзя — Let's Encrypt проверяет
домен через внешний порт 80. Подробности — в [`proxy/README.md`](proxy/README.md).

Обновление с GitHub и пересборка — одной командой (см. [`tools/`](tools/README.md)):

```bash
./tools/update.sh                                                 # Linux / macOS / Git Bash
powershell -ExecutionPolicy Bypass -File tools\update.ps1         # Windows
```

## Проверка видео

1. Запусти сервер (локально — `run.ps1`, на сервере — `docker compose up -d --build`).
2. Открой страницу в двух вкладках или на двух устройствах:
   локально — **http://localhost**, с других устройств — **https://<IP-сервера>/**.
3. В обеих введи один код комнаты (напр. `TEST01`) и разные имена.
4. Разреши доступ к камере — участники увидят видео друг друга, список участников и общий чат.

# MeetUp — сервер

WebSocket-relay сервер конференций (Qt6, C++20) + примитивный веб-клиент для тестирования.
Сервер не декодирует медиа — только пересылает сжатые кадры участникам одной комнаты (SFU).

`C++20` · `Qt6 (Core, Network, WebSockets)` · `CMake + Ninja` · `MinGW`

## Что уже работает

- WebSocket-relay: `join` / `join_ok` / `participant_joined` / `participant_left`
- Текстовый чат с временными метками
- Бинарный релей видео-кадров (JPEG) внутри комнаты
- Раздача веб-клиента по HTTP (без Python — встроенный `HttpFileServer`)

## Структура проекта

Каждая папка документирована отдельно — здесь только карта и ссылки.

| Папка / файл | Что внутри | Подробности |
|---|---|---|
| `main.cpp` | Точка входа: разбор аргументов, запуск серверов | — |
| `core/` | Модель времени выполнения: сессии, комнаты, реестр комнат | [`core/README.md`](core/README.md) |
| `network/` | Входные точки: WebSocket-relay и HTTP-раздача статики | [`network/README.md`](network/README.md) |
| `services/` | Бизнес-логика (заготовка: регистрация, конференции, чат) | [`services/README.md`](services/README.md) |
| `storage/` | Слой хранения (заготовка под SQLite) | [`storage/README.md`](storage/README.md) |
| `web/` | Тестовый веб-клиент (одна страница) | [`web/README.md`](web/README.md) |
| `build.ps1` / `run.ps1` | Сборка и запуск одной командой | — |
| `Dockerfile` | Multi-stage сборка образа (Qt6 из пакетов Debian) | — |
| `proxy/` | nginx-прокси: https/wss с самоподписанным сертификатом | [`proxy/README.md`](proxy/README.md) |
| `docker-compose.yml` | Запуск связки приложение + прокси | — |
| `tools/` | Деплой/обновление в Docker (`update.sh` / `update.ps1`) | [`tools/README.md`](tools/README.md) |

Слои зависят строго сверху вниз:

```
main.cpp  →  network/  →  core/
                │
            services/  →  storage/   (будущая бизнес-логика и SQLite)
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
TLS и разводит трафик внутрь. Домена нет, поэтому сертификат самоподписанный —
браузер один раз предупредит: «Дополнительно → Перейти на сайт», это ожидаемо.

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

Обновление с GitHub и пересборка — одной командой (см. [`tools/`](tools/README.md)):

```bash
./tools/update.sh                                                 # Linux / macOS / Git Bash
powershell -ExecutionPolicy Bypass -File tools\update.ps1         # Windows
```

Появится домен — самоподписанный сертификат меняется на Let's Encrypt,
остальная схема не трогается (см. [`proxy/README.md`](proxy/README.md)).

## Проверка видео

1. Запусти сервер (локально — `run.ps1`, на сервере — `docker compose up -d --build`).
2. Открой страницу в двух вкладках или на двух устройствах:
   локально — **http://localhost**, с других устройств — **https://<IP-сервера>/**.
3. В обеих введи один код комнаты (напр. `TEST01`) и разные имена.
4. Разреши доступ к камере — участники увидят видео друг друга, список участников и общий чат.

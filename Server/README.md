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
| HTTP (веб-клиент) | **8081** | `--http-port N` |

> Порт 8080 из спеки на этой машине занят Steam (remote-debugging держит `127.0.0.1:8080`),
> поэтому WS по умолчанию на 9000.

## Проверка видео

1. Запусти сервер.
2. Открой **http://localhost:80** в двух вкладках (или в двух браузерах).
3. В обеих введи один код комнаты (напр. `TEST01`) и разные имена.
4. Разреши доступ к камере — участники увидят видео друг друга, список участников и общий чат.

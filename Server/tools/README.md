# tools/

**Скрипты, которыми запускают и разворачивают сервер.** Сам код сервера тут
не живёт — здесь только автоматизация: собрать локально, запустить локально,
обновить и пересобрать на боевом сервере. Ни один из этих файлов не участвует
в самой сборке C++ (её описывает `CMakeLists.txt` в корне) — это обёртки
поверх `cmake` и `docker compose`, чтобы не набирать длинные команды руками.

Четыре файла делятся на **две несвязанные группы** — не перепутай, они для
разных машин и разных задач:

| Группа | Файлы | Где запускают | Зачем |
|---|---|---|---|
| **Локальная разработка** | `build.ps1`, `run.ps1` | Windows-машина разработчика (нужен Qt6) | Собрать и погонять сервер у себя |
| **Боевой деплой** | `update.sh`, `update.ps1` | Сервер с Docker (домен `meetup.linkpc.net`) | Подтянуть код из GitHub и пересобрать в контейнерах |

> Почему пути в примерах вида `tools\build.ps1`: скрипты запускают **из корня
> репозитория**, а лежат они в `tools/`. Внутри каждый сам находит корень
> (`Split-Path -Parent $PSScriptRoot` / `dirname`), поэтому из какой папки ни
> зови — сработает; в примерах путь дан от корня для единообразия.

---

## Группа 1 — локальная разработка (Windows + Qt6)

Для машины, где стоит Qt 6.11 (MinGW). Собирают нативный `.exe` — **без
Docker**. Это цикл «поправил код → пересобрал → запустил → посмотрел».

### `build.ps1` — собрать сервер

Прогоняет полный цикл сборки: конфигурация CMake (генератор Ninja) → сборка →
`windeployqt` (кладёт нужные Qt/MinGW-DLL рядом с `.exe`, чтобы бинарь был
самодостаточным). Результат — `build\MeetUpServer.exe` в корне репозитория.

**Пути к Qt зашиты в шапке скрипта** (`$QtKit`, `$MinGW`, `$CMake`, `$Ninja`) —
если Qt лежит в другом месте, правь их там.

```powershell
# из корня репозитория
powershell -ExecutionPolicy Bypass -File tools\build.ps1
```

`-ExecutionPolicy Bypass` нужен, потому что по умолчанию Windows не даёт
запускать неподписанные `.ps1`; так политика обходится только для этого вызова,
насовсем ничего не меняется.

### `run.ps1` — запустить собранный сервер

Тонкая обёртка: находит `build\MeetUpServer.exe`, проверяет что он собран (иначе
подсказывает запустить `build.ps1`), запускает и **пробрасывает все аргументы**
в сам сервер.

```powershell
# запуск с портами по умолчанию (WS 9000, HTTP 80)
powershell -ExecutionPolicy Bypass -File tools\run.ps1

# переопределить порты — аргументы уходят прямо в exe
powershell -ExecutionPolicy Bypass -File tools\run.ps1 --ws-port 9500 --http-port 8081
```

Какие аргументы понимает сервер (`--ws-port`, `--http-port`, `--web-root`,
`--data-dir`) — см. `main.cpp` в корне. После запуска открывай
`http://localhost` в браузере.

> Локальный запуск идёт по **http** (без TLS) — камера в браузере работает в
> secure context, а `localhost` таким считается. Для доступа с других устройств
> нужен https — это уже Docker-деплой с прокси (группа 2).

---

## Группа 2 — деплой в Docker (боевой сервер)

Для машины, где крутится прод: нужен только `docker`, Qt ставится внутри
образа. Оба скрипта делают одно и то же (`git pull` → `docker compose up -d
--build`), просто под разные ОС: `.sh` — Linux/macOS/Git-Bash, `.ps1` —
Windows. Поднимают связку из [`docker-compose.yml`](../docker-compose.yml):
приложение + nginx-прокси с TLS (см. [`proxy/`](../proxy/README.md)).

**Логика обоих:**

1. `git pull --ff-only` — подтянуть свежие коммиты.
2. Если новых коммитов нет и не передан `--force` — выйти, ничего не пересобирая.
3. Иначе `docker compose up -d --build` — пересобрать изменившиеся образы и
   перезапустить только затронутые контейнеры.
4. Показать `docker compose ps` и итоговый адрес.

Порты и режим TLS передаются в `docker-compose.yml` через переменные окружения —
скрипт лишь выставляет их из флагов.

### Два режима TLS

Это главное, что выбирается при запуске:

| Режим | Когда | Как включить |
|---|---|---|
| **Самоподписанный** (по умолчанию) | Домена нет, вход по IP | ничего не передавать |
| **Let's Encrypt** | Есть домен (`meetup.linkpc.net`) | передать домен (+ email) |

В режиме Let's Encrypt порт HTTP (80) переопределять **нельзя**: ACME
HTTP-01-challenge всегда стучится на 80 — по нему центр сертификации проверяет,
что домен твой. Подробнее про выпуск и автопродление — в
[`proxy/README.md`](../proxy/README.md).

### `update.sh` — Linux / macOS / Git Bash

```bash
# обычное обновление: пересобрать, только если пришли новые коммиты
./tools/update.sh

# пересобрать и перезапустить принудительно (даже без новых коммитов)
./tools/update.sh --force

# боевой режим: Let's Encrypt для домена
./tools/update.sh --domain meetup.linkpc.net --email admin@example.com

# другие порты хоста (только в самоподписанном режиме)
HTTPS_PORT=8443 HTTP_PORT=8081 ./tools/update.sh

# справка по флагам
./tools/update.sh --help
```

Флаги: `--force`/`-f`, `--domain`, `--email`, `--https-port`, `--http-port`.
Те же значения можно задать переменными окружения (`DOMAIN`,
`LETSENCRYPT_EMAIL`, `HTTPS_PORT`, `HTTP_PORT`) — флаги имеют приоритет.

### `update.ps1` — Windows

Тот же скрипт для PowerShell; флаги — в стиле PowerShell (`-Force`, `-Domain`,
`-Email`, `-HttpsPort`, `-HttpPort`).

```powershell
# обычное обновление
powershell -ExecutionPolicy Bypass -File tools\update.ps1

# принудительная пересборка
powershell -ExecutionPolicy Bypass -File tools\update.ps1 -Force

# Let's Encrypt для домена
powershell -ExecutionPolicy Bypass -File tools\update.ps1 -Domain meetup.linkpc.net -Email admin@example.com

# другие порты хоста (самоподписанный режим)
powershell -ExecutionPolicy Bypass -File tools\update.ps1 -HttpsPort 8443 -HttpPort 8081
```

### Порты хоста (группа 2)

| Назначение | Порт (по умолчанию) | Примечание |
|---|---|---|
| https + wss | `443` | единственная рабочая точка входа; переопределяется `HTTPS_PORT` |
| http | `80` | редирект на https + ACME-challenge; переопределяется `HTTP_PORT` (нельзя с доменом) |

Внутренние порты приложения (80 — статика, 9000 — WebSocket) наружу не
публикуются — весь трафик идёт через nginx. Почему всё на одном порту —
см. [`proxy/README.md`](../proxy/README.md).

---

## Итог: что запускать

| Хочу… | Команда |
|---|---|
| Собрать локально (Windows) | `tools\build.ps1` |
| Запустить локально | `tools\run.ps1` |
| Обновить прод (по IP) | `./tools/update.sh` |
| Обновить прод (домен + Let's Encrypt) | `./tools/update.sh --domain meetup.linkpc.net --email …` |

Смотри также: [`../proxy/README.md`](../proxy/README.md) (TLS, nginx,
автопродление сертификата), [`../README.md`](../README.md) (сборка и
развёртывание в целом).

# tools/ — деплой и обновление

Скрипты для развёртывания сервера в Docker. Запускают связку из
[`docker-compose.yml`](../docker-compose.yml): приложение + nginx-прокси
с https (см. [`proxy/`](../proxy/README.md)).

| Файл | Назначение |
|---|---|
| `update.sh` | Linux/macOS/Git-Bash: `git pull` → `docker compose up -d --build` |
| `update.ps1` | То же для Windows (PowerShell) |

## Использование

```bash
# Linux / macOS / Git Bash
./tools/update.sh            # пересобрать, только если пришли новые коммиты
./tools/update.sh --force    # пересобрать и перезапустить принудительно
HTTPS_PORT=8443 HTTP_PORT=8081 ./tools/update.sh   # другие порты хоста
```

```powershell
# Windows
powershell -ExecutionPolicy Bypass -File tools\update.ps1
powershell -ExecutionPolicy Bypass -File tools\update.ps1 -Force
powershell -ExecutionPolicy Bypass -File tools\update.ps1 -HttpsPort 8443 -HttpPort 8081
```

## Порты хоста

| Назначение | Порт (по умолчанию) | Примечание |
|---|---|---|
| https + wss | `443` | единственная рабочая точка входа; переопределяется `HTTPS_PORT` |
| http | `80` | только редирект на https; переопределяется `HTTP_PORT` |

Внутренние порты приложения (80 — статика, 9000 — WebSocket) наружу не
публикуются — весь трафик идёт через nginx. Почему всё на одном порту —
см. [`proxy/README.md`](../proxy/README.md).

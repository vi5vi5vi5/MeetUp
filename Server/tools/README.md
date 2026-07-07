# tools/ — деплой и обновление

Скрипты для развёртывания сервера в Docker.

| Файл | Назначение |
|---|---|
| `update.sh` | Linux/macOS/Git-Bash: `git pull` → `docker build` → перезапуск контейнера |
| `update.ps1` | То же для Windows (PowerShell) |

## Использование

```bash
# Linux / macOS / Git Bash
./tools/update.sh            # пересобрать, только если пришли новые коммиты
./tools/update.sh --force    # пересобрать и перезапустить принудительно
HTTP_PORT=8081 ./tools/update.sh   # другой HTTP-порт хоста (по умолчанию 80)
```

```powershell
# Windows
powershell -ExecutionPolicy Bypass -File tools\update.ps1
powershell -ExecutionPolicy Bypass -File tools\update.ps1 -Force
powershell -ExecutionPolicy Bypass -File tools\update.ps1 -HttpPort 8081
```

## Порты

| Назначение | Контейнер | Хост (по умолчанию) | Примечание |
|---|---|---|---|
| HTTP (веб-клиент) | 80 | `80` | меняется флагом/`HTTP_PORT` |
| WebSocket-relay | 9000 | `9000` | **фиксирован** — зашит в `web/index.html` |

WS-порт менять нельзя: клиент подключается на `ws://<hostname>:9000`.

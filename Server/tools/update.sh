#!/usr/bin/env bash
# ============================================================
#  MeetUp — обновление с GitHub и пересборка Docker-контейнера
#  Использование:
#    ./update.sh          обновить (пересобрать только если есть изменения)
#    ./update.sh --force  пересобрать и перезапустить в любом случае
# ============================================================
set -euo pipefail

# --- Параметры ---
IMAGE="meetup-server"
CONTAINER="meetup-server"
# WS-порт жёстко зашит в web/index.html (ws://host:9000) — маппинг 9000:9000.
WS_PORT="9000"
# HTTP host-порт по умолчанию 80. Переопределить: HTTP_PORT=8081 ./update.sh
HTTP_PORT="${HTTP_PORT:-80}"

# Скрипт лежит в tools/; все операции идут из корня репозитория — на уровень выше.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

FORCE=0
if [[ "${1:-}" == "--force" ]]; then
    FORCE=1
fi

echo
echo "=== 1/4 Получение новой версии из GitHub ==="
OLD_REV="$(git rev-parse HEAD 2>/dev/null || echo none)"

git pull --ff-only

NEW_REV="$(git rev-parse HEAD 2>/dev/null || echo none)"

if [[ "$FORCE" -eq 0 && "$OLD_REV" == "$NEW_REV" ]]; then
    echo "Новых коммитов нет (HEAD = $NEW_REV)."
    echo "Пересборка не требуется. Запустите с --force, чтобы пересобрать принудительно."
    exit 0
fi

echo
echo "=== 2/4 Сборка Docker-образа \"$IMAGE\" ==="
docker build -t "$IMAGE" .

echo
echo "=== 3/4 Перезапуск контейнера \"$CONTAINER\" ==="

# Снести старый контейнер, если он есть (ошибку игнорируем — его может не быть)
docker rm -f "$CONTAINER" >/dev/null 2>&1 || true

docker run -d \
  --name "$CONTAINER" \
  --restart unless-stopped \
  -p "$HTTP_PORT:80" \
  -p "$WS_PORT:9000" \
  "$IMAGE"

echo
echo "=== 4/4 Проверка ==="
docker ps --filter "name=$CONTAINER"
echo
echo "Готово. HEAD = $NEW_REV"
echo "Веб-клиент:  http://localhost:$HTTP_PORT"
echo "WebSocket:   ws://localhost:$WS_PORT"

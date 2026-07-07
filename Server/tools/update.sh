#!/usr/bin/env bash
# ============================================================
#  MeetUp — обновление с GitHub и пересборка (docker compose)
#  Использование:
#    ./update.sh          обновить (пересобрать только если есть изменения)
#    ./update.sh --force  пересобрать и перезапустить в любом случае
#  Переопределить порты хоста:
#    HTTPS_PORT=8443 HTTP_PORT=8081 ./update.sh
# ============================================================
set -euo pipefail

# Скрипт лежит в tools/; все операции идут из корня репозитория — на уровень выше.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

FORCE=0
if [[ "${1:-}" == "--force" ]]; then
    FORCE=1
fi

echo
echo "=== 1/3 Получение новой версии из GitHub ==="
OLD_REV="$(git rev-parse HEAD 2>/dev/null || echo none)"

git pull --ff-only

NEW_REV="$(git rev-parse HEAD 2>/dev/null || echo none)"

if [[ "$FORCE" -eq 0 && "$OLD_REV" == "$NEW_REV" ]]; then
    echo "Новых коммитов нет (HEAD = $NEW_REV)."
    echo "Пересборка не требуется. Запустите с --force, чтобы пересобрать принудительно."
    exit 0
fi

echo
echo "=== 2/3 Пересборка и перезапуск (docker compose) ==="
# up -d --build сам пересоберёт изменившиеся образы и перезапустит
# только те контейнеры, которые поменялись.
docker compose up -d --build

echo
echo "=== 3/3 Проверка ==="
docker compose ps
echo
echo "Готово. HEAD = $NEW_REV"
echo "Веб-клиент: https://<IP-сервера>${HTTPS_PORT:+:$HTTPS_PORT}/"
echo "Сертификат самоподписанный — браузер предупредит; это ожидаемо."

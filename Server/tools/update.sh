#!/usr/bin/env bash
# ============================================================
#  MeetUp — обновление с GitHub и пересборка (docker compose)
#  Использование:
#    ./update.sh                     обновить (пересобрать только если есть изменения)
#    ./update.sh --force             пересобрать и перезапустить в любом случае
#  Выбор режима TLS:
#    ./update.sh                              самоподписанный сертификат (по умолчанию)
#    ./update.sh --domain meetup.linkpc.net --email you@mail.com   Let's Encrypt
#  Переопределить порты хоста (с доменом HTTP_PORT переопределять нельзя):
#    ./update.sh --https-port 8443 --http-port 8081
#  Переменные окружения HTTPS_PORT/HTTP_PORT/DOMAIN/LETSENCRYPT_EMAIL тоже работают.
# ============================================================
set -euo pipefail

# Скрипт лежит в tools/; все операции идут из корня репозитория — на уровень выше.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Флаги переопределяют одноимённые переменные окружения. docker compose читает
# окружение процесса, поэтому экспортируем DOMAIN/EMAIL/порты — так --domain
# включает режим Let's Encrypt (см. docker-compose.yml).
FORCE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --force|-f)          FORCE=1; shift ;;
        --domain)            export DOMAIN="$2"; shift 2 ;;
        --email)             export LETSENCRYPT_EMAIL="$2"; shift 2 ;;
        --http-port)         export HTTP_PORT="$2"; shift 2 ;;
        --https-port)        export HTTPS_PORT="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^#//'
            exit 0 ;;
        *)
            echo "Неизвестный аргумент: $1" >&2
            echo "Запустите ./update.sh --help" >&2
            exit 1 ;;
    esac
done

if [[ -n "${DOMAIN:-}" ]]; then
    echo "Режим TLS: Let's Encrypt для домена ${DOMAIN}"
    if [[ -z "${LETSENCRYPT_EMAIL:-}" ]]; then
        echo "  (email не задан — сертификат выпустится, но без уведомлений об истечении;"
        echo "   рекомендуется --email you@mail.com)"
    fi
else
    echo "Режим TLS: самоподписанный сертификат (домен не задан)."
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
if [[ -n "${DOMAIN:-}" ]]; then
    echo "Веб-клиент: https://${DOMAIN}/"
    echo "Сертификат Let's Encrypt (домен ${DOMAIN}); автопродление в контейнере proxy."
else
    echo "Веб-клиент: https://<IP-сервера>${HTTPS_PORT:+:$HTTPS_PORT}/"
    echo "Сертификат самоподписанный — браузер предупредит; это ожидаемо."
fi

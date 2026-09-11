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
#
#  Домен, почта и порты ЗАПОМИНАЮТСЯ в файле .env рядом с docker-compose.yml:
#  указали --domain один раз — дальше хватает `./update.sh` или даже
#  `docker compose up -d` без единого флага.
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

# ---- .env: настройки установки, которые незачем вводить каждый раз ----
# docker compose читает этот файл сам, поэтому запомненного домена хватает и
# для «голого» `docker compose up -d`. Окружение процесса сильнее файла — то
# есть флаг текущего запуска всегда побеждает запомненное.
ENV_FILE="$REPO_ROOT/.env"

env_file_get() {
    [[ -f "$ENV_FILE" ]] || return 0
    sed -n "s/^$1=//p" "$ENV_FILE" | tail -n 1
}

# Переписываем файл целиком, а не правим строку на месте: в домене и почте
# попадаются символы, которые пришлось бы экранировать для sed, и однажды
# кто-нибудь на этом обожжётся.
env_file_set() {
    local key="$1" value="$2"
    touch "$ENV_FILE"
    local tmp="${ENV_FILE}.tmp"
    grep -v "^${key}=" "$ENV_FILE" > "$tmp" || true
    printf '%s=%s\n' "$key" "$value" >> "$tmp"
    mv "$tmp" "$ENV_FILE"
}

# Флаг этого запуска — запоминаем. Флага нет — поднимаем запомненное.
for var in DOMAIN LETSENCRYPT_EMAIL HTTP_PORT HTTPS_PORT; do
    if [[ -n "${!var:-}" ]]; then
        env_file_set "$var" "${!var}"
    else
        remembered="$(env_file_get "$var")"
        if [[ -n "$remembered" ]]; then
            export "$var=$remembered"
        fi
    fi
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

# Из какого коммита собираем: сервер отдаёт это в GET /api/config. Внутри
# образа гита нет и не будет (см. Dockerfile), поэтому считаем здесь и
# передаём аргументами сборки через docker-compose.yml.
#
# ПОСЛЕ git pull, а не до: считали до — и в собранный бинарь попадал номер
# коммита, из которого мы уходим. Сервер потом честно докладывал в /api/config
# версию, которой в нём уже нет, и лечилось это только вторым запуском с
# --force, когда HEAD успевал догнать.
#
# Смотрим только на Server/ (мы в ней и стоим): правки в клиенте не делают
# сборку сервера «изменённой». Untracked-файлы не считаем — заметка, забытая
# рядом с исходниками, в бинарь не попадает, а CMakeLists, куда её пришлось бы
# вписать, отслеживается, и такое изменение мы увидим.
GIT_COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
if [[ -n "$(git status --porcelain -uno -- . 2>/dev/null)" ]]; then
    GIT_MODIFIED=1
else
    GIT_MODIFIED=0
fi
export GIT_COMMIT GIT_MODIFIED

echo
echo "=== 2/3 Пересборка и перезапуск (docker compose) ==="
# up -d --build сам пересоберёт изменившиеся образы и перезапустит
# только те контейнеры, которые поменялись.
docker compose up -d --build

echo
echo "=== 3/3 Проверка ==="
docker compose ps
echo
echo "Готово. HEAD = $NEW_REV (сборка $GIT_COMMIT$( [[ "$GIT_MODIFIED" == "1" ]] && echo ', с локальными изменениями' ))"
if [[ -n "${DOMAIN:-}" ]]; then
    echo "Домен запомнен в .env — дальше можно просто ./update.sh"
fi
if [[ -n "${DOMAIN:-}" ]]; then
    echo "Веб-клиент: https://${DOMAIN}/"
    echo "Сертификат Let's Encrypt (домен ${DOMAIN}); автопродление в контейнере proxy."
else
    echo "Веб-клиент: https://<IP-сервера>${HTTPS_PORT:+:$HTTPS_PORT}/"
    echo "Сертификат самоподписанный — браузер предупредит; это ожидаемо."
fi

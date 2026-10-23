#!/usr/bin/env bash
# ============================================================
#  MeetUp — дымовая проверка HTTP API.
#
#  Поднимает собранный сервер на свободных портах с пустой базой и
#  проходит главный сценарий аккаунта: конфиг, регистрация, сессия по
#  куке, личная комната, выход. Ничего не мокается — это тот же бинарь,
#  что уезжает в docker, и та же SQLite.
#
#  Запуск (после сборки сервера):
#      cmake -S Server -B Server/build -DCMAKE_BUILD_TYPE=Release
#      cmake --build Server/build
#      ./Server/tools/smoke.sh
#
#  Путь к бинарю можно задать снаружи: SERVER=... ./Server/tools/smoke.sh
#  Нужны: curl, jq.
# ============================================================
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVER="${SERVER:-$ROOT/build/MeetUpServer}"
HTTP_PORT="${HTTP_PORT:-18080}"
WS_PORT="${WS_PORT:-19000}"
BASE="http://127.0.0.1:$HTTP_PORT"

[ -x "$SERVER" ] || { echo "не найден: $SERVER — соберите сервер или укажите SERVER=" >&2; exit 1; }

WORK="$(mktemp -d)"
JAR="$WORK/cookies"
PASS=0
FAIL=0
ok()  { echo "  [ok]   $1"; PASS=$((PASS + 1)); }
bad() { echo "  [ПРОВАЛ] $1"; FAIL=$((FAIL + 1)); }

"$SERVER" --http-port "$HTTP_PORT" --ws-port "$WS_PORT" \
          --web-root "$ROOT/web" --data-dir "$WORK/data" > "$WORK/server.log" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null; rm -rf "$WORK"' EXIT

up=0
for _ in $(seq 50); do
    curl -s -o /dev/null "$BASE/api/config" && { up=1; break; }
    sleep 0.1
done
if [ "$up" -ne 1 ]; then
    echo "сервер не ответил за 5 секунд" >&2
    tail -40 "$WORK/server.log" >&2
    exit 1
fi

# req МЕТОД ПУТЬ [ТЕЛО] — код ответа кладёт в $CODE, тело в $body.
req() {
    local args=(-s -b "$JAR" -c "$JAR" -o "$WORK/body" -w '%{http_code}' -X "$1" "$BASE$2")
    [ $# -ge 3 ] && args+=(-H 'Content-Type: application/json' --data "$3")
    CODE="$(curl "${args[@]}")"
    body="$(cat "$WORK/body" 2>/dev/null)"
    rm -f "$WORK/body"
}

echo "=== Сервер поднялся ==="
req GET /api/config
[ "$CODE" = 200 ] && echo "$body" | jq -e . >/dev/null \
    && ok "GET /api/config отвечает JSON" || bad "GET /api/config: $CODE $body"
req GET /
[ "$CODE" = 200 ] || [ "${CODE:0:1}" = 3 ] \
    && ok "веб-клиент раздаётся ($CODE)" || bad "GET /: $CODE"

echo "=== Аккаунт ==="
req GET /api/me
[ "$CODE" = 401 ] && ok "без сессии /api/me — 401" || bad "без сессии /api/me: $CODE"

req POST /api/auth/register '{"login":"smoke","password":"correct horse battery","display_name":"Смоук"}'
[ "$CODE" = 200 ] && [ "$(echo "$body" | jq -r .user.login)" = smoke ] \
    && ok "регистрация" || bad "регистрация: $CODE $body"

req POST /api/auth/register '{"login":"smoke","password":"correct horse battery","display_name":"Дубль"}'
[ "$CODE" = 409 ] && ok "повторный логин — 409" || bad "повторный логин: $CODE $body"

req GET /api/me
[ "$CODE" = 200 ] && [ "$(echo "$body" | jq -r .user.display_name)" = "Смоук" ] \
    && ok "сессия по куке, кириллица в имени цела" || bad "/api/me после регистрации: $CODE $body"

echo "=== Личные комнаты ==="
req GET /api/me/rooms
[ "$CODE" = 200 ] && echo "$body" | jq -e '.rooms | type == "array"' >/dev/null \
    && ok "список комнат" || bad "список комнат: $CODE $body"

echo "=== Выход ==="
req POST /api/auth/logout
[ "$CODE" = 200 ] && ok "выход" || bad "выход: $CODE"
req GET /api/me
[ "$CODE" = 401 ] && ok "после выхода сессии нет" || bad "после выхода /api/me: $CODE"

req POST /api/auth/login '{"login":"smoke","password":"неправильный"}'
[ "$CODE" = 401 ] && ok "чужой пароль — 401" || bad "чужой пароль: $CODE $body"

req POST /api/auth/login '{"login":"smoke","password":"correct horse battery"}'
[ "$CODE" = 200 ] && ok "вход снова" || bad "вход: $CODE $body"

echo
echo "прошло: $PASS, провалено: $FAIL"
if [ "$FAIL" -ne 0 ]; then
    echo "--- журнал сервера ---"
    tail -40 "$WORK/server.log"
    exit 1
fi

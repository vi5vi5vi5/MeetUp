# syntax=docker/dockerfile:1.6
# ============================================================
#  MeetUp — сервер конференций (Qt6, C++20)
#  Multi-stage: собираем в полном Qt6-окружении, кладём в slim-рантайм.
#  Qt6 в Linux берём из пакетов Debian — из исходников собирать не нужно
#  (это было ограничение Windows/MinGW, см. README).
# ============================================================

# ---------- 1. Стадия сборки ----------
FROM debian:bookworm AS build

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        qt6-base-dev \
        qt6-websockets-dev \
        nodejs \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Конфигурируем напрямую (без CMakePresets — там зашиты Windows-пути к Qt).
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build

# Веб-клиент: JSX компилируется ЗДЕСЬ, один раз, а не в браузере у каждого
# посетителя. Раньше страница везла с собой @babel/standalone (3,1 МБ) и
# dev-сборки React (1,2 МБ) и компилировала себя при каждом заходе — на
# телефоне это была самая тяжёлая часть загрузки. Сборщик берёт тот же самый
# assets/babel.js, который лежит в репозитории, поэтому ни npm install, ни
# сеть на этом шаге не нужны: у того, кто поднимает свой сервер, из
# инструментов есть только docker, и так должно остаться.
RUN node web/tools/build.js

# ---------- 2. Стадия рантайма ----------
FROM debian:bookworm-slim AS runtime

# Только рантайм-библиотеки Qt6. libqt6websockets6 тянет за собой
# Network и Core. Сервер headless (QCoreApplication) — GUI-плагины не нужны.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        libqt6websockets6 \
        libqt6network6 \
        libqt6core6 \
        ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /src/build/MeetUpServer /app/MeetUpServer
# Именно dist, а не исходники: в собранном виде нет ни babel.js, ни
# dev-React, страницы ссылаются на скомпилированные assets/pages/*.js, а к
# каждому ассету приписан ?v=<хэш содержимого> — забыть поднять версию и
# оставить людей с суточным кэшем старого кода больше нельзя.
COPY --from=build /src/web/dist /app/web

# HTTP-раздача веб-клиента и WebSocket-relay. Это внутренние порты:
# наружу их публикует docker-compose через nginx-прокси (https/wss),
# см. proxy/ и docker-compose.yml.
EXPOSE 80 9000

# --web-root обязателен: вшитый в бинарь WEB_ROOT_DEFAULT указывает на путь
# исходников (/src/web), которого в рантайм-образе нет.
# --data-dir указывает в точку монтирования ./mount (см. docker-compose.yml) —
# без тома данные остались бы внутри контейнера и умерли при пересборке.
CMD ["/app/MeetUpServer", "--http-port", "80", "--ws-port", "9000", \
     "--web-root", "/app/web", "--data-dir", "/app/mount"]

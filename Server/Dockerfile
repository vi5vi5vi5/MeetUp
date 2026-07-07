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
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Конфигурируем напрямую (без CMakePresets — там зашиты Windows-пути к Qt).
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build

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
COPY web /app/web

# HTTP-раздача веб-клиента и WebSocket-relay.
# WS-порт 9000 жёстко зашит в web/index.html — менять маппинг нельзя.
EXPOSE 80 9000

# --web-root обязателен: вшитый в бинарь WEB_ROOT_DEFAULT указывает на путь
# исходников (/src/web), которого в рантайм-образе нет.
CMD ["/app/MeetUpServer", "--http-port", "80", "--ws-port", "9000", "--web-root", "/app/web"]

#!/bin/sh
# Самоподписанный TLS-сертификат для доступа по IP (домена нет).
# Официальный образ nginx сам выполняет скрипты из /docker-entrypoint.d/
# перед стартом. Сертификат кладётся в том certs и переживает перезапуски —
# браузерам не приходится принимать исключение заново.
set -e

CERT=/etc/nginx/certs/server.crt
KEY=/etc/nginx/certs/server.key

if [ ! -f "$CERT" ] || [ ! -f "$KEY" ]; then
    echo "Generating self-signed TLS certificate (valid 10 years)..."
    mkdir -p /etc/nginx/certs
    openssl req -x509 -nodes -newkey rsa:2048 -days 3650 \
        -subj "/CN=MeetUp" \
        -keyout "$KEY" -out "$CERT"
fi

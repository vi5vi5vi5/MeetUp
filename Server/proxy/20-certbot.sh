#!/bin/sh
# Let's Encrypt через HTTP-01 (webroot). Работает только если задан DOMAIN;
# без домена сразу выходит и остаётся самоподписанный сертификат.
#
# Официальный entrypoint выполняет скрипты из /docker-entrypoint.d/
# синхронно и ДО старта nginx, поэтому запрос делаем в фоне: дожидаемся,
# пока nginx займёт :80 и начнёт отдавать /.well-known/acme-challenge/,
# затем запрашиваем сертификат и переставляем live/ на Let's Encrypt.
set -e

[ -z "$DOMAIN" ] && exit 0

EXTRA_DOMAINS="${EXTRA_DOMAINS:-}"

CERTS=/etc/nginx/certs
LIVE="$CERTS/live"
LE_LIVE="/etc/letsencrypt/live/$DOMAIN"
WEBROOT=/var/www/certbot

# Имена, которые идут в сертификат: основной домен плюс соседи с этого же
# сервера (EXTRA_DOMAINS, через пробел или запятую). Сертификат получается
# один на всех — так проще и не упирается в лимит «дубликатов».
DOMAIN_ARGS="-d $DOMAIN"
if [ -n "$EXTRA_DOMAINS" ]; then
    for extra in $(echo "$EXTRA_DOMAINS" | tr ',' ' '); do
        [ -n "$extra" ] && DOMAIN_ARGS="$DOMAIN_ARGS -d $extra"
    done
fi

# Сертификат уже лежит — сразу направляем на него live/, чтобы nginx
# стартовал на настоящем, не дожидаясь ответа certbot.
if [ -d "$LE_LIVE" ]; then
    ln -sf "$LE_LIVE/fullchain.pem" "$LIVE/fullchain.pem"
    ln -sf "$LE_LIVE/privkey.pem"   "$LIVE/privkey.pem"
    echo "Сертификат Let's Encrypt для $DOMAIN уже есть — live/ на него."
fi

# Запрос делаем в любом случае, но с --keep-until-expiring: если
# сертификат свежий и уже покрывает все имена, certbot не пойдёт в сеть
# и ничего не перевыпустит.
#
# Раньше здесь стояло «есть каталог — не трогаем», и это был скрытый
# капкан: добавленный EXTRA_DOMAINS не попадал в сертификат никогда.
# Сосед получал ошибку имени, а причину было не видно — сертификат-то
# есть, и он валидный.
if true; then
    (
        # Ждём, пока nginx поднимется и начнёт слушать :80 — иначе HTTP-01
        # challenge не пройдёт. Пробуем до ~30 секунд.
        i=0
        while [ "$i" -lt 30 ]; do
            if wget -q -O /dev/null "http://127.0.0.1/.well-known/acme-challenge/" 2>/dev/null \
               || nc -z 127.0.0.1 80 2>/dev/null; then
                break
            fi
            i=$((i + 1))
            sleep 1
        done

        EMAIL_ARG="--register-unsafely-without-email"
        [ -n "$LETSENCRYPT_EMAIL" ] && EMAIL_ARG="--email $LETSENCRYPT_EMAIL"

        echo "Запрос сертификата Let's Encrypt: $DOMAIN${EXTRA_DOMAINS:+ + $EXTRA_DOMAINS} (HTTP-01, webroot)..."
        # || остаёмся на самоподписанном: контейнер работает в любом случае.
        # --expand: имя могли добавить к уже существующему сертификату.
        # --cert-name: путь live/ не должен зависеть от того, сколько
        #   имён в сертификате, иначе он уезжал бы при каждом соседе.
        if certbot certonly --webroot -w "$WEBROOT" \
            --non-interactive --agree-tos $EMAIL_ARG \
            $DOMAIN_ARGS \
            --cert-name "$DOMAIN" \
            --expand --keep-until-expiring; then

            ln -sf "$LE_LIVE/fullchain.pem" "$LIVE/fullchain.pem"
            ln -sf "$LE_LIVE/privkey.pem"   "$LIVE/privkey.pem"
            nginx -s reload
            echo "live/ → Let's Encrypt ($DOMAIN); nginx перечитал сертификат."
        else
            echo "Не удалось получить LE-сертификат — остаёмся на самоподписанном."
        fi
    ) &
fi

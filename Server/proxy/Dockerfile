# nginx-терминатор TLS для MeetUp: снаружи https/wss, внутри обычные http/ws.
FROM nginx:alpine

# openssl нужен скрипту генерации самоподписанного сертификата.
RUN apk add --no-cache openssl

COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY 10-gen-cert.sh /docker-entrypoint.d/10-gen-cert.sh
RUN chmod +x /docker-entrypoint.d/10-gen-cert.sh

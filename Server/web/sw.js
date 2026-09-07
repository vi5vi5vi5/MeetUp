/* MeetUp — service worker.
 *
 * Делает ровно одно: кэширует содержимое /assets/. Больше ничего не
 * перехватывает — и это осознанно. Сломанный service worker умеет запереть
 * людей на старой версии сайта так, что обычным обновлением страницы это не
 * чинится; чем меньше он на себя берёт, тем меньше ему ломать.
 *
 * Почему /assets/ безопасно кэшировать навсегда: сборщик (tools/build.js)
 * приписывает к каждой ссылке ?v=<хэш содержимого>. Изменился файл —
 * изменился адрес, и старая запись в кэше просто перестаёт запрашиваться.
 * Поэтому здесь «сначала кэш» без проверок и без срока годности.
 *
 * Страницы (HTML) и всё под /api/ и /ws НЕ трогаем вовсе: они всегда свежие
 * из сети. Комната, чат и медиа не должны зависеть от того, что подумал
 * кэш.
 */
"use strict";

const CACHE = "meetup-assets-v1";

self.addEventListener("install", (e) => {
  // Не ждём, пока закроются старые вкладки: обновление должно доезжать сразу.
  self.skipWaiting();
});

self.addEventListener("activate", (e) => {
  e.waitUntil((async () => {
    const names = await caches.keys();
    await Promise.all(names.filter(n => n !== CACHE).map(n => caches.delete(n)));
    await self.clients.claim();
  })());
});

self.addEventListener("fetch", (e) => {
  const req = e.request;
  if (req.method !== "GET") return;

  let url;
  try { url = new URL(req.url); } catch (err) { return; }
  if (url.origin !== self.location.origin) return;
  if (!url.pathname.startsWith("/assets/")) return;

  e.respondWith((async () => {
    const cache = await caches.open(CACHE);
    const hit = await cache.match(req);
    if (hit) return hit;
    const resp = await fetch(req);
    // Кладём только удачные ответы: закэшировать 404 или сбой прокси значит
    // сделать поломку постоянной.
    if (resp && resp.ok && resp.status === 200) cache.put(req, resp.clone());
    return resp;
  })());
});

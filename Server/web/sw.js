/* MeetUp — service worker.
 *
 * Делает ровно одно: кэширует версионированные файлы из /assets/. Больше
 * ничего не перехватывает — и это осознанно. Сломанный service worker умеет
 * запереть людей на старой версии сайта так, что обычным обновлением
 * страницы это не чинится; чем меньше он на себя берёт, тем меньше ему
 * ломать.
 *
 * «Навсегда» кэшируем ТОЛЬКО адреса с ?v=: сборщик (tools/build.js)
 * приписывает к ссылке ?v=<хэш содержимого>, звуки несут версию в коде.
 * Изменился файл — изменился адрес, и старая запись перестаёт
 * запрашиваться. Раньше вечными становились и файлы без версии (знак
 * meetup-mark-*.svg, шрифты): поменяй логотип — и у всех, кто поставил
 * сайт на телефон, навсегда остался бы старый. Такие файлы теперь идут
 * мимо нас, с обычным HTTP-кэшем сервера (сутки).
 *
 * При записи новой версии файла прежние версии того же пути удаляются:
 * иначе кэш рос бы с каждым обновлением сервера и не чистился никогда.
 *
 * Страницы (HTML) и всё под /api/ и /ws НЕ трогаем вовсе: они всегда свежие
 * из сети. Комната, чат и медиа не должны зависеть от того, что подумал
 * кэш.
 */
"use strict";

// v2: в v1 лежали и файлы без версии — при активации он удаляется целиком.
const CACHE = "meetup-assets-v2";

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
  if (!url.searchParams.has("v")) return;   // без версии — обычная сеть

  e.respondWith((async () => {
    const cache = await caches.open(CACHE);
    const hit = await cache.match(req);
    if (hit) return hit;
    const resp = await fetch(req);
    // Кладём только удачные ответы: закэшировать 404 или сбой прокси значит
    // сделать поломку постоянной.
    if (resp && resp.ok && resp.status === 200) {
      const copy = resp.clone();
      e.waitUntil((async () => {
        for (const old of await cache.keys()) {
          const u = new URL(old.url);
          if (u.pathname === url.pathname && u.search !== url.search) await cache.delete(old);
        }
        await cache.put(req, copy);
      })());
    }
    return resp;
  })());
});

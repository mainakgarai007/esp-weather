const CACHE_NAME = 'skycore-cache-v1';
const ASSETS = [
  './',
  './index.html',
  './dashboard.html',
  './status.html',
  './pairing.html',
  './pixel-legend.html',
  './led-legend.html',
  './firmware.html',
  './settings.html',
  './update.html',
  './logs.html',
  './notifications.html',
  './buttons.html',
  './help.html',
  './css/style.css',
  './js/storage.js',
  './js/pairing.js',
  './js/notifications.js',
  './js/pixel-legend.js',
  './js/onboarding.js',
  './js/firmware-viewer.js',
  './js/app.js',
  './js/pwa.js',
  './manifest.json',
  './firmware/SkyCore.ino',
];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => cache.addAll(ASSETS)).then(() => self.skipWaiting())
  );
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(keys.map((key) => (key === CACHE_NAME ? null : caches.delete(key))))
    ).then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', (event) => {
  if (event.request.method !== 'GET') return;
  event.respondWith(
    caches.match(event.request).then((cached) =>
      cached || fetch(event.request).then((response) => {
        const copy = response.clone();
        caches.open(CACHE_NAME).then((cache) => cache.put(event.request, copy));
        return response;
      }).catch(() => caches.match('./index.html'))
    )
  );
});

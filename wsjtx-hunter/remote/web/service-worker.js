const CACHE='ft8hunter-remote-2.2.1';
const ASSETS=['./','index.html','styles.css','app.js','manifest.json','icon.png'];
self.addEventListener('install',e=>e.waitUntil(caches.open(CACHE).then(c=>c.addAll(ASSETS))));
self.addEventListener('activate',e=>e.waitUntil(caches.keys().then(k=>Promise.all(k.filter(x=>x!==CACHE).map(x=>caches.delete(x))))));
self.addEventListener('fetch',e=>{if(e.request.url.includes('remote-config.json'))return;e.respondWith(fetch(e.request).catch(()=>caches.match(e.request)))});

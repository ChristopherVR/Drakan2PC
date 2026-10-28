// Serves play/ from the player's own DrakanPC-web build, disc image and optional HD texture folder (handles chosen
// on the loader page), with the cross-origin isolation headers the threaded WebAssembly build needs. Nothing under
// play/ is fetched from the network: this site only hosts the loader.
importScripts('handles.js');

const PLAY = new URL('play/', self.registration.scope).pathname;
const TYPES = {
  html: 'text/html; charset=utf-8', js: 'text/javascript', mjs: 'text/javascript', css: 'text/css',
  wasm: 'application/wasm', json: 'application/json', srt: 'text/plain; charset=utf-8',
  txt: 'text/plain; charset=utf-8', png: 'image/png', dds: 'application/octet-stream',
};
const TEXTURE = /^[0-9a-f]{16}\.(?:png|dds)$/i;
const MAX_RANGE = 1024 * 1024;
let cached = null;

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));
self.addEventListener('message', event => { if (event.data === 'refresh') cached = null; });

function headers(extra) {
  return new Headers({
    'Cross-Origin-Opener-Policy': 'same-origin',
    'Cross-Origin-Embedder-Policy': 'require-corp',
    'Cross-Origin-Resource-Policy': 'same-origin',
    'Cache-Control': 'no-store',
    ...extra,
  });
}
function failure(status, text) {
  return new Response(text, {status, headers: headers({'Content-Type': 'text/plain; charset=utf-8'})});
}
async function handles() {
  if (!cached) {
    cached = {
      build: await drakanHandles.get('build'),
      iso: await drakanHandles.get('iso'),
      textures: await drakanHandles.get('textures'),
    };
  }
  return cached;
}
async function fileAt(directory, path) {
  const parts = path.split('/').filter(Boolean);
  if (!directory || !parts.length || parts.some(part => part === '..' || part === '.')) return null;
  try {
    let folder = directory;
    for (const part of parts.slice(0, -1)) folder = await folder.getDirectoryHandle(part);
    return await (await folder.getFileHandle(parts[parts.length - 1])).getFile();
  } catch (_) {
    return null;
  }
}
function typeOf(name) {
  return TYPES[name.slice(name.lastIndexOf('.') + 1).toLowerCase()] || 'application/octet-stream';
}

async function disc(request, iso) {
  if (!iso) return failure(404, 'No disc image chosen');
  const file = await iso.getFile();
  if (request.method === 'HEAD')
    return new Response(null, {headers: headers({'Content-Length': String(file.size), 'Accept-Ranges': 'bytes'})});
  const match = /^bytes=(\d+)-(\d*)$/.exec(request.headers.get('Range') || '');
  const start = match ? Number(match[1]) : NaN;
  const end = match ? Math.min(match[2] ? Number(match[2]) : start + MAX_RANGE - 1, file.size - 1) : NaN;
  if (!match || start >= file.size || end < start || end - start + 1 > MAX_RANGE)
    return new Response(null, {status: 416, headers: headers({'Content-Range': `bytes */${file.size}`})});
  return new Response(file.slice(start, end + 1), {
    status: 206,
    headers: headers({
      'Content-Type': 'application/octet-stream',
      'Accept-Ranges': 'bytes',
      'Content-Range': `bytes ${start}-${end}/${file.size}`,
      'Content-Length': String(end - start + 1),
    }),
  });
}

async function texturePack(textures) {
  const index = {files: [], metadata: {}};
  if (textures) {
    for await (const [name, entry] of textures.entries()) {
      if (entry.kind !== 'file') continue;
      if (TEXTURE.test(name)) index.files.push(name);
      else if ((name === 'alias.txt' || name === 'exclude.txt')) {
        const file = await entry.getFile();
        if (file.size <= 8 * 1024 * 1024) index.metadata[name] = await file.text();
      }
    }
    index.files.sort();
  }
  return new Response(JSON.stringify(index), {headers: headers({'Content-Type': 'application/json'})});
}

async function serve(request) {
  const path = decodeURIComponent(new URL(request.url).pathname.slice(PLAY.length));
  let chosen;
  try { chosen = await handles(); } catch (error) { return failure(500, String(error)); }
  if (!chosen.build) return Response.redirect(new URL('../', new URL(PLAY, self.registration.scope)).href, 302);
  try {
    if (path === 'disc.iso') return await disc(request, chosen.iso);
    if (path === 'texture-pack.json') return await texturePack(chosen.textures);
    if (path.startsWith('textures/')) {
      const name = path.slice('textures/'.length);
      const file = TEXTURE.test(name) ? await fileAt(chosen.textures, name) : null;
      return file ? new Response(file, {headers: headers({'Content-Type': typeOf(name)})}) : failure(404, 'Not found');
    }
    const name = path === '' ? 'index.html' : path;
    const file = await fileAt(chosen.build, name);
    if (!file) return failure(404, 'Not found in the chosen browser build: ' + name);
    return new Response(request.method === 'HEAD' ? null : file, {
      headers: headers({'Content-Type': typeOf(name), 'Content-Length': String(file.size)}),
    });
  } catch (error) {
    // Typically the browser revoked folder access: send the player back to the loader to grant it again.
    if (error?.name === 'NotAllowedError' || error?.name === 'SecurityError')
      return Response.redirect(new URL('../', new URL(PLAY, self.registration.scope)).href, 302);
    return failure(500, String(error));
  }
}

self.addEventListener('fetch', event => {
  const url = new URL(event.request.url);
  if (url.origin !== self.location.origin || !url.pathname.startsWith(PLAY)) return;
  if (event.request.method !== 'GET' && event.request.method !== 'HEAD') return;
  event.respondWith(serve(event.request));
});

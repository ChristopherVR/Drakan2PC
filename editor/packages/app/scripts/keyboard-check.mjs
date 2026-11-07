// Keyboard and screen-reader semantics check of the built editor, in a real Chrome or Edge (headless), with real key events.
//
//   npm run build -w @drakan/editor-app
//   node packages/app/scripts/keyboard-check.mjs [--browser <chrome or edge exe>] [--sizes 1280x720,1920x1080]
//
// What it does, with a fresh browser profile and no game data:
//   1. Opens the campaign wizard with the keyboard only (Enter on the start page button), enables the campaign, and on every
//      step presses Tab and Shift+Tab 60 times each: the focus must never leave the wizard. Escape must close it and put the
//      focus back on a control of the editor (not on the page body).
//   2. Checks the accessible structure of the wizard and the start page (axe-style rules): every button, link, input and
//      select has a name, dialogs are named and modal, ids are unique, aria-labelledby/controls targets exist, tabs sit in a
//      tablist, headings do not skip a level, images have alt text, a live region exists for messages, the page has a
//      language, a title, a main landmark.
// Exit code 1 on any finding. The same checks run at each size.
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const arg = (n, d) => {
    const i = args.indexOf(`--${n}`);
    return i >= 0 ? args[i + 1] : d;
};
const sizes = arg('sizes', '1280x720,1920x1080')
    .split(',')
    .map((s) => s.split('x').map(Number));
const dist = join(root, 'dist');
if (!existsSync(join(dist, 'index.html'))) {
    console.error('packages/app/dist is missing: run `npm run build -w @drakan/editor-app` first');
    process.exit(2);
}
const browser = [
    arg('browser', ''),
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
    '/usr/bin/google-chrome',
    '/usr/bin/chromium',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
]
    .filter(Boolean)
    .find((c) => existsSync(c));
if (!browser) {
    console.error('no Chrome or Edge found; pass --browser <exe>');
    process.exit(2);
}

const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.svg': 'image/svg+xml', '.png': 'image/png', '.json': 'application/json', '.wasm': 'application/wasm', '.woff2': 'font/woff2' };
const server = createServer((req, res) => {
    let p = decodeURIComponent((req.url ?? '/').split('?')[0]);
    if (p === '/') p = '/index.html';
    const f = join(dist, p);
    if (!f.startsWith(dist) || !existsSync(f)) {
        res.statusCode = 404;
        return res.end();
    }
    res.setHeader('Content-Type', types[extname(f)] ?? 'application/octet-stream');
    res.end(readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const url = `http://127.0.0.1:${server.address().port}/index.html`;

const profile = mkdtempSync(join(tmpdir(), 'drakan-kbd-'));
const port = 9400 + Math.floor(Math.random() * 400);
const proc = spawn(browser, ['--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, '--no-first-run', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', 'about:blank'], { stdio: 'ignore' });
process.on('exit', () => {
    try {
        proc.kill();
    } catch {}
    server.close();
    try {
        rmSync(profile, { recursive: true, force: true });
    } catch {}
});
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let target;
for (let i = 0; i < 60 && !target; i++) {
    await sleep(300);
    try {
        target = (await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find((t) => t.type === 'page');
    } catch {}
}
if (!target) {
    console.error('the browser did not start');
    process.exit(2);
}
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((r) => (ws.onopen = r));
let nextId = 1;
const pending = new Map();
ws.onmessage = (m) => {
    const d = JSON.parse(m.data);
    pending.get(d.id)?.(d);
    pending.delete(d.id);
};
const send = (method, params = {}) =>
    new Promise((r) => {
        const id = nextId++;
        pending.set(id, r);
        ws.send(JSON.stringify({ id, method, params }));
    });
const ev = async (expression) => (await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })).result?.result?.value;
const KEYS = { Tab: ['Tab', 9], Enter: ['Enter', 13], Escape: ['Escape', 27] };
async function press(key, shift = false) {
    const [code, vk] = KEYS[key];
    const mod = shift ? 8 : 0;
    const base = { key, code, windowsVirtualKeyCode: vk, nativeVirtualKeyCode: vk, modifiers: mod };
    await send('Input.dispatchKeyEvent', { type: key === 'Enter' ? 'keyDown' : 'rawKeyDown', ...base, text: key === 'Enter' ? '\r' : undefined });
    await send('Input.dispatchKeyEvent', { type: 'keyUp', ...base });
}
await send('Page.enable');

const where = `(() => { const a = document.activeElement; const w = document.querySelector('#cwz-root:not([hidden])');
  return { inside: !!w && w.contains(a), body: a === document.body, tag: a ? a.tagName.toLowerCase() + (a.id ? '#' + a.id : '') : '' }; })()`;

const findings = [];
const note = (size, what) => findings.push(`${size} | ${what}`);

const STRUCTURE = `(() => {
  const out = [];
  const id = (e) => e.tagName.toLowerCase() + (e.id ? '#' + e.id : '') + '.' + String(e.className && e.className.baseVal === undefined ? e.className : '').split(' ')[0];
  const txt = (e) => (e.textContent || '').trim();
  const labelledBy = (e) => (e.getAttribute('aria-labelledby') || '').split(/\\s+/).filter(Boolean).map((i) => document.getElementById(i)?.textContent || '').join(' ').trim();
  const name = (e) => (e.getAttribute('aria-label') || labelledBy(e) || e.title || txt(e) || (e.labels && [...e.labels].map(txt).join(' ')) || e.getAttribute('placeholder') || '').trim();
  const vis = (e) => { const r = e.getBoundingClientRect(); return r.width > 0 && r.height > 0 && !e.closest('[hidden], [inert]'); };
  for (const e of document.querySelectorAll('button, a[href], input:not([type=hidden]), select, textarea, [role=button], [role=tab], [role=menuitem], [role=checkbox], [role=switch], [role=slider], [role=option]'))
    if (vis(e) && !name(e)) out.push('no accessible name: ' + id(e));
  for (const d of document.querySelectorAll('[role=dialog], [role=alertdialog]'))
    if (vis(d)) {
      if (!name(d) && !labelledBy(d)) out.push('dialog without a name: ' + id(d));
      if (d.getAttribute('aria-modal') !== 'true') out.push('dialog not aria-modal: ' + id(d));
    }
  const ids = new Map();
  for (const e of document.querySelectorAll('[id]')) ids.set(e.id, (ids.get(e.id) || 0) + 1);
  for (const [k, n] of ids) if (n > 1) out.push('duplicate id: ' + k + ' x' + n);
  for (const attr of ['aria-labelledby', 'aria-describedby', 'aria-controls', 'aria-owns'])
    for (const e of document.querySelectorAll('[' + attr + ']'))
      for (const i of e.getAttribute(attr).split(/\\s+/).filter(Boolean))
        if (!document.getElementById(i) && !(attr === 'aria-controls' && !vis(e))) out.push(attr + ' points at a missing id "' + i + '" on ' + id(e));
  for (const t of document.querySelectorAll('[role=tab]'))
    if (vis(t) && !t.closest('[role=tablist]')) out.push('tab outside a tablist: ' + id(t));
  for (const i of document.querySelectorAll('img')) if (vis(i) && i.getAttribute('alt') === null) out.push('image without alt: ' + (i.src || '').slice(-30));
  for (const s of document.querySelectorAll('svg')) {
    const btn = s.closest('button, a');
    if (btn && vis(s) && !s.hasAttribute('aria-hidden') && !s.getAttribute('aria-label') && !s.querySelector('title') && name(btn) && !s.closest('[aria-hidden=true]'))
      out.push('icon read twice next to its text: ' + id(btn));
  }
  // Headings: inside the open wizard only (its own outline), no skipped level.
  const scope = document.querySelector('#cwz-root:not([hidden])') || document;
  let last = 0;
  for (const h of scope.querySelectorAll('h1, h2, h3, h4, h5, h6, [role=heading]')) {
    if (!vis(h)) continue;
    const lvl = h.getAttribute('aria-level') ? +h.getAttribute('aria-level') : +h.tagName[1];
    if (last && lvl > last + 1) out.push('heading level jumps h' + last + ' -> h' + lvl + ': ' + txt(h).slice(0, 30));
    last = lvl;
  }
  if (!document.documentElement.lang) out.push('page has no lang');
  if (!document.title.trim()) out.push('page has no title');
  if (!document.querySelector('main, [role=main]')) out.push('no main landmark');
  if (!document.querySelector('[aria-live], [role=status], [role=alert], [role=log]')) out.push('no live region');
  return out;
})()`;

for (const [w, h] of sizes) {
    const size = `${w}x${h}`;
    await send('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 1, mobile: false });
    await send('Page.navigate', { url });
    await sleep(2500);
    await ev('localStorage.clear()');
    await send('Page.navigate', { url });
    await sleep(2500);

    for (const f of await ev(STRUCTURE)) note(size, 'start page: ' + f);

    // Keyboard only: focus the template button of the start page, choose the campaign tile, open the wizard.
    const opened = await ev(`(() => { const t = document.querySelector('[data-template="build-campaign"]'); if (!t) return false; t.focus(); return true; })()`);
    if (!opened) note(size, 'start page has no campaign template tile');
    await press('Enter');
    await sleep(300);
    await ev(`document.querySelector('.sp-go')?.focus()`);
    await press('Enter');
    await sleep(900);
    let st = await ev(where);
    if (!(await ev(`!!document.querySelector('#cwz-root:not([hidden])')`))) {
        note(size, 'the wizard did not open from the keyboard');
        continue;
    }
    if (!st.inside) note(size, `the wizard did not take the focus when it opened (${st.tag || 'body'})`);
    for (const f of await ev(STRUCTURE)) note(size, 'wizard (intro): ' + f);

    if (await ev(`!!document.getElementById('cwz-make-this')`)) {
        await ev(`document.getElementById('cwz-make-this').focus()`);
        await press('Enter');
        await sleep(900);
        st = await ev(where);
        if (!st.inside) note(size, `focus fell out of the wizard after "Make a campaign" (${st.tag || 'body'})`);
    }
    // A first-run tour card would take the keys; dismiss it by the keyboard if it is there.
    if (await ev(`!!document.querySelector('.tour-card')`)) {
        await press('Escape');
        await sleep(300);
    }

    for (const stepId of ['about', 'levels', 'exits', 'check', 'play', 'ship']) {
        await ev(`document.getElementById('cwz-step-${stepId}')?.focus()`);
        await press('Enter');
        await sleep(600);
        for (const f of await ev(STRUCTURE)) note(size, `wizard step ${stepId}: ` + f);
        let out = 0;
        const seen = new Set();
        for (const shift of [false, true]) {
            for (let i = 0; i < 60; i++) {
                await press('Tab', shift);
                const s = await ev(where);
                seen.add(s.tag);
                if (!s.inside) out++;
            }
        }
        if (out) note(size, `step ${stepId}: the focus left the wizard ${out} times in 120 Tab presses`);
        if (seen.size < 5) note(size, `step ${stepId}: only ${seen.size} different controls were reachable by Tab`);
    }

    await press('Escape');
    await sleep(400);
    if (await ev(`!!document.querySelector('#cwz-root:not([hidden])')`)) note(size, 'Escape did not close the wizard');
    else if ((await ev(where)).body) note(size, 'after Escape the focus is on the page body, not on a control of the editor');
}

if (findings.length) {
    console.log(`keyboard-check: ${findings.length} findings`);
    for (const f of findings) console.log('  ' + f);
    process.exit(1);
}
console.log(`keyboard-check: ${sizes.length} sizes, wizard focus trap and structure checks passed`);
process.exit(0);

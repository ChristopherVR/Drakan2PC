// Visual sweep of the built editor: opens every workspace, panel, dialog, menu and empty state it can reach without a
// disc, at 820x700, 1280x720 and 1920x1080, takes a screenshot of each and runs layout checks (the page scrolling
// sideways, text clipped out of its box, controls off the window or on top of each other).
//
//   npm run build -w @drakan/editor-app
//   node packages/app/scripts/ui-sweep.mjs [--out build/ui-sweep] [--browser <chrome or edge exe>] [--iso <path>] [--sizes 820x700,1280x720]
//
// No packages and no game data are needed: it serves packages/app/dist, starts Chrome or Edge headless and talks to it
// over the DevTools protocol. With `--iso` (a disc image of your own) it also opens a level first, so the panels that
// need one are covered; without it they show their empty states. File choosers are intercepted, so nothing blocks.
// Exit code 1 when a check finds something that is not in KNOWN (below).
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const arg = (n, d) => {
    const i = args.indexOf(`--${n}`);
    return i >= 0 ? args[i + 1] : d;
};
const out = resolve(process.env.INIT_CWD ?? process.cwd(), arg('out', 'build/ui-sweep'));
const iso0 = arg('iso', process.env.DRAKAN_SWEEP_ISO ?? '');
// With a disc the level is drawn in software at every size: two sizes by default.
const sizes = arg('sizes', iso0 ? '820x700,1280x720' : '820x700,1280x720,1920x1080')
    .split(',')
    .map((s) => s.split('x').map(Number));
const iso = arg('iso', process.env.DRAKAN_SWEEP_ISO ?? '');
const dist = join(root, 'dist');
if (!existsSync(join(dist, 'index.html'))) {
    console.error('packages/app/dist is missing: run `npm run build -w @drakan/editor-app` first');
    process.exit(2);
}
const candidates = [
    arg('browser', ''),
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
    '/usr/bin/google-chrome',
    '/usr/bin/chromium',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
].filter(Boolean);
const browser = candidates.find((c) => existsSync(c));
if (!browser) {
    console.error('no Chrome or Edge found; pass --browser <exe>');
    process.exit(2);
}

/** Findings that are by design (scroll strips, fixed overlays). Matched against "<state> | <finding>". */
const KNOWN = [/sp-tile|start-level|hp-item/, /#tabs|ws-tab/, /ed-edge-tab/];

// ---- static server --------------------------------------------------------------------------------------------------
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

// ---- browser -------------------------------------------------------------------------------------------------------
const profile = mkdtempSync(join(tmpdir(), 'drakan-sweep-'));
const port = 9400 + Math.floor(Math.random() * 400);
const proc = spawn(browser, ['--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, '--no-first-run', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--hide-scrollbars=false', 'about:blank'], { stdio: 'ignore' });
const cleanup = () => {
    try {
        proc.kill();
    } catch {}
    server.close();
    setTimeout(() => rmSync(profile, { recursive: true, force: true }), 1500).unref();
};
process.on('exit', cleanup);
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
    const p = pending.get(d.id);
    if (p) {
        pending.delete(d.id);
        p(d);
    }
};
const send = (method, params = {}) => new Promise((r) => {
    const id = nextId++;
    pending.set(id, r);
    ws.send(JSON.stringify({ id, method, params }));
});
const ev = async (expression) => {
    const r = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
    return r.result?.result?.value;
};
await send('Page.enable');
await send('Page.setInterceptFileChooserDialog', { enabled: true });
mkdirSync(out, { recursive: true });

// ---- page helpers (evaluated in the editor) -------------------------------------------------------------------------
const SCAN = `(() => {
  const out = [];
  const vis = (e) => { const r = e.getBoundingClientRect(); return r.width > 0 && r.height > 0 && e.offsetParent !== null; };
  const nm = (e) => e.tagName.toLowerCase() + (e.id ? '#' + e.id : '') + '.' + String(e.className || '').split(' ')[0];
  const overlay = (e) => { for (let n = e; n; n = n.parentElement) if (getComputedStyle(n).position === 'fixed') return true; return false; };
  const scrollers = (e) => { for (let n = e.parentElement; n; n = n.parentElement) { const o = getComputedStyle(n).overflowX; if (o === 'auto' || o === 'scroll' || o === 'hidden') return true; } return false; };
  const vp = document.getElementById('lv-viewport');
  if (vp && vp.offsetParent && innerWidth < 900 && vp.getBoundingClientRect().width < 520) out.push('viewport only ' + Math.round(vp.getBoundingClientRect().width) + ' px wide');
  if (document.documentElement.scrollWidth > innerWidth + 1) out.push('page scrolls sideways ' + document.documentElement.scrollWidth + '>' + innerWidth);
  for (const e of document.querySelectorAll('button, .btn, label, h1, h2, h3, [role=tab], select')) {
    if (!vis(e)) continue;
    const cs = getComputedStyle(e);
    const r = e.getBoundingClientRect();
    const t = (e.textContent || '').trim().slice(0, 24);
    if (e.scrollWidth > e.clientWidth + 2 && cs.overflow === 'visible' && cs.textOverflow !== 'ellipsis' && !e.children.length)
      out.push('text overflows ' + nm(e) + ' "' + t + '"');
    if ((r.right > innerWidth + 1 || r.left < -1) && !scrollers(e)) out.push('off window ' + nm(e) + ' "' + t + '"');
  }
  const btns = [...document.querySelectorAll('button, select')].filter((b) => vis(b) && !b.closest('[hidden]'));
  for (let i = 0; i < btns.length; i++) for (let j = i + 1; j < btns.length; j++) {
    const a = btns[i], b = btns[j];
    if (a.contains(b) || b.contains(a)) continue;
    if (a.closest('.popover, [role=menu]') || b.closest('.popover, [role=menu]')) continue; // menus open over other controls
    if (overlay(a) !== overlay(b)) continue; // a drawer or wizard over the toolbar covers it by design
    const onTop = (e) => { const r = e.getBoundingClientRect(); const t = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2); return !!t && e.contains(t); };
    if (!onTop(a) || !onTop(b)) continue; // one of them is clipped or covered, not drawn over the other
    const ra = a.getBoundingClientRect(), rb = b.getBoundingClientRect();
    const w = Math.min(ra.right, rb.right) - Math.max(ra.left, rb.left), h = Math.min(ra.bottom, rb.bottom) - Math.max(ra.top, rb.top);
    if (w > 6 && h > 6 && (() => { const t = document.elementFromPoint((Math.max(ra.left, rb.left) + Math.min(ra.right, rb.right)) / 2, (Math.max(ra.top, rb.top) + Math.min(ra.bottom, rb.bottom)) / 2); return !!t && (a.contains(t) || b.contains(t)); })()) out.push('overlap ' + nm(a) + ' "' + (a.textContent || '').trim().slice(0, 12) + '" / ' + nm(b) + ' "' + (b.textContent || '').trim().slice(0, 12) + '"');
  }
  return [...new Set(out)].slice(0, 8);
})()`;
const RESET = `(() => {
  document.getElementById('cwz-close')?.click();
  document.querySelector('.hp-x')?.click();
  for (const b of document.querySelectorAll('[aria-label="Close"], button[title^="Close"]')) if (b.offsetParent) b.click();
  for (const d of document.querySelectorAll('dialog[open]')) d.close();
  for (const b of document.querySelectorAll('.ui-modal [data-close], .modal-x')) b.click();
  document.dispatchEvent(new CustomEvent('ed-close-menus'));
  document.body.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', code: 'Escape', bubbles: true }));
})()`;
const runPalette = (q) => `(async () => {
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  document.body.dispatchEvent(new KeyboardEvent('keydown', { key: 'p', code: 'KeyP', ctrlKey: true, bubbles: true, cancelable: true }));
  await sleep(250);
  const i = document.querySelector('.pal-overlay:not([hidden]) .pal-input');
  if (!i) return null;
  i.value = '>' + ${JSON.stringify(q)}; i.dispatchEvent(new Event('input', { bubbles: true }));
  await sleep(250);
  const rows = [...document.querySelectorAll('.pal-row')];
  const r = rows.find((x) => x.textContent.toLowerCase().startsWith(${JSON.stringify(q)}.toLowerCase())) || rows[0];
  if (!r) return null;
  const t = r.textContent; r.click(); return t;
})()`;
const clickText = (sel, re) => `(() => { const b = [...document.querySelectorAll(${JSON.stringify(sel)})].find((x) => ${re}.test(x.textContent.trim())); b?.click(); return !!b; })()`;

// ---- states --------------------------------------------------------------------------------------------------------
const palette = ['Start page', 'Choose a level to edit', 'Import Order of the Flame levels', 'Undo History', 'Add: Arena', 'Add: Cut scene', 'Add: Dialogue line', 'Add: Level copy', 'Add: Menu page', 'Add: Puzzle', 'Add: Quest', 'Add: Vendor', 'Add a copy of a level', 'Export > Level for Unreal', 'Make a campaign', 'Keyboard shortcuts', 'Open the help guides', 'Start a tutorial', 'Story: Quests', 'Story: Dialogue', 'Story: Cinematics', 'Story: New cut scene', 'Story: New dialogue', 'Story: Strings', 'Story: Story pack', 'Story: Quest marker', 'Disc Files', 'Landscape mode', 'Open the Logic graph', 'Open the Menus editor', 'Open the Vendors editor', 'Open World Settings', 'Open Package and Play', 'Open Project Settings', 'Show Content Browser', 'Show Output Log', 'Show Validation', 'Level Editor', 'Open disc image', 'Remove converted', 'Open a saved level', 'Help: What the editor does', 'Help: Play test your level', 'Start: Create a multiplayer map', 'Start: Edit a save'];
const menus = ['File', 'Edit', 'Window', 'Tools', 'Help']; // the browser menu bar
const toolbarMenus = ['Select', 'Arrange', 'Add', 'Show', 'Display', 'Views'];

const BACK = `(() => { const b = [...document.querySelectorAll('#tabs button, button.ws-tab')].find((x) => /Level Editor/.test(x.textContent)); b?.click(); document.getElementById('cwz-close')?.click(); })()`;
const junk = join(profile, 'junk');
mkdirSync(junk, { recursive: true });
writeFileSync(join(junk, 'junk.iso'), Buffer.alloc(4096, 7));
writeFileSync(join(junk, 'broken.json'), '{ "level": [ not json');
writeFileSync(join(junk, 'notes.txt'), 'this folder is not an Order of the Flame install');
async function setFiles(selector, files) {
    const doc = (await send('DOM.getDocument', { depth: 1 })).result.root.nodeId;
    const q = await send('DOM.querySelector', { nodeId: doc, selector });
    if (!q.result?.nodeId) return false;
    await send('DOM.setFileInputFiles', { files, nodeId: q.result.nodeId });
    return true;
}
const findings = [];
const log = [];
async function snap(size, name) {
    const file = join(out, `${size}-${name.replace(/\W+/g, '_').slice(0, 60)}.png`);
    const s = await send('Page.captureScreenshot', { format: 'png' });
    writeFileSync(file, Buffer.from(s.result.data, 'base64'));
    const found = (await ev(SCAN)) ?? [];
    for (const f of found) {
        const line = `${size} ${name} | ${f}`;
        if (!KNOWN.some((k) => k.test(line))) findings.push(line);
    }
    log.push({ size, name, findings: found.length });
}

await send('Page.navigate', { url });
await sleep(2500);
// Error states, injected with files that are not what the editor expects (before any real disc is opened).
for (const [w, h] of sizes) {
    const size = `${w}x${h}`;
    await send('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 1, mobile: false });
    await sleep(500);
    await ev(RESET);
    await setFiles('#iso-input', [join(junk, 'junk.iso')]);
    await sleep(2500);
    await snap(size, 'error not a disc image');
    await ev(RESET);
    await setFiles('#lv-load', [join(junk, 'broken.json')]);
    await sleep(1500);
    await snap(size, 'error corrupt saved level');
    await ev(RESET);
    await ev(runPalette('Import Order of the Flame'));
    await sleep(800);
    await setFiles('dialog.ootf-dlg input[type=file]', [join(junk, 'notes.txt')]);
    await sleep(1500);
    await snap(size, 'error not an OotF install');
    await ev(RESET);
    await ev(`document.getElementById('ed-play')?.click()`);
    await sleep(700);
    await snap(size, 'play needs the desktop app');
    await ev(RESET);
    await ev(`window.drakanEditor?.levelUi.openLevel('ENGLISH\LEVELS\NOPE').catch(() => {})`);
    await sleep(1500);
    await snap(size, 'error level missing');
}
if (iso) {
    const doc = (await send('DOM.getDocument')).result.root.nodeId;
    const q = await send('DOM.querySelector', { nodeId: doc, selector: '#iso-input' });
    await send('DOM.setFileInputFiles', { files: [resolve(iso)], nodeId: q.result.nodeId });
    for (let i = 0; i < 120; i++) {
        await sleep(1000);
        if ((await ev(`document.querySelectorAll('select option').length`)) > 10) break;
    }
    await ev(`(async () => { const o = [...document.querySelectorAll('select option')].find((x) => /Surdana/.test(x.textContent)); if (o) await window.drakanEditor.levelUi.openLevel(o.value); })()`);
    await sleep(8000);
    // software rendering: draw the 3D view at a third of the resolution (the layout checks do not look at the picture)
    await ev(`(() => { const r = window.drakanEditor.levelUi.ctx().view().renderer; r.setPixelRatio(0.3); })()`);
}
for (const [w, h] of sizes) {
    const size = `${w}x${h}`;
    await send('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 1, mobile: false });
    await sleep(700);
    await ev(RESET);
    await snap(size, 'initial');
    if (iso) {
        // level-loaded states: a selected object (Details, Outliner row, gizmo) and a viewport context menu on it
        await ev(`(() => { const c = window.drakanEditor.levelUi.ctx(); const o = [...c.doc().objects()].find((x) => x.kind === 'player') ?? [...c.doc().objects()][0]; c.view().select([o.id]); c.view().frameSelection(); })()`);
        await sleep(1200);
        await snap(size, 'level object selected');
        await send('Input.dispatchMouseEvent', { type: 'mousePressed', x: Math.round(w / 2), y: Math.round(h / 3), button: 'right', clickCount: 1 });
        await send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: Math.round(w / 2), y: Math.round(h / 3), button: 'right', clickCount: 1 });
        await sleep(500);
        await snap(size, 'level context menu');
        await ev(RESET);
    }
    for (const q of palette) {
        await ev(RESET);
        await ev(BACK);
        await sleep(150);
        const row = await ev(runPalette(q));
        await sleep(900);
        await snap(size, row ? `cmd ${q}` : `missing ${q}`);
        if (!row) log.at(-1).missing = true;
    }
    await ev(RESET);
    for (const m of menus) {
        await ev(RESET);
        await ev(BACK);
        await ev(clickText('button, [role=menuitem], .mb-item, .menubar *', `/^${m}$/`));
        await sleep(400);
        await snap(size, `menu ${m}`);
    }
    for (const m of toolbarMenus) {
        await ev(RESET);
        await ev(BACK);
        await ev(clickText('.ed-toolbar button, .tbar button, button.tool', `/^${m}$/`));
        await sleep(400);
        await snap(size, `toolbar ${m}`);
    }
    // a context menu on the viewport
    await ev(RESET);
    await ev(BACK);
    await send('Input.dispatchMouseEvent', { type: 'mousePressed', x: Math.round(w / 2), y: Math.round(h / 2), button: 'right', clickCount: 1 });
    await send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: Math.round(w / 2), y: Math.round(h / 2), button: 'right', clickCount: 1 });
    await sleep(400);
    await snap(size, 'context menu');
    // light theme
    await ev(RESET);
    await ev(`document.body.dispatchEvent(new KeyboardEvent('keydown', { key: 'l', code: 'KeyL', ctrlKey: true, shiftKey: true, bubbles: true, cancelable: true }))`);
    await sleep(500);
    await snap(size, 'light theme');
    await ev(`document.body.dispatchEvent(new KeyboardEvent('keydown', { key: 'l', code: 'KeyL', ctrlKey: true, shiftKey: true, bubbles: true, cancelable: true }))`);
}
writeFileSync(join(out, 'report.json'), JSON.stringify({ log, findings }, null, 2));
console.log(`${log.length} states in ${sizes.length} size(s); ${log.filter((l) => l.missing).length} palette entries not found; ${findings.length} finding(s)`);
for (const f of findings) console.log(`  ${f}`);
console.log(`screenshots and report.json in ${out}`);
cleanup();
process.exit(findings.length ? 1 : 0);

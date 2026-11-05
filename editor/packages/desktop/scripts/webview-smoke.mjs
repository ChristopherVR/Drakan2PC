// Native WebView2 smoke test of the built desktop editor (Windows): the Order of the Flame import dialog's
// "Template level" dropdown must open above the modal dialog and take mouse and keyboard selection.
//
//   npm run smoke:webview -w @drakan/desktop [-- --exe <path to drakan-editor-desktop.exe>] [--port 9339]
//
// Starts the exe with WebView2 remote debugging and drives it over the Chrome DevTools Protocol (no extra packages).
// Needs no disc image or game data: when no disc is open, the template list is filled with synthetic names.
// Close other editor windows first: WebView2 instances share one browser process, which keeps its own options.
// Exit code 0 when every check passed.
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const arg = (name, fallback) => {
    const i = args.indexOf(`--${name}`);
    return i >= 0 ? args[i + 1] : fallback;
};
const exe = arg('exe', join(root, 'src-tauri', 'target', 'release', 'drakan-editor-desktop.exe'));
const port = Number(arg('port', '9339'));
if (!existsSync(exe)) {
    console.error(`${exe} does not exist; build it with npm run build -w @drakan/desktop`);
    process.exit(2);
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const app = spawn(exe, [], {
    env: { ...process.env, WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: `--remote-debugging-port=${port}` },
    stdio: 'ignore',
});

let ws;
let nextId = 1;
const pending = new Map();
function send(method, params = {}) {
    const id = nextId++;
    ws.send(JSON.stringify({ id, method, params }));
    return new Promise((resolve, reject) => pending.set(id, { resolve, reject }));
}
async function evaluate(expression) {
    const r = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
    if (r.exceptionDetails) throw new Error(`${expression.slice(0, 80)}: ${r.exceptionDetails.text}`);
    return r.result.value;
}
async function waitFor(expression, what, ms = 20000) {
    for (const end = Date.now() + ms; Date.now() < end; await sleep(200)) if (await evaluate(expression)) return;
    throw new Error(`timed out waiting for ${what}`);
}
async function click([x, y]) {
    await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
    await send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', clickCount: 1 });
    await send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', clickCount: 1 });
    await sleep(150);
}
const KEYS = { ArrowDown: 40, ArrowUp: 38, Enter: 13, Escape: 27 };
async function key(k) {
    const base = { key: k, code: k, windowsVirtualKeyCode: KEYS[k], nativeVirtualKeyCode: KEYS[k] };
    await send('Input.dispatchKeyEvent', { type: 'rawKeyDown', ...base });
    await send('Input.dispatchKeyEvent', { type: 'keyUp', ...base });
    await sleep(100);
}

const results = [];
const check = (name, ok, detail = '') => {
    results.push(ok);
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail ? `: ${detail}` : ''}`);
};

// Page state helpers (evaluated in the editor page).
const STATE = `(() => {
    const dlg = document.querySelector('dialog.ootf-dlg');
    const sel = dlg?.querySelector('select');
    const trig = sel?.nextElementSibling;
    const c = (el) => { const r = el.getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; };
    const d = dlg?.getBoundingClientRect();
    const opts = [...document.querySelectorAll('.ui-dd-opt')].map((o) => {
        const [x, y] = c(o);
        const top = document.elementFromPoint(x, y);
        const list = o.parentElement.getBoundingClientRect(); // options scrolled out of the list are clipped
        const shown = y > list.top && y < list.bottom && y < innerHeight;
        return { x, y, i: Number(o.dataset.i), shown, below: y > d.bottom, hit: !!top && o.contains(top) };
    }).filter((o) => o.shown);
    return {
        open: !!dlg?.open, modal: !!dlg?.matches(':modal'), index: sel?.selectedIndex ?? -1, count: sel?.options.length ?? 0,
        trigger: trig ? c(trig) : null, focusTrigger: document.activeElement === trig,
        popup: opts.length > 0, popupInDialog: !!dlg && opts.length > 0 && dlg.contains(document.querySelector('.ui-dd-opt')),
        opts, popups: document.querySelectorAll('.ui-dd-opt').length,
    };
})()`;

try {
    let target;
    for (const end = Date.now() + 30000; !target && Date.now() < end; await sleep(300)) {
        try {
            const list = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
            target = list.find((t) => t.type === 'page' && t.url.startsWith('http://tauri.localhost'));
        } catch {
            // not listening yet
        }
    }
    if (!target) throw new Error(`no WebView2 page on port ${port}`);
    ws = new WebSocket(target.webSocketDebuggerUrl);
    ws.onmessage = (m) => {
        const msg = JSON.parse(m.data);
        const p = pending.get(msg.id);
        if (!p) return;
        pending.delete(msg.id);
        if (msg.error) p.reject(new Error(msg.error.message));
        else p.resolve(msg.result);
    };
    await new Promise((resolve, reject) => ((ws.onopen = resolve), (ws.onerror = reject)));
    check('desktop shell', await evaluate(`'__TAURI_INTERNALS__' in window && /Edg\\//.test(navigator.userAgent)`));

    // Open the importer from the start page (its template tile, then the tile's button).
    await waitFor(`!!document.querySelector('[data-template=ootf]')`, 'the start page');
    await evaluate(`document.querySelector('[data-template=ootf]').click()`);
    await waitFor(
        `[...document.querySelectorAll('button')].some((b) => /Open the importer/.test(b.textContent))`,
        'the importer button',
    );
    await evaluate(
        `[...document.querySelectorAll('button')].find((b) => /Open the importer/.test(b.textContent)).click()`,
    );
    await waitFor(`!!document.querySelector('dialog.ootf-dlg[open]')`, 'the import dialog');
    // Without a disc there are no templates; synthetic names make the list long enough to leave the dialog.
    await evaluate(`(() => {
        const sel = document.querySelector('dialog.ootf-dlg select');
        for (let i = sel.options.length; i < 16; i++) sel.add(new Option('Synthetic template ' + i, 'T' + i));
    })()`);
    let s = await evaluate(STATE);
    check('import dialog is modal', s.open && s.modal);

    // Mouse: open, pick an option below the dialog's bottom edge.
    const before = s.index;
    await click(s.trigger);
    s = await evaluate(STATE);
    check('mouse opens the popup inside the modal', s.popup && s.popupInDialog);
    const outside = s.opts.filter((o) => o.below);
    check(
        'options below the dialog are hit-testable',
        outside.length > 0 && s.opts.every((o) => o.hit),
        `${outside.length} below`,
    );
    const pick = outside.find((o) => o.i !== before && o.i > 0) ?? s.opts.find((o) => o.i !== before && o.i > 0);
    await click([pick.x, pick.y]);
    s = await evaluate(STATE);
    check('mouse click selects the option', s.index === pick.i && !s.popup, `index ${s.index}, wanted ${pick.i}`);

    // Keyboard: ArrowDown opens, ArrowUp moves, Enter commits.
    await evaluate(`document.querySelector('dialog.ootf-dlg select').nextElementSibling.focus()`);
    const was = s.index;
    await key('ArrowDown');
    s = await evaluate(STATE);
    check('ArrowDown opens the popup', s.popup && s.popupInDialog);
    await key('ArrowUp');
    await key('Enter');
    s = await evaluate(STATE);
    check(
        'ArrowUp + Enter selects the previous option',
        s.index === was - 1 && !s.popup,
        `index ${s.index}, wanted ${was - 1}`,
    );

    // Escape closes only the popup.
    await key('Enter');
    await key('Escape');
    s = await evaluate(STATE);
    check('Escape closes the popup and keeps the dialog', !s.popup && s.open && s.focusTrigger);

    // Closing the dialog with an open popup leaves nothing behind.
    await key('Enter');
    await evaluate(
        `[...document.querySelectorAll('dialog.ootf-dlg button')].find((b) => b.textContent === 'Close').click()`,
    );
    await sleep(200);
    s = await evaluate(STATE);
    check('closing the dialog removes the popup', !s.open && s.popups === 0);
} catch (e) {
    check('run', false, e instanceof Error ? e.message : String(e));
} finally {
    ws?.close();
    app.kill();
}
const failed = results.filter((ok) => !ok).length;
console.log(failed ? `${failed} check(s) failed` : `all ${results.length} checks passed`);
process.exit(failed ? 1 : 0);

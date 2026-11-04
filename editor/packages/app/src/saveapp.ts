// The save game editor as its own application (save.html; on the desktop its own window). It shares the code packages
// and the save UI (saveui.ts, save/*) with the level editor but has its own shell and keeps the Drakan skin. The disc
// image is optional here: it only supplies item names, pictures and quest text.

import type { Api, Method, Request, Response } from './rpc.ts';
import { initSaveUi } from './saveui.ts';
import './save/app.css';
import { createItemArt } from './save/itemart.ts';
import { createStoryArt } from './save/storyart.ts';
import { installControls } from './editor/ui.ts';
import { installSaveWindowTours } from './editor/tour/index.ts';
import * as platform from './platform.ts';
import { isDesktop, openAppWindow, type IsoSource } from './platform.ts';

installControls();

const worker = new Worker(new URL('./worker.ts', import.meta.url), { type: 'module' });
const pending = new Map<number, { resolve: (v: unknown) => void; reject: (e: Error) => void }>();
let nextId = 1;
worker.onmessage = (ev: MessageEvent<Response>) => {
    const p = pending.get(ev.data.id);
    if (!p) return;
    pending.delete(ev.data.id);
    if (ev.data.ok) p.resolve(ev.data.result);
    else p.reject(new Error(ev.data.error));
};
function call<M extends Method>(method: M, ...args: Parameters<Api[M]>): Promise<ReturnType<Api[M]>> {
    return new Promise((resolve, reject) => {
        const id = nextId++;
        pending.set(id, { resolve: resolve as (v: unknown) => void, reject });
        worker.postMessage({ id, method, args } satisfies Request);
    });
}

let lastStatus = '';
const setStatus = (msg: string, error = false): void => {
    lastStatus = msg;
    if (error) console.warn(msg);
};

const itemArt = createItemArt((method, ...args) => call(method, ...args));
const storyArt = createStoryArt((method, ...args) => call(method, ...args));
const saveUi = initSaveUi({
    setStatus,
    classInfo: (r, thumbnail) => itemArt.info(r, thumbnail),
    onItemArt: (fn) => itemArt.onChange(fn),
    story: () => storyArt.get(),
    onStory: (fn) => storyArt.onChange(fn),
});

// ---- the disc (optional: names, pictures and quest text) -------------------------------------------------------------
const discBtn = document.createElement('button');
discBtn.className = 'sv-app-btn';
discBtn.type = 'button';
discBtn.textContent = 'Disc: none';
discBtn.title = 'Open your own disc image for item names, pictures and quest text. Saves can be edited without it.';
async function openIso(src: IsoSource): Promise<void> {
    discBtn.textContent = 'Disc: opening...';
    try {
        await call('open', src);
        itemArt.reset();
        storyArt.reset();
        discBtn.textContent = 'Disc: open';
        discBtn.title = `${src.name}: item names, pictures and quest text come from it.`;
    } catch (e) {
        discBtn.textContent = 'Disc: none';
        setStatus(e instanceof Error ? e.message : String(e), true);
    }
}
const isoInput = document.getElementById('iso-input') as HTMLInputElement;
isoInput.onchange = () => {
    const f = isoInput.files?.[0];
    if (f) void openIso(f);
};
discBtn.onclick = () => {
    if (isDesktop) {
        void platform.pickIso().then((s) => (s ? openIso(s) : undefined));
    } else isoInput.click();
};

// ---- link back to the level editor (an application of its own, not a tab) -------------------------------------------
const back = document.createElement('button');
back.className = 'sv-app-btn';
back.type = 'button';
back.id = 'sv-to-editor';
back.textContent = 'Level Editor';
back.title = 'Open the level and mod editor';
back.onclick = () => void openAppWindow('main');

const bar = document.querySelector('#save-main .sv-appbar');
const tools = document.createElement('div');
tools.className = 'sv-app-tools';
// The standalone save editor release (`?app=save`) ships without the level editor, so it has no way back.
const standalone = new URLSearchParams(location.search).get('app') === 'save';
tools.append(...(standalone ? [discBtn] : [discBtn, back]));
bar?.append(tools);

// ---- actions: #open=file|folder in the URL, or a save-action event from the level editor's File menu ------------------
const act = (a: string): void => {
    if (a === 'file') document.getElementById('sv-open')?.click();
    else if (a === 'folder') document.getElementById('sv-folder')?.click();
};
const hashAct = /open=(file|folder)/.exec(location.hash)?.[1];
if (hashAct) window.setTimeout(() => act(hashAct), 300);
void platform.onSaveAction(act);

// ---- drag and drop ------------------------------------------------------------------------------------------------
const drop = document.getElementById('drop')!;
let depth = 0;
window.addEventListener('dragenter', (e) => {
    e.preventDefault();
    depth++;
    drop.hidden = false;
});
window.addEventListener('dragleave', () => {
    if (--depth <= 0) {
        depth = 0;
        drop.hidden = true;
    }
});
window.addEventListener('dragover', (e) => e.preventDefault());
window.addEventListener('drop', (e) => {
    e.preventDefault();
    depth = 0;
    drop.hidden = true;
    const f = e.dataTransfer?.files[0];
    if (!f) return;
    if (/\.rsg$/i.test(f.name)) void saveUi.openFile(f);
    else void openIso(f);
});

// Desktop: the disc of the last session gives the pictures straight away.
if (isDesktop) {
    void platform
        .getSettings()
        .then(async (s) => (s.recentIsos[0] ? openIso(await platform.openRecentIso(s.recentIsos[0])) : undefined))
        .catch(() => undefined);
}

(window as unknown as { drakanSave: unknown }).drakanSave = { saveUi, status: () => lastStatus };

// Guided tour of this window (started when the editor's "Edit a save" tour hands over): editor/tour/.
installSaveWindowTours();

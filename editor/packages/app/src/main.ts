import type { DiscProfile } from '@drakan/formats';
import type { Api, EntryInfo, FileInfo, FileOpenResult, LevelInfo, Method, Request, Response } from './rpc.ts';
import { TextureViewport } from './viewport.ts';
import { initLevelUi } from './levelui.ts';
import { installMultiplayerMaps } from './editor/arena/install.ts';
import { installProject } from './editor/project/install.ts';
import { runCommand } from './editor/registry.ts';
import './files.css';
import { installPlayTest } from './editor/playtestui.ts';
import { initSaveUi } from './saveui.ts';
import { createItemArt } from './save/itemart.ts';
import { createStoryArt } from './save/storyart.ts';
import { initStoryEditors } from './story/register.ts';
import { isDesktop, type IsoSource } from './platform.ts';
import { initDesktop } from './desktop.ts';
import { installControls } from './editor/ui.ts';

// Shared control family for every mode: custom dropdowns, slider fills (editor/ui.ts, editor/dropdown.ts).
installControls();

const $ = <T extends HTMLElement>(id: string): T => {
    const el = document.getElementById(id);
    if (!el) throw new Error(`missing #${id}`);
    return el as T;
};

// ---- worker RPC -----------------------------------------------------------------------------------------------

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

// ---- state + elements -----------------------------------------------------------------------------------------

const status = $('status');
const langSel = $<HTMLSelectElement>('lang');
const levelsEl = $('levels');
const filesEl = $('files');
const filesPath = $('files-path');
const entriesTable = $<HTMLTableElement>('entries');
const entriesInfo = $('entries-info');
const textView = $('text-view');
const strView = $('str-view');
const hexWrap = $('hex-wrap');
const pvEmpty = $('pv-empty');
const entriesEmpty = $('entries-empty');
const actionsEl = $('fl-actions');
const vpEl = $('viewport');
const vpReset = $('vp-reset');
const hexEl = $('hex');
const hexInfo = $('hex-info');
const hexInflate = $<HTMLInputElement>('hex-inflate');
const hexPrev = $<HTMLButtonElement>('hex-prev');
const hexNext = $<HTMLButtonElement>('hex-next');
const showTextures = $<HTMLButtonElement>('show-textures');
const vpInfo = $('vp-info');

const PAGE = 4096;
let currentFile: string | null = null;
let currentEntries: EntryInfo[] = [];
let currentEntry: EntryInfo | null = null;
let hexOffset = 0;
let hexTotal = 0;
let viewport: TextureViewport | null = null;
let isoName = '';
let discProfile: DiscProfile | null = null;

function setStatus(msg: string, error = false): void {
    status.textContent = msg;
    status.classList.toggle('error', error);
}

function fmtSize(n: number): string {
    if (n < 1024) return `${n} B`;
    if (n < 1 << 20) return `${(n / 1024).toFixed(1)} KB`;
    if (n < 2 ** 30) return `${(n / (1 << 20)).toFixed(1)} MB`;
    return `${(n / 2 ** 30).toFixed(2)} GB`;
}

const hex = (n: number, w: number): string => n.toString(16).padStart(w, '0');

function select(list: HTMLElement, li: HTMLElement): void {
    list.querySelectorAll('li.sel').forEach((x) => x.classList.remove('sel'));
    li.classList.add('sel');
}

function reportError(e: unknown): void {
    setStatus(e instanceof Error ? e.message : String(e), true);
}

// ---- opening a disc -------------------------------------------------------------------------------------------

async function openIso(file: IsoSource): Promise<void> {
    setStatus(`Opening ${file.name} (${fmtSize(file.size)})...`);
    try {
        const r = await call('open', file);
        langSel.replaceChildren(...r.languages.map((l) => new Option(l, l, false, l === r.language)));
        langSel.disabled = r.languages.length < 2;
        renderLevels(r.levels);
        isoName = file.name;
        discProfile = r.profile;
        levelUi.setDisc({ isoName, levels: r.levels, profile: discProfile });
        storyEditors.setDisc(discProfile, r.language);
        projectUi.onDisc();
        itemArt.reset();
        storyArt.reset();
        filesEl.replaceChildren();
        filesPath.textContent = '';
        clearEntries();
        void showDir('').catch(reportError);
        setStatus(
            `${file.name}: volume "${r.volumeId}", ${r.levels.length} levels in ${r.language || 'no language tree'}.`,
        );
        // The Level tab is the editor and the start page; Files is the advanced raw view.
        if (r.levels.length > 0) showTab('level');
        if (r.levels.length === 0) {
            setStatus(`${file.name}: no LEVELLST.TXT found (volume "${r.volumeId}"). Showing the root directory.`);
            await showDir('');
        }
    } catch (e) {
        reportError(e);
    }
}

let levelsOnDisc: LevelInfo[] = [];

function renderLevels(levels: LevelInfo[]): void {
    levelsOnDisc = levels;
    levelsEl.replaceChildren(
        ...levels.map((l) => {
            const li = document.createElement('li');
            li.textContent = l.name;
            li.title = l.present ? l.dirPath : `${l.dirPath} (listed in LEVELLST.TXT but not on this disc)`;
            li.classList.toggle('missing', !l.present);
            li.onclick = () => {
                select(levelsEl, li);
                if (l.present) showDir(l.dirPath).catch(reportError);
                else setStatus(`${l.name}: ${l.dirPath} is listed in LEVELLST.TXT but is not on this disc.`);
            };
            return li;
        }),
    );
}

async function showDir(path: string): Promise<void> {
    const files = await call('listDir', path);
    filesPath.textContent = path || '(root)';
    const rows: HTMLElement[] = [];
    if (path) {
        const up = document.createElement('li');
        up.textContent = '..';
        up.onclick = () => showDir(path.includes('\\') ? path.slice(0, path.lastIndexOf('\\')) : '').catch(reportError);
        rows.push(up);
    }
    for (const f of files) rows.push(fileRow(f));
    filesEl.replaceChildren(...rows);
}

function fileRow(f: FileInfo): HTMLElement {
    const li = document.createElement('li');
    const name = document.createElement('span');
    name.textContent = f.isDir ? `${f.name}/` : f.name;
    const size = document.createElement('small');
    size.textContent = f.isDir ? '' : fmtSize(f.size);
    li.append(name, size);
    li.onclick = () => {
        if (f.isDir) {
            showDir(f.path).catch(reportError);
            return;
        }
        select(filesEl, li);
        openFile(f).catch(reportError);
    };
    return li;
}

// ---- file / entry views ---------------------------------------------------------------------------------------

// What a file or entry is, in words a modder can use. Unknown types stay as numbers.
const ENTRY_KINDS: Record<number, string> = {
    0x0000: 'dependency list',
    0x0020: 'object list',
    0x0040: 'texture',
};
const entryKind = (e: EntryInfo): string => ENTRY_KINDS[e.type] ?? `data 0x${hex(e.type, 4)}`;

type Preview = 'picture' | 'text' | 'hex' | 'none';
let preview: Preview = 'none';
let currentFileInfo: FileInfo | null = null;

function clearEntries(): void {
    currentFile = null;
    currentFileInfo = null;
    currentEntries = [];
    currentEntry = null;
    entriesTable.replaceChildren();
    entriesTable.hidden = false;
    textView.hidden = true;
    entriesInfo.textContent = '';
    entriesEmpty.hidden = false;
    hexEl.textContent = '';
    hexInfo.textContent = '';
    strView.textContent = '';
    hexPrev.disabled = hexNext.disabled = true;
    showTextures.disabled = true;
    actionsEl.replaceChildren();
    setPreview('none');
    pvEmpty.hidden = false;
    for (const id of ['pv-picture', 'pv-text', 'pv-hex']) ($(id) as HTMLButtonElement).disabled = true;
}

/** Buttons that take the selected file to the editor that understands it. */
function renderActions(f: FileInfo): void {
    const btns: HTMLElement[] = [];
    const add = (label: string, title: string, run: () => void): void => {
        const b = document.createElement('button');
        b.className = 'ui-btn sm primary';
        b.textContent = label;
        b.title = title;
        b.onclick = run;
        btns.push(b);
    };
    const upper = f.path.toUpperCase();
    const level = levelsOnDisc.find((l) => {
        const dir = l.dirPath.toUpperCase();
        const want = `${dir}\\${dir.split('\\').pop()}.LVL`;
        return l.present && (upper === want || upper.endsWith(`\\${want}`));
    });
    if (level) {
        add('Open this level in the Level tab', `Edit ${level.name}`, () => {
            showTab('level');
            levelUi.openLevel(level.dirPath).catch(reportError);
        });
    }
    if (/\.XDB$/.test(upper)) {
        add('Edit these strings', 'Open the Strings page of the Story editor', () => {
            showTab('level');
            runCommand('story.strings');
        });
    } else if (/\.(SSD|SDB)$/.test(upper)) {
        add('Open the Story editor', 'Dialogue and cut scenes live in the Story editor', () => {
            showTab('level');
            runCommand('panel.story');
        });
    }
    if (!btns.length) {
        const p = document.createElement('span');
        p.className = 'fl-noact';
        p.textContent = 'Nothing here opens in an editor. Click an entry to look at it.';
        btns.push(p);
    }
    actionsEl.replaceChildren(...btns);
}

async function openFile(f: FileInfo): Promise<void> {
    clearEntries();
    currentFileInfo = f;
    setStatus(`Reading ${f.path}...`);
    const r: FileOpenResult = await call('openFile', f.path);
    currentFile = f.path;
    renderActions(f);
    if (r.kind === 'srsc') {
        currentEntries = r.entries;
        const compressed = r.entries.filter((e) => e.zlib).length;
        entriesInfo.textContent = `${r.entries.length} entries${compressed ? `, ${compressed} compressed` : ''}, ${fmtSize(r.size)}`;
        entriesEmpty.hidden = true;
        renderEntries(r.entries);
        showTextures.disabled = !r.entries.some((e) => e.type === 0x40);
        setStatus(r.warnings.length ? `${f.name}: ${r.warnings.slice(0, 3).join('; ')}` : `${f.name}: container file.`);
    } else if (r.kind === 'text') {
        entriesEmpty.hidden = true;
        entriesTable.hidden = true;
        textView.hidden = false;
        let t = r.text;
        if (r.deps)
            t += `\n\nResolved dependency stems (extension appended by the loader):\n${r.deps.map((d) => `  ${d.index}  ${d.resolved}`).join('\n')}`;
        textView.textContent = t;
        setStatus(`${f.name}: text file.`);
    } else {
        entriesEmpty.hidden = true;
        hexEl.textContent = '';
        setStatus(`${f.name}: not a container file (${fmtSize(r.size)}); nothing to list.`);
    }
}

function renderEntries(entries: EntryInfo[]): void {
    const head = '<thead><tr><th>#</th><th>What</th><th>id</th><th>sub</th><th>size</th></tr></thead>';
    const tbody = document.createElement('tbody');
    for (const e of entries) {
        const tr = document.createElement('tr');
        tr.dataset.index = String(e.index);
        tr.title = `offset 0x${hex(e.offset, 6)}${e.zlib ? ', compressed' : ''}`;
        tr.innerHTML = `<td>${e.index}</td><td>${entryKind(e)}</td><td>${e.id}</td><td>${e.sub}</td><td>${fmtSize(e.size)}</td>`;
        tr.onclick = () => {
            tbody.querySelectorAll('tr.sel').forEach((x) => x.classList.remove('sel'));
            tr.classList.add('sel');
            selectEntry(e).catch(reportError);
        };
        tbody.appendChild(tr);
    }
    entriesTable.innerHTML = head;
    entriesTable.appendChild(tbody);
}

function setPreview(mode: Preview): void {
    preview = mode;
    vpEl.hidden = mode !== 'picture';
    vpReset.hidden = mode !== 'picture';
    strView.hidden = mode !== 'text';
    hexWrap.hidden = mode !== 'hex';
    for (const m of ['picture', 'text', 'hex'] as const) $(`pv-${m}`).classList.toggle('primary', m === mode);
}

async function selectEntry(e: EntryInfo): Promise<void> {
    currentEntry = e;
    hexOffset = 0;
    pvEmpty.hidden = true;
    const isTexture = e.type === 0x40;
    ($('pv-picture') as HTMLButtonElement).disabled = !isTexture;
    ($('pv-text') as HTMLButtonElement).disabled = false;
    ($('pv-hex') as HTMLButtonElement).disabled = false;
    const isStrings = /\.XDB$/i.test(currentFileInfo?.name ?? '');
    setPreview(isTexture ? 'picture' : isStrings ? 'text' : 'hex');
    await renderHex();
    if (isTexture) {
        // Keep the file's whole texture set on screen and frame the selected one; lay the set out first if needed.
        if (!viewport?.focus(e.index)) {
            await showTexturesFor(null);
            viewport?.focus(e.index);
        }
    }
    if (!isTexture) await renderStrings();
}

/** Readable text in an entry: runs of 4 or more printable characters, as plain ASCII or UTF-16. */
function extractText(b: Uint8Array): string[] {
    const out: string[] = [];
    let run = '';
    const flush = (): void => {
        if (run.length >= 4) out.push(run);
        run = '';
    };
    for (let i = 0; i < b.length; i++) {
        const c = b[i]!;
        if (c >= 32 && c < 127) {
            run += String.fromCharCode(c);
            // UTF-16LE: a zero byte after each character
            if (b[i + 1] === 0 && b[i + 2] !== undefined && b[i + 2]! >= 32 && b[i + 2]! < 127 && b[i + 3] === 0) i++;
        } else if (c === 0 && run && b[i - 1]! >= 32) {
            continue;
        } else flush();
    }
    flush();
    return out;
}

async function renderStrings(): Promise<void> {
    if (!currentFile || !currentEntry) return;
    const r = await call('readEntry', currentFile, currentEntry.index, 0, 1 << 18, true);
    const lines = extractText(r.bytes);
    strView.textContent = lines.length
        ? lines.join('\n') +
          (r.total > r.bytes.length ? `\n\n(first ${fmtSize(r.bytes.length)} of ${fmtSize(r.total)})` : '')
        : 'No readable text in this entry. Try the Hex view.';
}

async function renderHex(): Promise<void> {
    if (!currentFile || !currentEntry) return;
    const r = await call('readEntry', currentFile, currentEntry.index, hexOffset, PAGE, hexInflate.checked);
    hexTotal = r.total;
    const lines: string[] = [];
    for (let i = 0; i < r.bytes.length; i += 16) {
        const row = r.bytes.subarray(i, i + 16);
        const h = Array.from(row, (b) => hex(b, 2)).join(' ');
        const a = Array.from(row, (b) => (b >= 32 && b < 127 ? String.fromCharCode(b) : '.')).join('');
        lines.push(`${hex(hexOffset + i, 8)}  ${h.padEnd(47)}  ${a}`);
    }
    hexEl.textContent = lines.join('\n');
    hexEl.scrollTop = 0;
    const end = hexOffset + r.bytes.length;
    hexInfo.textContent = `entry ${currentEntry.index}: bytes ${hexOffset}-${end} of ${hexTotal}${r.inflated ? ' (decompressed)' : ''}`;
    hexPrev.disabled = hexOffset === 0;
    hexNext.disabled = end >= hexTotal;
}

$('pv-picture').onclick = () => setPreview('picture');
$('pv-text').onclick = () => {
    setPreview('text');
    renderStrings().catch(reportError);
};
$('pv-hex').onclick = () => setPreview('hex');

hexPrev.onclick = () => {
    hexOffset = Math.max(0, hexOffset - PAGE);
    renderHex().catch(reportError);
};
hexNext.onclick = () => {
    hexOffset += PAGE;
    renderHex().catch(reportError);
};
hexInflate.onchange = () => {
    hexOffset = 0;
    renderHex().catch(reportError);
};

// ---- textures -------------------------------------------------------------------------------------------------

function getViewport(): TextureViewport {
    if (!viewport) {
        viewport = new TextureViewport($('viewport'));
        viewport.onHover = (i) => (vpInfo.textContent = i ? i.label : lastVpInfo);
        viewport.onPick = (i) => {
            const row = entriesTable.querySelector<HTMLElement>(`tr[data-index="${i.index}"]`);
            row?.scrollIntoView({ block: 'nearest' });
            row?.click();
        };
    }
    return viewport;
}
let lastVpInfo = '';

async function showTexturesFor(indices: number[] | null): Promise<void> {
    if (!currentFile) return;
    const vp = getViewport();
    const tex = await call('decodeTextures', currentFile, indices);
    const shown = vp.setTextures(tex);
    const failed = tex.filter((t) => t.error);
    lastVpInfo = `${shown} shown${failed.length ? `, ${failed.length} undecodable (${failed[0]!.error})` : ''}`;
    vpInfo.textContent = lastVpInfo;
}

showTextures.onclick = () => {
    showTexturesFor(null).catch(reportError);
};
$('vp-reset').onclick = () => viewport?.reset();

// ---- input: picker + drag/drop --------------------------------------------------------------------------------

// Desktop app: the Open ISO button and every programmatic click on the input use the native dialog, which remembers the
// disc and lets the worker range-read it by path.
$<HTMLInputElement>('iso-input').addEventListener(
    'click',
    (e) => {
        if (!isDesktop) return;
        e.preventDefault();
        e.stopPropagation();
        desktop.openIsoDialog();
    },
    true,
);
$<HTMLInputElement>('iso-input').onchange = (e) => {
    const f = (e.target as HTMLInputElement).files?.[0];
    if (f) void openIso(f);
};
langSel.onchange = () => {
    call('setLanguage', langSel.value)
        .then((levels) => {
            renderLevels(levels);
            levelUi.setDisc({ isoName, levels, profile: discProfile });
            storyEditors.setDisc(discProfile, langSel.value);
            projectUi.onDisc();
            itemArt.reset();
            storyArt.reset();
        })
        .catch(reportError);
};

const drop = $('drop');
let dragDepth = 0;
window.addEventListener('dragenter', (e) => {
    e.preventDefault();
    dragDepth++;
    drop.hidden = false;
});
window.addEventListener('dragleave', () => {
    if (--dragDepth <= 0) {
        dragDepth = 0;
        drop.hidden = true;
    }
});
window.addEventListener('dragover', (e) => e.preventDefault());
window.addEventListener('drop', (e) => {
    e.preventDefault();
    dragDepth = 0;
    drop.hidden = true;
    const f = e.dataTransfer?.files[0];
    if (!f) return;
    if (/\.rsg$/i.test(f.name)) {
        showTab('save');
        saveUi.openFile(f).catch(reportError);
    } else if (/\.json$/i.test(f.name)) {
        showTab('level');
        levelUi.loadFile(f).catch(reportError);
    } else void openIso(f);
});

// ---- tabs + level mode ----------------------------------------------------------------------------------------

const levelUi = initLevelUi({
    call: (method, ...args) => call(method, ...args),
    setStatus,
});

const multiplayer = installMultiplayerMaps({
    ui: levelUi,
    download: (name, data, type) => {
        const a = document.createElement('a');
        a.href = URL.createObjectURL(new Blob([data], { type }));
        a.download = name;
        a.click();
        setTimeout(() => URL.revokeObjectURL(a.href), 10_000);
    },
    setStatus,
});

// Right-click "Play from here", F5 and the Play drop-down (desktop app; disabled entries in the browser).
installPlayTest(levelUi, setStatus);

// Strings / Quests / Story pack dock panels of the Level tab (content editing; see story/register.ts).
const storyEditors = initStoryEditors({
    call: (method, ...args) => call(method, ...args),
    setStatus,
    ctx: () => levelUi.ctx(),
    play: (stem, id) => levelUi.ctx().refs().play(stem, id),
    stop: () => levelUi.ctx().refs().stop(),
});

// Mod projects, task checklists, the Project panel and the Help drawer (editor/project/).
const projectUi = installProject({
    ui: levelUi,
    call: (method, ...args) => call(method, ...args),
    setStatus,
    showTab: (t) => showTab(t),
    arena: multiplayer.store,
    story: storyEditors,
});

// Item names and pictures for the Save tab work without a level: they come from the shared Resources database.
const itemArt = createItemArt((method, ...args) => call(method, ...args));
const storyArt = createStoryArt((method, ...args) => call(method, ...args));
const saveUi = initSaveUi({
    setStatus,
    classInfo: (r) => itemArt.info(r) ?? levelUi.classInfo(r),
    onItemArt: (fn) => itemArt.onChange(fn),
    story: () => storyArt.get(),
    onStory: (fn) => storyArt.onChange(fn),
});

type Tab = 'inspect' | 'level' | 'save';
function showTab(tab: Tab): void {
    $('inspect-main').hidden = tab !== 'inspect';
    $('level-main').hidden = tab !== 'level';
    $('save-main').hidden = tab !== 'save';
    for (const t of ['inspect', 'level', 'save'] as const) $(`tab-${t}`).classList.toggle('active', t === tab);
    if (tab === 'level') levelUi.view();
}
$('tab-inspect').onclick = () => showTab('inspect');
$('tab-level').onclick = () => showTab('level');
$('tab-save').onclick = () => showTab('save');
(window as unknown as { drakanEditor: unknown }).drakanEditor = { levelUi, saveUi, showTab };

const desktop = initDesktop({
    openIso: (src) => openIso(src),
    showTab,
    setStatus,
});

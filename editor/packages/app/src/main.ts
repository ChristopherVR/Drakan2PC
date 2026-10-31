import type { DiscProfile } from '@drakan/formats';
import type { Api, EntryInfo, FileInfo, FileOpenResult, LevelInfo, Method, Request, Response } from './rpc.ts';
import { TextureViewport } from './viewport.ts';
import { initLevelUi } from './levelui.ts';
import { initSaveUi } from './saveui.ts';
import { createItemArt } from './save/itemart.ts';

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

async function openIso(file: File): Promise<void> {
    setStatus(`Opening ${file.name} (${fmtSize(file.size)})...`);
    try {
        const r = await call('open', file);
        langSel.replaceChildren(...r.languages.map((l) => new Option(l, l, false, l === r.language)));
        langSel.disabled = r.languages.length < 2;
        renderLevels(r.levels);
        isoName = file.name;
        discProfile = r.profile;
        levelUi.setDisc({ isoName, levels: r.levels, profile: discProfile });
        itemArt.reset();
        filesEl.replaceChildren();
        filesPath.textContent = '';
        clearEntries();
        setStatus(
            `${file.name}: volume "${r.volumeId}", ${r.levels.length} levels in ${r.language || 'no language tree'}.`,
        );
        if (r.levels.length === 0) {
            setStatus(`${file.name}: no LEVELLST.TXT found (volume "${r.volumeId}"). Showing the root directory.`);
            await showDir('');
        }
    } catch (e) {
        reportError(e);
    }
}

function renderLevels(levels: LevelInfo[]): void {
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

function clearEntries(): void {
    currentFile = null;
    currentEntries = [];
    currentEntry = null;
    entriesTable.replaceChildren();
    entriesTable.hidden = false;
    textView.hidden = true;
    entriesInfo.textContent = '';
    hexEl.textContent = '';
    hexInfo.textContent = '';
    hexPrev.disabled = hexNext.disabled = true;
    showTextures.disabled = true;
}

async function openFile(f: FileInfo): Promise<void> {
    clearEntries();
    setStatus(`Reading ${f.path}...`);
    const r: FileOpenResult = await call('openFile', f.path);
    currentFile = f.path;
    if (r.kind === 'srsc') {
        currentEntries = r.entries;
        const compressed = r.entries.filter((e) => e.zlib).length;
        entriesInfo.textContent = `${r.entries.length} entries, ${compressed} zlib, ${fmtSize(r.size)}`;
        renderEntries(r.entries);
        showTextures.disabled = !r.entries.some((e) => e.type === 0x40);
        setStatus(r.warnings.length ? `${f.name}: ${r.warnings.slice(0, 3).join('; ')}` : `${f.name}: SRSC container.`);
    } else if (r.kind === 'text') {
        entriesTable.hidden = true;
        textView.hidden = false;
        let t = r.text;
        if (r.deps)
            t += `\n\nResolved dependency stems (extension appended by the loader):\n${r.deps.map((d) => `  ${d.index}  ${d.resolved}`).join('\n')}`;
        textView.textContent = t;
        setStatus(`${f.name}: text file.`);
    } else {
        hexEl.textContent = '';
        setStatus(`${f.name}: not an SRSC container (${fmtSize(r.size)}).`);
    }
}

function renderEntries(entries: EntryInfo[]): void {
    const head =
        '<thead><tr><th>#</th><th>type</th><th>id</th><th>sub</th><th>offset</th><th>size</th><th>zlib</th></tr></thead>';
    const tbody = document.createElement('tbody');
    for (const e of entries) {
        const tr = document.createElement('tr');
        tr.dataset.index = String(e.index);
        tr.innerHTML = `<td>${e.index}</td><td>0x${hex(e.type, 4)}</td><td>${e.id}</td><td>${e.sub}</td><td>0x${hex(e.offset, 6)}</td><td>${e.size}</td><td>${e.zlib ? 'yes' : ''}</td>`;
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

async function selectEntry(e: EntryInfo): Promise<void> {
    currentEntry = e;
    hexOffset = 0;
    await renderHex();
    if (e.type !== 0x40) return;
    // Keep the file's whole texture set on screen and frame the selected one; lay the set out first if needed.
    if (!viewport?.focus(e.index)) {
        await showTexturesFor(null);
        viewport?.focus(e.index);
    }
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
    hexInfo.textContent = `entry ${currentEntry.index}  ${hexOffset}-${end} of ${hexTotal}${r.inflated ? ' (inflated)' : ''}`;
    hexPrev.disabled = hexOffset === 0;
    hexNext.disabled = end >= hexTotal;
}

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

$<HTMLInputElement>('iso-input').onchange = (e) => {
    const f = (e.target as HTMLInputElement).files?.[0];
    if (f) void openIso(f);
};
langSel.onchange = () => {
    call('setLanguage', langSel.value)
        .then((levels) => {
            renderLevels(levels);
            levelUi.setDisc({ isoName, levels, profile: discProfile });
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

// Item names and pictures for the Save tab work without a level: they come from the shared Resources database.
const itemArt = createItemArt((method, ...args) => call(method, ...args));
const saveUi = initSaveUi({
    setStatus,
    classInfo: (r) => itemArt.info(r) ?? levelUi.classInfo(r),
    onItemArt: (fn) => itemArt.onChange(fn),
});

type Tab = 'inspect' | 'level' | 'save';
function showTab(tab: Tab): void {
    document.querySelector('main')!.hidden = tab !== 'inspect';
    $('level-main').hidden = tab !== 'level';
    $('save-main').hidden = tab !== 'save';
    for (const t of ['inspect', 'level', 'save'] as const) $(`tab-${t}`).classList.toggle('active', t === tab);
    if (tab === 'level') levelUi.view();
}
$('tab-inspect').onclick = () => showTab('inspect');
$('tab-level').onclick = () => showTab('level');
$('tab-save').onclick = () => showTab('save');
(window as unknown as { drakanEditor: unknown }).drakanEditor = { levelUi, saveUi, showTab };

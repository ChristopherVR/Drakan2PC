import { LevelDocument, buildModZip, parseWorldDump, type EntityKind, type LevelObject } from '@drakan/editor-core';
import { KINDS, LevelView } from './level.ts';
import type { Api } from './rpc.ts';

const $ = <T extends HTMLElement>(id: string): T => {
    const el = document.getElementById(id);
    if (!el) throw new Error(`missing #${id}`);
    return el as T;
};

export interface LevelUiHost {
    call<M extends 'exportRoundTrip'>(method: M, ...args: Parameters<Api[M]>): Promise<ReturnType<Api[M]>>;
    currentFile(): string | null;
    setStatus(msg: string, error?: boolean): void;
}

function download(name: string, data: BlobPart, type: string): void {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([data], { type }));
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 10_000);
}

/** Wires the Level tab: toolbar, filters, property panel, file loading. Returns the loader for drag-and-drop. */
export function initLevelUi(host: LevelUiHost): { view: () => LevelView; loadFile(f: File): Promise<void> } {
    const props = $('lv-props');
    const hud = $('lv-hud');
    const empty = $('lv-empty');
    const btn = (id: string): HTMLButtonElement => $<HTMLButtonElement>(id);
    let view: LevelView | null = null;
    let levelName = 'level';

    const getView = (): LevelView => {
        if (!view) {
            view = new LevelView($('lv-viewport'), { onSelection: () => renderProps(), onChange: () => refresh() });
            view.setDocument(new LevelDocument());
            view.setActive(true);
            hudTimer = window.setInterval(updateHud, 500);
        }
        return view;
    };
    let hudTimer = 0;
    void hudTimer;

    function updateHud(): void {
        if (!view) return;
        hud.textContent = `${view.visibleCount}/${view.doc.size} shown, ${view.selection.size} selected, ${view.fps} fps`;
    }

    function refresh(): void {
        const v = getView();
        btn('lv-undo').disabled = !v.stack.canUndo;
        btn('lv-redo').disabled = !v.stack.canRedo;
        btn('lv-undo').title = v.stack.undoLabel ?? '';
        btn('lv-redo').title = v.stack.redoLabel ?? '';
        btn('lv-save').disabled = v.doc.size === 0;
        btn('lv-export').disabled = v.doc.size === 0;
        empty.hidden = v.doc.size > 0;
        renderProps();
        updateHud();
    }

    // ---- kind filters ----
    const hidden = new Set<EntityKind>();
    const kindsEl = $('lv-kinds');
    for (const k of KINDS) {
        const l = document.createElement('label');
        const cb = document.createElement('input');
        cb.type = 'checkbox';
        cb.checked = true;
        cb.onchange = () => {
            if (cb.checked) hidden.delete(k.kind);
            else hidden.add(k.kind);
            getView().setFilters({ hidden: new Set(hidden) });
            updateHud();
        };
        const sw = document.createElement('i');
        sw.style.background = `#${k.color.toString(16).padStart(6, '0')}`;
        l.append(cb, sw, k.label);
        kindsEl.append(l);
    }
    $<HTMLInputElement>('lv-search').oninput = (e) =>
        getView().setFilters({ search: (e.target as HTMLInputElement).value });
    $<HTMLInputElement>('lv-class').oninput = (e) =>
        getView().setFilters({ cls: (e.target as HTMLInputElement).value.replace(/^0x/i, '') });

    // ---- toolbar ----
    btn('lv-undo').onclick = () => getView().undo();
    btn('lv-redo').onclick = () => getView().redo();
    btn('lv-dup').onclick = () => getView().duplicateSelected();
    btn('lv-del').onclick = () => getView().deleteSelected();
    btn('lv-frame').onclick = () => getView().frameSelection();
    const setMode = (m: 'translate' | 'rotate'): void => {
        getView().setMode(m);
        btn('lv-move').classList.toggle('active', m === 'translate');
        btn('lv-rot').classList.toggle('active', m === 'rotate');
    };
    btn('lv-move').onclick = () => setMode('translate');
    btn('lv-rot').onclick = () => setMode('rotate');
    btn('lv-save').onclick = () => {
        download(`${levelName}.level.json`, JSON.stringify(getView().doc, null, 1), 'application/json');
    };
    btn('lv-export').onclick = () => {
        void exportMod().catch((e: unknown) => host.setStatus(e instanceof Error ? e.message : String(e), true));
    };

    async function exportMod(): Promise<void> {
        const path = host.currentFile();
        if (!path) {
            host.setStatus(
                'Export mod: open an SRSC file in the Inspector tab first (provisional self-test export).',
                true,
            );
            return;
        }
        const rt = await host.call('exportRoundTrip', path, 8);
        if (!rt.roundTrips) throw new Error('SRSC writer self-test failed; refusing to export');
        const zip = buildModZip({
            name: `${levelName} (round-trip self-test)`,
            description: 'PROVISIONAL: unchanged entries rewritten byte-for-byte; no level edits are encoded yet.',
            entries: rt.entries.map((e) => ({ file: rt.file, entry: e, data: e.data })),
        });
        download(`${levelName}.mod.zip`, zip as BlobPart, 'application/zip');
        host.setStatus(`Exported provisional mod: ${rt.entries.length} round-tripped entries from ${rt.file}.`);
    }

    // ---- property panel ----
    function renderProps(): void {
        if (!view) return;
        const ids = [...view.selection];
        btn('lv-dup').disabled = btn('lv-del').disabled = ids.length === 0;
        if (ids.length === 0) {
            props.textContent = 'Select an object.';
            return;
        }
        if (ids.length > 1) {
            props.textContent = `${ids.length} objects selected. Use the gizmo, Duplicate or Delete.`;
            return;
        }
        const o = view.doc.get(ids[0]!);
        if (!o) return;
        const t = document.createElement('table');
        const row = (label: string, ...cells: (HTMLElement | string)[]): void => {
            const tr = t.insertRow();
            tr.insertCell().textContent = label;
            const c = tr.insertCell();
            c.append(...cells);
        };
        row('id', String(o.id));
        row('kind', `${o.kind} (type ${o.type})`);
        row('class', o.class);
        row('resource', o.resource);
        row('model', o.model);
        const numIn = (field: 'pos' | 'rot', axis: number, scale: number): HTMLInputElement => {
            const i = document.createElement('input');
            i.type = 'number';
            i.step = field === 'pos' ? '10' : '1';
            i.value = (o.transform[field][axis]! * scale).toFixed(field === 'pos' ? 1 : 2);
            i.onchange = () => view?.setTransformField(o.id, field, axis, Number(i.value) / scale);
            return i;
        };
        for (const [a, n] of ['x', 'y', 'z'].entries()) row(`pos ${n}`, numIn('pos', a, 1));
        row('yaw (deg)', numIn('rot', 1, 180 / Math.PI));
        for (const [k, v] of Object.entries(o.props)) row(k, propInput(o, k, v));
        props.replaceChildren(t);
    }

    function propInput(o: LevelObject, k: string, v: string | number | boolean): HTMLInputElement {
        const i = document.createElement('input');
        i.type = 'text';
        i.value = String(v);
        i.onchange = () => {
            const n = Number(i.value);
            const val = typeof v === 'number' && i.value.trim() !== '' && Number.isFinite(n) ? n : i.value;
            view?.setProp(o.id, k, val);
        };
        return i;
    }

    async function loadFile(f: File): Promise<void> {
        const text = await f.text();
        const j = JSON.parse(text) as { format?: string };
        const doc =
            j.format === 'drakan-level-document'
                ? LevelDocument.fromJSON(text)
                : LevelDocument.fromWorld(parseWorldDump(text));
        levelName = doc.level || f.name.replace(/\.json$/i, '');
        const v = getView();
        v.setDocument(doc);
        host.setStatus(`${f.name}: ${doc.size} objects in "${doc.level}".`);
    }
    const onFile = (e: Event): void => {
        const i = e.target as HTMLInputElement;
        const f = i.files?.[0];
        i.value = '';
        if (f)
            loadFile(f).catch((err: unknown) => host.setStatus(err instanceof Error ? err.message : String(err), true));
    };
    $<HTMLInputElement>('lv-dump').onchange = onFile;
    $<HTMLInputElement>('lv-load').onchange = onFile;

    return { view: getView, loadFile };
}

import {
    LevelDocument,
    ModError,
    SetScaleCommand,
    compareWithWorld,
    diffDocument,
    documentFromLvl,
    parseWorldDump,
    type EntityKind,
    type LevelObject,
    type ModMeta,
    type WorldComparison,
} from '@drakan/editor-core';
import type { DiscProfile } from '@drakan/formats';
import { KINDS, LevelView } from './level.ts';
import { buildLevelPack, canWriteFolder, writePackToModsFolder, zipPack } from './modio.ts';
import type { Api, LevelInfo, LevelLoadResult } from './rpc.ts';

const $ = <T extends HTMLElement>(id: string): T => {
    const el = document.getElementById(id);
    if (!el) throw new Error(`missing #${id}`);
    return el as T;
};

export interface LevelUiHost {
    call<M extends 'loadLevel'>(method: M, ...args: Parameters<Api[M]>): Promise<ReturnType<Api[M]>>;
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
export interface DiscContext {
    isoName: string;
    levels: LevelInfo[];
    profile: DiscProfile | null;
}

export interface LevelUi {
    view: () => LevelView;
    loadFile(f: File): Promise<void>;
    setDisc(d: DiscContext | null): void;
    openLevel(dirPath: string): Promise<void>;
}

export function initLevelUi(host: LevelUiHost): LevelUi {
    const props = $('lv-props');
    const hud = $('lv-hud');
    const empty = $('lv-empty');
    const btn = (id: string): HTMLButtonElement => $<HTMLButtonElement>(id);
    let view: LevelView | null = null;
    let levelName = 'level';
    let disc: DiscContext | null = null;
    /** The level the document was loaded from (LVL source); null for a world dump or a saved document. */
    let src: LevelLoadResult | null = null;
    let worldDoc: LevelDocument | null = null;
    let comparison: WorldComparison | null = null;

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
        refreshExport();
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
    // ---- open from disc ----
    const levelsEl = $('lv-levels');
    const fail = (e: unknown): void => host.setStatus(e instanceof Error ? e.message : String(e), true);

    function setDisc(d: DiscContext | null): void {
        disc = d;
        $('lv-open-info').textContent = d ? d.isoName : 'open an ISO in the Inspector first';
        levelsEl.replaceChildren(
            ...(d?.levels ?? []).map((l) => {
                const li = document.createElement('li');
                li.textContent = l.name;
                li.classList.toggle('missing', !l.present);
                li.title = l.present ? l.dirPath : `${l.dirPath} is listed in LEVELLST.TXT but not on this disc`;
                li.onclick = () => {
                    if (!l.present) return;
                    levelsEl.querySelectorAll('li.sel').forEach((x) => x.classList.remove('sel'));
                    li.classList.add('sel');
                    openLevel(l.dirPath).catch(fail);
                };
                return li;
            }),
        );
        renderTarget();
        refreshExport();
    }

    async function openLevel(dirPath: string): Promise<void> {
        const info = disc?.levels.find((l) => l.dirPath === dirPath);
        host.setStatus(`Loading ${info?.name ?? dirPath}...`);
        const r = await host.call('loadLevel', dirPath);
        const { doc, unresolved } = documentFromLvl(r.lvl, r.odbs, info?.name ?? r.file);
        src = r;
        levelName = r.file.replace(/\.lvl$/i, '');
        const v = getView();
        v.setDocument(doc);
        const idEl = $<HTMLInputElement>('lv-mod-id');
        if (!idEl.dataset.touched) idEl.value = `editor.${levelName.toLowerCase()}-edit`;
        const nameEl = $<HTMLInputElement>('lv-mod-name');
        if (!nameEl.dataset.touched) nameEl.value = `${info?.name ?? levelName} edit`;
        updateOverlay();
        host.setStatus(
            `${info?.name ?? r.file}: ${doc.size} objects from ${r.file}` +
                (unresolved ? `, ${unresolved} with unresolved class names` : '') +
                (r.missingOdbs.length ? `; missing ODBs: ${r.missingOdbs.join(', ')}` : '') +
                '.',
        );
    }

    /** Pair the disc level with a loaded world dump and draw the unmatched dump entities as outlines. */
    function updateOverlay(): void {
        const v = getView();
        const row = $('lv-ghost-row');
        if (!src || !worldDoc) {
            comparison = null;
            row.hidden = true;
            v.setGhost([]);
            return;
        }
        comparison = compareWithWorld(v.doc, worldDoc);
        row.hidden = false;
        const matched = new Set(comparison.pairs.values());
        const show = $<HTMLInputElement>('lv-ghost').checked;
        v.setGhost(show ? [...worldDoc.objects()].filter((o) => !matched.has(o.id)) : []);
    }
    $<HTMLInputElement>('lv-ghost').onchange = () => updateOverlay();

    // ---- export ----
    const textOf = (id: string): string => $<HTMLInputElement>(id).value.trim();
    for (const id of ['lv-mod-id', 'lv-mod-name']) {
        $<HTMLInputElement>(id).oninput = (e) => ((e.target as HTMLInputElement).dataset.touched = '1');
    }

    function target(): ModMeta['target'] | null {
        const p = disc?.profile;
        if (!p || (!p.profile && !p.hash)) return null;
        return { ...(p.profile ? { profile: p.profile } : {}), ...(p.hash ? { hash: p.hash } : {}) };
    }

    function renderTarget(): void {
        const p = disc?.profile;
        $('lv-mod-target').textContent = p
            ? `Target: ${p.serial}, profile ${p.profile ?? 'unknown'}, hash ${p.hash ?? 'unknown'}`
            : 'Target: open a disc image to derive the disc profile and hash.';
    }

    function meta(): ModMeta {
        const t = target();
        if (!t) throw new ModError('The disc profile is unknown; open the ISO in the Inspector first.');
        return {
            id: textOf('lv-mod-id'),
            name: textOf('lv-mod-name') || undefined,
            version: textOf('lv-mod-version') || undefined,
            author: textOf('lv-mod-author') || undefined,
            description: `Level edits to ${src?.file ?? 'a level'} made with the Drakan editor.`,
            target: t,
        };
    }

    function refreshExport(): void {
        const d = src && view ? diffDocument(src.lvl.list, view.doc) : null;
        const n = d ? d.moved.length + d.rotated.length + d.scaled.length + d.added.length + d.deleted.length : 0;
        const ready = n > 0 && target() !== null;
        btn('lv-export').disabled = !ready;
        btn('lv-export-dir').disabled = !ready || !canWriteFolder();
        $('lv-mod-info').textContent = d
            ? `${d.moved.length} moved, ${d.rotated.length} rotated, ${d.scaled.length} scaled, ${d.added.length} added, ${d.deleted.length} deleted`
            : 'open a level from the disc to export edits';
    }

    async function exportMod(toFolder: boolean): Promise<void> {
        if (!src || !view) throw new ModError('Open a level from the disc first.');
        const pack = buildLevelPack(src, view.doc, meta());
        const e = pack.entry;
        const lines = [
            `overrides/${e.file}/${e.type.toString(16).padStart(4, '0')}_${e.id}_${e.sub}.bin: ${e.bytes} bytes (was ${e.originalBytes})`,
            ...pack.warnings,
        ];
        if (toFolder) {
            const where = await writePackToModsFolder(pack);
            if (where === null) return;
            lines.push(`Written to ${where} and enabled in mods.ini.`);
            host.setStatus(`Mod '${pack.meta.id}' written to ${where}.`);
        } else {
            download(`${pack.meta.id}.zip`, zipPack(pack) as BlobPart, 'application/zip');
            lines.push(`Downloaded ${pack.meta.id}.zip. Put it in the game's mods/ folder and set it on in mods.ini.`);
            host.setStatus(`Exported ${pack.meta.id}.zip.`);
        }
        $('lv-mod-report').textContent = lines.join('\n');
    }
    btn('lv-export').onclick = () => void exportMod(false).catch(fail);
    btn('lv-export-dir').onclick = () => void exportMod(true).catch(fail);

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
        if (o.origin !== undefined) {
            row('rot x (deg)', numIn('rot', 0, 180 / Math.PI));
            row('rot z (deg)', numIn('rot', 2, 180 / Math.PI));
        }
        if (o.scale) {
            for (const [a, n] of ['x', 'y', 'z'].entries()) {
                const i = document.createElement('input');
                i.type = 'number';
                i.step = '0.1';
                i.value = String(o.scale[a]);
                i.onchange = () => {
                    const next = [...o.scale!] as [number, number, number];
                    next[a] = Number(i.value);
                    if (Number.isFinite(next[a])) view?.run(new SetScaleCommand(o.id, next));
                };
                row(`scale ${n}`, i);
            }
        }
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
        const v = getView();
        if (j.format === 'drakan-level-document') {
            const doc = LevelDocument.fromJSON(text);
            src = null;
            levelName = doc.level || f.name.replace(/\.json$/i, '');
            v.setDocument(doc);
            updateOverlay();
            host.setStatus(
                `${f.name}: ${doc.size} objects in "${doc.level}" (saved document, no LVL source: export disabled).`,
            );
            return;
        }
        const world = LevelDocument.fromWorld(parseWorldDump(text));
        worldDoc = world;
        if (src) {
            updateOverlay();
            const c = comparison!;
            host.setStatus(
                `${f.name}: ${world.size} live entities overlaid on the disc level: ${c.matched} matched, ${c.lvlOnly} level objects not live, ${c.worldOnly} entities not in the list.`,
            );
        } else {
            levelName = world.level || f.name.replace(/\.json$/i, '');
            v.setDocument(world);
            host.setStatus(`${f.name}: ${world.size} objects in "${world.level}".`);
        }
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

    renderTarget();
    return { view: getView, loadFile, setDisc, openLevel };
}

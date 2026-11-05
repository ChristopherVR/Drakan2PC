// "Import Order of the Flame levels": pick the player's own OotF install folder, choose levels and a PS2 template level,
// convert (packages/formats convert.ts) and save the result as a mod pack for the PS2 port. Everything runs in the page;
// the OotF files are read from the folder the player picks and nothing is uploaded.

import { buildOotfPack, ootfArenaFromPc, zipOotfPack, type ModMeta } from '@drakan/editor-core';
import {
    SrscReader,
    ps2BlobFor,
    convertOotfLevels,
    findPcLevels,
    loadPcLevel,
    matchClass,
    CELL_SIZE,
    type PcLevel,
    type ClassTables,
    parseLevelHeader,
    pcLevelName,
    ps2ClassTables,
    type PcFileSource,
} from '@drakan/formats';
import { ootfDiscContext } from './ootfdisc.ts';
import type { Api, Method } from '../rpc.ts';
import { h } from './dom.ts';
import { registerCommand } from './registry.ts';
import { canWriteFolder, writePackToModsFolder } from '../modio.ts';

type Call = <M extends Method>(method: M, ...args: Parameters<Api[M]>) => Promise<ReturnType<Api[M]>>;

export interface OotfHost {
    call: Call;
    /** PS2 levels of the open disc (empty when no disc is open). */
    levels(): { name: string; dirPath: string }[];
    toast(msg: string, kind?: 'info' | 'error' | 'ok' | 'warn'): void;
    /** Disc profile id for the mod manifest (`pal` / `usa`), or null. */
    profile(): string | null;
}

const STYLE = `
dialog.ootf-dlg { background: var(--surface-2); color: var(--fg); border: 1px solid var(--border-strong);
  border-radius: var(--r-lg); padding: 0; width: min(760px, 94vw); max-height: 88vh; font: var(--fs-md) var(--font); }
dialog.ootf-dlg::backdrop { background: rgba(0,0,0,.55); }
.ootf-dlg .od-head { padding: var(--sp-4) var(--sp-5); border-bottom: 1px solid var(--border); font-size: var(--fs-lg); }
.ootf-dlg .od-body { padding: var(--sp-4) var(--sp-5); display: grid; gap: var(--sp-3); overflow: auto; max-height: 62vh; }
.ootf-dlg .od-foot { padding: var(--sp-3) var(--sp-5); border-top: 1px solid var(--border); display: flex; gap: var(--sp-2); justify-content: flex-end; }
.ootf-dlg .od-row { display: flex; gap: var(--sp-3); align-items: center; flex-wrap: wrap; }
.ootf-dlg .od-list { border: 1px solid var(--border); border-radius: var(--r-md); background: var(--surface-1); max-height: 240px; overflow: auto; }
.ootf-dlg .od-list label { display: flex; gap: var(--sp-2); padding: 3px var(--sp-3); }
.ootf-dlg .od-list label:hover { background: var(--surface-3); }
.ootf-dlg .od-dim { color: var(--fg-muted); font-size: var(--fs-sm); }
.ootf-dlg .od-bad { color: var(--bad); }
.ootf-dlg table { border-collapse: collapse; width: 100%; font-size: var(--fs-sm); }
.ootf-dlg td, .ootf-dlg th { text-align: left; padding: 2px var(--sp-3); border-bottom: 1px solid var(--border); }
.ootf-dlg select, .ootf-dlg input[type=text] { background: var(--field); color: var(--fg); border: 1px solid var(--border);
  border-radius: var(--r-sm); height: var(--h-ctl); padding: 0 var(--sp-2); }
`;

/** A `PcFileSource` over the files of a folder picker; the top folder name is dropped from every path. */
export function folderSource(files: FileList | File[]): PcFileSource {
    const byLower = new Map<string, File>();
    const paths: string[] = [];
    for (const f of Array.from(files)) {
        const rel = (f as File & { webkitRelativePath?: string }).webkitRelativePath || f.name;
        const parts = rel.split('/');
        const p = parts.length > 1 ? parts.slice(1).join('/') : rel;
        byLower.set(p.toLowerCase(), f);
        paths.push(p);
    }
    return {
        read: async (p) => {
            const f = byLower.get(p.toLowerCase());
            return f ? new Uint8Array(await f.arrayBuffer()) : null;
        },
        list: async () => paths,
    };
}

/**
 * Top-down overview of a PC level: floor layers shaded by height, objects as dots (green = a PS2 class of the same name
 * exists, orange = left out by the converter). Cheap enough to redraw for the biggest OotF levels (512 x 512 cells).
 */
export function drawOverview(
    canvas: HTMLCanvasElement,
    pc: PcLevel,
    ps2: ClassTables | null,
): { mapped: number; total: number } {
    const size = canvas.width;
    const g = canvas.getContext('2d')!;
    g.fillStyle = '#14161a';
    g.fillRect(0, 0, size, size);
    let minX = Infinity,
        minZ = Infinity,
        maxX = -Infinity,
        maxZ = -Infinity,
        lo = Infinity,
        hi = -Infinity;
    for (const L of pc.terrain) {
        minX = Math.min(minX, L.originX);
        minZ = Math.min(minZ, L.originZ);
        maxX = Math.max(maxX, L.originX + L.width);
        maxZ = Math.max(maxZ, L.originZ + L.height);
        for (const y of L.heights) {
            lo = Math.min(lo, y);
            hi = Math.max(hi, y);
        }
    }
    if (!Number.isFinite(minX)) return { mapped: 0, total: pc.list.objects.length };
    const span = Math.max(maxX - minX, maxZ - minZ, 1);
    const k = (size - 4) / span;
    const px = (cx: number): number => 2 + (cx - minX) * k;
    const pz = (cz: number): number => 2 + (cz - minZ) * k;
    for (const L of [...pc.terrain].sort((a, b) => a.type - b.type)) {
        const stride = L.width + 1;
        for (let z = 0; z < L.height; z++) {
            for (let x = 0; x < L.width; x++) {
                const y = L.heights[z * stride + x]!;
                const t = hi > lo ? (y - lo) / (hi - lo) : 0.5;
                const v = Math.round(40 + t * 150);
                g.fillStyle = L.type === 0 ? `rgb(${v},${v},${v})` : `rgba(${v},${v},${Math.min(255, v + 40)},0.35)`;
                g.fillRect(px(L.originX + x), pz(L.originZ + z), Math.max(1, k), Math.max(1, k));
            }
        }
    }
    let mapped = 0;
    const seen = new Map<string, boolean>();
    for (const o of pc.list.objects) {
        const key = `${o.dbIndex}:${o.classId}`;
        let ok = seen.get(key);
        if (ok === undefined) {
            const c = pc.classes.get(o.dbIndex)?.get(o.classId);
            ok = !!(c && ps2 && matchClass(c, ps2));
            seen.set(key, ok);
        }
        if (ok) mapped++;
        g.fillStyle = ok ? '#5fd38d' : '#f0a23c';
        g.fillRect(px(o.pos[0] / CELL_SIZE) - 1, pz(o.pos[2] / CELL_SIZE) - 1, 3, 3);
    }
    return { mapped, total: pc.list.objects.length };
}

export function registerOotfImport(host: OotfHost): void {
    registerCommand({
        id: 'file.importOotf',
        title: 'Import Order of the Flame levels...',
        category: 'File',
        icon: 'download',
        hint: 'Convert levels from your own Drakan: Order of the Flame install into a mod for this game',
        keywords: 'ootf pc convert import levels campaign multiplayer',
        run: () => openDialog(host),
    });
}

interface Row {
    path: string;
    title: string;
    box: HTMLInputElement;
}

function openDialog(host: OotfHost): void {
    if (!document.getElementById('ootf-style')) {
        const st = document.createElement('style');
        st.id = 'ootf-style';
        st.textContent = STYLE;
        document.head.append(st);
    }
    const ps2 = host.levels();
    // Keep the shared dropdown popup inside the modal's top layer (body siblings are inert and paint below it).
    const dlg = h('dialog', { class: 'ootf-dlg', 'data-dd-host': '' });
    const body = h('div', { class: 'od-body' });
    const foot = h('div', { class: 'od-foot' });
    dlg.append(h('div', { class: 'od-head' }, 'Import Order of the Flame levels'), body, foot);
    document.body.append(dlg);
    dlg.addEventListener('close', () => dlg.remove());

    let src: PcFileSource | null = null;
    let rows: Row[] = [];

    const close = h('button', { class: 'btn', type: 'button', onclick: () => dlg.close() }, 'Close');
    const go = h('button', { class: 'btn primary', type: 'button', disabled: true }, 'Convert');
    foot.append(close, go);

    const intro = h(
        'div',
        { class: 'od-dim' },
        'Convert levels from your own Order of the Flame install. Nothing is uploaded. Objects without a matching class are left out; the report lists them.',
    );
    const picker = h('input', { type: 'file', webkitdirectory: true, multiple: true, hidden: true });
    const folderLabel = h('span', { class: 'od-dim' }, 'No folder chosen');
    const pickBtn = h(
        'button',
        { class: 'btn', type: 'button', onclick: () => picker.click() },
        'Choose install folder...',
    );
    const tplSel = h('select', {}, ...ps2.map((l) => h('option', { value: l.dirPath }, l.name)));
    const prefix = h('input', { type: 'text', value: 'OotF - ', spellcheck: 'false' });
    const list = h('div', { class: 'od-list' });
    const out = h('div', {});
    const overview = h('canvas', {
        width: 480,
        height: 480,
        style: 'width:100%;max-width:480px;border-radius:6px;display:none',
    });
    const overviewNote = h('div', { class: 'od-dim' });
    let tplClasses: ClassTables | null = null;
    tplSel.addEventListener('change', () => (tplClasses = null));
    async function preview(path: string): Promise<void> {
        overviewNote.textContent = 'Loading...';
        try {
            if (!tplClasses && ps2.length) {
                const t = await host.call('loadLevel', tplSel.value);
                tplClasses = ps2ClassTables(t.lvl.header, t.odbs);
            }
            const pc = await loadPcLevel(src!, path);
            const r = drawOverview(overview, pc, tplClasses);
            overview.style.display = 'block';
            overviewNote.textContent =
                `${pcLevelName(pc.header)}: ${pc.list.objects.length} objects, ${pc.terrain.length} layers` +
                (tplClasses
                    ? `, ${r.mapped} have a same-named class in the template (green), the rest are left out (orange)`
                    : '');
        } catch (e) {
            overviewNote.textContent = e instanceof Error ? e.message : String(e);
        }
    }
    const warn = !ps2.length
        ? h('div', { class: 'od-bad' }, 'Open your Drakan disc image first: its levels are the conversion templates.')
        : null;

    body.append(
        intro,
        ...(warn ? [warn] : []),
        h('div', { class: 'od-row' }, pickBtn, folderLabel, picker),
        h(
            'div',
            { class: 'od-row' },
            h('span', {}, 'Template level'),
            tplSel,
            h('span', { class: 'od-dim' }, 'its dependencies and extra data are reused'),
        ),
        h('div', { class: 'od-row' }, h('span', {}, 'Title prefix'), prefix),
        list,
        overview,
        overviewNote,
        out,
    );

    const refreshGo = (): void => {
        go.disabled = !src || !ps2.length || !rows.some((r) => r.box.checked);
    };

    picker.addEventListener('change', () => {
        if (!picker.files?.length) return;
        src = folderSource(picker.files);
        void (async () => {
            list.replaceChildren(h('div', { class: 'od-dim', style: 'padding:6px 12px' }, 'Reading levels...'));
            const paths = await findPcLevels(src!);
            rows = [];
            for (const p of paths) {
                let title = p.replace(/^.*\//, '').replace(/\.lvl$/i, '');
                try {
                    const raw = await src!.read(p);
                    if (raw) {
                        const r = await SrscReader.fromBytes(raw);
                        if (r.platform !== 'pc') continue;
                        const e = r.entries.find((x) => x.type === 0);
                        if (e) title = pcLevelName(parseLevelHeader(await r.readRaw(e))) || title;
                    }
                } catch {
                    continue;
                }
                const box = h('input', { type: 'checkbox' });
                box.addEventListener('change', refreshGo);
                rows.push({ path: p, title, box });
            }
            folderLabel.textContent = rows.length
                ? `${rows.length} level${rows.length === 1 ? '' : 's'} found`
                : 'No Order of the Flame levels found in that folder';
            list.replaceChildren(
                ...rows.map((r) =>
                    h(
                        'label',
                        {},
                        r.box,
                        h(
                            'span',
                            {
                                title: 'Click to preview',
                                style: 'cursor:pointer',
                                onclick: (ev: Event) => {
                                    ev.preventDefault();
                                    void preview(r.path);
                                },
                            },
                            r.title,
                        ),
                        h('span', { class: 'od-dim' }, r.path),
                    ),
                ),
            );
            refreshGo();
        })();
    });

    go.addEventListener('click', () => {
        go.disabled = true;
        out.replaceChildren(h('div', { class: 'od-dim' }, 'Converting...'));
        void convertSelected().catch((e: unknown) => {
            out.replaceChildren(h('div', { class: 'od-bad' }, e instanceof Error ? e.message : String(e)));
            refreshGo();
        });
    });

    async function convertSelected(): Promise<void> {
        const tplDir = tplSel.value;
        const tplBytes = await host.call('readLevelFile', tplDir);
        const template = await SrscReader.fromBytes(tplBytes);
        const loaded = await host.call('loadLevel', tplDir);
        const { context, splats } = await ootfDiscContext(template, loaded.lvl.header, tplDir, (path) =>
            host.call('readStoryFile', path),
        );
        const selected = rows.filter((r) => r.box.checked);
        const result = await convertOotfLevels(
            src!,
            selected.map((r) => r.path),
            context,
            {
                name: (pc) => `${prefix.value}${pcLevelName(pc.header)}`.slice(0, 60),
                log: (message) => {
                    out.textContent = message;
                },
            },
        );
        const failed = result.stages.filter((stage) => !stage.ok);
        if (failed.length)
            throw new Error(`Conversion failed: ${failed.map((s) => `${s.stage}: ${s.error}`).join('; ')}`);
        const inputs = [];
        const lines: HTMLElement[] = [];
        for (const level of result.levels) {
            const pc = await loadPcLevel(src!, level.pcPath);
            const R = level.parts.report;
            lines.push(
                h(
                    'tr',
                    {},
                    h('td', {}, level.title),
                    h('td', {}, `${R.objectsOut} / ${R.objectsIn}`),
                    h('td', {}, String(R.layers)),
                    h('td', {}, `${R.unmappedClasses.size} classes`),
                ),
            );
            inputs.push({
                title: level.title,
                parts: level.parts,
                splats: level.splats,
                arena: ootfArenaFromPc(pc, level.title) ?? undefined,
            });
        }
        const start = result.levels.find((l) => !/^multiplayer[\\/]/i.test(l.pcPath));
        const meta: Omit<ModMeta, 'levels'> = {
            id: 'ootf-levels',
            name: 'Order of the Flame levels',
            description: 'Levels converted from Drakan: Order of the Flame.',
            target: { profile: host.profile() ?? 'pal' },
            ...(start
                ? { campaign: 'ootf', campaignName: 'Drakan: Order of the Flame', campaignStart: start.title }
                : {}),
        };
        const pack = await buildOotfPack(meta, template, inputs, ps2BlobFor, { splats, extraFiles: result.files });
        const zip = zipOotfPack(pack);
        const save = h(
            'button',
            {
                class: 'btn primary',
                type: 'button',
                onclick: () => {
                    const a = h('a', {
                        href: URL.createObjectURL(new Blob([zip as BlobPart])),
                        download: 'ootf-levels.zip',
                    });
                    a.click();
                    setTimeout(() => URL.revokeObjectURL(a.href), 10_000);
                },
            },
            'Download mod (.zip)',
        );
        const folder = canWriteFolder()
            ? h(
                  'button',
                  {
                      class: 'btn',
                      type: 'button',
                      onclick: () =>
                          void writePackToModsFolder(pack).then((where) => {
                              if (where) host.toast(`Saved to ${where}`, 'ok');
                          }),
                  },
                  'Save to mods folder...',
              )
            : null;
        out.replaceChildren(
            h(
                'table',
                {},
                h(
                    'tr',
                    {},
                    h('th', {}, 'Level'),
                    h('th', {}, 'Objects kept'),
                    h('th', {}, 'Layers'),
                    h('th', {}, 'Left out'),
                ),
                ...lines,
            ),
            ...[...result.warnings, ...pack.warnings].slice(0, 6).map((w) => h('div', { class: 'od-dim' }, w)),
            h('div', { class: 'od-row' }, save, folder),
            h(
                'div',
                { class: 'od-dim' },
                start
                    ? "Install and enable the zip in the game's Mods menu, restart, choose Drakan: Order of the Flame, then New Game."
                    : "Install and enable the zip in the game's Mods menu, restart, then choose the map in Multiplayer.",
            ),
        );
        refreshGo();
    }

    dlg.showModal();
}

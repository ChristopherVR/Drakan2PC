// File > Export > Level for Unreal (glTF): a small options dialog, then the open level is written as a .glb with
// textures (zip download, or the desktop save dialog). Format and import steps: docs/reference/unreal-export.md.

import { zipModFiles } from '@drakan/editor-core';
import { downloadFile } from '../platform.ts';
import { collectLevel, type CollectHost } from '../unreal/collect.ts';
import { flipGreenInBrowser, PbrPack } from '../unreal/pbr.ts';
import {
    buildLevelGltf,
    type TextureSource,
    DEFAULT_EXPORT_OPTIONS,
    type ExportOptions,
    type LevelExport,
} from '../unreal/levelglb.ts';
import { h } from './dom.ts';
import { registerCommand } from './registry.ts';

export interface UnrealExportHost {
    /** The open level's data sources, or null when no disc level is open. */
    collect(): CollectHost | null;
    toast(msg: string, kind?: 'info' | 'error' | 'ok' | 'warn'): void;
}

const STYLE = `
dialog.ue-dlg { background: var(--surface-2); color: var(--fg); border: 1px solid var(--border-strong);
  border-radius: var(--r-lg); padding: 0; width: min(520px, 94vw); font: var(--fs-md) var(--font); }
dialog.ue-dlg::backdrop { background: rgba(0,0,0,.55); }
.ue-dlg .ud-head { padding: var(--sp-4) var(--sp-5); border-bottom: 1px solid var(--border); font-size: var(--fs-lg); }
.ue-dlg .ud-body { padding: var(--sp-4) var(--sp-5); display: grid; gap: var(--sp-3); }
.ue-dlg .ud-foot { padding: var(--sp-3) var(--sp-5); border-top: 1px solid var(--border); display: flex; gap: var(--sp-2); justify-content: flex-end; }
.ue-dlg label { display: flex; gap: var(--sp-2); align-items: center; }
.ue-dlg .ud-dim { color: var(--fg-muted); font-size: var(--fs-sm); }
.ue-dlg select { background: var(--field); color: var(--fg); border: 1px solid var(--border); border-radius: var(--r-sm); height: var(--h-ctl); padding: 0 var(--sp-2); }
`;

/** Build the export (no UI): returns the glb, textures and a report. */
export async function runUnrealExport(
    host: CollectHost,
    options: Partial<ExportOptions>,
    pack: PbrPack | null = null,
): Promise<LevelExport> {
    const opt = { ...DEFAULT_EXPORT_OPTIONS, ...options };
    const input = await collectLevel(host, opt);
    if (pack) {
        const src: TextureSource[] = [];
        for (const t of input.terrain?.textures ?? [])
            src.push({ key: `terrain:${t.ref}`, kind: 'terrain', scope: 'Terrain', ref: t.ref, txd: t.txd });
        for (const c of input.classes)
            for (const t of c.textures)
                src.push({ key: `class:${c.resource}`, kind: 'model', scope: c.name, ref: t.ref, txd: t.txd });
        await pack.prepare(src);
        opt.resolver = pack.resolver();
    }
    host.status('Writing glTF...');
    return buildLevelGltf(input, opt);
}

/** The files of an export as a zip: `<name>.glb`, `textures/*.png`, and the import script when it can be fetched. */
export async function zipUnrealExport(name: string, out: LevelExport): Promise<Uint8Array> {
    const files: Record<string, Uint8Array> = { [`${name}.glb`]: out.glb };
    for (const [p, b] of out.files) files[p] = b;
    files['export_report.json'] = new TextEncoder().encode(JSON.stringify(out.report, null, 2));
    try {
        const r = await fetch('ue5/drakan_import.py');
        if (r.ok) files['drakan_import.py'] = new Uint8Array(await r.arrayBuffer());
    } catch {
        /* the script is also in the repository, editor/packages/app/public/ue5 */
    }
    return zipModFiles(files);
}

function checkbox(id: string, on: boolean): HTMLInputElement {
    const i = h('input', { type: 'checkbox', id }) as HTMLInputElement;
    i.checked = on;
    return i;
}

function openDialog(host: UnrealExportHost): void {
    const ctx = host.collect();
    if (!ctx) {
        host.toast('Open a level first.', 'warn');
        return;
    }
    if (!document.getElementById('ue-dlg-style')) {
        const s = document.createElement('style');
        s.id = 'ue-dlg-style';
        s.textContent = STYLE;
        document.head.append(s);
    }
    const motion = h(
        'select',
        { id: 'ue-motion' },
        h('option', { value: 'separate' }, 'Separate track (recommended)'),
        h('option', { value: 'strip' }, 'Stripped (play in place)'),
        h('option', { value: 'keep' }, 'Kept in the skeleton'),
    );
    const chTerrain = checkbox('ue-terrain', true);
    const chModels = checkbox('ue-models', true);
    const chSkin = checkbox('ue-skinned', true);
    const chMarkers = checkbox('ue-markers', true);
    const chInst = checkbox('ue-instancing', false);
    const chEmbed = checkbox('ue-embed', false);
    const packInput = h('input', {
        type: 'file',
        id: 'ue-pack',
        webkitdirectory: true,
        multiple: true,
    }) as HTMLInputElement;
    const status = h('div', { class: 'ud-dim', id: 'ue-status' }, 'Creates a .glb plus PNG textures in a zip.');
    const goBtn = h('button', { class: 'ui-btn primary', type: 'button', id: 'ue-go' }, 'Export');
    const cancel = h('button', { class: 'ui-btn', type: 'button' }, 'Close');
    const row = (c: HTMLInputElement, label: string): HTMLElement => h('label', {}, c, label);
    const dlg = h(
        'dialog',
        { class: 'ue-dlg', id: 'ue-dialog' },
        h('div', { class: 'ud-head' }, 'Export level for Unreal (glTF)'),
        h(
            'div',
            { class: 'ud-body' },
            h(
                'div',
                { class: 'ud-dim' },
                `${ctx.level}: glTF is Y-up metres; Unreal imports it Z-up in centimetres (docs/reference/unreal-export.md).`,
            ),
            row(chTerrain, 'Terrain (chunked meshes)'),
            row(chModels, 'Static models'),
            row(chSkin, 'Skinned characters with skeleton and animations'),
            row(chMarkers, 'Objects without a model (empty nodes carrying their extras)'),
            row(chInst, 'GPU instancing instead of one node per object'),
            row(chEmbed, 'Embed textures in the .glb'),
            h('label', {}, 'Root motion: ', motion),
            h('div', { class: 'ud-dim' }, 'PBR material pack folder (build/pbr from pbr_pack.py), optional:'),
            packInput,
            status,
        ),
        h('div', { class: 'ud-foot' }, cancel, goBtn),
    );
    document.body.append(dlg);
    dlg.addEventListener('close', () => dlg.remove());
    cancel.addEventListener('click', () => dlg.close());
    goBtn.addEventListener('click', () => {
        const c = host.collect();
        if (!c) return;
        goBtn.setAttribute('disabled', '');
        const say = (m: string): void => {
            status.textContent = m;
            c.status(m);
        };
        const name = `${c.level.replace(/[^A-Za-z0-9_-]+/g, '_')}_unreal`;
        const loadPack = async (fl: FileList): Promise<PbrPack> => {
            const files = new Map<string, Uint8Array>();
            for (const f of Array.from(fl)) {
                const rel = ((f as File & { webkitRelativePath?: string }).webkitRelativePath || f.name).split('/');
                const key = rel.slice(-2).join('/');
                if (/^(Materials|Textures)\//.test(key)) files.set(key, new Uint8Array(await f.arrayBuffer()));
            }
            const p = new PbrPack(files, { flipGreen: flipGreenInBrowser });
            say(`PBR pack: ${p.materials} materials.`);
            return p;
        };
        void (async () => {
            try {
                const pack = packInput.files?.length ? await loadPack(packInput.files) : null;
                const out = await runUnrealExport(
                    { ...c, status: say },
                    {
                        includeTerrain: chTerrain.checked,
                        includeModels: chModels.checked,
                        includeSkinned: chSkin.checked,
                        includeMarkers: chMarkers.checked,
                        instancing: chInst.checked,
                        textures: chEmbed.checked ? 'embedded' : 'external',
                        rootMotion: motion.value as ExportOptions['rootMotion'],
                    },
                    pack,
                );
                const r = out.report;
                const zip = await zipUnrealExport(name, out);
                const hook = window as unknown as { __unrealExport?: unknown };
                hook.__unrealExport = {
                    report: r,
                    bytes: out.glb.length,
                    pbrMissing: pack ? [...pack.missing].length : null,
                };
                const where = await downloadFile(`${name}.zip`, zip as BlobPart, 'application/zip');
                const msg = `${name}.zip: ${r.terrainChunks} terrain chunks, ${r.instances} instances, ${r.skinnedClasses} skeletons, ${r.animations} animations, ${r.textures} textures${r.warnings.length ? `, ${r.warnings.length} warnings` : ''}.`;
                say(msg + (where ? ` Saved to ${where}.` : ''));
                host.toast(msg, r.warnings.length ? 'warn' : 'ok');
            } catch (e) {
                const m = e instanceof Error ? e.message : String(e);
                say(`Export failed: ${m}`);
                host.toast(`Unreal export failed: ${m}`, 'error');
            }
            goBtn.removeAttribute('disabled');
        })();
    });
    dlg.showModal();
}

export function registerUnrealExport(host: UnrealExportHost): void {
    registerCommand({
        id: 'file.exportUnreal',
        title: 'Export > Level for Unreal (glTF)...',
        category: 'File',
        icon: 'download',
        hint: 'Write the open level as a glTF 2.0 .glb with textures, skeletons, animations and per-object extras for an Unreal Engine 5 import script',
        keywords: 'unreal ue5 gltf glb export port',
        run: () => openDialog(host),
    });
}

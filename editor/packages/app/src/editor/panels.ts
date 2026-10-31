import {
    ModError,
    applyDocumentToLvl,
    checkEntrySize,
    diffDocument,
    validateDocument,
    type DocumentDiff,
    type LevelDocument,
    type ModMeta,
    type ValidationIssue,
} from '@drakan/editor-core';
import { writeObjectList } from '@drakan/formats';
import { buildLevelPack, canWriteFolder, changeCount, writePackToModsFolder, zipPack } from '../modio.ts';
import type { Ctx, Panel } from './ctx.ts';
import { fill, h, icon, raf } from './dom.ts';

// ---- bottom dock with tabs -------------------------------------------------------------------------------------

export interface DockTab {
    id: string;
    title: string;
    icon: string;
    panel: Panel;
}

export interface Dock {
    el: HTMLElement;
    show(id: string): void;
    /** Id of the visible tab. */
    active(): string;
    setBadge(id: string, text: string, kind?: 'warn' | 'bad' | ''): void;
}

export function createDock(tabs: DockTab[]): Dock {
    const bar = h('div', { class: 'dock-tabs', role: 'tablist' });
    const content = h('div', { class: 'dock-content' });
    const buttons = new Map<string, HTMLButtonElement>();
    const badges = new Map<string, HTMLElement>();
    let current = tabs[0]!.id;
    const show = (id: string): void => {
        current = id;
        for (const t of tabs) {
            const on = t.id === id;
            buttons.get(t.id)!.classList.toggle('active', on);
            buttons.get(t.id)!.setAttribute('aria-selected', String(on));
            t.panel.el.hidden = !on;
        }
    };
    for (const t of tabs) {
        const badge = h('span', { class: 'badge-n', hidden: true });
        const b = h(
            'button',
            { class: 'dock-tab', type: 'button', role: 'tab', 'data-tab': t.id, onclick: () => show(t.id) },
            icon(t.icon, 14),
            t.title,
            badge,
        );
        buttons.set(t.id, b);
        badges.set(t.id, badge);
        bar.append(b);
        content.append(t.panel.el);
    }
    show(tabs[0]!.id);
    return {
        el: h('div', { class: 'dock' }, bar, content),
        show,
        active: () => current,
        setBadge(id, text, kind = '') {
            const b = badges.get(id);
            if (!b) return;
            b.textContent = text;
            b.hidden = !text;
            b.className = `badge-n ${kind}`;
        },
    };
}

// ---- history ---------------------------------------------------------------------------------------------------

export function createHistory(ctx: Ctx): Panel {
    const list = h('div', { class: 'hist-list' });
    const head = h(
        'div',
        { class: 'panel-tools' },
        h('span', { class: 'dim' }, 'Click a step to jump to that state. Later steps stay redoable until you edit.'),
    );
    const root = h('div', { class: 'panel history' }, head, list);
    function render(): void {
        const { labels, done } = ctx.view().stack.history();
        const rows: HTMLElement[] = [];
        const row = (i: number, text: string): HTMLElement => {
            const r = h(
                'button',
                {
                    class: `hrow${i === done ? ' current' : ''}${i > done ? ' future' : ''}`,
                    type: 'button',
                    onclick: () => ctx.view().stack.goTo(i),
                },
                h('span', { class: 'n' }, String(i)),
                h('span', {}, text),
            );
            return r;
        };
        rows.push(row(0, 'Initial state'));
        labels.forEach((l, i) => rows.push(row(i + 1, l)));
        fill(list, ...rows);
        list.querySelector('.current')?.scrollIntoView({ block: 'nearest' });
    }
    const soon = raf(render);
    return { el: root, onChange: soon, onLoad: soon };
}

// ---- validation ------------------------------------------------------------------------------------------------

export interface ValidationPanel extends Panel {
    issues(): ValidationIssue[];
}

export function createValidation(ctx: Ctx, onCount: (errors: number, warnings: number) => void): ValidationPanel {
    const list = h('div', { class: 'val-list' });
    const summary = h('span', { class: 'dim' });
    const root = h(
        'div',
        { class: 'panel validation' },
        h(
            'div',
            { class: 'panel-tools' },
            summary,
            h('span', { class: 'spacer' }),
            h('button', { class: 'btn', type: 'button', onclick: () => run() }, 'Re-check'),
        ),
        list,
    );
    let issues: ValidationIssue[] = [];
    let timer = 0;
    function run(): void {
        const doc: LevelDocument = ctx.doc();
        const src = ctx.src();
        issues = validateDocument(doc, {
            sourceLevel: src !== null,
            rawIds: src
                ? [
                      ...src.lvl.list.objects.map((o) => o.id),
                      ...[...doc.objects()].filter((o) => o.origin === undefined).map((o) => o.id),
                  ]
                : undefined,
        });
        const errors = issues.filter((i) => i.severity === 'error').length;
        const warnings = issues.length - errors;
        onCount(errors, warnings);
        summary.textContent =
            doc.size === 0
                ? 'No level open.'
                : issues.length
                  ? `${errors} error(s), ${warnings} warning(s)`
                  : 'No problems found.';
        if (!issues.length) {
            fill(
                list,
                h(
                    'div',
                    { class: 'empty-note ok' },
                    icon('check', 18),
                    doc.size
                        ? 'The level passes every check: links, ids, transforms, class names, extent.'
                        : 'Open a level to check it.',
                ),
            );
            return;
        }
        fill(
            list,
            ...issues.slice(0, 500).map((i) =>
                h(
                    'button',
                    {
                        class: `issue ${i.severity}`,
                        type: 'button',
                        title: 'Select and frame',
                        onclick: () => ctx.locate(i.ids),
                    },
                    icon(i.severity === 'error' ? 'warn' : 'warn', 14),
                    h('span', { class: 'msg' }, i.message),
                    h('span', { class: 'dim code' }, i.code),
                ),
            ),
            issues.length > 500 ? h('div', { class: 'more' }, `${issues.length - 500} more`) : null,
        );
    }
    const later = (): void => {
        window.clearTimeout(timer);
        timer = window.setTimeout(run, 400);
    };
    return { el: root, issues: () => issues, onChange: later, onLoad: run };
}

// ---- mod panel -------------------------------------------------------------------------------------------------

export interface ModPanel extends Panel {
    exportMod(toFolder: boolean): Promise<void>;
    /** Fill the id/name fields when a level opens. */
    suggest(id: string, name: string): void;
    values(): { id: string; name: string; version: string; author: string };
    restore(v: Partial<{ id: string; name: string; version: string; author: string }>): void;
}

export interface ModHost {
    target(): ModMeta['target'] | null;
    targetText(): string;
    download(name: string, data: BlobPart, type: string): void;
    setStatus(msg: string, error?: boolean): void;
}

function describeDiff(ctx: Ctx, d: DocumentDiff): { label: string; ids: number[]; tone: string }[] {
    const doc = ctx.doc();
    const name = (id: number): string => {
        const o = doc.get(id);
        return o ? `${o.class} #${id}` : `object ${id}`;
    };
    const out: { label: string; ids: number[]; tone: string }[] = [];
    const add = (verb: string, tone: string, ids: number[]): void => {
        for (const id of ids) out.push({ label: `${verb} ${name(id)}`, ids: doc.has(id) ? [id] : [], tone });
    };
    add(
        'Added',
        'add',
        d.added.map((a) => a.id),
    );
    add('Deleted', 'del', d.deleted);
    add('Moved', 'mod', d.moved);
    add('Rotated', 'mod', d.rotated);
    add('Scaled', 'mod', d.scaled);
    add('Links changed on', 'mod', d.linked);
    add('Fields changed on', 'mod', d.params);
    return out;
}

export function createModPanel(ctx: Ctx, host: ModHost): ModPanel {
    const input = (
        id: string,
        label: string,
        value = '',
        placeholder = '',
    ): { wrap: HTMLElement; el: HTMLInputElement } => {
        const el = h('input', { type: 'text', id, value, placeholder, spellcheck: 'false' });
        return { wrap: h('label', { class: 'mf' }, h('span', {}, label), el), el };
    };
    const idF = input('mod-id', 'Id', '', 'letters, digits . _ -');
    const nameF = input('mod-name', 'Name');
    const verF = input('mod-version', 'Version', '1.0.0');
    const authF = input('mod-author', 'Author');
    for (const f of [idF, nameF]) f.el.addEventListener('input', () => (f.el.dataset.touched = '1'));
    const info = h('div', { class: 'mod-summary' });
    const changes = h('div', { class: 'mod-changes' });
    const report = h('pre', { class: 'mod-report' });
    const play = h('div', { class: 'play' });
    const target = h('div', { class: 'dim small' });
    const zipBtn = h(
        'button',
        { class: 'btn primary', type: 'button', id: 'mod-export', onclick: () => void doExport(false).catch(fail) },
        icon('download', 14),
        'Export zip',
    );
    const dirBtn = h(
        'button',
        {
            class: 'btn',
            type: 'button',
            id: 'mod-export-dir',
            title: "Write into the game's mods folder and enable it in mods.ini",
            onclick: () => void doExport(true).catch(fail),
        },
        icon('open', 14),
        'Write to mods folder',
    );

    const form = h(
        'div',
        { class: 'mod-form' },
        h('div', { class: 'row2' }, idF.wrap, nameF.wrap),
        h('div', { class: 'row2' }, verF.wrap, authF.wrap),
        target,
        h('div', { class: 'btnrow' }, zipBtn, dirBtn),
        report,
    );
    const right = h('div', { class: 'mod-side' }, h('div', { class: 'lbl' }, 'Changed entries'), info, changes);
    const root = h('div', { class: 'panel mod' }, form, right, play);

    const fail = (e: unknown): void => {
        const msg = e instanceof Error ? e.message : String(e);
        report.textContent = msg;
        host.setStatus(msg, true);
        ctx.toast(msg, 'error');
    };
    const values = (): { id: string; name: string; version: string; author: string } => ({
        id: idF.el.value.trim(),
        name: nameF.el.value.trim(),
        version: verF.el.value.trim(),
        author: authF.el.value.trim(),
    });

    function meta(): ModMeta {
        const t = host.target();
        if (!t) throw new ModError('The disc profile is unknown; open the ISO in the Inspector first.');
        const v = values();
        return {
            id: v.id,
            name: v.name || undefined,
            version: v.version || undefined,
            author: v.author || undefined,
            description: `Level edits to ${ctx.src()?.file ?? 'a level'} made with the Drakan editor.`,
            target: t,
        };
    }

    async function doExport(toFolder: boolean): Promise<void> {
        const src = ctx.src();
        if (!src) throw new ModError('Open a level from the disc first.');
        const pack = buildLevelPack(src, ctx.doc(), meta());
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
            ctx.toast(`Written to ${where}`, 'ok');
        } else {
            host.download(`${pack.meta.id}.zip`, zipPack(pack) as BlobPart, 'application/zip');
            lines.push(`Downloaded ${pack.meta.id}.zip.`);
            host.setStatus(`Exported ${pack.meta.id}.zip.`);
            ctx.toast(`Exported ${pack.meta.id}.zip`, 'ok');
        }
        report.textContent = lines.join('\n');
    }

    function renderPlay(ready: boolean): void {
        const v = values();
        const id = v.id || '<mod id>';
        const cmd = `Copy-Item "${id}.zip" "dist\\DrakanPC\\mods\\"\n(Get-Content dist\\DrakanPC\\mods\\mods.ini) -replace '^${id.replace(/[.]/g, '\\.')}\\s*=.*','${id} = on' | Set-Content dist\\DrakanPC\\mods\\mods.ini`;
        fill(
            play,
            h('div', { class: 'lbl' }, icon('play', 14), 'Play in game'),
            h(
                'ol',
                { class: 'steps' },
                h(
                    'li',
                    {},
                    ready
                        ? 'Export the zip, or use Write to mods folder and pick the game folder (the step below is then done).'
                        : 'Make an edit, then export.',
                ),
                h('li', {}, 'Put the zip (or the folder) in ', h('code', {}, 'dist/DrakanPC/mods/'), '.'),
                h(
                    'li',
                    {},
                    'Start the game once so ',
                    h('code', {}, 'mods/mods.ini'),
                    ' lists it, then set ',
                    h('code', {}, `${id} = on`),
                    ' (the Mods page in game settings does the same).',
                ),
                h(
                    'li',
                    {},
                    'Restart the game and start ',
                    h('code', {}, ctx.doc().level || 'the level'),
                    '. The log shows ',
                    h('code', {}, `mods: loaded '${id}'`),
                    '.',
                ),
            ),
            h('pre', { class: 'cmd' }, cmd),
            h(
                'button',
                {
                    class: 'btn',
                    type: 'button',
                    onclick: () =>
                        void navigator.clipboard?.writeText(cmd).then(
                            () => ctx.toast('Command copied', 'ok'),
                            () => ctx.toast('Copy failed', 'error'),
                        ),
                },
                icon('copy', 14),
                'Copy PowerShell',
            ),
        );
    }
    idF.el.addEventListener('input', () => renderPlay(lastReady));
    let lastReady = false;

    let timer = 0;
    function refresh(): void {
        const src = ctx.src();
        target.textContent = host.targetText();
        const d = src ? diffDocument(src.lvl.list, ctx.doc()) : null;
        const n = d ? changeCount(d) : 0;
        const ready = n > 0 && host.target() !== null && !!idF.el.value.trim();
        lastReady = ready;
        zipBtn.disabled = !ready;
        dirBtn.disabled = !ready || !canWriteFolder();
        fill(changes);
        if (!src || !d) {
            fill(
                info,
                h(
                    'span',
                    { class: 'dim' },
                    'Open a level from the disc to export edits. A world dump or saved document has no LVL source.',
                ),
            );
            renderPlay(false);
            return;
        }
        const rows = describeDiff(ctx, d);
        let sizeLine = '';
        let over = false;
        if (n > 0) {
            try {
                const { list } = applyDocumentToLvl(src.lvl.list, ctx.doc());
                const bytes = writeObjectList(list).length;
                const lim = checkEntrySize(src.file, src.objects.size, bytes);
                over = !!lim;
                sizeLine = `${src.file} object list: ${bytes} bytes (was ${src.objects.size})${lim ? ' - over the whole-entry limit' : ''}`;
            } catch (e) {
                sizeLine = e instanceof Error ? e.message : String(e);
                over = true;
            }
        }
        fill(
            info,
            h('strong', {}, n ? `${n} change${n === 1 ? '' : 's'}` : 'No changes yet'),
            h('span', { class: `dim${over ? ' bad' : ''}` }, sizeLine ? ` ${sizeLine}` : ` in ${src.file}`),
        );
        changes.append(
            ...rows.slice(0, 300).map((r) =>
                h(
                    'button',
                    {
                        class: `chg ${r.tone}`,
                        type: 'button',
                        disabled: !r.ids.length,
                        onclick: () => r.ids.length && ctx.locate(r.ids),
                    },
                    r.label,
                ),
            ),
        );
        if (rows.length > 300) changes.append(h('div', { class: 'more' }, `${rows.length - 300} more`));
        renderPlay(ready);
    }
    const later = (): void => {
        window.clearTimeout(timer);
        timer = window.setTimeout(refresh, 250);
    };
    return {
        el: root,
        exportMod: doExport,
        suggest(id, name) {
            if (!idF.el.dataset.touched) idF.el.value = id;
            if (!nameF.el.dataset.touched) nameF.el.value = name;
            refresh();
        },
        values,
        restore(v) {
            if (v.id) idF.el.value = v.id;
            if (v.name) nameF.el.value = v.name;
            if (v.version) verF.el.value = v.version;
            if (v.author) authF.el.value = v.author;
        },
        onChange: later,
        onLoad: refresh,
    };
}

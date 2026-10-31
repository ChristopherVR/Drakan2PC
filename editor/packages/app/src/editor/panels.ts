import {
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
import { buildLevelPack, changeCount, playInGame } from '../modio.ts';
import { isDesktop } from '../platform.ts';
import { createPlayControls } from './playopts.ts';
import type { Ctx, Panel } from './ctx.ts';
import { fill, h, icon, raf } from './dom.ts';
import { projects } from './project/store.ts';

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
    /** Add a tab after creation (`registerPanel`). */
    addTab(tab: DockTab): void;
}

export function createDock(initial: DockTab[]): Dock {
    const tabs = [...initial];
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
    const mount = (t: DockTab): void => {
        const badge = h('span', { class: 'badge-n', hidden: true });
        const b = h(
            'button',
            {
                class: 'dock-tab',
                type: 'button',
                role: 'tab',
                'data-tab': t.id,
                title: t.title,
                'aria-label': t.title,
                onclick: () => show(t.id),
            },
            icon(t.icon, 14),
            h('span', { class: 'dock-tab-label' }, t.title),
            badge,
        );
        buttons.set(t.id, b);
        badges.set(t.id, badge);
        bar.append(b);
        content.append(t.panel.el);
    };
    for (const t of tabs) mount(t);
    show(tabs[0]!.id);

    // Tab order: drag a tab sideways to move it; the order is remembered per dock (by the ids it was created with).
    const orderKey = `dock-order:${initial.map((t) => t.id).join(',')}`;
    const saveOrder = (): void => {
        try {
            localStorage.setItem(
                orderKey,
                JSON.stringify([...bar.children].map((b) => (b as HTMLElement).dataset.tab)),
            );
        } catch {
            /* storage may be unavailable */
        }
    };
    const applyOrder = (): void => {
        let saved: string[] = [];
        try {
            saved = JSON.parse(localStorage.getItem(orderKey) ?? '[]') as string[];
        } catch {
            /* ignore */
        }
        const rank = (b: Element): number => {
            const i = saved.indexOf((b as HTMLElement).dataset.tab ?? '');
            return i < 0 ? saved.length + 1 : i;
        };
        const sorted = [...bar.children].sort((a, b) => rank(a) - rank(b));
        if (sorted.some((b, i) => b !== bar.children[i])) bar.append(...sorted);
    };
    applyOrder();
    let dragged: { btn: HTMLElement; x0: number; moved: boolean } | null = null;
    bar.addEventListener('pointerdown', (e) => {
        const btn = (e.target as HTMLElement).closest<HTMLElement>('.dock-tab');
        if (!btn || e.button !== 0) return;
        dragged = { btn, x0: e.clientX, moved: false };
    });
    window.addEventListener('pointermove', (e) => {
        const d = dragged;
        if (!d) return;
        if (!d.moved && Math.abs(e.clientX - d.x0) < 6) return;
        if (!d.moved) {
            d.moved = true;
            d.btn.classList.add('dragging');
        }
        const others = [...bar.children].filter((b) => b !== d.btn) as HTMLElement[];
        const next = others.find((b) => {
            const r = b.getBoundingClientRect();
            return e.clientX < r.left + r.width / 2;
        });
        if (next) {
            if (d.btn.nextElementSibling !== next) bar.insertBefore(d.btn, next);
        } else if (bar.lastElementChild !== d.btn) bar.append(d.btn);
    });
    window.addEventListener('pointerup', () => {
        const d = dragged;
        dragged = null;
        if (!d?.moved) return;
        d.btn.classList.remove('dragging');
        saveOrder();
        // The click that ends a drag must not switch tabs.
        const stop = (ev: Event): void => ev.stopImmediatePropagation();
        d.btn.addEventListener('click', stop, { capture: true, once: true });
        window.setTimeout(() => d.btn.removeEventListener('click', stop, true), 0);
    });
    // Narrow dock: drop the labels of inactive tabs (icon-only, the tooltip names them); if even that does not fit the
    // strip scrolls sideways (mouse wheel too) and the active tab is kept in view.
    const fit = (): void => {
        bar.classList.remove('compact');
        if (bar.clientWidth > 0 && bar.scrollWidth > bar.clientWidth + 1) bar.classList.add('compact');
        buttons.get(current)?.scrollIntoView({ block: 'nearest', inline: 'nearest' });
    };
    bar.addEventListener(
        'wheel',
        (e) => {
            if (bar.scrollWidth > bar.clientWidth && Math.abs(e.deltaY) > Math.abs(e.deltaX)) {
                bar.scrollLeft += e.deltaY;
                e.preventDefault();
            }
        },
        { passive: false },
    );
    if (typeof ResizeObserver !== 'undefined') new ResizeObserver(fit).observe(bar);
    return {
        addTab(t) {
            if (buttons.has(t.id)) return;
            tabs.push(t);
            mount(t);
            t.panel.el.hidden = true;
            applyOrder();
            fit();
        },
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

/** Extra checks shown in the Validation tab next to the document checks (the multiplayer map mode adds its own). */
export interface ExtraIssue {
    severity: 'error' | 'warning';
    code: string;
    message: string;
    run?: () => void;
}
export interface ValidationProvider {
    issues(): ExtraIssue[];
    /** Called with a refresh function the provider invokes when its issues changed. */
    onChange?(refresh: () => void): void;
}
const validationProviders: ValidationProvider[] = [];
let refreshValidation: (() => void) | null = null;
export function addValidationProvider(p: ValidationProvider): void {
    validationProviders.push(p);
    p.onChange?.(() => refreshValidation?.());
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
        const extra = validationProviders.flatMap((p) => p.issues());
        const errors =
            issues.filter((i) => i.severity === 'error').length + extra.filter((i) => i.severity === 'error').length;
        const warnings = issues.length + extra.length - errors;
        onCount(errors, warnings);
        summary.textContent =
            doc.size === 0
                ? 'No level open.'
                : issues.length + extra.length
                  ? `${errors} error(s), ${warnings} warning(s)`
                  : 'No problems found.';
        if (!issues.length && !extra.length) {
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
            ...extra.map((i) =>
                h(
                    'button',
                    {
                        class: `issue ${i.severity}`,
                        type: 'button',
                        title: 'Select and frame',
                        onclick: () => i.run?.(),
                    },
                    icon('warn', 14),
                    h('span', { class: 'msg' }, i.message),
                    h('span', { class: 'dim code' }, i.code),
                ),
            ),
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
    refreshValidation = later;
    return { el: root, issues: () => issues, onChange: later, onLoad: run };
}

// ---- mod panel -------------------------------------------------------------------------------------------------

/**
 * The Mod tab: what the open level changes and how to play it. Exporting is the one Export dialog (Ctrl+E, the toolbar
 * and File menu); this panel only opens it.
 */
export interface ModPanel extends Panel {
    /** Desktop app: write the pack into the game's mods folder and start the game. */
    play(): Promise<void>;
}

export interface ModHost {
    target(): ModMeta['target'] | null;
    targetText(): string;
    setStatus(msg: string, error?: boolean): void;
    /** Open the Export dialog. */
    exportMod(): void;
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
    const info = h('div', { class: 'mod-summary' });
    const changes = h('div', { class: 'mod-changes' });
    const report = h('pre', { class: 'mod-report' });
    const play = h('div', { class: 'play' });
    const target = h('div', { class: 'dim small' });
    const exportBtn = h(
        'button',
        {
            class: 'btn primary',
            type: 'button',
            id: 'mod-export',
            title: 'Export the mod pack (Ctrl+E)',
            onclick: () => host.exportMod(),
        },
        icon('download', 14),
        'Export...',
    );

    const form = h(
        'div',
        { class: 'mod-form' },
        h('div', { class: 'lbl' }, 'Mod pack'),
        h(
            'p',
            { class: 'dim small' },
            'The pack is built from the project: every project level with changes, its copies and the story edits. Name, version and author are set in the Project tab.',
        ),
        target,
        h('div', { class: 'btnrow' }, exportBtn),
        report,
    );
    const right = h('div', { class: 'mod-side' }, h('div', { class: 'lbl' }, 'Changed entries'), info, changes);
    const statusCard = h('div', { class: 'mod-status', role: 'status' });
    const root = h('div', { class: 'panel mod' }, statusCard, form, right, play);
    /** The last export of this session and how many changes the open level had then (to tell when it is out of date). */
    let lastOut: { where: string; at: Date; changes: number } | null = null;
    let seenExport: unknown = null;
    let lastN = 0;

    const fail = (e: unknown): void => {
        const msg = e instanceof Error ? e.message : String(e);
        report.textContent = msg;
        host.setStatus(msg, true);
        ctx.toast(msg, 'error');
    };
    function meta(): ModMeta {
        const t = host.target();
        if (!t) throw new Error('The disc profile is unknown; open your disc image first.');
        const p = projects.get();
        return {
            id: p?.id || 'editor.level-edit',
            name: p?.name || undefined,
            version: p?.version || undefined,
            author: p?.author || undefined,
            description: `Level edits to ${ctx.src()?.file ?? 'a level'} made with the Drakan editor.`,
            target: t,
        };
    }

    async function doPlay(): Promise<void> {
        const src = ctx.src();
        const d = src ? diffDocument(src.lvl.list, ctx.doc()) : null;
        const edited = !!d && changeCount(d) > 0;
        const pack = edited ? buildLevelPack(src!, ctx.doc(), meta()) : null;
        host.setStatus('Starting the game...');
        const msg = await playInGame(pack, desktopPlay.read(), src ? ctx.doc().level || null : null);
        if (msg === null) return;
        report.textContent = [...(pack?.warnings ?? []), msg].join('\n');
        host.setStatus(msg);
        ctx.toast('Game started', 'ok');
    }
    const desktopPlay = createPlayControls(
        () => void doPlay().catch(fail),
        () => true,
    );

    function renderPlay(ready: boolean): void {
        if (isDesktop) {
            fill(play, desktopPlay.el);
            return;
        }
        const id = projects.get()?.id || '<mod id>';
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
    projects.on(() => renderPlay(lastReady));
    let lastReady = false;

    /** One place that answers: what is edited, what the mod holds, where it was written, how to play it. */
    function paintStatus(n: number, file: string | null): void {
        const cell = (tone: string, title: string, text: string, sub = ''): HTMLElement =>
            h(
                'div',
                { class: `ms-cell ${tone}` },
                h('div', { class: 'ms-t' }, title),
                h('div', { class: 'ms-v' }, text),
                sub ? h('div', { class: 'ms-s dim' }, sub) : null,
            );
        const out = lastOut;
        const stale = !!out && out.changes !== n;
        const id = projects.get()?.id ?? '';
        fill(
            statusCard,
            cell(
                n ? 'warn' : 'ok',
                '1  Edits',
                !file
                    ? 'No level source'
                    : n
                      ? `${n} change${n === 1 ? '' : 's'} not in the game yet`
                      : 'Nothing changed',
                file ? `in ${file}` : 'Open a level from the disc',
            ),
            cell(
                n ? 'info' : 'dim',
                '2  Mod contents',
                n && id ? `${id}: one level override` : n ? 'Create a project first' : 'Empty',
                n ? 'Only the changed level data is written, never game files' : '',
            ),
            cell(
                !out ? 'dim' : stale ? 'warn' : 'ok',
                '3  Written to',
                out ? out.where : 'Not exported yet',
                out
                    ? `${out.at.toLocaleTimeString()}${stale ? ' - edits since then are not in it' : ''}`
                    : 'Press Export',
            ),
            cell(
                !out || stale ? 'dim' : 'ok',
                '4  Play',
                isDesktop
                    ? n || out
                        ? 'Play button runs the game with your edits'
                        : 'Make an edit first'
                    : 'Copy the zip into dist/DrakanPC/mods/',
                isDesktop
                    ? 'Toolbar Play or F5; right-click > Play from here'
                    : 'Steps are listed at the bottom of this panel',
            ),
        );
    }

    let timer = 0;
    function refresh(): void {
        const src = ctx.src();
        target.textContent = host.targetText();
        const le = projects.session.lastExport;
        if (le && le !== seenExport) {
            seenExport = le;
            lastOut = { where: le.where, at: new Date(le.at), changes: lastN };
        }
        const d = src ? diffDocument(src.lvl.list, ctx.doc()) : null;
        const n = d ? changeCount(d) : 0;
        lastN = n;
        paintStatus(n, src?.file ?? null);
        const ready = n > 0 && host.target() !== null;
        lastReady = ready;
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
        play: doPlay,
        onChange: later,
        onLoad: refresh,
    };
}

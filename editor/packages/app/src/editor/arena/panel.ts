import {
    FILTER_REASONS,
    MAX_PLAYERS,
    MODE_LIMITS,
    TEAM_COLORS,
    TEAM_NAMES,
    ADOPT_RADIUS,
    baseFilterReason,
    cloneMode,
    defaultArenaBounds,
    defaultMode,
    DEFAULT_TEAM_COLORS,
    DEFAULT_TEAM_NAMES,
    MODE_TEMPLATES,
    modeGoal,
    modeMapProblem,
    modeSummary,
    teamCount,
    insideBounds,
    overrideIndex,
    parseArenaIni,
    planArenaFilter,
    previewFileName,
    serializeArena,
    serializeMapFile,
    serializeMode,
    slugify,
    suggestSpawns,
    validateArena,
    validateMode,
    type ArenaDraft,
    type ArenaIssue,
    type AssetClass,
    type ClassInfo,
    type FilterPlan,
    type LevelObject,
    type ModMeta,
    type ModeDef,
    type Vec3,
} from '@drakan/editor-core';
import { buildArenaPackFiles, exportBlockers } from '@drakan/editor-core';
import { zipModFiles } from '@drakan/editor-core';
import { writePackToModsFolder } from '../../modio.ts';
import type { Ctx, Panel } from '../ctx.ts';
import { fill, h, icon, raf } from '../dom.ts';
import { openPopover } from '../popover.ts';
import { emptyState, searchField } from '../ui.ts';
import { ArenaOverlay } from './overlay.ts';
import { capturePreview } from './preview.ts';
import type { ArenaSel, ArenaStore, ArenaTool } from './store.ts';

export interface ArenaHost {
    /** The disc's levels (title and folder) for starting a map from any campaign level. */
    levels(): { name: string; dirPath: string }[];
    openLevel(dirPath: string): Promise<void>;
    /** Wait for the terrain and models of the last opened level. */
    visualsReady(): Promise<void>;
    /** Target of the disc, or null when unknown. */
    target(): ModMeta['target'] | null;
    download(name: string, data: BlobPart, type: string): void;
    setStatus(msg: string, error?: boolean): void;
}

export interface ArenaPanel extends Panel {
    /** Issues for the Validation dock tab. */
    issues(): ArenaIssue[];
    /** Select and frame what an issue points at. */
    reveal(issue: ArenaIssue): void;
    overlay: ArenaOverlay;
    /** Be told when the issue list was recomputed (the Validation tab refreshes). */
    onIssues(fn: () => void): void;
}

const hex = (n: number): string => `0x${(n >>> 0).toString(16)}`;
const colorCss = (n: number): string => `#${n.toString(16).padStart(6, '0')}`;
const RES_DB = 3;
const TOOLS: { id: ArenaTool; label: string; icon: string; hint: string }[] = [
    { id: 'select', label: 'Select', icon: 'select', hint: 'Click a spawn, pickup or marker to select and move it' },
    { id: 'spawn', label: 'Spawn', icon: 'character', hint: 'Click the terrain to place a player spawn point' },
    { id: 'pickup', label: 'Pickup', icon: 'item', hint: 'Click the terrain to place a weapon or item pickup' },
    { id: 'flag', label: 'Flag', icon: 'trigger', hint: 'Click to place a flag marker (capture the flag modes)' },
    { id: 'base', label: 'Base', icon: 'building', hint: 'Click to place a team base marker' },
    { id: 'zone', label: 'Zone', icon: 'layers', hint: 'Click to place a capture / hold zone' },
    { id: 'bounds', label: 'Bounds', icon: 'top', hint: 'Drag on the terrain to draw the playable rectangle' },
];

type Tab = 'map' | 'place' | 'world' | 'modes' | 'export';

export function createArenaPanel(ctx: Ctx, st: ArenaStore, host: ArenaHost): ArenaPanel {
    const overlay = new ArenaOverlay(ctx.view(), st);
    let tab: Tab = 'map';
    let plan: FilterPlan | null = null;
    let issues: ArenaIssue[] = [];
    let worldReason = 'all';
    let modeSel = '';
    let preview: { png: Uint8Array; url: string } | null = null;
    let report = '';
    let importText: { source: string; arenas: ArenaDraft[]; modes: ModeDef[] } | null = null;
    let packFields = { id: '', name: '', author: '', version: '1.0.0', touched: false };
    let startName = '';
    let startLevel = '';
    let dirtyPlan = true;
    let startKey = '';
    const startKeyNow = (): string => `${host.levels().length}:${ctx.doc().size}:${ctx.doc().level}`;
    let timer = 0;
    let issueTimer = 0;

    const body = h('div', { class: 'mp-body' });
    const tabsBar = h('div', { class: 'mp-tabs', role: 'tablist' });
    const chip = h('button', { class: 'mp-chip', type: 'button', title: 'Open the Validation tab', onclick: () => {} });
    const title = h('span', { class: 'mp-title' });
    const root = h(
        'div',
        { class: 'panel mp' },
        h(
            'div',
            { class: 'mp-bar' },
            h('span', { class: 'mp-brand' }, icon('rocket', 14), 'Multiplayer map'),
            title,
            h('span', { class: 'spacer' }),
            chip,
        ),
        tabsBar,
        body,
    );

    // ---- environment for the pure checks ----

    const catalogInfo = (): Map<string, ClassInfo> => {
        const m = new Map<string, ClassInfo>();
        for (const c of ctx.catalog()) m.set(c.resource, { rflClass: c.rflClass, kind: c.kind });
        return m;
    };
    let infoMap: Map<string, ClassInfo> | null = null;
    const infoOf = (o: LevelObject): ClassInfo | undefined => (infoMap ??= catalogInfo()).get(o.resource);

    const env = (reach = true): Parameters<typeof validateArena>[1] => {
        const v = ctx.view();
        const items: Vec3[] = [];
        for (const o of v.doc.objects())
            if (baseFilterReason(o, infoOf(o)) === 'loose-item') items.push(o.transform.pos);
        return {
            groundAt: (x, z) => v.groundHeightAt(x, z),
            hasGround: v.hasGround,
            modes: st.modes,
            items,
            reach,
        };
    };

    function recompute(): void {
        if (!st.active) {
            overlay.setPlan(null, new Map());
            return;
        }
        const v = ctx.view();
        plan = planArenaFilter(v.doc.objects(), infoOf, st.draft);
        const pos = new Map<number, Vec3>();
        for (const id of plan.entries.keys()) {
            const o = v.doc.get(id);
            if (o) pos.set(id, o.transform.pos);
        }
        overlay.setPlan(plan, pos);
        dirtyPlan = false;
    }

    function revalidate(): void {
        issues = st.active ? validateArena(st.draft, env()) : [];
        const errors = issues.filter((i) => i.severity === 'error').length;
        const warns = issues.filter((i) => i.severity === 'warning').length;
        chip.textContent = !st.active
            ? ''
            : errors
              ? `${errors} error${errors === 1 ? '' : 's'}`
              : warns
                ? `${warns} warning${warns === 1 ? '' : 's'}`
                : 'Ready to export';
        chip.className = `mp-chip${!st.active ? ' hidden' : errors ? ' bad' : warns ? ' warn' : ' ok'}`;
        for (const l of issueListeners) l();
        renderIssues();
    }
    const issueListeners = new Set<() => void>();

    // ---- small widgets ----

    const numInput = (
        value: number,
        set: (n: number) => void,
        o: { step?: number; min?: number; max?: number; w?: number; label: string },
    ): HTMLInputElement => {
        const el = h('input', {
            type: 'number',
            class: 'mp-num',
            step: o.step ?? 1,
            'aria-label': o.label,
            ...(o.min !== undefined ? { min: o.min } : {}),
            ...(o.max !== undefined ? { max: o.max } : {}),
            value: String(Math.round(value * 100) / 100),
        });
        el.addEventListener('change', () => {
            let n = Number(el.value);
            if (!Number.isFinite(n)) return void (el.value = String(value));
            if (o.min !== undefined) n = Math.max(o.min, n);
            if (o.max !== undefined) n = Math.min(o.max, n);
            set(n);
        });
        el.addEventListener('keydown', (e) => e.stopPropagation());
        return el;
    };
    const textInput = (
        value: string,
        set: (s: string) => void,
        label: string,
        ph = '',
        max = 200,
    ): HTMLInputElement => {
        const el = h('input', {
            type: 'text',
            class: 'mp-text',
            value,
            'aria-label': label,
            placeholder: ph,
            maxlength: max,
            spellcheck: 'false',
        });
        el.addEventListener('change', () => set(el.value.trim()));
        el.addEventListener('keydown', (e) => e.stopPropagation());
        return el;
    };
    const field = (label: string, ...ctl: (Node | string)[]): HTMLElement =>
        h('label', { class: 'mp-f' }, h('span', {}, label), h('div', { class: 'mp-ctl' }, ...ctl));
    const btn = (
        label: string,
        run: () => void,
        o: { ico?: string; cls?: string; title?: string; disabled?: boolean } = {},
    ): HTMLButtonElement =>
        h(
            'button',
            { class: `btn ${o.cls ?? ''}`, type: 'button', title: o.title, disabled: o.disabled, onclick: run },
            o.ico ? icon(o.ico, 13) : null,
            label,
        );
    const section = (name: string, ...kids: (Node | null)[]): HTMLElement =>
        h('section', { class: 'mp-sec' }, h('h4', {}, name), ...kids);
    const dot = (c: number): HTMLElement => h('span', { class: 'mp-dot', style: `background:${colorCss(c)}` });

    const frame = (p: Vec3 | null): void => {
        if (p) overlay.frameAt(p);
    };

    // ---- resource names and the item picker ----

    function itemClass(res: number): AssetClass | undefined {
        const id = res & 0xffff;
        return ctx.catalog().find((c) => c.classId === id && /^resour/i.test(c.db));
    }
    const itemName = (res: number): string => (res ? (itemClass(res)?.name ?? hex(res)) : 'Level item (adopted)');
    function thumbEl(res: number, cls = 'mp-thumb'): HTMLElement {
        const c = itemClass(res);
        const box = h('span', { class: cls });
        const url = c ? ctx.classThumb(c) : null;
        if (url) box.append(h('img', { src: url, alt: '' }));
        else box.append(icon('item', 16));
        box.dataset.res = String(res);
        return box;
    }

    function openItemPicker(anchor: HTMLElement, onPick: (res: number) => void, opts: { adopt?: boolean } = {}): void {
        const search = searchField('Search items', 'Search items');
        const grid = h('div', { class: 'mp-pick-grid' });
        let all = false;
        const allCls = (): AssetClass[] =>
            ctx
                .catalog()
                .filter((c) => /^resour/i.test(c.db) && (all || c.kind === 'weapon'))
                .sort(
                    (a, b) =>
                        Number(a.name.startsWith('!')) - Number(b.name.startsWith('!')) || a.name.localeCompare(b.name),
                );
        const pop = openPopover(
            anchor,
            (close) => {
                const render = (): void => {
                    const q = search.input.value.trim().toLowerCase();
                    const list = allCls().filter(
                        (c) => !q || c.name.toLowerCase().includes(q) || hex((RES_DB << 16) | c.classId).includes(q),
                    );
                    const rows: HTMLElement[] = [];
                    if (opts.adopt && !q) {
                        rows.push(
                            h(
                                'button',
                                { class: 'mp-pick adopt', type: 'button', onclick: () => (onPick(0), close()) },
                                h('span', { class: 'mp-thumb' }, icon('link', 16)),
                                h('span', { class: 'n' }, 'Adopt the level item'),
                                h('span', { class: 'dim' }, `nearest within ${ADOPT_RADIUS}`),
                            ),
                        );
                    }
                    for (const c of list.slice(0, 400)) {
                        const res = (RES_DB << 16) | c.classId;
                        rows.push(
                            h(
                                'button',
                                {
                                    class: 'mp-pick',
                                    type: 'button',
                                    title: c.name,
                                    onclick: () => (onPick(res), close()),
                                },
                                thumbEl(res),
                                h('span', { class: 'n' }, c.name),
                                h('span', { class: 'dim mono' }, hex(res)),
                            ),
                        );
                    }
                    if (!list.length)
                        rows.push(
                            h(
                                'div',
                                { class: 'dim pad' },
                                'No item classes match. This level lists no Resources database or the search is too narrow.',
                            ),
                        );
                    fill(grid, ...rows);
                };
                search.input.addEventListener('input', render);
                const toggle = h(
                    'label',
                    { class: 'mp-check' },
                    h('input', {
                        type: 'checkbox',
                        onchange: (e: Event) => ((all = (e.target as HTMLInputElement).checked), render()),
                    }),
                    'Every Resources class',
                );
                render();
                const refresh = window.setInterval(() => {
                    if (!grid.isConnected) return window.clearInterval(refresh);
                    for (const el of grid.querySelectorAll<HTMLElement>('.mp-thumb[data-res]')) {
                        if (el.querySelector('img')) continue;
                        const res = Number(el.dataset.res);
                        const c = itemClass(res);
                        const url = c ? ctx.classThumb(c) : null;
                        if (url) el.replaceChildren(h('img', { src: url, alt: '' }));
                    }
                }, 700);
                return h('div', { class: 'mp-picker' }, h('div', { class: 'mp-pick-head' }, search.el, toggle), grid);
            },
            { className: 'mp-pop' },
        );
        void pop;
    }

    // ---- inactive: start a map ----

    function startCard(): HTMLElement {
        const doc = ctx.doc();
        const levels = host.levels();
        const open = doc.size > 0 ? doc.level : '';
        startLevel ||= open;
        const sel = h('select', { class: 'mp-text', 'aria-label': 'Campaign level' }) as HTMLSelectElement;
        sel.append(
            h(
                'option',
                { value: '' },
                open ? `Open level: ${open}` : levels.length ? 'Choose a level...' : 'Open a disc image first',
            ),
        );
        for (const l of levels) sel.append(h('option', { value: l.dirPath }, l.name));
        const nameIn = textInput(startName, (s) => (startName = s), 'Map name', 'Map name', 64);
        nameIn.addEventListener('input', () => (startName = nameIn.value));
        const saved = st.saved();
        const go = async (): Promise<void> => {
            try {
                if (sel.value) {
                    host.setStatus('Opening the level for the new map...');
                    await host.openLevel(sel.value);
                    await host.visualsReady();
                }
                begin();
            } catch (e) {
                ctx.toast(e instanceof Error ? e.message : String(e), 'error');
            }
        };
        const begin = (init?: ArenaDraft): void => {
            const d = ctx.doc();
            if (!d.size) return void ctx.toast('Open a level first.', 'warn');
            st.start(d.level, init);
            if (!init) {
                const name = nameIn.value.trim() || `${d.level} Arena`;
                st.commit('Start multiplayer map', (n) => {
                    n.name = name;
                    n.id = slugify(name);
                    n.bounds = defaultArenaBounds([...d.objects()].map((o) => o.transform.pos));
                });
            }
            if (st.draft.bounds) overlay.frameRect(st.draft.bounds);
            growDock();
            tab = 'map';
            render();
            ctx.toast('Multiplayer map started: place spawns and pickups, then export.', 'ok');
        };
        const imp = h('input', { type: 'file', accept: '.ini,.txt', hidden: true, 'aria-label': 'Import arena map' });
        imp.addEventListener('change', async () => {
            const f = imp.files?.[0];
            if (!f) return;
            const r = parseArenaIni(await f.text(), f.name);
            importText = { source: f.name, arenas: r.arenas, modes: r.modes };
            if (r.errors.length) ctx.toast(`${f.name}: ${r.errors.length} line(s) skipped (${r.errors[0]})`, 'warn');
            render();
        });
        const imported = importText
            ? h(
                  'div',
                  { class: 'mp-imports' },
                  h(
                      'div',
                      { class: 'dim' },
                      `${importText.source}: ${importText.arenas.length} map(s), ${importText.modes.length} mode(s)`,
                  ),
                  ...importText.arenas.map((a) =>
                      btn(`Edit "${a.name || a.id}"`, () => void editImported(a), {
                          ico: 'open',
                          title: `Level: ${a.level}`,
                      }),
                  ),
              )
            : null;
        async function editImported(a: ArenaDraft): Promise<void> {
            try {
                const lv = levels.find((l) => l.name.toLowerCase() === a.level.toLowerCase());
                if (doc.level !== a.level || !doc.size) {
                    if (!lv)
                        return void ctx.toast(`Level "${a.level}" is not on the open disc; open it first.`, 'warn');
                    await host.openLevel(lv.dirPath);
                    await host.visualsReady();
                }
                if (importText?.modes.length)
                    st.setModes([
                        ...st.modes.filter((m) => !importText!.modes.some((x) => x.id === m.id)),
                        ...importText.modes,
                    ]);
                begin(a);
            } catch (e) {
                ctx.toast(e instanceof Error ? e.message : String(e), 'error');
            }
        }
        return h(
            'div',
            { class: 'mp-start' },
            emptyState(
                'rocket',
                'Build a multiplayer map',
                'Pick a campaign level for its terrain, set spawn points and pickups, draw the arena bounds and export a mod pack. Deathmatch runs on these arenas; co-op stays on the campaign.',
            ),
            h(
                'div',
                { class: 'mp-start-form' },
                field('Terrain from', sel),
                field('Map name', nameIn),
                h(
                    'div',
                    { class: 'mp-btnrow' },
                    btn('Start new map', () => void go(), {
                        cls: 'primary',
                        ico: 'plus',
                        disabled: !open && !levels.length,
                    }),
                    btn('Import map...', () => imp.click(), {
                        ico: 'open',
                        title: 'Edit a map from the game or a pack',
                    }),
                    imp,
                    saved && saved.level === open
                        ? btn(`Resume "${saved.name || saved.id}"`, () => begin(saved), {
                              ico: 'history',
                              title: 'The draft this browser kept',
                          })
                        : null,
                ),
                imported,
            ),
        );
    }

    /** The panel wants room: raise the bottom dock to about 430 px unless the user already made it taller. */
    function growDock(): void {
        const lm = document.getElementById('level-main');
        if (!lm) return;
        const cur = parseInt(lm.style.getPropertyValue('--ed-bottom') || '0', 10);
        if (cur >= 400) return;
        lm.style.setProperty('--ed-bottom', `${Math.min(430, Math.round(window.innerHeight * 0.5))}px`);
        window.dispatchEvent(new Event('resize'));
    }

    // ---- Map page ----

    function mapPage(): HTMLElement {
        const d = st.draft;
        const set = (label: string, fn: (n: ArenaDraft) => void): void => st.commit(label, fn);
        const idIn = textInput(
            d.id,
            (s) => set('Map id', (n) => (n.id = s.replace(/[^A-Za-z0-9_-]/g, '-'))),
            'Map id',
            'letters, digits, - _',
            40,
        );
        const modeBoxes = ['dm', 'tdm', ...st.modes.map((m) => m.id)].map((id) => {
            const m = st.modes.find((x) => x.id === id);
            const cb = h('input', { type: 'checkbox', checked: d.modes.includes(id) });
            cb.addEventListener('change', () =>
                set('Supported modes', (n) => {
                    n.modes = cb.checked ? [...new Set([...n.modes, id])] : n.modes.filter((x) => x !== id);
                }),
            );
            return h(
                'label',
                {
                    class: 'mp-check',
                    title: m ? m.description || m.name : id === 'dm' ? 'Deathmatch' : 'Team deathmatch',
                },
                cb,
                m ? m.name : id === 'dm' ? 'Deathmatch' : 'Team deathmatch',
                m ? h('span', { class: 'mp-tag' }, 'custom') : null,
            );
        });
        const b = d.bounds;
        const boundsRow = h(
            'div',
            { class: 'mp-grid4' },
            ...(['min X', 'min Z', 'max X', 'max Z'] as const).map((l, i) =>
                field(
                    l,
                    numInput(
                        b?.[i] ?? 0,
                        (n) => set('Edit bounds', (m) => ((m.bounds ??= [0, 0, 0, 0]), (m.bounds[i] = n))),
                        { step: 256, label: l },
                    ),
                ),
            ),
        );
        const loadout = h(
            'div',
            { class: 'mp-chips' },
            ...d.loadout.map((res, i) =>
                h(
                    'span',
                    { class: 'mp-chipitem', title: hex(res) },
                    thumbEl(res, 'mp-thumb sm'),
                    itemName(res),
                    h(
                        'button',
                        {
                            class: 'x',
                            type: 'button',
                            'aria-label': 'Remove',
                            onclick: () => set('Remove loadout item', (n) => n.loadout.splice(i, 1)),
                        },
                        icon('x', 10),
                    ),
                ),
            ),
            btn(
                'Add item',
                () => openItemPicker(loadoutBtn, (res) => res && set('Add loadout item', (n) => n.loadout.push(res))),
                { ico: 'plus' },
            ),
        );
        const loadoutBtn = loadout.lastElementChild as HTMLElement;
        return h(
            'div',
            { class: 'mp-cols' },
            section(
                'Map',
                field(
                    'Name',
                    textInput(
                        d.name,
                        (s) => set('Map name', (n) => ((n.name = s), !n.id && (n.id = slugify(s)))),
                        'Map name',
                        'Shown in the multiplayer menu',
                        64,
                    ),
                ),
                field('Id', idIn),
                field(
                    'Description',
                    textInput(
                        d.description,
                        (s) => set('Map description', (n) => (n.description = s)),
                        'Description',
                        'One line for the menu',
                        120,
                    ),
                ),
                h(
                    'div',
                    { class: 'mp-f ro' },
                    h('span', {}, 'Base level'),
                    h(
                        'div',
                        { class: 'mp-ctl' },
                        d.level,
                        h('span', { class: 'dim' }, ` terrain, ${ctx.doc().size} objects`),
                    ),
                ),
            ),
            section(
                'Rules',
                h(
                    'div',
                    { class: 'mp-row2' },
                    field(
                        'Max players',
                        numInput(d.players, (n) => set('Max players', (m) => (m.players = Math.round(n))), {
                            min: 2,
                            max: MAX_PLAYERS,
                            label: 'Max players',
                        }),
                    ),
                    field(
                        'Start health',
                        numInput(d.health, (n) => set('Start health', (m) => (m.health = Math.round(n))), {
                            min: 1,
                            max: 1000,
                            step: 10,
                            label: 'Start health',
                        }),
                    ),
                ),
                h(
                    'div',
                    { class: 'mp-f' },
                    h('span', {}, 'Supported modes'),
                    h('div', { class: 'mp-checks' }, ...modeBoxes),
                ),
                h('div', { class: 'mp-f' }, h('span', {}, 'Loadout (every life)'), loadout),
            ),
            section(
                'Bounds',
                boundsRow,
                h(
                    'div',
                    { class: 'mp-btnrow' },
                    btn(
                        'Draw in viewport',
                        () => (
                            st.setTool('bounds'),
                            ctx.toast('Drag on the terrain to draw the playable rectangle. Esc ends.')
                        ),
                        { ico: 'top' },
                    ),
                    btn('Fit to spawns', () => fitBounds(), { disabled: !d.spawns.length && !d.pickups.length }),
                    btn('Frame', () => d.bounds && overlay.frameRect(d.bounds), { disabled: !d.bounds }),
                    btn('Close map', () => (st.stop(), render()), {
                        cls: 'quiet',
                        ico: 'x',
                        title: 'Leave the multiplayer map mode (the draft stays in this browser)',
                    }),
                ),
                h(
                    'div',
                    { class: 'dim small' },
                    'Leaving the bounds for a few seconds sends the player back to a spawn point. The blue rectangle in the viewport follows the terrain.',
                ),
            ),
        );
    }

    function fitBounds(): void {
        const pts = [
            ...st.draft.spawns.map((s) => s.pos),
            ...st.draft.pickups.map((p) => p.pos),
            ...st.draft.markers.map((m) => m.pos),
        ];
        if (!pts.length) return;
        const pad = 1500;
        st.commit('Fit bounds to the map', (n) => {
            n.bounds = [
                Math.min(...pts.map((p) => p[0])) - pad,
                Math.min(...pts.map((p) => p[2])) - pad,
                Math.max(...pts.map((p) => p[0])) + pad,
                Math.max(...pts.map((p) => p[2])) + pad,
            ];
        });
    }

    // ---- Place page ----

    const selLabel = (s: ArenaSel): string =>
        !s
            ? ''
            : s.kind === 'bounds'
              ? `Bounds corner ${s.corner === 0 ? 'min' : 'max'}`
              : s.kind === 'spawn'
                ? `Spawn ${st.draft.spawns.findIndex((x) => x.uid === s.uid) + 1}`
                : s.kind === 'pickup'
                  ? `Pickup ${st.draft.pickups.findIndex((x) => x.uid === s.uid) + 1}`
                  : 'Marker';

    function posFields(p: Vec3, apply: (i: number, n: number) => void, ground: () => void): HTMLElement {
        return h(
            'div',
            { class: 'mp-grid3' },
            ...(['X', 'Y', 'Z'] as const).map((l, i) =>
                field(
                    l,
                    numInput(p[i]!, (n) => apply(i, n), { step: 10, label: l }),
                ),
            ),
            h(
                'div',
                { class: 'mp-btnrow span3' },
                btn('Snap to ground', ground, { ico: 'ground', title: 'Move to the terrain height under the point' }),
            ),
        );
    }

    function groundTo(label: string, list: (d: ArenaDraft) => { uid: number; pos: Vec3 }[], uid: number): void {
        const v = ctx.view();
        st.commit(label, (n) => {
            const it = list(n).find((x) => x.uid === uid);
            if (!it) return;
            const g = v.groundHeightAt(it.pos[0], it.pos[2]);
            if (g !== null) it.pos = [it.pos[0], g, it.pos[2]];
        });
    }

    function propsForm(): HTMLElement {
        const s = st.sel;
        const d = st.draft;
        if (!s)
            return h(
                'div',
                { class: 'dim pad' },
                'Nothing selected. Pick a tool and click the terrain, or click an item in the viewport or the lists.',
            );
        if (s.kind === 'bounds') {
            return h(
                'div',
                { class: 'mp-props' },
                h('h4', {}, selLabel(s)),
                h(
                    'div',
                    { class: 'dim small' },
                    'Drag the handle (gizmo axes X and Z) or type values on the Map page.',
                ),
            );
        }
        const del = (): void => {
            st.commit(`Delete ${s.kind}`, (n) => {
                const k = s.kind === 'spawn' ? n.spawns : s.kind === 'pickup' ? n.pickups : n.markers;
                const at = k.findIndex((x) => x.uid === s.uid);
                if (at >= 0) k.splice(at, 1);
            });
            st.select(null);
        };
        const dup = (): void => {
            const uid = d.nextUid;
            st.commit(`Duplicate ${s.kind}`, (n) => {
                const k = (s.kind === 'spawn' ? n.spawns : s.kind === 'pickup' ? n.pickups : n.markers) as {
                    uid: number;
                    pos: Vec3;
                }[];
                const src = k.find((x) => x.uid === s.uid);
                if (!src) return;
                n.nextUid = uid + 1;
                k.push({ ...structuredClone(src), uid, pos: [src.pos[0] + 400, src.pos[1], src.pos[2] + 400] });
            });
            st.select({ kind: s.kind, uid });
        };
        const actions = h(
            'div',
            { class: 'mp-btnrow' },
            btn('Duplicate', dup, { ico: 'copy' }),
            btn('Delete', del, { ico: 'trash', cls: 'danger' }),
            btn('Frame', () => frame(overlay.focus(s)), { ico: 'focus' }),
        );
        if (s.kind === 'spawn') {
            const sp = d.spawns.find((x) => x.uid === s.uid);
            if (!sp) return h('div');
            const maxTeam = Math.max(2, ...d.modes.map((id) => st.modes.find((m) => m.id === id)?.teams ?? 0));
            const team = h('select', { class: 'mp-text', 'aria-label': 'Team' }) as HTMLSelectElement;
            for (let t = 0; t <= maxTeam; t++)
                team.append(
                    h('option', { value: String(t), selected: t === sp.team }, t === 0 ? 'Any team' : TEAM_NAMES[t]),
                );
            team.addEventListener('change', () =>
                st.commit('Spawn team', (n) => (n.spawns.find((x) => x.uid === s.uid)!.team = Number(team.value))),
            );
            return h(
                'div',
                { class: 'mp-props' },
                h('h4', {}, dot(TEAM_COLORS[sp.team] ?? TEAM_COLORS[0]), selLabel(s)),
                posFields(
                    sp.pos,
                    (i, n) => st.commit('Move spawn', (m) => (m.spawns.find((x) => x.uid === s.uid)!.pos[i] = n)),
                    () => groundTo('Spawn to ground', (m) => m.spawns, s.uid),
                ),
                h(
                    'div',
                    { class: 'mp-row2' },
                    field(
                        'Facing (deg)',
                        numInput(
                            sp.yaw,
                            (n) =>
                                st.commit(
                                    'Spawn facing',
                                    (m) => (m.spawns.find((x) => x.uid === s.uid)!.yaw = ((n % 360) + 360) % 360),
                                ),
                            { step: 15, label: 'Yaw' },
                        ),
                    ),
                    field('Team', team),
                ),
                h(
                    'div',
                    { class: 'dim small' },
                    'The white arrow shows the facing. The game keeps the camera heading on respawn, so facing is stored for later and for team readability.',
                ),
                actions,
            );
        }
        if (s.kind === 'pickup') {
            const p = d.pickups.find((x) => x.uid === s.uid);
            if (!p) return h('div');
            const pick = h(
                'button',
                { class: 'mp-itembtn', type: 'button', title: 'Choose the item' },
                thumbEl(p.res),
                h('span', { class: 'n' }, itemName(p.res)),
                h('span', { class: 'dim mono' }, p.res ? hex(p.res) : 'adopt'),
                icon('chevron-down', 12),
            );
            pick.addEventListener('click', () =>
                openItemPicker(
                    pick,
                    (res) =>
                        st.commit('Pickup item', (m) => {
                            const t = m.pickups.find((x) => x.uid === s.uid)!;
                            t.res = res;
                            if (res && !t.label) t.label = itemName(res);
                        }),
                    { adopt: true },
                ),
            );
            return h(
                'div',
                { class: 'mp-props' },
                h('h4', {}, dot(p.res ? 0xfacc15 : 0x22d3ee), selLabel(s)),
                field('Item', pick),
                posFields(
                    p.pos,
                    (i, n) => st.commit('Move pickup', (m) => (m.pickups.find((x) => x.uid === s.uid)!.pos[i] = n)),
                    () => groundTo('Pickup to ground', (m) => m.pickups, s.uid),
                ),
                h(
                    'div',
                    { class: 'mp-row2' },
                    field(
                        'Respawn (s)',
                        numInput(
                            p.respawn,
                            (n) =>
                                st.commit(
                                    'Pickup respawn',
                                    (m) => (m.pickups.find((x) => x.uid === s.uid)!.respawn = Math.round(n)),
                                ),
                            { min: 1, max: 3600, step: 5, label: 'Respawn seconds' },
                        ),
                    ),
                    field(
                        'Label',
                        textInput(
                            p.label,
                            (v) =>
                                st.commit('Pickup label', (m) => (m.pickups.find((x) => x.uid === s.uid)!.label = v)),
                            'Label',
                            'Shown in logs',
                            40,
                        ),
                    ),
                ),
                actions,
            );
        }
        const m = d.markers.find((x) => x.uid === s.uid);
        if (!m) return h('div');
        const kind = h('select', { class: 'mp-text', 'aria-label': 'Marker kind' }) as HTMLSelectElement;
        for (const k of ['flag', 'base', 'zone'] as const)
            kind.append(h('option', { value: k, selected: k === m.kind }, k));
        kind.addEventListener('change', () =>
            st.commit('Marker kind', (n) => (n.markers.find((x) => x.uid === s.uid)!.kind = kind.value as 'flag')),
        );
        const team = h('select', { class: 'mp-text', 'aria-label': 'Team' }) as HTMLSelectElement;
        for (let t = 0; t <= 4; t++)
            team.append(h('option', { value: String(t), selected: t === m.team }, t === 0 ? 'Neutral' : TEAM_NAMES[t]));
        team.addEventListener('change', () =>
            st.commit('Marker team', (n) => (n.markers.find((x) => x.uid === s.uid)!.team = Number(team.value))),
        );
        return h(
            'div',
            { class: 'mp-props' },
            h(
                'h4',
                {},
                dot(m.team ? TEAM_COLORS[m.team]! : 0xf472b6),
                `${m.kind[0]!.toUpperCase()}${m.kind.slice(1)} marker`,
            ),
            h('div', { class: 'mp-row2' }, field('Kind', kind), field('Team', team)),
            posFields(
                m.pos,
                (i, n) => st.commit('Move marker', (x) => (x.markers.find((y) => y.uid === s.uid)!.pos[i] = n)),
                () => groundTo('Marker to ground', (x) => x.markers, s.uid),
            ),
            h(
                'div',
                { class: 'mp-row2' },
                field(
                    'Radius',
                    numInput(
                        m.radius,
                        (n) =>
                            st.commit(
                                'Marker radius',
                                (x) => (x.markers.find((y) => y.uid === s.uid)!.radius = Math.round(n)),
                            ),
                        { min: 50, max: 5000, step: 50, label: 'Radius' },
                    ),
                ),
                field(
                    'Name',
                    textInput(
                        m.label,
                        (v) => st.commit('Marker name', (x) => (x.markers.find((y) => y.uid === s.uid)!.label = v)),
                        'Name',
                        '',
                        40,
                    ),
                ),
            ),
            actions,
        );
    }

    function placePage(): HTMLElement {
        const d = st.draft;
        const toolBtns = TOOLS.map((t) =>
            h(
                'button',
                {
                    class: `mp-tool${st.tool === t.id ? ' active' : ''}`,
                    type: 'button',
                    title: t.hint,
                    'data-tool': t.id,
                    onclick: () => (st.setTool(t.id), render()),
                },
                icon(t.icon, 14),
                t.label,
            ),
        );
        const sp = d.spawns[0] ? true : false;
        void sp;
        const gizmoBtns = h(
            'div',
            { class: 'seg' },
            ...(['translate', 'rotate'] as const).map((m) =>
                h(
                    'button',
                    {
                        class: `seg-btn${overlay.mode === m ? ' active' : ''}`,
                        type: 'button',
                        title:
                            m === 'rotate'
                                ? 'Turn a spawn point (yaw); snaps to the angle step'
                                : 'Move with the gizmo; snaps to the grid',
                        onclick: () => (overlay.setGizmoMode(m), render()),
                    },
                    m === 'translate' ? 'Move' : 'Rotate',
                ),
            ),
        );
        const row = (kind: 'spawn' | 'pickup' | 'marker', uid: number, ...kids: (Node | string)[]): HTMLElement => {
            const on = st.sel && st.sel.kind === kind && st.sel.uid === uid;
            return h(
                'button',
                {
                    class: `mp-row${on ? ' sel' : ''}`,
                    type: 'button',
                    onclick: () => st.select({ kind, uid }),
                    ondblclick: () => frame(overlay.focus({ kind, uid })),
                },
                ...kids,
            );
        };
        const spawnRows = d.spawns.map((s, i) =>
            row(
                'spawn',
                s.uid,
                dot(TEAM_COLORS[s.team] ?? TEAM_COLORS[0]),
                h('span', { class: 'n' }, `Spawn ${i + 1}`),
                h(
                    'span',
                    { class: 'dim mono' },
                    `${Math.round(s.pos[0])}, ${Math.round(s.pos[2])}  ${Math.round(s.yaw)}°${s.team ? ` ${TEAM_NAMES[s.team]}` : ''}`,
                ),
            ),
        );
        const pickRows = d.pickups.map((p, i) =>
            row(
                'pickup',
                p.uid,
                thumbEl(p.res, 'mp-thumb sm'),
                h('span', { class: 'n' }, `${i + 1}. ${p.label || itemName(p.res)}`),
                h('span', { class: 'dim mono' }, `${p.respawn}s`),
            ),
        );
        const markRows = d.markers.map((m, i) =>
            row(
                'marker',
                m.uid,
                dot(m.team ? TEAM_COLORS[m.team]! : 0xf472b6),
                h('span', { class: 'n' }, `${m.kind} ${i + 1}${m.label ? ` ${m.label}` : ''}`),
                h('span', { class: 'dim mono' }, `r${m.radius}`),
            ),
        );
        const suggest = (): void => {
            if (!d.bounds) return void ctx.toast('Draw the bounds first: spawns are suggested inside them.', 'warn');
            if (!ctx.view().hasGround) return void ctx.toast('The terrain is still loading.', 'warn');
            const want = Math.max(1, d.players - d.spawns.length);
            const pts = suggestSpawns(
                d.bounds,
                (x, z) => ctx.view().groundHeightAt(x, z),
                want,
                d.spawns.map((s) => s.pos),
            );
            if (!pts.length) return void ctx.toast('No flat ground found inside the bounds.', 'warn');
            let uid = d.nextUid;
            st.commit(`Suggest ${pts.length} spawn points`, (n) => {
                for (const p of pts)
                    n.spawns.push({
                        uid: uid++,
                        pos: p,
                        yaw: 0,
                        team: n.spawns.length % 2 === 0 || !n.modes.includes('tdm') ? 0 : 0,
                    });
                n.nextUid = uid;
            });
            ctx.toast(`Added ${pts.length} spawn point(s) on flat ground; check them in the viewport.`, 'ok');
        };
        const teamSplit = (): void => {
            st.commit('Alternate spawn teams', (n) => n.spawns.forEach((s, i) => (s.team = (i % 2) + 1)));
        };
        return h(
            'div',
            { class: 'mp-place' },
            h(
                'div',
                { class: 'mp-tools' },
                ...toolBtns,
                gizmoBtns,
                btn('Suggest spawns', suggest, {
                    ico: 'sparkles' in {} ? 'effect' : 'effect',
                    title: 'Fill up to the player count with spread out points on flat ground',
                }),
                btn('Alternate teams', teamSplit, {
                    ico: 'layers',
                    title: 'Give the spawn points to Red and Blue in turn (team modes)',
                }),
                h('div', { class: 'dim small' }, TOOLS.find((t) => t.id === st.tool)?.hint ?? ''),
            ),
            h(
                'div',
                { class: 'mp-lists' },
                section(
                    `Spawn points (${d.spawns.length}/${d.players})`,
                    ...spawnRows,
                    spawnRows.length ? null : h('div', { class: 'dim pad' }, 'None yet. Use the Spawn tool.'),
                ),
                section(
                    `Pickups (${d.pickups.length})`,
                    ...pickRows,
                    pickRows.length ? null : h('div', { class: 'dim pad' }, 'None yet. Use the Pickup tool.'),
                ),
                section(
                    `Markers (${d.markers.length})`,
                    ...markRows,
                    markRows.length ? null : h('div', { class: 'dim pad' }, 'Flags, bases and zones for custom modes.'),
                ),
            ),
            h('div', { class: 'mp-sel' }, propsForm()),
        );
    }

    // ---- World filter page ----

    function worldPage(): HTMLElement {
        const v = ctx.view();
        if (dirtyPlan || !plan) recompute();
        const p = plan!;
        const d = st.draft;
        const summary = FILTER_REASONS.map((r) => {
            const n = [...p.entries.values()].filter((e) => e.reason === r.reason).length;
            const removed = p.removed.get(r.reason) ?? 0;
            return { r, n, removed };
        }).filter((x) => x.n > 0 || x.r.reason !== 'manual');
        const rows = [...p.entries]
            .filter(([, e]) => worldReason === 'all' || e.reason === worldReason)
            .slice(0, 400)
            .map(([id, e]) => {
                const o = v.doc.get(id);
                if (!o) return null;
                const cb = h('input', { type: 'checkbox', checked: !e.kept, 'aria-label': 'Removed in the arena' });
                cb.addEventListener('click', (ev) => ev.stopPropagation());
                cb.addEventListener('change', () => toggleKeep(o, e.reason, !cb.checked));
                return h(
                    'div',
                    { class: 'mp-row world', onclick: () => ctx.locate([id]) },
                    cb,
                    h('span', { class: 'n' }, `${o.class || 'Object'} #${id}`),
                    h(
                        'span',
                        { class: `dim tag ${e.kept ? 'keep' : ''}` },
                        e.kept ? 'kept' : (FILTER_REASONS.find((r) => r.reason === e.reason)?.label ?? e.reason),
                    ),
                );
            });
        const sel = [...v.selection].map((id) => v.doc.get(id)).filter((o): o is LevelObject => !!o);
        const selEntry = sel[0] ? p.entries.get(sel[0].id) : undefined;
        const selBar = sel[0]
            ? h(
                  'div',
                  { class: 'mp-selobj' },
                  h('strong', {}, `${sel[0].class || 'Object'} #${sel[0].id}`),
                  selEntry
                      ? h(
                            'span',
                            { class: 'dim' },
                            selEntry.kept
                                ? ' is kept although the filter would remove it'
                                : ` is removed (${FILTER_REASONS.find((r) => r.reason === selEntry.reason)?.label ?? selEntry.reason})`,
                        )
                      : h('span', { class: 'dim' }, ' stays in the arena'),
                  selEntry && !selEntry.kept
                      ? btn('Keep it', () => toggleKeep(sel[0]!, selEntry.reason, false), { ico: 'check' })
                      : null,
                  selEntry?.kept
                      ? btn('Remove it again', () => toggleKeep(sel[0]!, selEntry.reason, true), { ico: 'trash' })
                      : null,
                  !selEntry
                      ? btn('Remove it too', () => toggleKeep(sel[0]!, 'manual', true), {
                            ico: 'trash',
                            title: 'Adds a remove = x y z line (authored; the game filter does not read it yet)',
                        })
                      : null,
              )
            : h('div', { class: 'dim small' }, 'Select an object in the viewport to keep or remove it individually.');
        const reasonSel = h('select', { class: 'mp-text', 'aria-label': 'Filter by reason' }) as HTMLSelectElement;
        reasonSel.append(h('option', { value: 'all' }, `All (${p.entries.size})`));
        for (const x of summary)
            if (x.n)
                reasonSel.append(
                    h('option', { value: x.r.reason, selected: worldReason === x.r.reason }, `${x.r.label} (${x.n})`),
                );
        reasonSel.addEventListener('change', () => ((worldReason = reasonSel.value), render()));
        const showCb = h('input', { type: 'checkbox', checked: st.showFilter });
        showCb.addEventListener('change', () => st.setShowFilter(showCb.checked));
        void d;
        return h(
            'div',
            { class: 'mp-cols' },
            section(
                'What the arena removes',
                h(
                    'div',
                    { class: 'dim small' },
                    `${[...p.removed.values()].reduce((a, b) => a + b, 0)} of ${v.doc.size} objects. Red dots are removed, green are kept by you, orange are marked for removal.`,
                ),
                ...summary.map((x) =>
                    h(
                        'div',
                        { class: 'mp-sum' },
                        h('span', { class: 'n' }, x.r.label),
                        h('span', { class: 'cnt' }, String(x.removed)),
                        h('span', { class: 'dim note' }, x.r.note),
                    ),
                ),
                h('label', { class: 'mp-check' }, showCb, 'Show them in the viewport'),
                h(
                    'div',
                    { class: 'dim small' },
                    'Creatures and loose items are guessed from class names; the game decides by hit points and entity type, so the counts can differ a little.',
                ),
            ),
            section('Objects', reasonSel, h('div', { class: 'mp-scroll' }, ...rows)),
            section(
                'Selected object',
                selBar,
                h(
                    'div',
                    { class: 'dim small' },
                    'Keep and remove lines are saved as keep = x y z and remove = x y z in the map file. The game filter reads them in a later version; today the class filter always applies.',
                ),
            ),
        );
    }

    function toggleKeep(o: LevelObject, reason: string, removed: boolean): void {
        const pos = o.transform.pos.map((x) => Math.round(x)) as Vec3;
        st.commit(removed ? 'Remove object in the arena' : 'Keep object in the arena', (n) => {
            const strip = (list: Vec3[]): void => {
                const i = overrideIndex(list, pos);
                if (i >= 0) list.splice(i, 1);
            };
            if (reason === 'manual') {
                strip(n.keepAt);
                if (removed) n.removeAt.push(pos);
                else strip(n.removeAt);
            } else {
                strip(n.removeAt);
                if (removed) strip(n.keepAt);
                else n.keepAt.push(pos);
            }
        });
    }

    // ---- Modes page ----
    //
    // A game mode is a set of rules the server runs ([mode <id>] in the pack's maps/*.ini): who plays with whom, how a
    // match is won, what a kill is worth, respawns and items. Left: the modes; middle: the rules, grouped; right: what
    // players will see in the game (the same words the Host page and the HUD use) and whether this map offers it.

    function modesPage(): HTMLElement {
        const list = st.modes;
        if (!modeSel || !list.some((m) => m.id === modeSel)) modeSel = list[0]?.id ?? '';
        const cur = list.find((m) => m.id === modeSel);
        const save = (fn: (m: ModeDef) => void, rerender = false): void => {
            const next = list.map((m) => (m.id === modeSel ? cloneMode(m) : m));
            const m = next.find((x) => x.id === modeSel)!;
            const oldId = m.id;
            fn(m);
            modeSel = m.id;
            st.setModes(next);
            // A renamed id stays offered on this map.
            if (m.id !== oldId && st.draft.modes.includes(oldId))
                st.commit('Rename mode', (n) => (n.modes = n.modes.map((x) => (x === oldId ? m.id : x))));
            if (rerender) render();
            else refreshModeSide();
            revalidate();
        };
        const rows: HTMLElement[] = ['dm', 'tdm'].map((id) =>
            h(
                'div',
                { class: 'mp-row builtin' },
                h('span', { class: 'n' }, id === 'dm' ? 'Deathmatch' : 'Team deathmatch'),
                h('span', { class: 'dim' }, 'built in'),
            ),
        );
        for (const m of list)
            rows.push(
                h(
                    'button',
                    {
                        class: `mp-row mp-moderow${m.id === modeSel ? ' sel' : ''}`,
                        type: 'button',
                        title: modeSummary(m),
                        onclick: () => ((modeSel = m.id), render()),
                    },
                    h('span', { class: 'n' }, m.name || m.id),
                    h('span', { class: 'dim' }, modeGoal(m)),
                    validateMode(m).length
                        ? h('span', { class: 'mp-bad', title: 'Has problems' }, icon('warn', 11))
                        : null,
                ),
            );
        const freeId = (base: string): string => {
            let id = base;
            for (let i = 2; list.some((m) => m.id === id); i++) id = `${base}-${i}`;
            return id;
        };
        const addMode = (m: ModeDef): void => {
            modeSel = m.id;
            st.setModes([...list, m]);
            render();
        };
        const newBtn = btn('New mode', () => openTemplates(newBtn), { ico: 'plus', cls: 'primary' });
        function openTemplates(anchor: HTMLElement): void {
            openPopover(
                anchor,
                (close) =>
                    h(
                        'div',
                        { class: 'mp-templates' },
                        h('h4', {}, 'Start from'),
                        h(
                            'button',
                            {
                                class: 'mp-pick',
                                type: 'button',
                                onclick: () => (close(), addMode(defaultMode(freeId('my-mode')))),
                            },
                            h('span', { class: 'n' }, 'Blank mode'),
                            h('span', { class: 'dim' }, 'Free for all, first to 20 points'),
                        ),
                        ...MODE_TEMPLATES.map((t) =>
                            h(
                                'button',
                                {
                                    class: 'mp-pick',
                                    type: 'button',
                                    onclick: () => {
                                        close();
                                        const made = t.make('x');
                                        addMode({ ...made, id: freeId(slugify(made.name).slice(0, 26) || t.key) });
                                    },
                                },
                                h('span', { class: 'n' }, t.label),
                                h('span', { class: 'dim' }, t.hint),
                            ),
                        ),
                    ),
                { className: 'mp-pop' },
            );
        }
        const form = cur
            ? modeForm(cur, save)
            : h(
                  'div',
                  { class: 'mp-empty' },
                  h('h4', {}, 'Make your own rules'),
                  h(
                      'p',
                      { class: 'dim' },
                      'A game mode decides who plays with whom, how a match is won, what a kill is worth and how ' +
                          'players come back. Built-in deathmatch and team deathmatch stay as they are.',
                  ),
                  btn('New mode', () => openTemplates(newBtn), { ico: 'plus', cls: 'primary' }),
              );
        modeSide = h('div', { class: 'mp-support' });
        refreshModeSide();
        return h(
            'div',
            { class: 'mp-modes' },
            h(
                'div',
                { class: 'mp-modelist' },
                h('h4', {}, 'Game modes'),
                ...rows,
                h(
                    'div',
                    { class: 'mp-btnrow' },
                    newBtn,
                    btn(
                        'Duplicate',
                        () =>
                            cur &&
                            addMode({ ...cloneMode(cur), id: freeId(`${cur.id}-copy`), name: `${cur.name} copy` }),
                        {
                            disabled: !cur,
                        },
                    ),
                    btn(
                        'Delete',
                        () =>
                            cur &&
                            (st.setModes(list.filter((m) => m.id !== cur.id)),
                            st.commit('Drop deleted mode', (n) => (n.modes = n.modes.filter((x) => x !== cur.id))),
                            render()),
                        { disabled: !cur, cls: 'danger', ico: 'trash' },
                    ),
                ),
            ),
            h('div', { class: 'mp-modeform' }, form),
            modeSide,
        );
    }

    let modeSide: HTMLElement | null = null;
    /** The right column: the in-game card, this map's offer and the problems (refreshed on every edit). */
    function refreshModeSide(): void {
        if (!modeSide) return;
        const m = st.modes.find((x) => x.id === modeSel);
        if (!m) return void fill(modeSide);
        const d = st.draft;
        const problems = validateMode(m);
        const fit = modeMapProblem(m, { modes: d.modes, players: d.players, markers: d.markers });
        const offered = d.modes.includes(m.id);
        const offer = h('input', { type: 'checkbox', checked: offered });
        offer.addEventListener('change', () =>
            st.commit(offer.checked ? 'Offer mode on map' : 'Stop offering mode', (n) => {
                n.modes = offer.checked ? [...new Set([...n.modes, m.id])] : n.modes.filter((x) => x !== m.id);
            }),
        );
        const teams = teamCount(m);
        fill(
            modeSide,
            h('h4', {}, icon('eye', 12), 'In the game'),
            h(
                'div',
                { class: 'mp-modecard' },
                h('div', { class: 'mp-modecard-name' }, m.name || m.id),
                h('div', { class: 'mp-modecard-goal' }, modeGoal(m)),
                m.description ? h('p', { class: 'mp-modecard-desc' }, m.description) : null,
                h('p', { class: 'mp-modecard-rules' }, modeSummary(m)),
                teams >= 2
                    ? h(
                          'div',
                          { class: 'mp-modecard-teams' },
                          ...Array.from({ length: teams }, (_, t) =>
                              h(
                                  'span',
                                  { class: 'mp-teamchip' },
                                  h('span', {
                                      class: 'mp-dot',
                                      style: `background:#${m.teamColors[t] ?? DEFAULT_TEAM_COLORS[t]}`,
                                  }),
                                  m.teamNames[t] || DEFAULT_TEAM_NAMES[t],
                              ),
                          ),
                      )
                    : null,
            ),
            h(
                'div',
                { class: 'dim mp-note' },
                'The Host page shows this text under the settings; the HUD shows the goal.',
            ),
            h('h4', {}, icon('world', 12), 'This map'),
            h('label', { class: 'mp-check' }, offer, `Offer it on ${d.name || 'this map'}`),
            offered && fit
                ? h('div', { class: 'mp-warnline' }, icon('warn', 12), `Cannot be played here: ${fit}`)
                : null,
            !offered && !fit
                ? h(
                      'div',
                      { class: 'dim mp-note' },
                      `Offered already: the map has ${m.base === 'tdm' ? 'team games' : 'deathmatch'}. Tick it to list the mode by name.`,
                  )
                : null,
            problems.length
                ? h(
                      'div',
                      { class: 'mp-problems' },
                      h('h4', {}, 'Fix before export'),
                      ...problems.map((p) => h('div', {}, icon('warn', 12), p)),
                  )
                : h('div', { class: 'mp-okline' }, icon('check', 12), 'Valid: the game will load this mode'),
            h('details', { class: 'mp-preview' }, h('summary', {}, 'Mode file'), h('pre', {}, serializeMode(m))),
        );
    }

    function modeForm(m: ModeDef, save: (fn: (m: ModeDef) => void, rerender?: boolean) => void): HTMLElement {
        const n = (
            label: string,
            key: keyof ModeDef,
            lim: readonly [number, number],
            step = 1,
            rerender = false,
        ): HTMLElement =>
            field(
                label,
                numInput(m[key] as number, (v) => save((x) => ((x[key] as number) = v), rerender), {
                    min: lim[0],
                    max: lim[1],
                    step,
                    label,
                }),
            );
        const hint = (text: string): HTMLElement => h('div', { class: 'mp-hint' }, text);
        const select = (
            label: string,
            options: [string, string][],
            value: string,
            set: (v: string) => void,
        ): HTMLElement => {
            const el = h('select', { class: 'mp-text', 'aria-label': label }) as HTMLSelectElement;
            for (const [v, t] of options) el.append(h('option', { value: v, selected: v === value }, t));
            el.addEventListener('change', () => set(el.value));
            return field(label, el);
        };
        const check = (label: string, value: boolean, set: (v: boolean) => void): HTMLElement => {
            const cb = h('input', { type: 'checkbox', checked: value });
            cb.addEventListener('change', () => set(cb.checked));
            return h('label', { class: 'mp-check pushdown' }, cb, label);
        };
        const group = (name: string, ico: string, ...kids: (Node | null)[]): HTMLElement =>
            h('section', { class: 'mp-group' }, h('h4', {}, icon(ico, 12), name), ...kids);
        const teams = teamCount(m);

        // Teams: names and colours per team.
        const teamRows =
            teams >= 2
                ? Array.from({ length: teams }, (_, t) => {
                      const col = h('input', {
                          type: 'color',
                          class: 'mp-color',
                          'aria-label': `Team ${t + 1} colour`,
                          value: `#${m.teamColors[t] ?? DEFAULT_TEAM_COLORS[t]}`,
                      }) as HTMLInputElement;
                      col.addEventListener('change', () =>
                          save((x) => {
                              const cs = Array.from(
                                  { length: Math.max(x.teamColors.length, t + 1) },
                                  (_, i) => x.teamColors[i] ?? DEFAULT_TEAM_COLORS[i]!,
                              );
                              cs[t] = col.value.slice(1).toLowerCase();
                              x.teamColors = cs;
                          }, true),
                      );
                      return h(
                          'div',
                          { class: 'mp-teamrow' },
                          col,
                          textInput(
                              m.teamNames[t] ?? '',
                              (s) =>
                                  save((x) => {
                                      const ns = Array.from(
                                          { length: Math.max(x.teamNames.length, t + 1) },
                                          (_, i) => x.teamNames[i] ?? DEFAULT_TEAM_NAMES[i]!,
                                      );
                                      ns[t] = s || DEFAULT_TEAM_NAMES[t]!;
                                      x.teamNames = ns;
                                  }, true),
                              `Team ${t + 1} name`,
                              DEFAULT_TEAM_NAMES[t],
                              16,
                          ),
                      );
                  })
                : [];

        const resList = (label: string, key: 'loadout' | 'weapons', empty: string): HTMLElement => {
            const anchor = btn(
                'Add item',
                () => openItemPicker(anchor, (res) => res && save((x) => x[key].push(res), true)),
                { ico: 'plus' },
            );
            return h(
                'div',
                { class: 'mp-f' },
                h('span', {}, label),
                h(
                    'div',
                    { class: 'mp-chips' },
                    ...m[key].map((res, i) =>
                        h(
                            'span',
                            { class: 'mp-chipitem', title: hex(res) },
                            thumbEl(res, 'mp-thumb sm'),
                            itemName(res),
                            h(
                                'button',
                                {
                                    class: 'x',
                                    type: 'button',
                                    'aria-label': 'Remove',
                                    onclick: () => save((x) => x[key].splice(i, 1), true),
                                },
                                icon('x', 10),
                            ),
                        ),
                    ),
                    m[key].length ? null : h('span', { class: 'dim' }, empty),
                    anchor,
                ),
            );
        };
        const objs = m.objectives.map((o, i) =>
            h(
                'div',
                { class: 'mp-obj' },
                h(
                    'select',
                    {
                        class: 'mp-text',
                        'aria-label': 'Objective kind',
                        onchange: (e: Event) =>
                            save(
                                (x) => (x.objectives[i]!.kind = (e.target as HTMLSelectElement).value as 'hold'),
                                true,
                            ),
                    },
                    h('option', { value: 'capture', selected: o.kind === 'capture' }, 'Capture the flag'),
                    h('option', { value: 'hold', selected: o.kind === 'hold' }, 'Hold a zone'),
                ),
                field(
                    'Points',
                    numInput(o.points, (v) => save((x) => (x.objectives[i]!.points = v)), {
                        label: 'Points',
                        min: MODE_LIMITS.objectivePoints[0],
                        max: MODE_LIMITS.objectivePoints[1],
                    }),
                ),
                o.kind === 'hold'
                    ? field(
                          'Seconds',
                          numInput(o.seconds, (v) => save((x) => (x.objectives[i]!.seconds = v)), {
                              label: 'Seconds',
                              min: 1,
                              max: 600,
                          }),
                      )
                    : h('span'),
                field(
                    'Radius',
                    numInput(o.radius, (v) => save((x) => (x.objectives[i]!.radius = v)), {
                        label: 'Radius',
                        min: 50,
                        max: 5000,
                        step: 50,
                    }),
                ),
                h(
                    'button',
                    {
                        class: 'x',
                        type: 'button',
                        'aria-label': 'Remove objective',
                        onclick: () => save((x) => x.objectives.splice(i, 1), true),
                    },
                    icon('x', 12),
                ),
            ),
        );
        const pickupChoice = m.pickupRespawn < 0 ? 'map' : m.pickupRespawn === 0 ? 'never' : 'after';
        const protChoice = m.spawnProtection < 0 ? 'default' : String(m.spawnProtection);
        return h(
            'div',
            { class: 'mp-form' },
            group(
                'About',
                'info',
                h(
                    'div',
                    { class: 'mp-row2' },
                    field(
                        'Name',
                        textInput(m.name, (s) => save((x) => (x.name = s), true), 'Mode name', 'Shown in the menu', 40),
                    ),
                    field(
                        'Id',
                        textInput(
                            m.id,
                            (s) => save((x) => (x.id = s.replace(/[^A-Za-z0-9_-]/g, '-')), true),
                            'Mode id',
                            'letters, digits, - _',
                            32,
                        ),
                    ),
                ),
                field(
                    'Description',
                    textInput(
                        m.description,
                        (s) => save((x) => (x.description = s)),
                        'Description',
                        'One line for the menu',
                        120,
                    ),
                ),
            ),
            group(
                'Players and teams',
                'character',
                h(
                    'div',
                    { class: 'mp-row2' },
                    select(
                        'Play as',
                        [
                            ['dm', 'Everyone for themselves'],
                            ['tdm', 'Teams'],
                        ],
                        m.base,
                        (v) =>
                            save((x) => {
                                x.base = v as ModeDef['base'];
                                x.teams = v === 'tdm' ? Math.max(2, x.teams) : 0;
                            }, true),
                    ),
                    teams >= 2
                        ? select(
                              'Number of teams',
                              [
                                  ['2', '2 teams'],
                                  ['3', '3 teams'],
                                  ['4', '4 teams'],
                              ],
                              String(teams),
                              (v) => save((x) => (x.teams = Number(v)), true),
                          )
                        : h('span'),
                ),
                teams >= 2
                    ? h(
                          'div',
                          { class: 'mp-f' },
                          h('span', {}, 'Team names and colours (HUD and scoreboard)'),
                          ...teamRows,
                      )
                    : null,
                teams >= 2
                    ? h(
                          'div',
                          { class: 'mp-row2' },
                          check('Friendly fire (team mates can hurt each other)', m.friendlyFire, (v) =>
                              save((x) => (x.friendlyFire = v), true),
                          ),
                          m.friendlyFire ? n('Points for a team kill', 'scoreTeamKill', MODE_LIMITS.score) : h('span'),
                      )
                    : null,
            ),
            group(
                'How a match is won',
                'star',
                h(
                    'div',
                    { class: 'mp-grid3' },
                    n('Lives each', 'lives', MODE_LIMITS.lives, 1, true),
                    n('Score limit', 'fragLimit', MODE_LIMITS.fragLimit, 1, true),
                    field(
                        'Time limit (minutes)',
                        numInput(m.timeLimit / 60, (v) => save((x) => (x.timeLimit = Math.round(v * 60)), true), {
                            min: 0,
                            max: MODE_LIMITS.timeLimit[1] / 60,
                            step: 0.5,
                            label: 'Time limit in minutes',
                        }),
                    ),
                ),
                hint('0 means no limit.'),
                m.lives > 0
                    ? hint(
                          `Out of lives = out of the match. The last ${teams >= 2 ? 'team' : 'player'} with lives left wins; ` +
                              'players who join a running match watch until the next one.',
                      )
                    : null,
                m.lives === 0 && m.fragLimit === 0 && m.timeLimit === 0
                    ? h(
                          'div',
                          { class: 'mp-warnline' },
                          icon('warn', 12),
                          'Nothing ends this match: set lives, a score limit or a time limit.',
                      )
                    : null,
                h(
                    'div',
                    { class: 'mp-f' },
                    h('span', {}, 'Objectives (need flag, base or zone markers on the map)'),
                    ...objs,
                ),
                h(
                    'div',
                    { class: 'mp-btnrow' },
                    btn(
                        'Capture the flag',
                        () =>
                            save(
                                (x) => x.objectives.push({ kind: 'capture', points: 1, seconds: 10, radius: 300 }),
                                true,
                            ),
                        { ico: 'plus', disabled: m.objectives.length >= 8 },
                    ),
                    btn(
                        'Hold a zone',
                        () =>
                            save((x) => x.objectives.push({ kind: 'hold', points: 1, seconds: 15, radius: 700 }), true),
                        { ico: 'plus', disabled: m.objectives.length >= 8 },
                    ),
                ),
            ),
            group(
                'Scoring',
                'sliders',
                h(
                    'div',
                    { class: 'mp-grid3' },
                    n('Points per kill', 'scoreKill', MODE_LIMITS.score, 1, true),
                    n('Points per death', 'scoreDeath', MODE_LIMITS.score, 1, true),
                    n('Bonus per objective', 'scoreObjective', MODE_LIMITS.score),
                ),
                hint('Deaths without a killer (falls) are not counted.'),
            ),
            group(
                'Respawn',
                'history',
                h(
                    'div',
                    { class: 'mp-row2' },
                    n('Respawn delay, seconds', 'respawnDelay', MODE_LIMITS.respawnDelay),
                    select(
                        'Spawn protection',
                        [
                            ['default', 'Game default (2 s)'],
                            ['0', 'None'],
                            ['1', '1 second'],
                            ['3', '3 seconds'],
                            ['5', '5 seconds'],
                            ['10', '10 seconds'],
                            ...(['default', '0', '1', '3', '5', '10'].includes(protChoice)
                                ? []
                                : ([[protChoice, `${protChoice} seconds`]] as [string, string][])),
                        ],
                        protChoice,
                        (v) => save((x) => (x.spawnProtection = v === 'default' ? -1 : Number(v)), true),
                    ),
                ),
                n("Start health (0 = the map's)", 'startHealth', MODE_LIMITS.startHealth, 10),
            ),
            group(
                'Weapons and pickups',
                'item',
                h(
                    'div',
                    { class: 'mp-row2' },
                    select(
                        'Pickups come back',
                        [
                            ['map', 'As the map sets each one'],
                            ['never', 'Never (taken is gone)'],
                            ['after', 'After a fixed time'],
                        ],
                        pickupChoice,
                        (v) => save((x) => (x.pickupRespawn = v === 'map' ? -1 : v === 'never' ? 0 : 30), true),
                    ),
                    pickupChoice === 'after'
                        ? n('Seconds until back', 'pickupRespawn', [1, MODE_LIMITS.pickupRespawn[1]], 5, true)
                        : h('span'),
                ),
                resList("Extra starting items (on top of the map's)", 'loadout', 'none'),
                resList('Weapons that can hurt (empty = all)', 'weapons', 'all weapons'),
            ),
        );
    }

    // ---- Export page ----

    function exportPage(): HTMLElement {
        const d = st.draft;
        if (!packFields.touched) {
            packFields.id = d.id ? `mp-${d.id}`.slice(0, 64) : '';
            packFields.name = d.name;
        }
        const f = (label: string, key: 'id' | 'name' | 'author' | 'version', ph = ''): HTMLElement => {
            const el = textInput(
                packFields[key],
                (s) => ((packFields[key] = s), (packFields.touched = true), refreshExport()),
                label,
                ph,
                64,
            );
            return field(label, el);
        };
        const pv = h('div', { class: 'mp-pv' });
        const drawPv = (): void => {
            fill(
                pv,
                preview
                    ? h('img', { src: preview.url, alt: 'Map preview' })
                    : h(
                          'div',
                          { class: 'dim pad' },
                          'No picture yet. Capture one from the viewport (top-down) so the menu shows it.',
                      ),
            );
        };
        const target = host.target();
        const out = h('pre', { class: 'mod-report mp-report' }, report);
        const errors = issues.filter((i) => i.severity === 'error').length;
        const doExport = async (mode: 'zip' | 'folder' | 'ini'): Promise<void> => {
            try {
                const blockers = exportBlockers(d, env(false));
                if (blockers.length)
                    throw new Error(
                        `Fix the errors first: ${blockers[0]}${blockers.length > 1 ? ` (+${blockers.length - 1})` : ''}`,
                    );
                if (mode === 'ini') {
                    host.download(`${d.id}.ini`, serializeMapFile(d, st.modes), 'text/plain');
                    report = `Downloaded ${d.id}.ini (add it to a pack or the game folder).`;
                    return void refreshExport();
                }
                if (!preview) await capture(true);
                const meta: ModMeta = {
                    id: packFields.id.trim(),
                    name: packFields.name.trim() || d.name,
                    version: packFields.version.trim() || undefined,
                    author: packFields.author.trim() || undefined,
                    description: `Multiplayer map ${d.name} made with the Drakan editor.`,
                    target: target ?? { profile: '*' },
                };
                const files = buildArenaPackFiles(d, st.modes, meta, preview?.png ?? null);
                const lines = Object.entries(files).map(([p, b]) => `${p}  ${b.length} bytes`);
                if (mode === 'zip') {
                    host.download(`${meta.id}.zip`, zipModFiles(files) as BlobPart, 'application/zip');
                    lines.push(`Downloaded ${meta.id}.zip. Put it in the game's mods folder and turn it on.`);
                } else {
                    const where = await writePackToModsFolder({ meta, files });
                    if (where === null) return;
                    lines.push(`Written to ${where} and turned on.`);
                }
                lines.push(`Base level ${d.level} is used from the disc; no level file is needed.`);
                report = lines.join('\n');
                ctx.toast(`Exported ${meta.id}`, 'ok');
                host.setStatus(`Multiplayer map pack '${meta.id}' exported.`);
            } catch (e) {
                report = e instanceof Error ? e.message : String(e);
                ctx.toast(report, 'error');
            }
            refreshExport();
        };
        const capture = async (quiet = false): Promise<void> => {
            const png = await capturePreview(ctx.view(), d, [overlayRoot()]);
            if (!png) {
                if (!quiet) ctx.toast('Draw the bounds or place spawn points first, then capture.', 'warn');
                return;
            }
            if (preview) URL.revokeObjectURL(preview.url);
            preview = { png, url: URL.createObjectURL(new Blob([png as BlobPart], { type: 'image/png' })) };
            drawPv();
            if (!quiet) ctx.toast('Preview captured', 'ok');
        };
        const refreshExport = (): void => {
            if (tab === 'export') render();
        };
        drawPv();
        return h(
            'div',
            { class: 'mp-cols' },
            section(
                'Mod pack',
                f('Pack id', 'id', 'letters, digits . _ -'),
                f('Pack name', 'name'),
                h('div', { class: 'mp-row2' }, f('Version', 'version'), f('Author', 'author')),
                h('div', { class: 'dim small' }, target ? 'For your disc.' : 'Works with any disc.'),
                h(
                    'div',
                    { class: 'mp-btnrow' },
                    btn('Download zip', () => void doExport('zip'), {
                        cls: 'primary',
                        ico: 'download',
                        disabled: !!errors || !packFields.id.trim(),
                    }),
                    btn('Write to mods folder', () => void doExport('folder'), {
                        ico: 'open',
                        disabled: !!errors || !packFields.id.trim(),
                        title: "Write into the game's mods folder and turn it on",
                    }),
                    btn('Map INI only', () => void doExport('ini'), { ico: 'save', disabled: !!errors }),
                ),
                errors
                    ? h(
                          'div',
                          { class: 'mp-problems' },
                          h('div', {}, icon('warn', 12), `${errors} error(s) block the export; see Validation.`),
                      )
                    : null,
                out,
            ),
            section(
                'Preview picture',
                pv,
                h(
                    'div',
                    { class: 'mp-btnrow' },
                    btn('Capture top-down view', () => void capture(), { ico: 'camera' }),
                    btn('Remove', () => (preview && URL.revokeObjectURL(preview.url), (preview = null), render()), {
                        disabled: !preview,
                    }),
                ),
                h('div', { class: 'dim small' }, 'Shown in the multiplayer map list.'),
            ),
            section(
                'Contents',
                h('div', {}, `${d.name || d.id || 'Map'}${preview ? ' + preview picture' : ''}`),
                h(
                    'details',
                    { class: 'mp-preview' },
                    h('summary', {}, 'Advanced: map file'),
                    h('pre', {}, serializeMapFile(d, st.modes)),
                ),
            ),
        );
    }

    // ---- issues (Validation tab + this panel) ----

    const issueBox = h('div', { class: 'mp-issues' });
    function reveal(i: ArenaIssue): void {
        const t = i.target;
        if (t.kind === 'map') {
            tab = 'map';
            render();
        } else if (t.kind === 'bounds') {
            if (st.draft.bounds) overlay.frameRect(st.draft.bounds);
            st.select({ kind: 'bounds', corner: 0 });
        } else {
            st.select({ kind: t.kind, uid: t.uid });
            frame(overlay.focus({ kind: t.kind, uid: t.uid }));
            tab = 'place';
            render();
        }
    }
    function renderIssues(): void {
        if (tab !== 'export' && tab !== 'place' && tab !== 'map') return fill(issueBox);
        const shown = issues.filter((i) => i.severity !== 'info').slice(0, 6);
        fill(
            issueBox,
            ...shown.map((i) =>
                h(
                    'button',
                    { class: `mp-issue ${i.severity}`, type: 'button', onclick: () => reveal(i) },
                    icon('warn', 12),
                    i.message,
                ),
            ),
            issues.length > shown.length
                ? h('div', { class: 'dim small pad' }, `${issues.length - shown.length} more in the Validation tab`)
                : null,
        );
    }

    // ---- render ----

    const overlayRoot = (): import('three').Object3D => overlay.rootObject();

    function renderTabs(): void {
        const names: [Tab, string][] = [
            ['map', 'Map'],
            ['place', 'Place'],
            ['world', 'World'],
            ['modes', 'Modes'],
            ['export', 'Export'],
        ];
        fill(
            tabsBar,
            ...names.map(([id, label]) =>
                h(
                    'button',
                    {
                        class: `mp-tab${tab === id ? ' active' : ''}`,
                        type: 'button',
                        role: 'tab',
                        'data-mp-tab': id,
                        onclick: () => ((tab = id), render()),
                    },
                    label,
                ),
            ),
        );
    }

    function render(): void {
        title.textContent = st.active ? `${st.draft.name || '(unnamed)'} on ${st.draft.level}` : 'not started';
        tabsBar.hidden = !st.active;
        if (!st.active) {
            startKey = startKeyNow();
            fill(body, startCard());
            chip.className = 'mp-chip hidden';
            return;
        }
        renderTabs();
        const page =
            tab === 'map'
                ? mapPage()
                : tab === 'place'
                  ? placePage()
                  : tab === 'world'
                    ? worldPage()
                    : tab === 'modes'
                      ? modesPage()
                      : exportPage();
        fill(body, page, tab === 'world' || tab === 'modes' ? null : issueBox);
        renderIssues();
    }
    const renderSoon = raf(render);

    st.on((e) => {
        if (e === 'mode') {
            render();
            revalidate();
            recompute();
        } else if (e === 'draft') {
            window.clearTimeout(timer);
            timer = window.setTimeout(() => {
                dirtyPlan = true;
                recompute();
                if (tab === 'world') render();
            }, 250);
            window.clearTimeout(issueTimer);
            issueTimer = window.setTimeout(revalidate, 350);
            // typing in a field re-renders only when the structure changed: lists, props
            if (tab === 'place' || tab === 'map' || tab === 'modes') renderSoon();
        } else if (e === 'selection') {
            if (tab === 'place') renderSoon();
        } else if (e === 'tool') renderSoon();
        else if (e === 'modes') renderSoon();
    });

    chip.addEventListener('click', () => {
        const first = issues.find((i) => i.severity !== 'info');
        if (first) reveal(first);
        else ((tab = 'export'), render());
    });

    // The start card lists the disc's levels and the open level: refresh it when either changes.
    window.setInterval(() => {
        if (st.active) return;
        if (startKeyNow() !== startKey) {
            render();
        }
    }, 1000);

    render();
    return {
        el: root,
        overlay,
        issues: () => issues,
        reveal,
        onIssues: (fn) => void issueListeners.add(fn),
        onSelection() {
            if (!st.active) return;
            if (ctx.view().selection.size && st.sel) st.select(null);
            if (tab === 'world') renderSoon();
        },
        onChange() {
            if (!st.active) return;
            dirtyPlan = true;
            window.clearTimeout(timer);
            timer = window.setTimeout(() => recompute(), 250);
            window.clearTimeout(issueTimer);
            issueTimer = window.setTimeout(revalidate, 350);
        },
        onVisibility() {},
        onLoad() {
            infoMap = null;
            const d = ctx.doc();
            if (st.active && d.level !== st.draft.level) {
                st.stop();
                ctx.toast(
                    'Another level was opened: the multiplayer map was closed (its draft stays in this browser).',
                    'info',
                );
            }
            recompute();
            revalidate();
            render();
        },
    };
    void host;
    void insideBounds;
    void serializeArena;
}

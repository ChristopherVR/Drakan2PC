// Project Settings > Campaign: "Make this a campaign". The project's levels become a campaign of their own on the
// game's title card ("Choose your game", next to Drakan): its title, a short description, a cover picture, the level
// New Game opens and its own save folder. The checks say whether a player can walk from the start through every level.
// Pack format: docs/reference/modding.md "Campaign packs" (editor-core mod.ts / project.ts).
import {
    COVER_PALETTES,
    CAMPAIGN_DESCRIPTION_MAX,
    LevelDocument,
    campaignIdProblem,
    campaignSlug,
    destinationLevelName,
    newCampaign,
    renderCoverPng,
    validateCampaign,
    withExitOverrides,
    type CampaignCover,
    type ExitIndex,
    type ExitOverride,
    type ModProject,
    type ProjectCampaign,
    type ProjectLevel,
    type ProjectIssue,
} from '@drakan/editor-core';
import type { ExitIndexData } from '../../logicdata.ts';
import type { LevelInfo } from '../../rpc.ts';
import { h, icon } from '../dom.ts';
import { getMenusProvider, onMenusChanged } from '../menus/bridge.ts';
import { projects } from './store.ts';
import './campaign.css';

/** The text of a level string (the story session, with the user's edits). */
export interface CampaignStrings {
    ensure(): Promise<unknown>;
    text(file: string, id: number): string | undefined;
    changed(file: string, id: number): boolean;
}

export interface CampaignHost {
    levels(): LevelInfo[];
    loadExits(): Promise<ExitIndexData>;
    /** The edited document of a project level (open level, else its autosave), null when unedited. */
    levelDoc(title: string): Promise<LevelDocument | null>;
    strings: CampaignStrings;
    toast(msg: string, kind?: 'info' | 'error' | 'ok' | 'warn'): void;
}

export interface CampaignPane {
    /** The pane (the same element every time: the Project panel re-adds it on each render). */
    el(): HTMLElement;
    /** Errors that block an export (settings, and the exits once they were read). */
    errors(): string[];
}

const norm = (s: string): string => s.toLowerCase().replace(/[^a-z0-9]/g, '');

/**
 * The disc level whose exits a level has: a level copy of the project (`Surdana Copy`) has the exits and destination
 * strings of its source (`Level 1A - Surdana`), since the exit index only knows the disc's levels. Other names as is.
 */
export function exitSourceLevel(levels: readonly ProjectLevel[], name: string): string {
    return levels.find((l) => l.copyOf && norm(l.title) === norm(name))?.copyOf ?? name;
}

/**
 * The exits a project changed: an "Only this exit" retarget (the exit object's own destination string, field 0) or an
 * edit of a class's shared string. Both read from the level documents and the story strings.
 */
export async function projectExitOverrides(
    p: ModProject,
    data: ExitIndexData,
    levelDoc: (title: string) => Promise<LevelDocument | null>,
    strings: Pick<CampaignStrings, 'text' | 'changed'>,
): Promise<ExitOverride[]> {
    const out: ExitOverride[] = [];
    for (const l of p.levels) {
        const src = data.levels.find((x) => norm(x.name) === norm(l.copyOf ?? l.title));
        if (!src) continue;
        const doc = await levelDoc(l.title);
        const classes = Object.values(src.classes);
        for (const e of src.exits) {
            const cls = classes.find((c) => c.name === e.name && c.destRef);
            const file = cls?.destRef?.file;
            if (!file) continue;
            const own = doc?.get(e.id)?.fields?.find((f) => f.fieldIndex === 0 && typeof f.value === 'number');
            const id = own ? (own.value as number) : cls.destRef!.id;
            if (!own && !strings.changed(file, id)) continue;
            const text = strings.text(file, id);
            if (text) out.push({ level: l.title, exitId: e.id, dest: destinationLevelName(text) });
        }
    }
    return out;
}

/** A picture of the user's own, scaled and cropped to 960 x 540, as base64 PNG. */
async function pictureToCover(f: File): Promise<string> {
    const bmp = await createImageBitmap(f);
    const c = document.createElement('canvas');
    c.width = 960;
    c.height = 540;
    const g = c.getContext('2d')!;
    const s = Math.max(960 / bmp.width, 540 / bmp.height);
    const w = bmp.width * s;
    const hh = bmp.height * s;
    g.drawImage(bmp, (960 - w) / 2, (540 - hh) / 2, w, hh);
    const blob = await new Promise<Blob>((res, rej) =>
        c.toBlob((b) => (b ? res(b) : rej(new Error('no PNG'))), 'image/png'),
    );
    const bytes = new Uint8Array(await blob.arrayBuffer());
    let bin = '';
    for (let i = 0; i < bytes.length; i += 0x8000) bin += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
    return btoa(bin);
}

const SEV_ICON = { error: 'clear', warning: 'warn', info: 'info' } as const;

export function createCampaignPane(host: CampaignHost): CampaignPane {
    let exits: ExitIndex | null = null;
    /** The disc's exits, read once ("Check the exits" reads them again); a level edit only redoes the overrides. */
    let exitData: ExitIndexData | null = null;
    let exitsState: 'none' | 'loading' | 'done' | 'failed' = 'none';
    let exitsSig = '';
    let issues: ProjectIssue[] = [];
    let previewUrl = '';
    let previewKey = '';

    const root = h('section', { class: 'pjx-pane cpg-pane', id: 'pj-campaign' });
    const head = h('header', { class: 'pjx-pane-head' });
    const body = h('div', { class: 'pjx-pane-body cpg-body' });
    root.append(head, body);

    const update = (patch: Partial<ProjectCampaign>): void =>
        projects.update((q) => (q.campaign ? { ...q, campaign: { ...q.campaign, ...patch } } : q));

    function toggle(p: ModProject): HTMLElement {
        const on = !!p.campaign?.enabled;
        return h('button', {
            type: 'button',
            class: `ui-switch${on ? ' on' : ''}`,
            id: 'cpg-on',
            role: 'switch',
            'aria-checked': on ? 'true' : 'false',
            'aria-label': 'Make this a campaign',
            title: on ? 'Turn the campaign off (the levels stay in the project)' : 'Make this a campaign',
            onclick: () => {
                projects.update((q) =>
                    q.campaign
                        ? { ...q, campaign: { ...q.campaign, enabled: !q.campaign.enabled } }
                        : { ...q, campaign: newCampaign(q) },
                );
            },
        });
    }

    // Rynn can learn and cast spells in this campaign (manifest `campaign_spells`); Order of the Flame has none.
    function spellsSwitch(c: ProjectCampaign): HTMLElement {
        const on = c.spells !== false;
        return h(
            'div',
            { class: 'cpg-switch-row' },
            h('button', {
                type: 'button',
                class: `ui-switch${on ? ' on' : ''}`,
                id: 'cpg-spells',
                role: 'switch',
                'aria-checked': on ? 'true' : 'false',
                'aria-label': 'Rynn can learn and cast spells',
                onclick: () => update({ spells: !on }),
            }),
            h(
                'span',
                { class: 'cpg-switch-text' },
                on ? 'Rynn can learn and cast spells' : 'No spells in this campaign',
            ),
        );
    }

    // Defeated creatures, barrels, crates and chests drop gold and trinkets (manifest `campaign_treasure`); Order of the
    // Flame has no shops, so its campaign has none.
    function treasureSwitch(c: ProjectCampaign): HTMLElement {
        const on = c.treasure !== false;
        return h(
            'div',
            { class: 'cpg-switch-row' },
            h('button', {
                type: 'button',
                class: `ui-switch${on ? ' on' : ''}`,
                id: 'cpg-treasure',
                role: 'switch',
                'aria-checked': on ? 'true' : 'false',
                'aria-label': 'Creatures drop gold and trinkets',
                onclick: () => update({ treasure: !on }),
            }),
            h(
                'span',
                { class: 'cpg-switch-text' },
                on ? 'Creatures drop gold and trinkets' : 'No gold or trinket drops',
            ),
        );
    }

    const field = (
        label: string,
        id: string,
        input: HTMLElement,
        hint?: string | null,
        bad?: string | null,
    ): HTMLElement =>
        h(
            'div',
            { class: 'pjx-prop cpg-prop' },
            h('label', { for: id }, label),
            h(
                'div',
                { class: 'pjx-prop-v' },
                input,
                bad || hint ? h('div', { class: `pjx-prop-hint${bad ? ' bad' : ''}` }, bad ?? hint ?? '') : null,
            ),
        );
    const text = (id: string, value: string, on: (v: string) => void, placeholder = '', area = false): HTMLElement => {
        const el = area
            ? h('textarea', { rows: 2, id, spellcheck: 'true', placeholder, class: 'ui-input' })
            : h('input', { type: 'text', id, spellcheck: 'false', placeholder, class: 'ui-input' });
        el.value = value;
        el.addEventListener('input', () => on(el.value));
        return el;
    };

    function coverBlock(c: ProjectCampaign): HTMLElement {
        const img = h('img', { class: 'cpg-cover-img', alt: 'Cover picture', id: 'cpg-cover-img' });
        const key = JSON.stringify(c.cover);
        if (c.cover && key !== previewKey) {
            previewKey = key;
            if (previewUrl) URL.revokeObjectURL(previewUrl);
            const cover: CampaignCover = c.cover;
            const bytes =
                cover.kind === 'generated'
                    ? renderCoverPng({ ...cover, width: 480, height: 270 }) // the preview at half size: quick
                    : Uint8Array.from(atob(cover.png), (ch) => ch.charCodeAt(0));
            previewUrl = URL.createObjectURL(new Blob([bytes as BlobPart], { type: 'image/png' }));
        }
        if (c.cover) img.src = previewUrl;
        const gen = c.cover?.kind === 'generated' ? c.cover : null;
        const pal = h(
            'select',
            { class: 'ui-input cpg-sel', id: 'cpg-palette', 'aria-label': 'Colours of the picture' },
            ...COVER_PALETTES.map((x) => h('option', { value: x.id }, x.label)),
        );
        pal.value = gen?.palette ?? COVER_PALETTES[0]!.id;
        pal.disabled = !gen;
        pal.addEventListener('change', () => gen && update({ cover: { ...gen, palette: pal.value } }));
        const dragon = h('input', { type: 'checkbox', id: 'cpg-dragon', class: 'ui-check' });
        dragon.checked = gen?.dragon ?? true;
        dragon.disabled = !gen;
        dragon.addEventListener('change', () => gen && update({ cover: { ...gen, dragon: dragon.checked } }));
        const file = h('input', { type: 'file', accept: 'image/png,image/jpeg,image/webp', hidden: true });
        file.addEventListener('change', () => {
            const f = file.files?.[0];
            file.value = '';
            if (!f) return;
            void pictureToCover(f)
                .then((png) => update({ cover: { kind: 'picked', png, name: f.name } }))
                .catch((e: unknown) => host.toast(`Could not read that picture: ${String(e)}`, 'error'));
        });
        const btn = (id: string, label: string, title: string, run: () => void): HTMLElement =>
            h('button', { type: 'button', class: 'ui-btn sm', id, title, onclick: run }, label);
        return h(
            'div',
            { class: 'cpg-cover' },
            h(
                'div',
                { class: 'cpg-cover-frame' },
                c.cover ? img : h('div', { class: 'cpg-cover-none' }, 'No picture'),
                h('div', { class: 'cpg-cover-title' }, c.title || c.id),
            ),
            h(
                'div',
                { class: 'cpg-cover-tools' },
                btn('cpg-reroll', gen ? 'New picture' : 'Make a picture', 'Draw another mountain view', () =>
                    update({
                        cover: {
                            kind: 'generated',
                            palette: gen?.palette ?? COVER_PALETTES[0]!.id,
                            seed: Math.floor(Math.random() * 0x7fffffff),
                            dragon: gen?.dragon ?? true,
                        },
                    }),
                ),
                pal,
                h('label', { class: 'cpg-inline', for: 'cpg-dragon' }, dragon, 'Dragon'),
            ),
            h(
                'div',
                { class: 'cpg-cover-tools' },
                btn('cpg-pick', 'Use my own picture...', 'A picture from your own files (scaled to 960 x 540)', () =>
                    file.click(),
                ),
                c.cover ? btn('cpg-nocover', 'No picture', 'Show the title only', () => update({ cover: null })) : null,
                file,
            ),
            c.cover?.kind === 'picked' ? h('div', { class: 'pjx-prop-hint' }, `Your picture: ${c.cover.name}`) : null,
        );
    }

    function checksBlock(p: ModProject): HTMLElement {
        const rows = issues.map((i) =>
            h(
                'li',
                { class: `cpg-issue ${i.severity}` },
                (() => {
                    const s = icon(SEV_ICON[i.severity], 13);
                    s.classList.add('pjx-sev', i.severity);
                    return s;
                })(),
                h('span', {}, i.message),
            ),
        );
        const status =
            exitsState === 'loading'
                ? 'Reading the exits of every level...'
                : exitsState === 'failed'
                  ? 'The exits could not be read.'
                  : exitsState === 'none'
                    ? host.levels().length
                        ? 'The exits are not checked yet.'
                        : 'Open your disc image to check the exits.'
                    : '';
        return h(
            'div',
            { class: 'cpg-checks' },
            h(
                'div',
                { class: 'cpg-checks-head' },
                h('span', { class: 'pjx-sub' }, 'Campaign checks'),
                h('span', { class: 'spacer' }),
                h(
                    'button',
                    {
                        type: 'button',
                        class: 'ui-btn ghost sm',
                        id: 'cpg-check',
                        disabled: exitsState === 'loading' || !host.levels().length || !p.levels.length,
                        title: 'Follow every exit from the start level',
                        onclick: () => void checkExits(true),
                    },
                    exitsState === 'done' ? 'Check again' : 'Check the exits',
                ),
            ),
            status ? h('div', { class: 'pjx-prop-hint' }, status) : null,
            rows.length
                ? h('ul', { class: 'cpg-issues' }, ...rows)
                : h(
                      'div',
                      { class: 'cpg-ok' },
                      exitsState === 'done' ? 'Ready: every level can be reached.' : 'No problems with the settings.',
                  ),
        );
    }

    function render(): void {
        const p = projects.get();
        if (!p) return;
        const c = p.campaign;
        const active = document.activeElement as HTMLInputElement | null;
        const focusId = active && root.contains(active) ? active.id : '';
        const pos = focusId && 'selectionStart' in active! ? active!.selectionStart : null;
        head.replaceChildren(
            h('h2', {}, 'Campaign'),
            ...(c?.enabled ? [h('span', { class: 'pjx-count' }, c.id)] : []),
            h('span', { class: 'spacer' }),
            h('label', { class: 'cpg-inline', for: 'cpg-on' }, 'Make this a campaign'),
            toggle(p),
        );
        if (!c?.enabled) {
            body.replaceChildren(
                h(
                    'p',
                    { class: 'cpg-intro' },
                    'A campaign gets its own entry on the game\'s "Choose your game" card, next to Drakan: your levels in order, the level New Game starts in, and saves of its own.',
                ),
            );
            return;
        }
        const copies = p.levels.filter((l) => l.copyOf);
        const start = h('select', { class: 'ui-input cpg-sel', id: 'cpg-start' });
        start.append(h('option', { value: '' }, copies.length ? 'Choose a level' : 'Add a level copy first'));
        for (const l of p.levels)
            start.append(
                h(
                    'option',
                    { value: l.title, disabled: !l.copyOf },
                    l.copyOf ? l.title : `${l.title} (an original Drakan level: make a copy to use it)`,
                ),
            );
        start.value = c.start ?? '';
        start.addEventListener('change', () => update({ start: start.value || null }));
        const idBad = campaignIdProblem(c.id);
        const savesBad = /^[A-Za-z0-9_-]{1,32}$/.test(c.saves) ? null : 'Letters, digits, _ and - only (1 to 32)';
        const menus = getMenusProvider();
        body.replaceChildren(
            h(
                'div',
                { class: 'pjx-props cpg-props' },
                field(
                    'Title',
                    'cpg-title',
                    text('cpg-title', c.title, (v) => update({ title: v }), 'The Lost Valley'),
                    'Shown on the game card',
                    c.title.trim() ? null : 'Give it a title',
                ),
                field(
                    'Short text',
                    'cpg-desc',
                    text(
                        'cpg-desc',
                        c.description,
                        (v) => update({ description: v }),
                        'One line about the story',
                        true,
                    ),
                    `${c.description.length} / ${CAMPAIGN_DESCRIPTION_MAX}`,
                    c.description.length > CAMPAIGN_DESCRIPTION_MAX ? 'Too long: the card cuts it' : null,
                ),
                field('Starts in', 'cpg-start', start, 'New Game opens this level'),
                field(
                    'Campaign id',
                    'cpg-id',
                    text(
                        'cpg-id',
                        c.id,
                        (v) => {
                            const q = projects.get()?.campaign;
                            // The save folder follows the id until the user gives it its own name.
                            update({ id: v.trim(), ...(q && q.saves === q.id ? { saves: v.trim() } : {}) });
                        },
                        campaignSlug(p.name),
                    ),
                    'Small letters, digits, _ and -',
                    idBad,
                ),
                field(
                    'Save folder',
                    'cpg-saves',
                    text('cpg-saves', c.saves, (v) => update({ saves: v.trim() }), c.id),
                    `Saves go to mc0_${c.saves || c.id}, apart from Drakan's`,
                    savesBad,
                ),
            ),
            h('div', { class: 'pjx-sub' }, 'Gameplay'),
            h(
                'div',
                { class: 'pjx-props cpg-props' },
                field(
                    'Spells',
                    'cpg-spells',
                    spellsSwitch(c),
                    c.spells === false
                        ? 'No Spells page, no mana orb and no casting, as in Order of the Flame. Circle still parries.'
                        : 'Rynn learns spells from scrolls and casts them, as in Drakan.',
                ),
                field(
                    'Drops',
                    'cpg-treasure',
                    treasureSwitch(c),
                    c.treasure === false
                        ? 'Creatures, barrels, crates and chests leave only the items the level gives them, as in Order of the Flame.'
                        : 'Defeated creatures, broken barrels and opened chests scatter gold and trinkets, as in Drakan.',
                ),
            ),
            h('div', { class: 'pjx-sub' }, 'Cover picture'),
            coverBlock(c),
            menus?.dirty()
                ? h(
                      'div',
                      { class: 'cpg-note' },
                      `Your Menus changes ship with the campaign and show only while "${c.title || c.id}" is chosen.`,
                  )
                : h(
                      'div',
                      { class: 'cpg-note dim' },
                      'Menus: rename or reorder the title and pause menu entries in the Menus tab; they show only while this campaign is chosen.',
                  ),
            checksBlock(p),
        );
        if (focusId) {
            const el = root.querySelector<HTMLInputElement>(`#${focusId}`);
            if (el) {
                el.focus({ preventScroll: true });
                if (pos !== null && pos !== undefined && typeof el.setSelectionRange === 'function')
                    el.setSelectionRange(pos, pos);
            }
        }
    }

    /**
     * Read the exits and follow them from the start. `force` = the user's "Check the exits" (reads the disc's exits
     * again, says so in a toast); otherwise a quiet refresh after a level edit that keeps the last result on screen.
     */
    async function checkExits(force: boolean): Promise<void> {
        const p = projects.get();
        if (!p?.campaign?.enabled || !host.levels().length || exitsState === 'loading') return;
        if (force || !exitData) {
            exitsState = 'loading';
            recheck();
        }
        try {
            const data = force || !exitData ? await host.loadExits() : exitData;
            exitData = data;
            await host.strings.ensure().catch(() => undefined);
            const cur = projects.get() ?? p;
            const index: ExitIndex = { levels: new Map(data.levels.map((l) => [l.name, l.exits])) };
            const over = await projectExitOverrides(cur, data, host.levelDoc, host.strings);
            exits = withExitOverrides(cur, index, over);
            exitsSig = JSON.stringify(cur.levels);
            exitsState = 'done';
            if (force) host.toast(`Checked the exits of ${cur.levels.length} level(s).`, 'ok');
        } catch (e) {
            exitsState = 'failed';
            if (force) host.toast(`Could not read the exits: ${String(e)}`, 'error');
        }
        recheck();
    }

    function recheck(): void {
        const p = projects.get();
        if (!p) return;
        // A changed level list makes the exit check stale.
        if (exits && JSON.stringify(p.levels) !== exitsSig) {
            exits = null;
            exitsState = 'none';
        }
        issues = validateCampaign(p, { discLevels: host.levels().map((l) => l.name), exits });
        render();
    }

    let timer = 0;
    // The note under the cover says whether the Menus tab's changes ship with the campaign.
    let menusDirty = getMenusProvider()?.dirty() ?? false;
    onMenusChanged(() => {
        const d = getMenusProvider()?.dirty() ?? false;
        if (d === menusDirty) return;
        menusDirty = d;
        render();
    });
    projects.on(() => {
        window.clearTimeout(timer);
        timer = window.setTimeout(recheck, 120);
    });
    // Level edits (an exit pointed somewhere else) change the overrides: follow the exits again with the disc's exits
    // already read. (Autosaves and story loads also report edits, so the result must not just be thrown away.)
    let editTimer = 0;
    projects.onEdit(() => {
        if (exitsState !== 'done') return;
        window.clearTimeout(editTimer);
        editTimer = window.setTimeout(() => void checkExits(false), 300);
    });

    return {
        el() {
            if (!head.childElementCount) recheck();
            return root;
        },
        errors: () => issues.filter((i) => i.severity === 'error').map((i) => i.message),
    };
}

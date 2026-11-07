/**
 * Place: the Level Editor's left sidebar, like Unreal's Place Actors panel. Plain groups down the side (Characters,
 * Creatures, Items, Lights, Triggers and logic, Cut scenes ...), the things in the chosen group beside them. Drag a row
 * into the viewport to place it where the pointer meets the ground (the Content Browser's drag: assetdrag.ts), or
 * double-click / press Enter to place it in the middle of the view. Groups also list the ready-made set-ups that make
 * something (a cut scene, a quest, a puzzle), which open the editor that builds them. The story set-ups that need no
 * editor first (a cut scene, a quest start / finish area: story/storyactions.ts `storyPlaceables`) are placed like a
 * class: drag into the viewport, or click / Enter for the middle of the view; one that cannot be placed right now is
 * greyed with the reason. A search box looks through every group, and "Recently placed" keeps the last things used.
 * Grouping rules: placecats.ts.
 */
import { categoryInfo, classCategory, TEMPLATES, type AssetClass } from '@drakan/editor-core';
import { attachAssetDrag, attachPointDrag, dragState } from './assetdrag.ts';
import type { Ctx, Panel } from './ctx.ts';
import { fill, h, icon, store } from './dom.ts';
import { openTemplate } from './logicpanel.ts';
import {
    ACTION_CAT,
    compareNames,
    matchesQuery,
    PLACE_CATS,
    placeCategory,
    pushRecent,
    readRecent,
    sentence,
    storyCategory,
    templateCategory,
    type PlaceCat,
} from './placecats.ts';
import { installQuickAdd, quickAddItems } from './quickadd.ts';
import { onRegistryChange, registerCommand, runCommand } from './registry.ts';
import { emptyState, searchField } from './ui.ts';
import { placeableReason, storyPlaceables, storyPlacer, type StoryPlaceable } from '../story/storyactions.ts';
import { STORY_COLORS, storyGlyph } from '../story/storyview.ts';
import './placepanel.css';

export interface PlaceHost {
    /** Hide the sidebar (its header button). */
    hide(): void;
    /** Show or hide the sidebar (Window menu). */
    toggle(): void;
}

/** One row of the list: a class to place, or a set-up that opens the editor making it. */
type Entry =
    | { type: 'class'; key: string; cat: PlaceCat; cls: AssetClass }
    | { type: 'action'; key: string; cat: PlaceCat; title: string; summary: string; icon: string; run: () => void }
    | { type: 'story'; key: string; cat: PlaceCat; item: StoryPlaceable };
type StoryEntry = Extract<Entry, { type: 'story' }>;

type Sel = PlaceCat | 'recent';
const RECENT_KEY = 'place-recent';
const PER_GROUP = 200;

/** Colour and icon of the class's display category: the same dot colours as the Outliner and Content Browser. */
const look = (cls: AssetClass): { color: string; icon: string } => {
    const info = categoryInfo(classCategory(cls));
    return { color: `#${info.color.toString(16).padStart(6, '0')}`, icon: info.icon };
};

export function createPlacePanel(ctx: Ctx, host: PlaceHost): Panel {
    installQuickAdd();
    let recent = readRecent(store.json<unknown>(RECENT_KEY, []));
    let sel: Sel = (store.get('place-cat') as Sel | null) ?? 'recent';
    if (sel !== 'recent' && !PLACE_CATS.some((c) => c.id === sel)) sel = 'recent';
    let query = '';
    const showAll = new Set<string>();
    let entries: Entry[] = [];
    /** Objects of each class in the open level (the catalog's own count is from when the level was opened). */
    let counts = new Map<string, number>();
    const countLevel = (): void => {
        counts = new Map();
        for (const o of ctx.doc().objects()) counts.set(o.resource, (counts.get(o.resource) ?? 0) + 1);
    };
    const countBadge = (n: number): HTMLElement | null =>
        n ? h('small', { class: 'pl-count', title: `${n} in this level` }, n) : null;

    const searchBox = searchField('Search', 'Search everything you can place');
    const cats = h('div', {
        class: 'pl-cats',
        role: 'tablist',
        'aria-orientation': 'vertical',
        'aria-label': 'Groups',
    });
    const list = h('div', { class: 'pl-list', role: 'tabpanel' });
    const el = h(
        'div',
        { class: 'panel place' },
        h(
            'div',
            { class: 'panel-head pl-panel-head' },
            h('span', { class: 'panel-title' }, icon('place', 14), 'Place'),
            h('span', { class: 'spacer' }),
            // Landscape mode (editor/landscape/landscape.ts) swaps this panel for its ground tools.
            h(
                'button',
                {
                    class: 'btn sm pl-landscape',
                    type: 'button',
                    title: 'Sculpt the ground and paint its textures (Landscape mode, Shift+L)',
                    onclick: () => runCommand('landscape.mode'),
                },
                icon('ground', 14),
                'Landscape',
            ),
            h(
                'button',
                {
                    class: 'tool sm pl-hide',
                    type: 'button',
                    title: 'Hide the Place panel (Window menu brings it back)',
                    'aria-label': 'Hide the Place panel',
                    onclick: () => host.hide(),
                },
                icon('sidebar', 14),
            ),
        ),
        h('div', { class: 'panel-tools' }, searchBox.el),
        h('div', { class: 'pl-main' }, cats, list),
    );

    // ---- entries ------------------------------------------------------------------------------------------------

    function collect(): Entry[] {
        const out: Entry[] = [];
        for (const item of storyPlaceables())
            out.push({ type: 'story', key: item.id, cat: storyCategory(item.category), item });
        for (const it of quickAddItems()) {
            const cat = ACTION_CAT[it.id];
            if (!cat) continue;
            out.push({
                type: 'action',
                key: `add:${it.id}`,
                cat,
                title: it.title,
                summary: it.summary,
                icon: it.icon,
                run: () => void it.run(),
            });
        }
        for (const t of TEMPLATES) {
            out.push({
                type: 'action',
                key: `tpl:${t.id}`,
                cat: templateCategory(t.id),
                title: t.title,
                summary: t.summary,
                icon: 'link',
                run: () => void openTemplate(t.id),
            });
        }
        for (const cls of ctx.catalog()) out.push({ type: 'class', key: cls.resource, cat: placeCategory(cls), cls });
        return out;
    }

    const textOf = (e: Entry): string =>
        e.type === 'class'
            ? `${e.cls.name} ${e.cls.category}`
            : e.type === 'story'
              ? `${e.item.title} ${e.item.hint} ${e.item.category}`
              : `${e.title} ${e.summary}`;
    const nameOf = (e: Entry): string =>
        e.type === 'class' ? e.cls.name : e.type === 'story' ? e.item.title : e.title;
    const reasonOf = (item: StoryPlaceable): string | null => {
        try {
            return placeableReason(item, storyPlacer(item.command));
        } catch {
            return 'Open a level from the disc first';
        }
    };

    function use(e: Entry): void {
        recent = pushRecent(recent, e.key);
        store.set(RECENT_KEY, JSON.stringify(recent));
        if (sel === 'recent' && !query) renderList();
    }

    function activate(e: Entry): void {
        if (e.type === 'story') return placeStory(e, null);
        if (e.type === 'class') ctx.placeClass(e.cls);
        else e.run();
        use(e);
    }

    /** Place a story set-up at `at` (a drop), or in the middle of the view; say why when it cannot be placed now. */
    function placeStory(e: StoryEntry, at: [number, number, number] | null): void {
        const why = reasonOf(e.item);
        if (why) {
            ctx.toast(`${e.item.title}: ${sentence(why)}`, 'info');
            return;
        }
        storyPlacer(e.item.command)!.place(at);
        use(e);
    }

    // ---- rendering ----------------------------------------------------------------------------------------------

    const thumbs = new Map<string, HTMLImageElement>();
    const thumbFor = (resource: string): HTMLImageElement | undefined => {
        const known = thumbs.get(resource);
        if (known) return known;
        const cls = ctx.catalog().find((c) => c.resource === resource);
        const url = cls ? ctx.classThumb(cls) : null;
        if (!url) return undefined;
        const img = new Image();
        img.src = url;
        thumbs.set(resource, img);
        return img;
    };

    /** Greyed with the reason, or live with the hint (the reason changes with the selection: `refreshStory`). */
    function paintStory(b: HTMLElement, item: StoryPlaceable): void {
        const why = reasonOf(item);
        const off = !!why;
        if (b.getAttribute('aria-disabled') === String(off) && b.dataset.why === (why ?? '')) return;
        b.setAttribute('aria-disabled', String(off));
        b.dataset.why = why ?? '';
        b.classList.toggle('off', off);
        b.title = off
            ? `${item.hint} Not available now: ${sentence(why!)}`
            : `${item.hint} Drag into the viewport to place it there, or click to place it in the middle of the view.`;
        const small = b.querySelector('.pl-text small');
        if (small) small.textContent = off ? sentence(why!) : item.hint;
    }

    function storyRow(e: StoryEntry): HTMLElement {
        const item = e.item;
        const b = h(
            'button',
            {
                class: 'pl-row pl-story',
                type: 'button',
                role: 'listitem',
                'data-key': e.key,
                'data-command': item.command,
                style: `--k:#${STORY_COLORS[item.glyph].toString(16).padStart(6, '0')}`,
                onclick: () => {
                    if (!dragState.suppressClick) activate(e);
                },
            },
            h('span', { class: 'pl-ico' }, storyGlyph(item.glyph, 15)),
            h('span', { class: 'pl-text' }, h('span', { class: 'pl-name' }, item.title), h('small', {}, item.hint)),
        );
        paintStory(b, item);
        attachPointDrag(
            b,
            `story:${item.id}`,
            item.title,
            ctx,
            (at) => placeStory(e, at),
            () => storyGlyph(item.glyph, 14),
        );
        return b;
    }

    /** Selection or level changed: a quest area becomes available once one quest is selected. */
    function refreshStory(): void {
        const items = new Map(storyPlaceables().map((i) => [i.id, i]));
        for (const r of list.querySelectorAll<HTMLElement>('.pl-story')) {
            const item = items.get(r.dataset.key ?? '');
            if (item) paintStory(r, item);
        }
    }

    function row(e: Entry): HTMLElement {
        if (e.type === 'story') return storyRow(e);
        if (e.type === 'action') {
            return h(
                'button',
                {
                    class: 'pl-row pl-action',
                    type: 'button',
                    role: 'listitem',
                    'data-key': e.key,
                    title: `${e.summary} Opens the editor that sets it up.`,
                    onclick: () => activate(e),
                },
                h('span', { class: 'pl-ico act' }, icon(e.icon, 15)),
                h('span', { class: 'pl-text' }, h('span', { class: 'pl-name' }, e.title), h('small', {}, e.summary)),
                icon('plus', 13),
            );
        }
        const c = e.cls;
        const lk = look(c);
        const b = h(
            'button',
            {
                class: 'pl-row pl-class',
                type: 'button',
                role: 'listitem',
                'data-key': e.key,
                'data-res': c.resource,
                style: `--k:${lk.color}`,
                title: `${c.name}. Drag into the viewport to place it, or double-click to place it in the middle of the view.`,
            },
            h('span', { class: 'pl-ico' }, icon(lk.icon, 15)),
            h('span', { class: 'pl-text' }, h('span', { class: 'pl-name' }, c.name)),
            countBadge(counts.get(c.resource) ?? 0),
        );
        b.addEventListener('dblclick', () => {
            if (!dragState.suppressClick) activate(e);
        });
        b.addEventListener('keydown', (k) => {
            if (k.key === 'Enter') {
                k.preventDefault();
                activate(e);
            }
        });
        attachAssetDrag(
            b,
            c,
            ctx,
            thumbFor,
            (cls) => void ctx.classThumb(cls),
            '',
            () => use(e),
        );
        return b;
    }

    function group(title: string | null, items: Entry[], groupKey: string): HTMLElement[] {
        const out: HTMLElement[] = [];
        if (!items.length) return out;
        if (title) out.push(h('div', { class: 'pl-sub' }, title));
        const all = showAll.has(groupKey);
        const shown = all ? items : items.slice(0, PER_GROUP);
        const box = h('div', { class: 'pl-rows', role: 'list' }, ...shown.map(row));
        out.push(box);
        if (!all && items.length > shown.length)
            out.push(
                h(
                    'button',
                    {
                        class: 'ui-btn sm pl-more',
                        type: 'button',
                        onclick: () => {
                            showAll.add(groupKey);
                            renderList();
                        },
                    },
                    `Show all ${items.length}`,
                ),
            );
        return out;
    }

    const byName = (a: Entry, b: Entry): number => compareNames(nameOf(a), nameOf(b));

    function renderCats(): void {
        const counts = new Map<PlaceCat, number>();
        for (const e of entries) counts.set(e.cat, (counts.get(e.cat) ?? 0) + 1);
        const btn = (id: Sel, title: string, ico: string, n: number | null): HTMLElement => {
            const active = !query && sel === id;
            const b = h(
                'button',
                {
                    type: 'button',
                    role: 'tab',
                    class: `pl-cat${active ? ' active' : ''}`,
                    'aria-selected': String(active),
                    tabindex: (query ? id === 'recent' : active) ? '0' : '-1',
                    'data-cat': id,
                    title: n === null ? title : `${title} (${n})`,
                    onclick: () => {
                        sel = id;
                        store.set('place-cat', id);
                        if (query) {
                            query = '';
                            searchBox.input.value = '';
                        }
                        render();
                    },
                },
                icon(ico, 14),
                h('span', { class: 'pl-cat-name' }, title),
            );
            return b;
        };
        fill(
            cats,
            btn('recent', 'Recently placed', 'history', null),
            ...PLACE_CATS.filter((c) => counts.get(c.id)).map((c) => btn(c.id, c.title, c.icon, counts.get(c.id)!)),
        );
    }

    function renderList(): void {
        const hasLevel = ctx.catalog().length > 0;
        const kids: HTMLElement[] = [];
        if (query) {
            const hits = entries.filter((e) => matchesQuery(textOf(e), query));
            kids.push(h('div', { class: 'pl-head' }, h('strong', {}, `Results for "${query}"`)));
            if (!hits.length)
                kids.push(
                    emptyState(
                        'search',
                        'Nothing matches',
                        hasLevel
                            ? 'Try a shorter word, or clear the search to see every group.'
                            : 'Open a level to search what it can use.',
                    ),
                );
            for (const c of PLACE_CATS) {
                const inCat = hits.filter((e) => e.cat === c.id).sort(byName);
                kids.push(...group(c.title, inCat, `q:${c.id}`));
            }
        } else if (sel === 'recent') {
            kids.push(
                h(
                    'div',
                    { class: 'pl-head' },
                    h('strong', {}, 'Recently placed'),
                    h('span', {}, 'What you placed or set up last, newest first.'),
                ),
            );
            const byKey = new Map(entries.map((e) => [e.key, e]));
            const items = recent.map((k) => byKey.get(k)).filter((e): e is Entry => !!e);
            if (items.length) kids.push(...group(null, items, 'recent'));
            else
                kids.push(
                    emptyState(
                        'place',
                        'Nothing placed yet',
                        hasLevel
                            ? 'Pick a group on the left, then drag something into the viewport.'
                            : 'Open a level, then pick a group on the left and drag something into the viewport.',
                    ),
                );
        } else {
            const c = PLACE_CATS.find((x) => x.id === sel)!;
            kids.push(h('div', { class: 'pl-head' }, h('strong', {}, c.title), h('span', {}, c.hint)));
            const inCat = entries.filter((e) => e.cat === sel);
            const story = inCat.filter((e) => e.type === 'story');
            const actions = inCat.filter((e) => e.type === 'action');
            const classes = inCat.filter((e) => e.type === 'class').sort(byName);
            const titled = [story, actions, classes].filter((x) => x.length).length > 1;
            kids.push(...group(titled ? 'Place in the level' : null, story, `s:${sel}`));
            kids.push(...group(titled ? 'Set up in an editor' : null, actions, `a:${sel}`));
            if (classes.length) kids.push(...group(titled ? 'Objects' : null, classes, `c:${sel}`));
            else if (!hasLevel)
                kids.push(h('p', { class: 'pl-note' }, 'Open a level to see the objects it can use in this group.'));
        }
        fill(list, ...kids);
    }

    function render(): void {
        renderCats();
        renderList();
    }

    function rebuild(): void {
        countLevel();
        entries = collect();
        render();
    }

    // ---- keyboard: arrows move through the groups and through the rows -----------------------------------------

    cats.addEventListener('keydown', (e) => {
        const bs = [...cats.querySelectorAll<HTMLElement>('.pl-cat')];
        const i = bs.indexOf(document.activeElement as HTMLElement);
        if (i < 0) return;
        const to =
            e.key === 'ArrowDown'
                ? (i + 1) % bs.length
                : e.key === 'ArrowUp'
                  ? (i - 1 + bs.length) % bs.length
                  : e.key === 'Home'
                    ? 0
                    : e.key === 'End'
                      ? bs.length - 1
                      : -1;
        if (to < 0) return;
        e.preventDefault();
        bs[to]!.click();
        cats.querySelector<HTMLElement>(`[data-cat="${bs[to]!.dataset.cat}"]`)?.focus();
    });
    list.addEventListener('keydown', (e) => {
        if (e.key !== 'ArrowDown' && e.key !== 'ArrowUp') return;
        const rows = [...list.querySelectorAll<HTMLElement>('.pl-row')];
        const i = rows.indexOf(document.activeElement as HTMLElement);
        if (i < 0) return;
        e.preventDefault();
        rows[Math.max(0, Math.min(rows.length - 1, i + (e.key === 'ArrowDown' ? 1 : -1)))]!.focus();
    });
    searchBox.input.addEventListener('input', () => {
        query = searchBox.input.value.trim();
        render();
    });
    searchBox.input.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && searchBox.input.value) {
            e.stopPropagation();
            searchBox.input.value = '';
            query = '';
            render();
        } else if (e.key === 'ArrowDown') {
            e.preventDefault();
            list.querySelector<HTMLElement>('.pl-row')?.focus();
        }
    });

    // Features add their Quick Add rows as they install (Vendors, ...): list them once they appear.
    const actionSig = (): string =>
        quickAddItems()
            .map((i) => i.id)
            .join();
    let sig = actionSig();
    let pending = false;
    onRegistryChange(() => {
        if (pending) return;
        pending = true;
        queueMicrotask(() => {
            pending = false;
            const now = actionSig();
            if (now !== sig) {
                sig = now;
                rebuild();
            }
        });
    });
    registerCommand({
        id: 'view.left',
        title: 'Toggle the Place panel',
        category: 'View',
        icon: 'place',
        hint: 'Show or hide the sidebar with everything you can put in the level.',
        keywords: 'place actors sidebar left panel add objects',
        run: () => host.toggle(),
    });
    // The host finishes setting up the level view after the panels are made: fill the list once it has.
    queueMicrotask(rebuild);

    // The header keeps one line: when the title, the Landscape button, the hide button and the help button at the right edge
    // no longer fit in the panel's measured width, the Landscape button shows its icon only.
    const head = el.querySelector<HTMLElement>('.pl-panel-head');
    const fitHeader = (): void => {
        if (!head || !head.clientWidth) return;
        head.classList.remove('compact');
        const cs = getComputedStyle(head);
        const kids = [...head.children].filter((c) => !c.classList.contains('spacer')) as HTMLElement[];
        const gap = parseFloat(cs.columnGap) || 0;
        const used =
            kids.reduce((n, c) => n + c.offsetWidth, 0) +
            gap * Math.max(0, kids.length - 1) +
            (parseFloat(cs.paddingLeft) || 0) +
            (parseFloat(cs.paddingRight) || 0);
        // 40 px: the panel help button that sits at the header's right edge, plus a little air.
        if (used + 40 > head.clientWidth) head.classList.add('compact');
    };
    if (typeof ResizeObserver !== 'undefined' && head) new ResizeObserver(fitHeader).observe(head);
    return {
        el,
        onSelection: refreshStory,
        onLoad: () => {
            thumbs.clear();
            showAll.clear();
            rebuild();
        },
        onChange: () => {
            // Only the counts change: update them in place (focus and scroll stay where they are).
            refreshStory();
            countLevel();
            for (const r of list.querySelectorAll<HTMLElement>('.pl-class')) {
                const n = counts.get(r.dataset.res ?? '') ?? 0;
                const old = r.querySelector('.pl-count');
                if (Number(old?.textContent ?? 0) === n) continue;
                const badge = countBadge(n);
                if (old && badge) old.replaceWith(badge);
                else if (old) old.remove();
                else if (badge) r.append(badge);
            }
        },
    };
}

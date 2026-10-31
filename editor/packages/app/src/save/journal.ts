import { SetFieldCommand } from '@drakan/editor-core';
import {
    QUEST_SEEN_JOURNAL,
    QUEST_SEEN_MAP,
    describeQuestRows,
    listQuests,
    notStartedQuests,
    type QuestDiffRow,
    type QuestEntry,
    type QuestRow,
    type SaveFile,
} from '@drakan/formats';
import { badge, button, emptyState, h, hex, statTile } from './dom.ts';
import type { PanelCtx } from './panels.ts';
import type { StoryData } from './storyart.ts';

type Filter = 'all' | 'active' | 'done' | 'notStarted' | 'places';
const FILTERS: [Filter, string][] = [
    ['all', 'All'],
    ['active', 'Active'],
    ['done', 'Done'],
    ['notStarted', 'Not started'],
    ['places', 'Places'],
];

const journalState = { q: '', filter: 'all' as Filter, showNotStarted: true };

const STATE_LABEL: Record<QuestEntry['state'], string> = {
    notStarted: 'Not started',
    active: 'Active',
    done: 'Done',
    discovered: 'Place',
    unknown: 'Unknown',
};
const STATE_CLASS: Record<QuestEntry['state'], string> = {
    notStarted: 'low',
    active: 'medium',
    done: 'high',
    discovered: 'low',
    unknown: 'low',
};

/** Rows as they would be saved: pending done / seen edits applied. */
function effectiveRows(ctx: PanelCtx): QuestRow[] {
    return ctx.doc.quests.map((r) => ({
        ...r,
        icon: Number(ctx.doc.value(`quest.${r.index}.done`)),
        state: Number(ctx.doc.value(`quest.${r.index}`)),
    }));
}

function chapterTitle(e: QuestEntry): string {
    return e.chapter ? `Chapter ${e.chapter} · ${e.chapterName}` : 'Other';
}

function matches(e: QuestEntry, q: string): boolean {
    if (!q) return true;
    return `${e.title} ${e.place} ${e.cls?.name ?? ''} ${e.chapterName} 0x${hex(e.classId, 4)}`
        .toLowerCase()
        .includes(q);
}

/** The journal-style Story view: quests by chapter with state badges, search and the done tick (an undoable edit). */
export function renderJournal(ctx: PanelCtx): HTMLElement {
    const rows = ctx.doc.quests;
    if (!rows.length) {
        return emptyState(
            'scroll',
            'No quest table in this save',
            'The journal is read from a quest table that this save does not contain.',
        );
    }
    const data = ctx.story?.() ?? null;
    if (!data) {
        return h(
            'div',
            { class: 'journal' },
            emptyState(
                'scroll',
                'Quest names need the game disc',
                'The journal text lives in the disc’s System database. Open the ISO with Open ISO in the top bar and this view fills in with titles, chapters and states. The raw rows are below.',
            ),
            rawTable(ctx),
        );
    }
    const eff = effectiveRows(ctx);
    const entries = describeQuestRows(eff, data.classes, data.strings, data.slot);
    const todo = notStartedQuests(eff, data.classes, data.strings, data.slot);
    const journal = entries.filter((e) => e.state === 'active' || e.state === 'done');
    const done = journal.filter((e) => e.state === 'done').length;
    const places = entries.filter((e) => e.state === 'discovered');
    const fresh = entries.filter((e) => e.unseen).length;

    const body = h('div', { class: 'jbody' });
    const fill = (): void => {
        const q = journalState.q.trim().toLowerCase();
        const f = journalState.filter;
        const shown = entries.filter((e) => {
            if (f === 'active' && e.state !== 'active') return false;
            if (f === 'done' && e.state !== 'done') return false;
            if (f === 'places' && e.state !== 'discovered') return false;
            if (f === 'notStarted') return false;
            return matches(e, q);
        });
        const extra =
            f === 'notStarted' || (f === 'all' && journalState.showNotStarted) ? todo.filter((e) => matches(e, q)) : [];
        const all = [...shown, ...extra];
        const chapters = new Map<number, QuestEntry[]>();
        for (const e of all) {
            const list = chapters.get(e.chapter);
            if (list) list.push(e);
            else chapters.set(e.chapter, [e]);
        }
        const sections = [...chapters.entries()]
            .sort((a, b) => (a[0] || 99) - (b[0] || 99))
            .map(([, list]) => {
                const doneHere = list.filter((e) => e.state === 'done').length;
                const quests = list.filter((e) => e.state !== 'discovered');
                return h(
                    'section',
                    { class: 'chapter' },
                    h(
                        'h4',
                        null,
                        chapterTitle(list[0]!),
                        h(
                            'small',
                            { class: 'sv-dim' },
                            quests.length ? ` ${doneHere} of ${quests.length} done` : ` ${list.length} places`,
                        ),
                    ),
                    ...list
                        .sort((a, b) => stateRank(a) - stateRank(b) || a.classId - b.classId)
                        .map((e) => questRow(ctx, e)),
                );
            });
        body.replaceChildren(
            ...(sections.length ? sections : [h('p', { class: 'sv-dim' }, 'Nothing matches the filter.')]),
        );
    };

    const search = h('input', {
        type: 'search',
        placeholder: 'Search quests, places, chapters',
        value: journalState.q,
        oninput: () => {
            journalState.q = search.value;
            fill();
        },
    });
    const chips = h(
        'div',
        { class: 'chips' },
        ...FILTERS.map(([key, label]) =>
            h(
                'button',
                {
                    class: `sv-pill${journalState.filter === key ? ' on' : ''}`,
                    onclick: (e: Event) => {
                        journalState.filter = key;
                        chips.querySelectorAll('.sv-pill').forEach((c) => c.classList.remove('on'));
                        (e.currentTarget as HTMLElement).classList.add('on');
                        fill();
                    },
                },
                label,
            ),
        ),
    );
    const showTodo = h('input', {
        type: 'checkbox',
        checked: journalState.showNotStarted,
        onchange: () => {
            journalState.showNotStarted = showTodo.checked;
            fill();
        },
    });
    fill();
    return h(
        'div',
        { class: 'journal' },
        h(
            'div',
            { class: 'tiles' },
            statTile('Journal entries', String(journal.length), { note: `${todo.length} more in the game` }),
            statTile('Done', String(done), { bar: [done, Math.max(journal.length, 1)] }),
            statTile('Active', String(journal.length - done)),
            statTile('Places found', String(places.length)),
            statTile('New', String(fresh), { note: 'not opened in game yet' }),
        ),
        h(
            'div',
            { class: 'sv-filters' },
            search,
            chips,
            h('label', { class: 'sv-check' }, showTodo, ' list not started'),
        ),
        body,
        h(
            'p',
            { class: 'sv-dim' },
            'Ticking an entry sets its done flag, which the in-game journal draws as a ticked box (checked in game on ' +
                'edited saves). Quests the save has no row for cannot be added: the row is created when the game ' +
                'meets the quest, and marking them started is not supported. ',
            badge('done flag: medium', 'medium', 'Row word +0x0c; toggled both ways in the original journal screen.'),
        ),
        h('details', null, h('summary', null, 'Raw rows'), rawTable(ctx)),
    );
}

const stateRank = (e: QuestEntry): number =>
    e.state === 'active' ? 0 : e.state === 'done' ? 1 : e.state === 'discovered' ? 2 : 3;

/** Map extent of each chapter's quest classes, for the little location picture beside a quest. */
const extentCache = new WeakMap<object, Map<number, { x0: number; x1: number; y0: number; y1: number }>>();

function chapterExtent(data: StoryData, chapter: number): { x0: number; x1: number; y0: number; y1: number } | null {
    let m = extentCache.get(data);
    if (!m) {
        m = new Map();
        for (const k of data.classes.values()) {
            if (!k.mapX && !k.mapY) continue;
            const r = m.get(k.chapter) ?? { x0: Infinity, x1: -Infinity, y0: Infinity, y1: -Infinity };
            r.x0 = Math.min(r.x0, k.mapX);
            r.x1 = Math.max(r.x1, k.mapX);
            r.y0 = Math.min(r.y0, k.mapY);
            r.y1 = Math.max(r.y1, k.mapY);
            m.set(k.chapter, r);
        }
        extentCache.set(data, m);
    }
    return m.get(chapter) ?? null;
}

/**
 * The game has no quest pictures (the journal is text); what the data does have is the quest's map position, so each
 * quest gets a small map showing where its marker sits among the chapter's markers.
 */
function questPin(ctx: PanelCtx, e: QuestEntry): HTMLElement | null {
    const k = e.cls;
    const data = ctx.story?.();
    if (!k || !data || (!k.mapX && !k.mapY)) return null;
    const r = chapterExtent(data, k.chapter);
    if (!r) return null;
    const w = 44;
    const hgt = 32;
    const fx = r.x1 > r.x0 ? (k.mapX - r.x0) / (r.x1 - r.x0) : 0.5;
    const fy = r.y1 > r.y0 ? (k.mapY - r.y0) / (r.y1 - r.y0) : 0.5;
    const ns = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('viewBox', `0 0 ${w} ${hgt}`);
    svg.setAttribute('width', String(w));
    svg.setAttribute('height', String(hgt));
    svg.setAttribute('class', `qpin ${e.state}`);
    const frame = document.createElementNS(ns, 'rect');
    for (const [a, v] of Object.entries({ x: 0.5, y: 0.5, width: w - 1, height: hgt - 1, rx: 3, class: 'qpin-frame' }))
        frame.setAttribute(a, String(v));
    const dot = document.createElementNS(ns, 'circle');
    for (const [a, v] of Object.entries({ cx: 5 + fx * (w - 10), cy: 5 + fy * (hgt - 10), r: 3.2, class: 'qpin-dot' }))
        dot.setAttribute(a, String(v));
    svg.append(frame, dot);
    const wrap = h('div', { class: 'qpin-wrap', title: `Map marker at ${k.mapX}, ${k.mapY} within ${k.chapterName}` });
    wrap.append(svg);
    return wrap;
}

function questRow(ctx: PanelCtx, e: QuestEntry): HTMLElement {
    const pending = e.row >= 0 && ctx.doc.pendingEdits().some((p) => p.name === `quest.${e.row}.done`);
    const tick =
        e.state === 'active' || e.state === 'done'
            ? h('input', {
                  type: 'checkbox',
                  class: 'qtick',
                  checked: e.state === 'done',
                  title: 'Done flag (quest row word +0x0c)',
                  'aria-label': 'Done',
                  onchange: (ev: Event) =>
                      ctx.run(
                          new SetFieldCommand(`quest.${e.row}.done`, (ev.target as HTMLInputElement).checked ? 1 : 0),
                      ),
              })
            : h('span', { class: 'qtick none' });
    const isJournalRow = e.cls ? e.cls.kind !== 'map' : false;
    const seenBit = isJournalRow ? QUEST_SEEN_JOURNAL : QUEST_SEEN_MAP;
    const seenToggle =
        e.row >= 0 && e.cls && e.raw && e.raw.enabled !== 0
            ? button(e.unseen ? 'Mark seen' : 'Mark new', {
                  kind: 'ghost sm',
                  title: e.unseen
                      ? 'Set the seen bit: the game treats this entry as already opened'
                      : `Clear the seen bit: the game treats this entry as new until the ${isJournalRow ? 'journal' : 'map'} page draws it`,
                  onclick: () => {
                      const cur = Number(ctx.doc.value(`quest.${e.row}`));
                      ctx.run(new SetFieldCommand(`quest.${e.row}`, e.unseen ? cur | seenBit : cur & ~seenBit));
                  },
              })
            : null;
    const tip = [
        e.cls
            ? `Class 0x${hex(e.classId, 4)}: ${e.cls.name}`
            : `Class 0x${hex(e.classId, 4)} (not in the System database)`,
        e.row >= 0 ? `Save row ${e.row}` : 'No row in this save',
    ].join('\n');
    return h(
        'div',
        { class: `qrow ${e.state}${pending ? ' chg' : ''}`, title: tip },
        tick,
        questPin(ctx, e),
        h(
            'div',
            { class: 'qtext' },
            h('div', { class: 'qtitle' }, e.title || `Quest 0x${hex(e.classId, 4)}`),
            e.place ? h('div', { class: 'qsub sv-dim' }, `Place: ${e.place}`) : null,
        ),
        e.cls?.optional ? badge('Optional', 'low', 'Listed under Optional Quests in the game.') : null,
        e.unseen ? badge('New', 'medium', 'Not opened in the in-game journal / map yet.') : null,
        badge(STATE_LABEL[e.state], STATE_CLASS[e.state]),
        seenToggle,
    );
}

/** The raw rows, read only (edit the seen bits with Mark seen / Mark new in the journal list). */
function rawTable(ctx: PanelCtx): HTMLElement {
    const body = h(
        'tbody',
        null,
        ...ctx.doc.quests.map((r) =>
            h(
                'tr',
                null,
                h('td', null, String(r.index)),
                h('td', null, `0x${hex(r.questId, 4)}`),
                h('td', null, String(Number(ctx.doc.value(`quest.${r.index}.done`)))),
                h('td', null, r.enabled ? 'yes' : 'no'),
                h('td', null, `0x${hex(r.region, 2)}`),
                h('td', null, `${r.x}, ${r.y}`),
                h('td', null, String(Number(ctx.doc.value(`quest.${r.index}`)))),
            ),
        ),
    );
    return h(
        'table',
        { class: 'sv-grid' },
        h(
            'thead',
            null,
            h(
                'tr',
                null,
                ...['row', 'class', 'done', 'enabled', 'region', 'x, y', 'seen bits'].map((x) => h('th', null, x)),
            ),
        ),
        body,
    );
}

export function listQuestsSafe(save: SaveFile): QuestRow[] {
    try {
        return listQuests(save);
    } catch {
        return [];
    }
}

/** Compare tab section: quests that differ between the two saves (names when the disc is open). */
export function questCompare(diff: QuestDiffRow[], data: StoryData | null): HTMLElement {
    const changed = diff.filter((d) => d.change !== 'same');
    const title = (d: QuestDiffRow): string => {
        const row = (d.a ?? d.b)!;
        if (!data) return `Quest 0x${hex(d.classId, 4)}`;
        return (
            describeQuestRows([row], data.classes, data.strings, data.slot)[0]!.title || `Quest 0x${hex(d.classId, 4)}`
        );
    };
    const flag = (r: QuestRow | null): string => (r ? (r.icon ? 'done' : 'open') : '-');
    const label: Record<QuestDiffRow['change'], string> = {
        same: 'same',
        changed: 'changed',
        onlyA: 'only in A',
        onlyB: 'only in B',
    };
    return h(
        'div',
        { class: 'questcmp' },
        h('h4', null, `Quests: ${changed.length} differ, ${diff.length - changed.length} same`),
        changed.length
            ? h(
                  'table',
                  { class: 'sv-grid' },
                  h('thead', null, h('tr', null, ...['quest', 'change', 'A', 'B'].map((x) => h('th', null, x)))),
                  h(
                      'tbody',
                      null,
                      ...changed.map((d) =>
                          h(
                              'tr',
                              { class: 'sv-chg' },
                              h('td', null, title(d)),
                              h(
                                  'td',
                                  null,
                                  d.what.length ? `${label[d.change]} (${d.what.join(', ')})` : label[d.change],
                              ),
                              h('td', null, flag(d.a)),
                              h('td', null, flag(d.b)),
                          ),
                      ),
                  ),
              )
            : h('p', { class: 'sv-dim' }, 'The quest tables match.'),
    );
}

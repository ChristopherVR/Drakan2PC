import { SetFieldCommand, type SaveDocument } from '@drakan/editor-core';
import {
    FIELDS,
    PROGRESS_KINDS,
    formatFieldValue,
    parseSave,
    recordLabel,
    resolveField,
    transferPlan,
    validateSave,
    type FieldEdit,
    type FieldValue,
    type Issue,
    type SaveFile,
} from '@drakan/formats';
import type { SaveRef } from '../saveio.ts';
import { badge, button, card, emptyState, h, hex, statTile } from './dom.ts';
import { renderInventory, type ClassInfo } from './inventory.ts';
import { renderJournal } from './journal.ts';
import type { StoryData } from './storyart.ts';

/** What the extra panels need from the Save tab. */
export interface PanelCtx {
    doc: SaveDocument;
    run(cmd: SetFieldCommand): void;
    /** Move the player marker (an undoable transform edit). */
    setPlayerPos(v: [number, number, number]): void;
    err(e: unknown): void;
    status(msg: string, error?: boolean): void;
    /** Select a record in the World tab and optionally in 3D. */
    showRecord(entry: number, in3d: boolean): void;
    listed(): SaveRef[];
    /** Redraw everything that depends on the document. */
    changed(): void;
    /** Redraw only the current tab. */
    rerender(): void;
    /** Game name and model render of an item class (resource like `0x31bc9`), when a level with it is open. */
    classInfo?(resource: string): ClassInfo | null;
    /** Quest classes and journal text of the open disc (null until loaded or without a disc). */
    story?(): StoryData | null;
}

const fmt = (name: string, v: FieldValue | null | undefined): string =>
    v === null || v === undefined ? '-' : formatFieldValue(resolveField(name).def, v);

// ------------------------------------------------------------------------------------------------- story / progress

interface StoryState {
    q: string;
    nonZero: boolean;
    kind: string;
    allowEdit: boolean;
}
const storyState: StoryState = { q: '', nonZero: true, kind: '', allowEdit: false };

export function renderStory(ctx: PanelCtx): HTMLElement {
    const { doc } = ctx;
    const rows = doc.progress;
    const quests = renderJournal(ctx);
    if (!rows.length) {
        return h(
            'div',
            { class: 'story' },
            quests,
            emptyState(
                'file',
                'No state table',
                'This save has no recognisable state table at the end of its text payload.',
            ),
        );
    }
    const entryOfKey = new Map<number, number>();
    for (const r of doc.records) if (r.type === 0x102 && !entryOfKey.has(r.id)) entryOfKey.set(r.id, r.index);
    const nonZero = rows.filter((r) => doc.progressValue(r.key) !== 0).length;
    const body = h('tbody');
    const count = h('small', { class: 'sv-dim' });
    const fill = (): void => {
        const q = storyState.q.trim().toLowerCase();
        const shown = rows.filter((r) => {
            const v = doc.progressValue(r.key) ?? r.value;
            if (storyState.nonZero && v === 0) return false;
            if (storyState.kind && String(r.kind) !== storyState.kind) return false;
            if (!q) return true;
            return `${r.key} 0x${hex(r.key, 3)} kind ${r.kind} ${v}`.includes(q);
        });
        count.textContent = `${shown.length} of ${rows.length} rows`;
        body.replaceChildren(
            ...shown.slice(0, 500).map((r) => {
                const name = `progress.${r.key}`;
                const cur = doc.progressValue(r.key) ?? r.value;
                const changed = cur !== r.value;
                const input = h('input', {
                    type: 'number',
                    min: 0,
                    value: String(cur),
                    disabled: !storyState.allowEdit,
                    'data-field': name,
                    onchange: () => {
                        try {
                            const n = Number(input.value);
                            if (!Number.isInteger(n)) throw new Error('expected an integer');
                            const problem = doc.checkEdit(name, n);
                            if (problem) throw new Error(problem);
                            ctx.run(new SetFieldCommand(name, n));
                        } catch (e) {
                            ctx.err(e);
                            input.value = String(doc.progressValue(r.key) ?? r.value);
                        }
                    },
                });
                const link = entryOfKey.get(r.key);
                const rec = link === undefined ? null : doc.records[link]!;
                return h(
                    'tr',
                    { class: changed ? 'chg' : '' },
                    h('td', null, String(r.key), h('small', { class: 'sv-dim' }, ` 0x${hex(r.key, 3)}`)),
                    h('td', null, String(r.kind)),
                    h('td', null, input, changed ? h('small', { class: 'sv-dim' }, ` was ${r.value}`) : null),
                    h(
                        'td',
                        null,
                        rec
                            ? h(
                                  'button',
                                  {
                                      title: 'Open this record in World',
                                      onclick: () => ctx.showRecord(link!, rec.pos != null),
                                  },
                                  `#${link}${recordLabel(rec) ? ` ${recordLabel(rec)}` : ''}`,
                              )
                            : h('small', { class: 'sv-dim' }, '-'),
                    ),
                );
            }),
        );
    };
    const search = h('input', {
        type: 'search',
        placeholder: 'Search key, hex key, value',
        value: storyState.q,
        oninput: () => {
            storyState.q = search.value;
            fill();
        },
    });
    const kinds = h(
        'select',
        {
            onchange: () => {
                storyState.kind = kinds.value;
                fill();
            },
        },
        new Option('all kinds', ''),
        ...PROGRESS_KINDS.map((k) => new Option(`kind ${k}`, String(k))),
    );
    kinds.value = storyState.kind;
    const nz = h('input', {
        type: 'checkbox',
        checked: storyState.nonZero,
        onchange: () => {
            storyState.nonZero = nz.checked;
            fill();
        },
    });
    const allow = h('input', {
        type: 'checkbox',
        checked: storyState.allowEdit,
        onchange: () => {
            storyState.allowEdit = allow.checked;
            fill();
        },
    });
    const table = h(
        'table',
        { class: 'sv-grid' },
        h('thead', null, h('tr', null, ...['key', 'kind', 'value', 'record'].map((x) => h('th', null, x)))),
        body,
    );
    fill();
    return h(
        'div',
        { class: 'story' },
        quests,
        h('h4', null, 'State table'),
        h(
            'div',
            { class: 'tiles' },
            statTile('State rows', String(rows.length), { note: 'end of the text payload' }),
            statTile('With a value', String(nonZero), { bar: [nonZero, rows.length] }),
            statTile('Level', doc.level || 'unknown'),
        ),
        h(
            'p',
            { class: 'sv-dim' },
            'Each row is {kind, key, value}. Keys are numbers, not names, and none are decoded yet, so there are no readable ' +
                'labels. The table is shorter after play than in a fresh save, which points at per-actor state (killed or ' +
                'collected things), but that is a hypothesis. Editing is low confidence and off by default.',
            ' ',
            badge(FIELDS.progress!.confidence, FIELDS.progress!.confidence, FIELDS.progress!.note),
        ),
        h(
            'div',
            { class: 'sv-filters' },
            search,
            kinds,
            h('label', { class: 'sv-check' }, nz, ' with a value only'),
            h('label', { class: 'sv-check warn' }, allow, ' allow editing'),
            count,
        ),
        table,
    );
}

// ------------------------------------------------------------------------------------------------- items / equipment

export function renderItems(ctx: PanelCtx): HTMLElement {
    const { doc } = ctx;
    const p = doc.itemPanel;
    const item = p
        ? h(
              'div',
              { class: 'sv-box itemcard' },
              h(
                  'div',
                  { class: 'sv-meta' },
                  h('small', { class: 'sv-dim' }, 'Last inspected item'),
                  h('h3', null, p.name || 'Unnamed item'),
                  p.hand ? badge(`${p.hand} hand`, 'high', 'The item text says it is held in this hand.') : null,
                  ...p.lines.map((l) => h('div', { class: 'line' }, l)),
                  p.durability
                      ? h(
                            'div',
                            { class: 'dur' },
                            statTile(
                                /Arrows/.test(p.lines.join(' ')) ? 'Arrows' : 'Durability',
                                `${p.durability.current} / ${p.durability.max}`,
                                {
                                    bar: [p.durability.current, p.durability.max],
                                },
                            ),
                        )
                      : null,
              ),
          )
        : emptyState('file', 'No item text', 'Rynn’s record holds no inspected-item text in this save.');
    const arokhPos = doc.value('arokh.pos');
    const arokhIndex = doc.records.find((r) => r.resource === 0xa60)?.index;
    const arokh = h(
        'div',
        { class: 'sv-box' },
        h(
            'div',
            { class: 'sv-meta' },
            h('small', { class: 'sv-dim' }, 'Dragon'),
            h('h3', null, 'Arokh'),
            Array.isArray(arokhPos)
                ? h('div', { class: 'line' }, `Position ${fmt('arokh.pos', arokhPos)}`)
                : h('small', { class: 'sv-dim' }, 'No dragon record in this save (not every level has Arokh).'),
            Array.isArray(arokhPos) && arokhIndex !== undefined
                ? h('button', { onclick: () => ctx.showRecord(arokhIndex, true) }, 'Show Arokh in 3D')
                : null,
            badge(FIELDS['arokh.pos']!.confidence, FIELDS['arokh.pos']!.confidence, FIELDS['arokh.pos']!.note),
        ),
    );
    return h(
        'div',
        { class: 'items' },
        renderInventory(ctx),
        h(
            'details',
            { class: 'adv' },
            h('summary', null, 'Advanced / raw'),
            h('h4', null, 'Last inspected item (UI text buffer)'),
            h(
                'p',
                { class: 'sv-dim' },
                'The text the Equipment page last displayed, kept inside Rynn’s record. It is not the inventory; read only.',
            ),
            item,
            h('h4', null, 'Dragon'),
            arokh,
        ),
    );
}

// ------------------------------------------------------------------------------------------------- transfer

interface TransferState {
    source: { label: string; save: SaveFile } | null;
    ticked: Set<string>;
}
const transfer: TransferState = { source: null, ticked: new Set() };

export function renderTransfer(ctx: PanelCtx, rerender: () => void): HTMLElement {
    const { doc } = ctx;
    const load = async (label: string, read: () => Promise<Uint8Array>): Promise<void> => {
        transfer.source = { label, save: parseSave(await read()) };
        transfer.ticked = new Set();
        rerender();
    };
    const file = h('input', {
        type: 'file',
        accept: '.rsg',
        hidden: true,
        onchange: (e: Event) => {
            const i = e.target as HTMLInputElement;
            const f = i.files?.[0];
            i.value = '';
            if (f) load(f.name, async () => new Uint8Array(await f.arrayBuffer())).catch(ctx.err);
        },
    });
    const listed = ctx.listed();
    const pick = h(
        'select',
        {
            class: 'sv-select',
            'aria-label': 'Take values from a listed save',
            onchange: () => {
                const r = listed[Number(pick.value)];
                pick.value = '';
                if (r) load(r.name, r.read).catch(ctx.err);
            },
            disabled: !listed.length,
        },
        new Option(listed.length ? 'Take values from a listed save...' : 'No folder opened', ''),
        ...listed.map(
            (r, i) => new Option(`${r.folder ? `${r.folder.replace(/^BESCES-\d+/i, '')} · ` : ''}${r.name}`, String(i)),
        ),
    );
    const bar = h(
        'div',
        { class: 'sv-toolbar' },
        button('Take values from a file', { icon: 'file', onclick: () => file.click() }),
        pick,
        file,
    );
    const intro = h(
        'p',
        { class: 'sv-lede' },
        'Copy values from another save into the open one: health, gold, mana, XP, level, skill points, skill levels, item ' +
            'counts (same item in the same cell), and positions when both saves are from the same level. The result is ' +
            'pending until you save. Items themselves cannot be moved: adding items is not supported yet.',
    );
    if (!transfer.source) {
        return h(
            'div',
            { class: 'transfer' },
            bar,
            emptyState(
                'swap',
                'Choose a source save',
                'Pick a listed save or a file above. You then tick the values to copy into this save.',
            ),
            intro,
        );
    }
    const src = transfer.source;
    const sameLevel = (() => {
        try {
            return (
                new TextDecoder('latin1').decode(
                    src.save.entries.find((e) => e.type === 0x400)?.data ?? new Uint8Array(),
                ) ===
                new TextDecoder('latin1').decode(
                    doc.save.entries.find((e) => e.type === 0x400)?.data ?? new Uint8Array(1),
                )
            );
        } catch {
            return false;
        }
    })();
    const plan = transferPlan(src.save, doc.save, sameLevel);
    const rows = plan.map((r) => {
        const blocked = r.blocked ?? firstBlocked(ctx, r.edits);
        const cb = h('input', {
            type: 'checkbox',
            disabled: blocked !== null,
            checked: transfer.ticked.has(r.name) && blocked === null,
            'data-transfer': r.name,
            onchange: () => {
                if (cb.checked) transfer.ticked.add(r.name);
                else transfer.ticked.delete(r.name);
                rerender();
            },
        });
        return h(
            'tr',
            { class: blocked ? 'dim' : '' },
            h('td', null, cb),
            h('td', null, r.label),
            h('td', null, fmt(r.name, doc.value(r.name) ?? null)),
            h('td', null, fmt(r.name, r.from)),
            h('td', null, blocked ?? ''),
        );
    });
    const ticked = plan.filter((r) => transfer.ticked.has(r.name) && !(r.blocked ?? firstBlocked(ctx, r.edits)));
    const apply = button(`Copy ${ticked.length} selected`, {
        icon: 'swap',
        kind: 'primary',
        disabled: ticked.length === 0,
        onclick: () => {
            try {
                for (const r of ticked) applyEdits(ctx, r.edits);
                transfer.ticked = new Set();
                ctx.status(`Copied ${ticked.length} value(s) from ${src.label}; pending until you save.`);
                ctx.changed();
            } catch (e) {
                ctx.err(e);
                ctx.changed();
            }
        },
    });
    return h(
        'div',
        { class: 'transfer' },
        bar,
        intro,
        h(
            'div',
            { class: 'sv-cmphead' },
            h('span', { class: 'sv-ab b' }, 'From'),
            h('span', null, src.label),
            h('span', { class: 'sv-ab a' }, 'Into'),
            h('span', null, `${doc.level || 'the open save'}${sameLevel ? '' : ' (different levels)'}`),
        ),
        card(
            'Values',
            { tools: apply },
            h(
                'table',
                { class: 'sv-grid' },
                h(
                    'thead',
                    null,
                    h('tr', null, ...['', 'value', 'open save', 'source', 'note'].map((x) => h('th', null, x))),
                ),
                h('tbody', null, ...rows),
            ),
        ),
    );
}

function firstBlocked(ctx: PanelCtx, edits: FieldEdit[]): string | null {
    for (const e of edits) {
        if (e.name === 'pos') return ctx.doc.playerObject === null ? 'no player marker' : null;
        const why = ctx.doc.checkEdit(e.name, e.value);
        // A skill level that exceeds the max is fine when the same plan raises the max first.
        if (why && !/exceed max/.test(why)) return why;
    }
    return null;
}

function applyEdits(ctx: PanelCtx, edits: FieldEdit[]): void {
    for (const e of edits) {
        if (e.name === 'pos') {
            ctx.setPlayerPos(e.value as [number, number, number]);
            continue;
        }
        const why = ctx.doc.checkEdit(e.name, e.value);
        if (why) throw new Error(`${e.name}: ${why}`);
        ctx.run(new SetFieldCommand(e.name, e.value));
    }
}

// ------------------------------------------------------------------------------------------------- repair

const SEVERITY_ORDER = { error: 0, warn: 1, info: 2 } as const;

export function renderRepair(ctx: PanelCtx): HTMLElement {
    const { doc } = ctx;
    let issues: Issue[];
    let pendingNote = '';
    try {
        issues = doc.pendingEdits().length ? validateSave(doc.build().save) : doc.issues();
        if (doc.pendingEdits().length) pendingNote = 'Checked with your pending edits applied.';
    } catch (e) {
        issues = [
            { code: 'pending', severity: 'error', message: `The pending edits do not build: ${(e as Error).message}` },
        ];
    }
    issues = [...issues].sort((a, b) => SEVERITY_ORDER[a.severity] - SEVERITY_ORDER[b.severity]);
    const errors = issues.filter((i) => i.severity === 'error').length;
    const warns = issues.filter((i) => i.severity === 'warn').length;
    const list = issues.length
        ? issues.map((i) =>
              h(
                  'div',
                  { class: `issue ${i.severity}` },
                  h('span', { class: 'sev' }, i.severity),
                  h('span', { class: 'msg' }, i.message),
                  i.fix
                      ? h(
                            'button',
                            {
                                onclick: () => {
                                    try {
                                        applyEdits(ctx, i.fix!.edits);
                                        ctx.status(`${i.fix!.label}: pending until you save.`);
                                    } catch (e) {
                                        ctx.err(e);
                                    }
                                    ctx.changed();
                                },
                            },
                            i.fix.label,
                        )
                      : null,
              ),
          )
        : [
              h(
                  'div',
                  { class: 'issue ok' },
                  h('span', { class: 'sev' }, 'ok'),
                  h('span', { class: 'msg' }, 'Nothing to report.'),
              ),
          ];
    return h(
        'div',
        { class: 'repair' },
        h(
            'div',
            { class: 'tiles' },
            statTile('Errors', String(errors)),
            statTile('Warnings', String(warns)),
            statTile('Records', String(doc.save.entries.length), { note: 'all zlib streams inflate' }),
        ),
        pendingNote ? h('small', { class: 'sv-dim' }, pendingNote) : null,
        ...list,
        h(
            'p',
            { class: 'sv-dim' },
            'The container carries no checksum, so structure is checked here (keys, skill bounds, positions, the state table). ' +
                'Fixes are ordinary pending edits and can be undone. Whether the game itself accepts a save is only known from loading it.',
        ),
    );
}

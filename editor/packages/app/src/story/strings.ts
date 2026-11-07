// Strings panel: every XDB string table of the disc, per language slot, with search, new strings with fresh ids and the
// classes that use each string. Edits live in the StorySession and leave as pack entries (Story pack panel).

import { XDB_LANGUAGES } from '@drakan/formats';
import type { Panel } from '../editor/ctx.ts';
import { fill, h } from '../editor/dom.ts';
import { SYSTEM_XDB, type StorySession } from './session.ts';

const LANG_LABEL = ['English', 'German', 'French', 'Italian', 'Spanish'];
const LIMIT = 300;

export function createStringsPanel(session: StorySession): Panel {
    const state = { file: SYSTEM_XDB, q: '', slot: 0, sel: 0 as number, onlyChanged: false, unused: false, ootf: true };
    // String ids a converted Order of the Flame pack adds, by table (`SYSTEM`, `LEVEL1ON`): they get their own scope.
    let converted: Record<string, number[]> = {};
    const stemOf = (file: string): string => (file.split(/[\\/]/).pop() ?? file).replace(/\.XDB$/i, '').toUpperCase();
    const addedIds = (): Set<number> | null => {
        const ids = converted[stemOf(state.file)];
        return ids?.length ? new Set(ids) : null;
    };
    const fileSel = h('select', { class: 'story-sel', 'aria-label': 'String table' });
    const slotSel = h(
        'select',
        { class: 'story-sel', 'aria-label': 'Language slot' },
        ...LANG_LABEL.map((l, i) => h('option', { value: i }, l)),
    );
    const search = h('input', {
        type: 'search',
        class: 'story-search',
        placeholder: 'Search text or id',
        spellcheck: 'false',
        'aria-label': 'Search strings',
    });
    const changedBox = h('input', { type: 'checkbox' });
    const unusedBox = h('input', { type: 'checkbox' });
    const ootfBox = h('input', { type: 'checkbox', checked: true });
    const ootfLabel = h('label', { class: 'story-chk', title: 'Only the strings the converted pack adds' }, ootfBox, 'Order of the Flame');
    ootfLabel.hidden = true;
    const list = h('div', { class: 'story-list', role: 'listbox', 'aria-label': 'Strings' });
    const detail = h('div', { class: 'story-detail' });
    const info = h('div', { class: 'story-info' });
    const addBtn = h(
        'button',
        {
            class: 'btn',
            type: 'button',
            title: 'Add a string with the next free id to this table',
            onclick: () => {
                const id = session.addString(state.file, 'New string');
                state.sel = id;
                state.q = '';
                search.value = '';
                renderAll();
            },
        },
        'New string',
    );
    const loadBtn = h(
        'button',
        {
            class: 'btn primary',
            type: 'button',
            onclick: () => void session.ensure().catch((e) => session.host.status(String(e?.message ?? e), true)),
        },
        'Load strings from the disc',
    );
    const root = h(
        'div',
        { class: 'panel story story-strings' },
        h(
            'div',
            { class: 'story-bar' },
            fileSel,
            slotSel,
            search,
            h('label', { class: 'story-chk' }, changedBox, 'Changed'),
            ootfLabel,
            h('label', { class: 'story-chk', title: 'Strings no class default points at' }, unusedBox, 'Unused'),
            addBtn,
            info,
        ),
        h('div', { class: 'story-body' }, list, detail),
    );

    fileSel.addEventListener('change', () => {
        state.file = fileSel.value;
        state.sel = 0;
        renderAll();
    });
    slotSel.addEventListener('change', () => {
        state.slot = Number(slotSel.value);
        renderAll();
    });
    search.addEventListener('input', () => {
        state.q = search.value.trim().toLowerCase();
        renderList();
    });
    changedBox.addEventListener('change', () => {
        state.onlyChanged = changedBox.checked;
        renderList();
    });
    ootfBox.addEventListener('change', () => {
        state.ootf = ootfBox.checked;
        renderList();
    });
    unusedBox.addEventListener('change', () => {
        state.unused = unusedBox.checked;
        renderList();
    });

    const names = (): Map<number, string> => new Map(session.xdb(state.file)?.names ?? []);

    function renderFiles(): void {
        const d = session.data;
        fill(
            fileSel,
            ...(d?.xdbs ?? []).map((x) => {
                const edits = session.strings(x.file).filter((s) => session.isChanged(x.file, s.id)).length;
                return h(
                    'option',
                    { value: x.file, ...(x.file === state.file ? { selected: true } : {}) },
                    `${x.title}${edits ? ` (${edits} edited)` : ''}`,
                );
            }),
        );
        fileSel.value = state.file;
    }

    function rows(): { id: number; text: string; used: number; changed: boolean; sub: number }[] {
        const out = [];
        const added = addedIds();
        ootfLabel.hidden = !added;
        for (const s of session.strings(state.file)) {
            if (added && state.ootf && !added.has(s.id) && !session.isNewString(state.file, s.id)) continue;
            const text = session.text(state.file, s.id, state.slot);
            const hay = `${s.id} ${text}`.toLowerCase();
            if (state.q && !hay.includes(state.q)) continue;
            const used = session.refCount(state.file, s.id);
            const changed = session.isChanged(state.file, s.id);
            if (state.onlyChanged && !changed) continue;
            if (state.unused && used > 0) continue;
            out.push({ id: s.id, text, used, changed, sub: s.sub });
        }
        return out;
    }

    function renderList(): void {
        if (!session.data) {
            fill(list, h('div', { class: 'story-empty' }, 'Open a disc image, then load its strings.', loadBtn));
            info.textContent = '';
            return;
        }
        const r = rows();
        info.textContent = `${r.length} string${r.length === 1 ? '' : 's'}`;
        fill(
            list,
            ...r.slice(0, LIMIT).map((s) =>
                h(
                    'button',
                    {
                        type: 'button',
                        role: 'option',
                        class: `story-row${s.id === state.sel ? ' sel' : ''}`,
                        'aria-selected': s.id === state.sel,
                        onclick: () => {
                            state.sel = s.id;
                            renderList();
                            renderDetail();
                        },
                    },
                    h('span', { class: 'story-id' }, String(s.id)),
                    h('span', { class: 'story-text' }, s.text.replace(/\s+/g, ' ') || '(empty)'),
                    s.changed
                        ? h(
                              'span',
                              { class: 'story-tag warn' },
                              session.isNewString(state.file, s.id) ? 'new' : 'edited',
                          )
                        : null,
                    h(
                        'span',
                        { class: 'story-ref', title: 'Class defaults and placed objects using this string' },
                        s.used ? `${s.used}x` : '',
                    ),
                ),
            ),
            r.length > LIMIT
                ? h('div', { class: 'story-empty' }, `${r.length - LIMIT} more: narrow the search.`)
                : null,
        );
    }

    function renderDetail(): void {
        const id = state.sel;
        const s = id ? session.str(state.file, id) : undefined;
        if (!s) {
            fill(detail, h('div', { class: 'story-empty' }, 'Select a string.'));
            return;
        }
        const x = session.xdb(state.file)!;
        const refs = x.refs.find((r) => r[0] === id)?.[1] ?? [];
        const objs = session.objectRefs(state.file, id);
        const objCount = objs.reduce((n, u) => n + u.refs.length, 0);
        const group = names().get(s.sub);
        const problem = session.stringProblem(state.file, id);
        const areas = XDB_LANGUAGES.map((lang, slot) => {
            const ta = h('textarea', {
                class: 'story-ta',
                rows: 3,
                spellcheck: 'false',
                'aria-label': `${LANG_LABEL[slot]} text`,
            });
            ta.value = s.text[slot] ?? '';
            ta.addEventListener('input', () => {
                session.setText(state.file, id, slot, ta.value);
            });
            return h(
                'label',
                { class: 'story-slot' },
                h('span', {}, `${LANG_LABEL[slot]}${slot === session.data!.slot ? ' (disc language)' : ''}`),
                ta,
            );
        });
        fill(
            detail,
            h('div', { class: 'story-title' }, `String ${id}`, h('span', { class: 'dim' }, ` ${x.title}`)),
            h(
                'div',
                { class: 'dim small' },
                `Group ${s.sub}${group ? ` "${group}"` : ''}. Empty slots show the English text.`,
            ),
            ...areas,
            problem ? h('div', { class: 'story-warn' }, problem) : null,
            h(
                'div',
                { class: 'story-refs' },
                h('div', { class: 'lbl' }, `Used by ${refs.length} class default${refs.length === 1 ? '' : 's'}`),
                refs.length
                    ? h(
                          'ul',
                          {},
                          ...refs
                              .slice(0, 20)
                              .map((r) => h('li', {}, `${r.className} (class ${r.classId}), field "${r.field}"`)),
                          refs.length > 20 ? h('li', { class: 'dim' }, `and ${refs.length - 20} more`) : null,
                      )
                    : h(
                          'div',
                          { class: 'dim small' },
                          session.stringUsage
                              ? 'No class default points here.'
                              : 'No class default points here. Scan the levels to count level objects too.',
                      ),
                session.stringUsage
                    ? h(
                          'div',
                          {},
                          h('div', { class: 'lbl' }, `Used by ${objCount} placed object${objCount === 1 ? '' : 's'}`),
                          objCount
                              ? h(
                                    'ul',
                                    {},
                                    ...objs.slice(0, 12).map((u) =>
                                        h(
                                            'li',
                                            {},
                                            `${u.level}: ${u.refs
                                                .slice(0, 4)
                                                .map((r) => `#${r.objectId} ${r.className} "${r.field}"`)
                                                .join(
                                                    ', ',
                                                )}${u.refs.length > 4 ? ` and ${u.refs.length - 4} more` : ''}`,
                                        ),
                                    ),
                                )
                              : null,
                      )
                    : h(
                          'button',
                          {
                              class: 'btn',
                              type: 'button',
                              title: 'Reads every level of the disc (several seconds)',
                              onclick: () =>
                                  void session
                                      .scanStrings()
                                      .catch((e) => session.host.status(String(e?.message ?? e), true)),
                          },
                          'Scan levels for object strings',
                      ),
            ),
            h(
                'div',
                { class: 'btnrow' },
                h(
                    'button',
                    {
                        class: 'btn',
                        type: 'button',
                        disabled: !session.isChanged(state.file, id) || session.isNewString(state.file, id),
                        onclick: () => {
                            session.revertString(state.file, id);
                            renderAll();
                        },
                    },
                    'Revert',
                ),
            ),
        );
    }

    function renderAll(): void {
        renderFiles();
        renderList();
        renderDetail();
    }

    let inited = false;
    session.subscribe(() => {
        if (session.data && !inited) {
            inited = true;
            void session.host
                .call('convertedStory')
                .then((c) => {
                    converted = Object.fromEntries(Object.entries(c).map(([k, v]) => [k, v.texts ?? []]));
                    renderList();
                })
                .catch(() => undefined);
            state.slot = session.data.slot;
            slotSel.value = String(state.slot);
        }
        if (
            document.activeElement &&
            root.contains(document.activeElement) &&
            document.activeElement.tagName === 'TEXTAREA'
        ) {
            // typing: only the list and counters change
            renderFiles();
            renderList();
            return;
        }
        renderAll();
    });
    slotSel.value = '0';
    renderAll();
    return {
        el: root,
        onLoad() {
            state.slot = session.data?.slot ?? 0;
            slotSel.value = String(state.slot);
            renderAll();
        },
    };
}

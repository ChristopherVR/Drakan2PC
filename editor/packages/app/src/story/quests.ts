// Quests panel: the System.ODB quest classes (journal entries and map markers) grouped by chapter. Edit kind, optional
// flag, journal and map text (the XDB strings they point at), the marker position on the chapter map, add new quests and
// see where each quest is placed and started / completed in the levels.

import { chapterOfGroup, type QuestKind } from '@drakan/formats';
import type { Panel } from '../editor/ctx.ts';
import { fill, h } from '../editor/dom.ts';
import { openTemplate } from '../editor/logicpanel.ts';
import { SYSTEM_XDB, type QuestRow, type StorySession } from './session.ts';

const KINDS: [QuestKind, string][] = [
    ['journal', 'Journal entry'],
    ['map', 'Map marker'],
    ['both', 'Journal entry and map marker'],
];
const MAP_MAX = 512;

export function createQuestsPanel(session: StorySession): Panel {
    const state = { q: '', sel: 0, slot: 0, dragging: false };
    const search = h('input', {
        type: 'search',
        class: 'story-search',
        placeholder: 'Search quests',
        spellcheck: 'false',
        'aria-label': 'Search quests',
    });
    const list = h('div', { class: 'story-list', role: 'listbox', 'aria-label': 'Quests' });
    const detail = h('div', { class: 'story-detail' });
    const info = h('div', { class: 'story-info' });
    const newBtn = h(
        'button',
        {
            class: 'btn',
            type: 'button',
            title: 'Create a quest from the selected one (copies its sound and colour)',
            onclick: () => {
                if (!session.data) return;
                const id = session.addQuest(state.sel || session.data.quests[0]!.cls.classId, 'New quest objective');
                state.sel = id;
                renderAll();
            },
        },
        'New quest',
    );
    const scanBtn = h(
        'button',
        {
            class: 'btn',
            type: 'button',
            title: 'Read every level and list where each quest is placed and triggered (takes a few seconds)',
            onclick: () => {
                scanBtn.disabled = true;
                scanBtn.textContent = 'Scanning levels...';
                session
                    .scanUsage()
                    .catch((e) => session.host.status(String(e?.message ?? e), true))
                    .finally(() => {
                        scanBtn.disabled = false;
                        scanBtn.textContent = 'Scan levels';
                    });
            },
        },
        'Scan levels',
    );
    const loadBtn = h(
        'button',
        {
            class: 'btn primary',
            type: 'button',
            onclick: () => void session.ensure().catch((e) => session.host.status(String(e?.message ?? e), true)),
        },
        'Load quests from the disc',
    );
    const root = h(
        'div',
        { class: 'panel story story-quests' },
        h('div', { class: 'story-bar' }, search, newBtn, scanBtn, info),
        h('div', { class: 'story-body' }, list, detail),
    );
    search.addEventListener('input', () => {
        state.q = search.value.trim().toLowerCase();
        renderList();
    });

    const titleOf = (r: QuestRow): string => {
        const j = r.draft.journalId ? session.text(SYSTEM_XDB, r.draft.journalId, state.slot) : '';
        const m = r.draft.mapTextId ? session.text(SYSTEM_XDB, r.draft.mapTextId, state.slot) : '';
        return (r.draft.kind === 'map' ? m || j : j || m) || r.name;
    };
    const groupName = (g: number): string => session.data?.groups.find((x) => x[0] === g)?.[1].name ?? `Group ${g}`;
    const chapterLabel = (g: number): string => {
        const c = chapterOfGroup(groupName(g));
        return c.chapter ? `Chapter ${c.chapter} - ${c.label}` : groupName(g);
    };

    function renderList(): void {
        if (!session.data) {
            fill(list, h('div', { class: 'story-empty' }, 'Open a disc image, then load its quests.', loadBtn));
            info.textContent = '';
            return;
        }
        const rows = session
            .questRows()
            .filter((r) => !state.q || `${titleOf(r)} ${r.name} ${r.classId}`.toLowerCase().includes(state.q));
        info.textContent = `${rows.length} quest${rows.length === 1 ? '' : 's'}`;
        const byGroup = new Map<number, QuestRow[]>();
        for (const r of rows) byGroup.set(r.group, [...(byGroup.get(r.group) ?? []), r]);
        const groups = [...byGroup].sort((a, b) => chapterLabel(a[0]).localeCompare(chapterLabel(b[0])));
        const kids: (Node | null)[] = [];
        for (const [g, rs] of groups) {
            kids.push(h('div', { class: 'story-group' }, chapterLabel(g)));
            for (const r of rs) {
                kids.push(
                    h(
                        'button',
                        {
                            type: 'button',
                            role: 'option',
                            class: `story-row${r.classId === state.sel ? ' sel' : ''}`,
                            'aria-selected': r.classId === state.sel,
                            onclick: () => {
                                state.sel = r.classId;
                                renderList();
                                renderDetail();
                            },
                        },
                        h('span', { class: 'story-id' }, String(r.classId)),
                        h('span', { class: 'story-text' }, titleOf(r)),
                        h(
                            'span',
                            { class: 'story-tag' },
                            r.draft.kind === 'both' ? 'J+M' : r.draft.kind === 'map' ? 'Map' : 'J',
                        ),
                        r.draft.optional ? h('span', { class: 'story-tag' }, 'Opt') : null,
                        r.isNew
                            ? h('span', { class: 'story-tag warn' }, 'new')
                            : r.edited
                              ? h('span', { class: 'story-tag warn' }, 'edited')
                              : null,
                    ),
                );
            }
        }
        fill(list, ...kids);
    }

    /** Text field bound to the XDB string a quest field points at (creates one when the field has none). */
    function textField(r: QuestRow, label: string, key: 'journalId' | 'mapTextId'): HTMLElement {
        const id = r.draft[key];
        const ta = h('textarea', { class: 'story-ta', rows: 3, spellcheck: 'false', 'aria-label': label });
        ta.value = id ? session.text(SYSTEM_XDB, id, state.slot) : '';
        ta.addEventListener('input', () => {
            let sid = session.questRows().find((x) => x.classId === r.classId)!.draft[key];
            if (!sid) {
                sid = session.addString(SYSTEM_XDB, ta.value);
                session.setDraft(r.classId, { [key]: sid });
                return;
            }
            session.setText(SYSTEM_XDB, sid, state.slot, ta.value);
        });
        const shared = id ? session.refCount(SYSTEM_XDB, id) : 0;
        return h(
            'label',
            { class: 'story-slot' },
            h(
                'span',
                {},
                `${label}${id ? ` (string ${id}${shared > 1 ? `, shared by ${shared} classes` : ''})` : ' (none yet: typing creates a string)'}`,
            ),
            ta,
        );
    }

    function mapPlot(r: QuestRow): HTMLElement {
        const size = 240;
        const cv = h('canvas', {
            class: 'story-map',
            width: size,
            height: size,
            'aria-label': 'Marker position on the chapter map',
        });
        const ctx = cv.getContext('2d')!;
        const same = session.questRows().filter((x) => x.group === r.group && x.draft.kind !== 'journal');
        const draw = (): void => {
            const cur = session.questRows().find((x) => x.classId === r.classId) ?? r;
            const css = getComputedStyle(cv);
            ctx.clearRect(0, 0, size, size);
            ctx.fillStyle = css.getPropertyValue('--field') || '#0f0f11';
            ctx.fillRect(0, 0, size, size);
            ctx.strokeStyle = css.getPropertyValue('--border') || '#2f2f35';
            for (let i = 1; i < 4; i++) {
                ctx.beginPath();
                ctx.moveTo((i * size) / 4, 0);
                ctx.lineTo((i * size) / 4, size);
                ctx.moveTo(0, (i * size) / 4);
                ctx.lineTo(size, (i * size) / 4);
                ctx.stroke();
            }
            for (const o of same) {
                if (o.classId === cur.classId) continue;
                ctx.fillStyle = css.getPropertyValue('--fg-faint') || '#6f6f7a';
                ctx.fillRect((o.draft.mapX / MAP_MAX) * size - 2, (o.draft.mapY / MAP_MAX) * size - 2, 5, 5);
            }
            ctx.fillStyle = css.getPropertyValue('--accent') || '#3d8bfd';
            ctx.beginPath();
            ctx.arc((cur.draft.mapX / MAP_MAX) * size, (cur.draft.mapY / MAP_MAX) * size, 6, 0, Math.PI * 2);
            ctx.fill();
        };
        const move = (e: PointerEvent): void => {
            const b = cv.getBoundingClientRect();
            const x = Math.round(Math.min(MAP_MAX, Math.max(0, ((e.clientX - b.left) / b.width) * MAP_MAX)));
            const y = Math.round(Math.min(MAP_MAX, Math.max(0, ((e.clientY - b.top) / b.height) * MAP_MAX)));
            session.setDraft(r.classId, { mapX: x, mapY: y });
        };
        cv.addEventListener('pointerdown', (e) => {
            cv.setPointerCapture(e.pointerId);
            state.dragging = true;
            move(e);
        });
        cv.addEventListener('pointerup', () => {
            state.dragging = false;
            renderDetail();
        });
        cv.addEventListener('pointermove', (e) => {
            if (cv.hasPointerCapture(e.pointerId)) move(e);
        });
        draw();
        const unsub = session.subscribe(() => {
            if (!cv.isConnected) unsub();
            else draw();
        });
        return cv;
    }

    function usageBlock(r: QuestRow): HTMLElement {
        const wrap = h('div', { class: 'story-refs' }, h('div', { class: 'lbl' }, 'Where it is used'));
        if (r.isNew) {
            wrap.append(
                h(
                    'div',
                    { class: 'dim small' },
                    'A new quest needs something to start it: place the quest class in a level (Content Browser, System objects) and link a trigger to it with message 1; message 18 completes it.',
                ),
            );
            return wrap;
        }
        if (!session.usage) {
            wrap.append(h('div', { class: 'dim small' }, 'Press Scan levels to read the placements and triggers.'));
            return wrap;
        }
        const levels = session.usage.filter((u) => u.placements.some((p) => p[0] === r.classId));
        if (!levels.length) wrap.append(h('div', { class: 'dim small' }, 'Not placed in any level.'));
        for (const u of levels) {
            const pls = u.placements.find((p) => p[0] === r.classId)![1];
            for (const p of pls) {
                const items = p.sources.map((s) => {
                    const what =
                        s.role === 'giver'
                            ? `quest giver sends ${s.giverMessages.map((m) => `${m.value}`).join(', ')}`
                            : s.role === 'start'
                              ? 'starts it (message 1)'
                              : s.role === 'complete'
                                ? 'completes it (message 18)'
                                : `sends message ${s.message ?? '?'}`;
                    return h('li', {}, `${s.className} #${s.id}: ${what}`);
                });
                wrap.append(
                    h('div', { class: 'story-lvl' }, `${u.level} object #${p.objectId}`),
                    p.sources.length ? h('ul', {}, ...items) : h('div', { class: 'dim small' }, 'Nothing links to it.'),
                );
            }
        }
        return wrap;
    }

    function renderDetail(): void {
        const r = session.questRows().find((x) => x.classId === state.sel);
        if (!r) {
            fill(detail, h('div', { class: 'story-empty' }, 'Select a quest.'));
            return;
        }
        const kindSel = h(
            'select',
            { class: 'story-sel', 'aria-label': 'Quest kind' },
            ...KINDS.map(([k, label]) =>
                h('option', { value: k, ...(k === r.draft.kind ? { selected: true } : {}) }, label),
            ),
        );
        kindSel.addEventListener('change', () => session.setDraft(r.classId, { kind: kindSel.value as QuestKind }));
        const opt = h('input', { type: 'checkbox', ...(r.draft.optional ? { checked: true } : {}) });
        opt.addEventListener('change', () => session.setDraft(r.classId, { optional: opt.checked }));
        const num = (label: string, key: 'mapX' | 'mapY'): HTMLElement => {
            const i = h('input', { type: 'number', min: 0, max: MAP_MAX, value: r.draft[key], 'aria-label': label });
            i.addEventListener('change', () =>
                session.setDraft(r.classId, { [key]: Math.max(0, Number(i.value) || 0) }),
            );
            return h('label', { class: 'mf' }, h('span', {}, label), i);
        };
        const needsMap = r.draft.kind !== 'journal';
        fill(
            detail,
            h(
                'div',
                { class: 'story-title' },
                titleOf(r),
                h('span', { class: 'dim' }, ` class ${r.classId}, ${chapterLabel(r.group)}`),
            ),
            h('div', { class: 'dim small' }, `Developer name: ${r.name}`),
            h('label', { class: 'mf' }, h('span', {}, 'Kind'), kindSel),
            h('label', { class: 'story-chk' }, opt, 'Optional quest (listed under Optional Quests)'),
            r.draft.kind !== 'map' ? textField(r, 'Journal entry', 'journalId') : null,
            r.draft.kind !== 'journal' ? textField(r, 'Map text (place name)', 'mapTextId') : null,
            needsMap ? h('div', { class: 'row2' }, num('Map X', 'mapX'), num('Map Y', 'mapY')) : null,
            needsMap ? mapPlot(r) : null,
            usageBlock(r),
            h(
                'div',
                { class: 'btnrow' },
                h(
                    'button',
                    {
                        class: 'btn',
                        type: 'button',
                        title: "Open the Logic panel's Place quest object setup for this quest class: the object plus optional start and complete trigger volumes",
                        onclick: () => {
                            if (!openTemplate('quest-object', { classId: r.classId }))
                                session.host.status('Open a level in the editor first.', true);
                        },
                    },
                    'Place quest object in level...',
                ),
                r.isNew
                    ? h(
                          'button',
                          {
                              class: 'btn',
                              type: 'button',
                              onclick: () => {
                                  session.removeNewQuest(r.classId);
                                  state.sel = 0;
                                  renderAll();
                              },
                          },
                          'Remove new quest',
                      )
                    : h(
                          'button',
                          {
                              class: 'btn',
                              type: 'button',
                              disabled: !r.edited,
                              onclick: () => {
                                  session.revertQuest(r.classId);
                                  renderAll();
                              },
                          },
                          'Revert quest fields',
                      ),
            ),
        );
    }

    function renderAll(): void {
        renderList();
        renderDetail();
    }

    let inited = false;
    session.subscribe(() => {
        if (session.data && !inited) {
            inited = true;
            state.slot = session.data.slot;
        }
        if (state.dragging) return; // the plot redraws itself
        const a = document.activeElement;
        if (a && detail.contains(a) && (a.tagName === 'TEXTAREA' || a.tagName === 'INPUT')) {
            renderList(); // typing or dragging: keep the focused field
            return;
        }
        renderAll();
    });
    renderAll();
    return { el: root, onLoad: renderAll };
}

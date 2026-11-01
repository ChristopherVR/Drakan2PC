// Story pack panel: lists the changed entries of the story session and exports them as a mod pack.

import { ModError } from '@drakan/editor-core';
import type { Panel } from '../editor/ctx.ts';
import { fill, h } from '../editor/dom.ts';
import type { StorySession } from './session.ts';

const hex4 = (n: number): string => n.toString(16).padStart(4, '0');

export function createStoryPackPanel(session: StorySession): Panel {
    const idIn = h('input', { type: 'text', value: 'my.story', spellcheck: 'false', 'aria-label': 'Mod id' });
    const nameIn = h('input', { type: 'text', value: 'My story edits', 'aria-label': 'Mod name' });
    const authIn = h('input', { type: 'text', value: '', 'aria-label': 'Author' });
    const entries = h('div', { class: 'story-entries' });
    const report = h('pre', { class: 'mod-report' });
    const meta = (): ReturnType<StorySession['meta']> =>
        session.meta(idIn.value.trim(), nameIn.value.trim(), authIn.value.trim());
    const fail = (e: unknown): void => {
        const msg = e instanceof Error ? e.message : String(e);
        report.textContent = msg;
        session.host.status(msg, true);
    };
    const zip = h(
        'button',
        {
            class: 'btn primary',
            type: 'button',
            onclick: () =>
                void session
                    .exportZip(meta())
                    .then(() => (report.textContent = `Exported ${idIn.value.trim()}.zip`))
                    .catch(fail),
        },
        'Export zip',
    );
    const folder = h(
        'button',
        {
            class: 'btn',
            type: 'button',
            title: "Write into the game's mods folder and turn it on",
            onclick: () =>
                void session
                    .exportToMods(meta())
                    .then((r) => (report.textContent = r.where ? `Written to ${r.where} and turned on.` : 'Cancelled.'))
                    .catch(fail),
        },
        'Write to mods folder',
    );
    const root = h(
        'div',
        { class: 'panel story story-pack' },
        h(
            'div',
            { class: 'mod-form' },
            h('label', { class: 'mf' }, h('span', {}, 'Id'), idIn),
            h('label', { class: 'mf' }, h('span', {}, 'Name'), nameIn),
            h('label', { class: 'mf' }, h('span', {}, 'Author'), authIn),
            h('div', { class: 'btnrow' }, zip, folder),
            report,
        ),
        h('div', { class: 'mod-side' }, h('div', { class: 'lbl' }, 'Changed entries'), entries),
    );

    let seq = 0;
    async function refresh(): Promise<void> {
        const mine = ++seq;
        try {
            const ch = await session.changes();
            if (mine !== seq) return;
            fill(
                entries,
                ch.length
                    ? h(
                          'ul',
                          {},
                          ...ch.map((c) =>
                              h(
                                  'li',
                                  {},
                                  `${c.isNew ? 'new' : 'override'} ${c.file} ${hex4(c.type)}/${c.id}/${c.sub} (${c.data.length} bytes)`,
                              ),
                          ),
                      )
                    : h(
                          'div',
                          { class: 'dim small' },
                          'No story edits yet. Change a string or a quest and it shows up here.',
                      ),
            );
        } catch (e) {
            if (!(e instanceof ModError))
                fill(entries, h('div', { class: 'story-warn' }, String((e as Error).message ?? e)));
        }
    }
    session.subscribe(() => void refresh());
    void refresh();
    return { el: root, onLoad: () => void refresh() };
}

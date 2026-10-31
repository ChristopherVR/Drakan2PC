// The Story area: one dock tab that holds the story editors (Strings, Quests, Dialogue, Cinematics, Pack) behind a
// small tab strip, so they do not each take a place in the crowded bottom dock. Registered through the shell's registry.

import type { Panel } from '../editor/ctx.ts';
import { fill, h } from '../editor/dom.ts';

export interface StoryTab {
    id: string;
    title: string;
    panel: Panel;
}

export interface StoryArea {
    panel: Panel;
    /** Show a tab of the area (the area itself is shown by the caller). */
    select(id: string): void;
    selected(): string;
}

export function createStoryArea(tabs: readonly StoryTab[], initial?: string): StoryArea {
    let cur = tabs.find((t) => t.id === initial)?.id ?? tabs[0]!.id;
    const strip = h('div', { class: 'story-tabs', role: 'tablist', 'aria-label': 'Story editors' });
    const body = h('div', { class: 'story-area-body' });
    const root = h('div', { class: 'panel story-area' }, strip, body);
    const active = (): StoryTab => tabs.find((t) => t.id === cur) ?? tabs[0]!;

    function render(): void {
        fill(
            strip,
            ...tabs.map((t) =>
                h(
                    'button',
                    {
                        type: 'button',
                        role: 'tab',
                        class: `story-tab${t.id === cur ? ' active' : ''}`,
                        'aria-selected': t.id === cur,
                        onclick: () => select(t.id),
                    },
                    t.title,
                ),
            ),
        );
        fill(body, active().panel.el);
    }
    function select(id: string): void {
        if (!tabs.some((t) => t.id === id)) return;
        cur = id;
        render();
        active().panel.onLoad?.();
    }
    render();
    const each = (f: (p: Panel) => void): void => tabs.forEach((t) => f(t.panel));
    return {
        select,
        selected: () => cur,
        panel: {
            el: root,
            onSelection: () => active().panel.onSelection?.(),
            onChange: () => each((p) => p.onChange?.()),
            onVisibility: () => each((p) => p.onVisibility?.()),
            onLoad: () => each((p) => p.onLoad?.()),
        },
    };
}

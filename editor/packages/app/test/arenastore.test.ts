// Synthetic only: the multiplayer map store (undo through the command stack, live drags, mode persistence).
import { describe, expect, it } from 'vitest';
import { CommandStack, LevelDocument, defaultArenaBounds, defaultMode } from '@drakan/editor-core';
import type { LevelView } from '../src/level.ts';
import { ArenaStore } from '../src/editor/arena/store.ts';

function setup(): { st: ArenaStore; stack: CommandStack } {
    const stack = new CommandStack(new LevelDocument());
    const view = { run: (c: Parameters<CommandStack['run']>[0]) => stack.run(c) } as unknown as LevelView;
    const st = new ArenaStore(() => view);
    st.start('Level X');
    return { st, stack };
}

describe('arena store', () => {
    it('runs edits through the command stack so undo and redo work', () => {
        const { st, stack } = setup();
        st.commit('Place spawn', (d) => d.spawns.push({ uid: d.nextUid++, pos: [1, 2, 3], yaw: 0, team: 0 }));
        st.commit('Place spawn', (d) => d.spawns.push({ uid: d.nextUid++, pos: [4, 5, 6], yaw: 90, team: 1 }));
        expect(st.draft.spawns).toHaveLength(2);
        expect(stack.history().labels).toEqual(['Place spawn', 'Place spawn']);
        stack.undo();
        expect(st.draft.spawns.map((s) => s.pos)).toEqual([[1, 2, 3]]);
        stack.redo();
        expect(st.draft.spawns[1]).toMatchObject({ yaw: 90, team: 1 });
    });

    it('records one step for a drag and drops a selection that undo removed', () => {
        const { st, stack } = setup();
        st.commit('Place spawn', (d) => d.spawns.push({ uid: d.nextUid++, pos: [0, 0, 0], yaw: 0, team: 0 }));
        const uid = st.draft.spawns[0]!.uid;
        st.select({ kind: 'spawn', uid });
        st.beginLive();
        for (let i = 1; i <= 5; i++) st.liveEdit((d) => (d.spawns[0]!.pos = [i * 100, 0, 0]));
        st.endLive('Move spawn point');
        expect(stack.history().labels).toEqual(['Place spawn', 'Move spawn point']);
        expect(st.draft.spawns[0]!.pos[0]).toBe(500);
        stack.undo();
        expect(st.draft.spawns[0]!.pos[0]).toBe(0);
        stack.undo();
        expect(st.draft.spawns).toHaveLength(0);
        expect(st.sel).toBeNull();
    });

    it('an unchanged drag adds no history row', () => {
        const { st, stack } = setup();
        st.beginLive();
        st.endLive('Nothing');
        expect(stack.depth).toBe(0);
    });

    it('keeps custom modes', () => {
        const { st } = setup();
        st.setModes([defaultMode('a'), defaultMode('b')]);
        expect(st.modes.map((m) => m.id)).toEqual(['a', 'b']);
        const events: string[] = [];
        st.on((e) => events.push(e));
        st.setModes([]);
        expect(events).toContain('modes');
    });
});

describe('default bounds', () => {
    it('covers the middle of the objects, ignoring parked ones and outliers', () => {
        const pts: number[][] = [[0, 0, 0]];
        for (let i = 0; i < 100; i++) pts.push([10000 + i * 100, 50, 20000 + i * 50]);
        pts.push([9e6, 0, 9e6]);
        const b = defaultArenaBounds(pts)!;
        expect(b[0]).toBeLessThan(11000);
        expect(b[2]).toBeLessThan(21000);
        expect(b[2]).toBeGreaterThan(19000);
        expect(defaultArenaBounds([[1, 2, 3]])).toBeNull();
    });
});

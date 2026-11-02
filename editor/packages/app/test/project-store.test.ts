// Synthetic only. The store works without local storage (node has none).
import { addDiscLevel, newProject } from '@drakan/editor-core';
import { describe, expect, it } from 'vitest';
import { projects } from '../src/editor/project/store.ts';

describe('project store', () => {
    it('creates, updates, ticks and notifies', () => {
        let n = 0;
        const off = projects.on(() => n++);
        expect(projects.get()).toBeNull();
        const p = projects.ensure('My map', 'multiplayer');
        expect(p.id).toBe('my-map');
        projects.update((q) => addDiscLevel(q, 'Level A - One'));
        projects.tick('multiplayer', 'export', true);
        projects.tick('multiplayer', 'export', true);
        expect(projects.get()!.ticks.multiplayer).toEqual(['export']);
        projects.tick('multiplayer', 'export', false);
        expect(projects.get()!.ticks.multiplayer).toEqual([]);
        expect(n).toBeGreaterThanOrEqual(4);
        off();
    });
    it('forgets what happened this session when another project is opened', () => {
        projects.session.exported = true;
        projects.set(newProject('Other'));
        expect(projects.session.exported).toBe(false);
    });
});

describe('unedited levels', () => {
    it('lists the levels whose stored edits changed nothing', async () => {
        const { uneditedTitles } = await import('../src/editor/project/export.ts');
        const counts = new Map([
            ['A', 0],
            ['B', 3],
            ['C', 0],
        ]);
        // C has no stored edits at all (never loaded a document): nothing to drop
        expect(uneditedTitles(counts, new Set(['A', 'B']))).toEqual(['A']);
    });
});

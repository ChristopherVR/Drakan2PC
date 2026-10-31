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

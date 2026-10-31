import { describe, expect, it } from 'vitest';
import {
    allCommands,
    bindPanelHost,
    fuzzyScore,
    registerCommand,
    registerPanel,
    runCommand,
} from '../src/editor/registry.ts';

describe('command registry', () => {
    it('registers, runs, honours enabled and unregisters', () => {
        let n = 0;
        const off = registerCommand({ id: 't.a', title: 'Test A', run: () => n++ });
        registerCommand({ id: 't.b', title: 'Test B', enabled: () => false, run: () => n++ });
        expect(runCommand('t.a')).toBe(true);
        expect(runCommand('t.b')).toBe(false);
        expect(n).toBe(1);
        off();
        expect(allCommands().some((c) => c.id === 't.a')).toBe(false);
    });

    it('queues panels until the host binds, then adds a show command', () => {
        const added: string[] = [];
        const shown: string[] = [];
        registerPanel({ id: 'early', title: 'Early', icon: 'cube', panel: { el: {} as HTMLElement } });
        bindPanelHost({ add: (s) => added.push(s.id), show: (id) => shown.push(id) });
        registerPanel({ id: 'late', title: 'Late', icon: 'cube', panel: { el: {} as HTMLElement } });
        expect(added).toEqual(['early', 'late']);
        runCommand('panel.late');
        expect(shown).toEqual(['late']);
    });
});

describe('fuzzyScore', () => {
    it('ranks exact > prefix > substring > subsequence and rejects misses', () => {
        const s = (t: string): number => fuzzyScore(t, 'dra');
        expect(fuzzyScore('Dragon', 'dragon')).toBeGreaterThan(s('Dragon'));
        expect(s('Dragon')).toBeGreaterThan(s('Big Dragon'));
        expect(s('Big Dragon')).toBeGreaterThan(s('Abdra'));
        expect(fuzzyScore('Dragon', 'xyz')).toBe(0);
        expect(fuzzyScore('Place Dragon Armor', 'dragon armor')).toBeGreaterThan(0);
    });
});

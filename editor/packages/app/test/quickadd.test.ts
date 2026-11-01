import { describe, expect, it } from 'vitest';
import { installQuickAdd, quickAddItems, registerQuickAdd } from '../src/editor/quickadd.ts';
import { allCommands, registerCommand } from '../src/editor/registry.ts';

describe('quick add', () => {
    it('lists the built-in rows in order and gives each a palette command', () => {
        installQuickAdd();
        const ids = quickAddItems().map((i) => i.id);
        for (const id of ['arena', 'puzzle', 'cutscene', 'dialogue', 'quest', 'level']) expect(ids).toContain(id);
        expect(ids.indexOf('arena')).toBeLessThan(ids.indexOf('level'));
        expect(allCommands().some((c) => c.id === 'add.arena')).toBe(true);
    });

    it('shows rows of other features only once their command exists, and runs them', () => {
        installQuickAdd();
        expect(quickAddItems().some((i) => i.id === 'vendor')).toBe(false);
        let n = 0;
        registerCommand({ id: 'panel.vendors', title: 'Vendors', run: () => n++ });
        const row = quickAddItems().find((i) => i.id === 'vendor');
        expect(row).toBeTruthy();
        row!.run();
        expect(n).toBe(1);
        registerQuickAdd({ id: 'x', title: 'X', summary: '', icon: 'plus', order: 1, run: () => true });
        expect(quickAddItems()[0]!.id).toBe('x');
    });
});

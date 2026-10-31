import { describe, expect, it } from 'vitest';
import { applyFieldEdits, getField, parseSave, sameBytes, transferPlan } from '@drakan/formats';
import { liveSave } from '../../formats/test/livefixture.ts';
import { buildSave, saveEntries } from '../../formats/test/savefixture.ts';
import { CommandStack, SaveDocument, SetFieldCommand } from '../src/index.ts';

describe('SaveDocument: stats, inventory, quests', () => {
    it('reads the stats block, inventory cells and quest rows', () => {
        const d = SaveDocument.open(liveSave());
        expect(d.hasStats).toBe(true);
        expect([d.value('stat.hp'), d.value('stat.maxhp'), d.value('stat.gold')]).toEqual([84, 100, 646]);
        expect(d.inventory.map((c) => c.cell)).toEqual([0, 1, 2, 8]);
        expect(d.inventory[0]).toMatchObject({ resource: 0x31bc9, durability: 181, max: 200, equipped: 1 });
        expect(d.inventory[2]).toMatchObject({ resource: 0x33cbc, durability: 0xffffffff, hot: 1 });
        expect(d.quests.map((q) => [q.index, q.questId, q.state, q.enabled])).toEqual([
            [0, 0x10c7, 4, 1],
            [1, 0x1047, 2, 1],
        ]);
    });

    it('a save without a stats block still opens, with no stats and no inventory', () => {
        const d = SaveDocument.open(buildSave(saveEntries()));
        expect(d.hasStats).toBe(false);
        expect(d.inventory).toEqual([]);
        expect(d.quests).toEqual([]);
    });

    it('edits max HP and an item count, labels them, and changes only those entries', () => {
        const blob = liveSave();
        const d = SaveDocument.open(blob);
        const s = new CommandStack(d);
        s.run(new SetFieldCommand('stat.maxhp', 333));
        s.run(new SetFieldCommand('inv.0.durability', 77));
        s.run(new SetFieldCommand('inv.1.hot', 1));
        expect(d.pendingEdits().map((e) => `${e.label}:${e.before}->${e.after}`)).toEqual([
            'Max health:100->333',
            'cell 0 durability:181->77',
            'cell 1 quick slot:0->1',
        ]);
        const out = parseSave(d.build().bytes);
        expect(getField(out, 'stat.maxhp')).toBe(333);
        expect(getField(out, 'inv.0.durability')).toBe(77);
        const orig = parseSave(blob);
        const changed = out.entries.filter((e, i) => !sameBytes(e.data, orig.entries[i]!.data)).map((e) => e.id);
        expect(changed.sort((a, b) => a - b)).toEqual([56, 301, 893]);
    });

    it('rejects edits to an empty cell and a cell without an item', () => {
        const d = SaveDocument.open(liveSave());
        expect(d.checkEdit('inv.5.durability', 1)).not.toBeNull();
    });
});

describe('transferPlan with stats and inventory', () => {
    const src = parseSave(liveSave({ gold: 9999, xp: 777, level: 5, skillpoints: 7, maxhp: 333, hp: 200 }));
    const dst = parseSave(liveSave());

    it('lists the stats block instead of the legacy hp / gold', () => {
        const plan = transferPlan(src, dst, true);
        const names = plan.map((r) => r.name);
        expect(names).toContain('stat.gold');
        expect(names).not.toContain('gold');
        const out = applyFieldEdits(
            dst,
            plan.filter((r) => !r.blocked).flatMap((r) => r.edits),
        );
        expect([getField(out, 'stat.gold'), getField(out, 'stat.level'), getField(out, 'stat.maxhp')]).toEqual([
            9999, 5, 333,
        ]);
    });

    it('copies item counts only for the same item in the same cell', () => {
        const edited = applyFieldEdits(parseSave(liveSave()), [{ name: 'inv.0.durability', value: 12 }]);
        const plan = transferPlan(edited, dst, true).filter((r) => r.name.startsWith('inv.'));
        expect(plan.find((r) => r.name === 'inv.0.durability')).toMatchObject({ from: 12, to: 181, blocked: null });
        const out = applyFieldEdits(
            dst,
            plan.filter((r) => !r.blocked).flatMap((r) => r.edits),
        );
        expect(getField(out, 'inv.0.durability')).toBe(12);
    });
});

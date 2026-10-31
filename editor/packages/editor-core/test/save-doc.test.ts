import { describe, expect, it } from 'vitest';
import { assembleSave, getField, parseSave, sameBytes } from '@drakan/formats';
import { buildSave, saveEntries } from '../../formats/test/savefixture.ts';
import { CommandStack, SaveDocument, SetFieldCommand, TransformCommand, type Transform } from '../src/index.ts';

const open = (o = {}) => SaveDocument.open(buildSave(saveEntries(o)));

describe('SaveDocument', () => {
    it('turns positioned records into markers and finds the player', () => {
        const d = open();
        expect(d.level).toBe('Testland');
        expect(d.size).toBe(5);
        const p = d.get(d.playerObject!)!;
        expect(p.kind).toBe('player');
        expect(p.transform.pos).toEqual([1000, 2000, 3000]);
        expect(d.entryOfObject.get(p.id)).toBe(5);
        expect(d.skills?.map((s) => s.name)).toEqual(['Melee', 'Archery', 'Magic']);
    });

    it('builds an identical file when nothing is pending', () => {
        const blob = buildSave();
        const d = SaveDocument.open(blob);
        expect(d.pendingEdits()).toEqual([]);
        expect(sameBytes(d.build().bytes, blob)).toBe(true);
    });

    it('tracks field edits through the command stack with undo and redo', () => {
        const d = open();
        const s = new CommandStack(d);
        s.run(new SetFieldCommand('gold', 9999));
        s.run(new SetFieldCommand('hp', 50));
        expect(d.pendingEdits().map((e) => `${e.name}:${e.before}->${e.after}`)).toEqual([
            'gold:376->9999',
            'hp:84->50',
        ]);
        s.undo();
        expect(d.value('hp')).toBe(84);
        s.redo();
        expect(d.value('hp')).toBe(50);
        s.run(new SetFieldCommand('gold', 376));
        expect(d.pendingEdits().map((e) => e.name)).toEqual(['hp']); // equal to the stored value is not an edit
        const out = parseSave(d.build().bytes);
        expect(getField(out, 'hp')).toBe(50);
        expect(getField(out, 'gold')).toBe(376);
    });

    it('moving the player marker edits the saved position and nothing else', () => {
        const d = open();
        const s = new CommandStack(d);
        const id = d.playerObject!;
        const before = new Map<number, Transform>([[id, structuredClone(d.get(id)!.transform)]]);
        s.run(new TransformCommand(before, new Map([[id, { pos: [10, 20, 30], rot: [0, 0, 0] }]])));
        expect(d.pendingEdits().map((e) => e.name)).toEqual(['pos']);
        expect(d.value('pos')).toEqual([10, 20, 30]);
        const built = d.build();
        const out = parseSave(built.bytes);
        expect(getField(out, 'pos')).toEqual([10, 20, 30]);
        const orig = parseSave(buildSave());
        const changed = out.entries.filter((e, i) => !sameBytes(e.data, orig.entries[i]!.data));
        expect(changed.map((e) => e.index)).toEqual([5]);
        // untouched entries keep their original compressed bytes
        for (const e of built.save.entries) if (e.index !== 5) expect(e.raw).toBe(d.save.entries[e.index]!.raw);
        s.undo();
        expect(d.pendingEdits()).toEqual([]);
    });

    it('validates edits, including skill bounds against pending values', () => {
        const d = open();
        expect(d.checkEdit('gold', -5)).toMatch(/at least 0/);
        expect(d.checkEdit('gold', 5)).toBeNull();
        expect(d.checkEdit('skill.1', 9)).toMatch(/exceed max/);
        const s = new CommandStack(d);
        s.run(new SetFieldCommand('skill.1.max', 12));
        expect(d.checkEdit('skill.1', 9)).toBeNull();
        expect(d.checkEdit('skill.1.max', 0)).toMatch(/below the level/);
        expect(d.checkEdit('skill.9', 1)).toBe('field not present in this save');
    });

    it('needs an explicit unlock for unconfirmed fields', () => {
        const d = open({ rynnId: 53 });
        expect(d.probe('hp').status).toBe('unconfirmed');
        expect(d.checkEdit('hp', 10)).toBe('unlock this field first');
        d.unlocked.add('hp');
        expect(d.checkEdit('hp', 10)).toBeNull();
        new CommandStack(d).run(new SetFieldCommand('hp', 10));
        const p = d.pendingEdits()[0]!;
        expect(p.unconfirmed).toBe(true);
        expect(getField(parseSave(d.build().bytes), 'hp', true)).toBe(10);
        // the written file still parses and round-trips
        const bytes = d.build().bytes;
        expect(sameBytes(assembleSave(parseSave(bytes)).bytes, bytes)).toBe(true);
    });
});

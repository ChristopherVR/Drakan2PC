import { describe, expect, it, vi } from 'vitest';
import type { ItemTemplate } from '@drakan/editor-core';
import { addChosen, addedMessage, addEntries, noRoomMessage, type ClassInfo } from '../src/save/inventory.ts';
import { fileChanged } from '../src/save/dom.ts';
import { POWER_UP_RFL_CLASSES } from '../src/itemdata.ts';

const tpl = (resource: number, entry: number): ItemTemplate => ({ resource, entry, size: 100 });

function fakeCtx(room: number | null) {
    const calls: string[] = [];
    return {
        calls,
        ctx: {
            doc: { placeFor: () => room },
            run: vi.fn(),
            err: vi.fn(),
            status: (m: string, e?: boolean) => calls.push(`${e ? 'error' : 'ok'}: ${m}`),
        } as never,
    };
}

describe('Add item status line', () => {
    it('shows the no-room message, then replaces it when a later add succeeds', () => {
        const t = tpl(0x33752, 7);
        const full = fakeCtx(null);
        addChosen(full.ctx, t, null, 1, undefined);
        expect(full.calls).toEqual([`error: ${noRoomMessage('Health Elixir', 1, undefined)}`]);

        // same status line, later add with room
        const roomy = fakeCtx(0);
        addChosen(roomy.ctx, t, null, 1, undefined);
        expect(roomy.calls).toEqual(['ok: Added Health Elixir; pending until you save.']);
        expect(roomy.calls[0]).not.toContain('does not fit');
    });

    it('reports a failed add as an error and does not claim success', () => {
        const c = fakeCtx(0);
        (c.ctx as { run: () => void }).run = () => {
            throw new Error('boom');
        };
        addChosen(c.ctx, tpl(0x33752, 7), null, 1, undefined);
        expect(c.calls).toEqual([]);
        expect((c.ctx as { err: ReturnType<typeof vi.fn> }).err).toHaveBeenCalled();
    });

    it('says a power-up is used up at once', () => {
        expect(addedMessage('Full Mana PwrUp', true)).toContain('uses it up');
    });
});

describe('save date label', () => {
    it('labels the file time plainly and shows nothing without one', () => {
        expect(fileChanged(Date.UTC(2026, 10, 2, 12))).toMatch(/^File changed .+/);
        expect(fileChanged(0)).toBe('');
    });
});

describe('power-ups in Add item', () => {
    const info: Record<number, ClassInfo> = {
        0x31b33: { name: 'Full Mana PwrUp', thumb: null, powerUp: true },
        0x33752: { name: 'Health Potion (Red)', thumb: null },
    };
    const infoOf = (r: number | undefined): ClassInfo | null => (r === undefined ? null : (info[r] ?? null));

    it('separates power-ups from items when the disc names them', () => {
        const g = addEntries([tpl(0x31b33, 1), tpl(0x33752, 2)], infoOf, () => true);
        expect(g.items.map((e) => e.value)).toEqual(['2']);
        expect(g.powerUps.map((e) => e.text)).toEqual(['Full Mana PwrUp (0x31b33)']);
    });

    it('keeps everything in one list without catalog information', () => {
        const g = addEntries(
            [tpl(0x31b33, 1), tpl(0x33752, 2)],
            () => null,
            () => true,
        );
        expect(g.powerUps).toEqual([]);
        expect(g.items).toHaveLength(2);
    });

    it('marks entries with no room', () => {
        const g = addEntries([tpl(0x33752, 2)], infoOf, () => false);
        expect(g.items[0]!.text).toMatch(/ - no room$/);
    });

    it('only the confirmed mana class is treated as a power-up', () => {
        expect([...POWER_UP_RFL_CLASSES]).toEqual([171]);
    });
});

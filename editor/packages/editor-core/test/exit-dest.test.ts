// An exit destination edit leaves as a replaced XDB string entry (synthetic data only).
import { describe, expect, it } from 'vitest';
import { decodeXdbSlots, exitDestinationText, withDestination, type XdbString, type XdbStrings } from '@drakan/formats';
import { buildStoryPackFiles, stringChanges } from '../src/index.ts';

describe('exit destination export', () => {
    const B = String.fromCharCode(92);
    const orig: XdbStrings = new Map<number, XdbString>([
        [5, { id: 5, sub: 3, text: [['Levels', 'Level 2', 'Level 2.lvl'].join(B), '', '', '', '', '', '', ''] }],
        [6, { id: 6, sub: 3, text: ['portal', '', '', '', '', '', '', ''] }],
    ]);
    it('ships one override with the new level path', () => {
        const edited = new Map(orig);
        edited.set(5, withDestination(orig.get(5)!, exitDestinationText(orig.get(5)!.text[0]!, 'Level 3')));
        const ch = stringChanges('LEVEL1ON.XDB', orig, edited);
        expect(ch.length).toBe(1);
        expect(ch[0]).toMatchObject({ file: 'LEVEL1ON.XDB', id: 5, sub: 3, isNew: false });
        expect(decodeXdbSlots(ch[0]!.data)[0]).toBe(['Levels', 'Level 3', 'Level 3.lvl'].join(B));
        const files = buildStoryPackFiles({ id: 't', version: '1.0.0', target: { profile: 'p' } } as never, ch);
        expect(Object.keys(files).some((k) => k.startsWith('overrides/LEVEL1ON.XDB/0400_5_3'))).toBe(true);
    });
});

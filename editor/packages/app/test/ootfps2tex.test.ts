import { describe, expect, it } from 'vitest';
import { DEFAULT_PS2_TEXTURE_SUBSTITUTIONS, effectiveSubstitutions } from '@drakan/formats';
import { choiceToOptions, customPair, defaultChoice, readChoice, resultText } from '../src/editor/ootfps2tex.ts';

describe('Drakan 2 textures in the import dialog', () => {
    it('is on by default and converts every pair of the table', () => {
        const o = choiceToOptions(defaultChoice());
        expect(o.enabled).toBe(true);
        expect(effectiveSubstitutions(o)).toEqual([...DEFAULT_PS2_TEXTURE_SUBSTITUTIONS]);
    });

    it('turns the whole feature or single pairs off and adds pairs of your own', () => {
        const first = DEFAULT_PS2_TEXTURE_SUBSTITUTIONS[0]!;
        const own = customPair('Lvl/Own#3', 'levels/level9on/level9on.txd#12', 'Rock');
        expect(own).toEqual({
            ootf: 'Lvl/Own#3',
            ps2: 'LEVELS/LEVEL9ON/LEVEL9ON.TXD#12',
            label: 'Rock',
            why: 'Added by you',
        });
        const rows = effectiveSubstitutions(
            choiceToOptions({ enabled: true, off: [first.ootf], extra: [own as never] }),
        ).map((r) => r.ootf);
        expect(rows).not.toContain(first.ootf);
        expect(rows).toContain('Lvl/Own#3');
        expect(effectiveSubstitutions(choiceToOptions({ enabled: false, off: [], extra: [] }))).toEqual([]);
    });

    it('explains a badly typed pair in plain words', () => {
        expect(customPair('Lvl/Own', 'X.TXD#1')).toMatch(/Order of the Flame texture/);
        expect(customPair('Lvl/Own#1', 'X#1')).toMatch(/must end in \.TXD/);
        expect(customPair('Lvl/Own#1', 'X.TXD')).toMatch(/Drakan 2 texture/);
        expect((customPair('Lvl/Own#7', 'X.TXD#1') as { label: string }).label).toBe('Texture 7');
    });

    it('reads a stored choice and ignores anything malformed', () => {
        expect(readChoice(null)).toEqual(defaultChoice());
        expect(readChoice('x')).toEqual(defaultChoice());
        const c = readChoice({
            enabled: false,
            off: ['A#1', 5],
            extra: [{ ootf: 'B#2', ps2: 'C.TXD#3', label: 'Bark' }, { ootf: 'bad' }, null],
        });
        expect(c.enabled).toBe(false);
        expect(c.off).toEqual(['A#1']);
        expect(c.extra.map((r) => r.ootf)).toEqual(['B#2']);
    });

    it('says what was used after the conversion', () => {
        expect(resultText(undefined, false)).toMatch(/off/);
        expect(resultText({ used: [] }, true)).toMatch(/none of the selected levels/);
        expect(resultText({ used: [{ label: 'Pine trunk bark' }] }, true)).toBe(
            'Drakan 2 textures used for 1 texture: Pine trunk bark.',
        );
    });
});

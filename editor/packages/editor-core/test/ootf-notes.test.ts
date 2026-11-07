import { describe, expect, it } from 'vitest';
import { packLevelNotes } from '../src/ootf.ts';

describe('converted level notes in the pack warnings', () => {
    it('lists every class kept out on purpose and every gap, then three other notes', () => {
        const notes = [
            'duplicate object id 112 at index 215',
            'PC class defaults kept on 7 object field(s): Trigger.Detect Method 1 (PS2 0) x3',
            'object 927 is not an exit class on PS2; replaced it with the exit class',
            'object 2136 is not an exit class on PS2; replaced it with the exit class',
            'kept out on purpose: "Fog Color Changer" (7 objects): the converted level keeps one sky and fog',
            'kept out on purpose: "Black Box" (2 objects): the install has no model for it',
            'kept out on purpose: "Splash 03" (1 object): the install has no model for it',
            'kept out on purpose: "Sky Trigger" (10 objects): the converted level keeps one sky and fog',
            'no PS2 class for "Mystery": 1 object dropped',
        ];
        expect(packLevelNotes(notes)).toEqual([
            notes[4],
            notes[5],
            notes[6],
            notes[7],
            notes[8],
            notes[0],
            notes[1],
            notes[2],
        ]);
    });

    it('keeps a level without such notes to its first three', () => {
        expect(packLevelNotes(['a', 'b', 'c', 'd'])).toEqual(['a', 'b', 'c']);
        expect(packLevelNotes([])).toEqual([]);
    });
});

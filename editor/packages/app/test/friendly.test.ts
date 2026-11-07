import { describe, expect, it } from 'vitest';
import { friendlyMessage } from '../src/shared/friendly.ts';

describe('friendlyMessage', () => {
    it('explains a file that is not a disc image', () => {
        expect(friendlyMessage('primary volume descriptor not found')).toMatch(/not a disc image/);
    });
    it('explains a file that is not valid JSON', () => {
        expect(friendlyMessage(`Unexpected token 'o', "{ [ not json" is not valid JSON`)).toMatch(/not valid JSON/);
        expect(friendlyMessage('Unexpected end of JSON input')).toMatch(/saved level or project/);
    });
    it('leaves other messages alone', () => {
        expect(friendlyMessage('Nothing to export yet.')).toBe('Nothing to export yet.');
    });
});

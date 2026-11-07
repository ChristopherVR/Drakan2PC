import { describe, expect, it } from 'vitest';
import { playInGame, setDesktopOnlyNotice } from '../src/modio.ts';
import { defaultPlayOptions } from '../src/launch.ts';

describe('Play in a browser', () => {
    it('shows the desktop-only notice instead of throwing', async () => {
        let shown = 0;
        setDesktopOnlyNotice(() => shown++);
        const msg = await playInGame(null, { ...defaultPlayOptions }, 'Level 1A - Surdana');
        expect(msg).toBeNull();
        expect(shown).toBe(1);
    });
});

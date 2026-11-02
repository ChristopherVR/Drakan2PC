import { describe, expect, it } from 'vitest';
import { checkPlayOptions, defaultPlayOptions, launchEnv, parsePlayOptions, type PlayOptions } from '../src/launch.ts';

const opts = (o: Partial<PlayOptions> = {}): PlayOptions => ({ ...defaultPlayOptions, ...o });

describe('launchEnv', () => {
    it('single player: level and auto start only', () => {
        const env = launchEnv(opts(), 'Level 4A - Ravenshold');
        expect(env.DRAKAN_START_LEVEL).toBe('Level 4A - Ravenshold');
        expect(env.DRAKAN_AUTOPRESS).toBe('3000');
        expect(env.DRAKAN_INPUT_AUTOSTART).toBe('1');
        expect(Object.keys(env).some((k) => k.startsWith('DRAKAN_NET'))).toBe(false);
    });

    it('auto start runs on a throw-away card as Drakan 2, so the title never offers Continue or the game card', () => {
        const env = launchEnv(opts(), 'Level 4A - Ravenshold');
        expect(env.DRAKAN_PLAYTEST_CARD).toBe('1');
        expect(env.DRAKAN_FAST_START).toBe('1');
        expect(env.DRAKAN_GAME).toBe('d2');
        expect(env.DRAKAN_PLAYTEST_GOD).toBeUndefined();
    });

    it('every level goes straight into the intro slot (no movie, no opening cut scene)', () => {
        // a disc level, a project's level copy and a converted Order of the Flame level alike
        for (const level of ['Level 4A - Ravenshold', 'Surdana Copy', 'OotF - Islands']) {
            const env = launchEnv(opts(), level);
            expect(env.DRAKAN_START_LEVEL).toBe(level);
            expect(env.DRAKAN_START_LEVEL_FROM).toBe('Level 0 - Intro');
            expect(env.DRAKAN_GAME).toBe('d2');
        }
        // the intro itself loads as itself
        expect(launchEnv(opts(), 'Level 0 - Intro').DRAKAN_START_LEVEL_FROM).toBeUndefined();
        expect(launchEnv(opts({ startLevel: false }), 'Surdana Copy').DRAKAN_START_LEVEL_FROM).toBeUndefined();
    });

    it('invincible keeps the player alive', () => {
        expect(launchEnv(opts({ invincible: true }), 'Level 1A - Surdana').DRAKAN_PLAYTEST_GOD).toBe('1');
        expect(parsePlayOptions(JSON.stringify({ invincible: true })).invincible).toBe(true);
    });

    it('can leave the game on its own defaults', () => {
        expect(launchEnv(opts({ startLevel: false, autoStart: false }), 'Level 1A - Surdana')).toEqual({});
    });

    it('hosting sets port, mode and ghosts', () => {
        const env = launchEnv(opts({ mode: 'host-coop', port: '4000', name: ' Rynn ' }), null);
        expect(env).toMatchObject({
            DRAKAN_NET_HOST: '4000',
            DRAKAN_NET_MODE: 'coop',
            DRAKAN_GHOST: '1',
            DRAKAN_NET_NAME: 'Rynn',
        });
        expect(env.DRAKAN_START_LEVEL).toBeUndefined();
    });

    it('joining appends the port only when the address has none', () => {
        expect(launchEnv(opts({ mode: 'join', address: '10.0.0.5', port: '27960' }), null).DRAKAN_NET_CONNECT).toBe(
            '10.0.0.5:27960',
        );
        expect(launchEnv(opts({ mode: 'join', address: 'host:99' }), null).DRAKAN_NET_CONNECT).toBe('host:99');
    });

    it('rejects bad input', () => {
        expect(checkPlayOptions(opts({ mode: 'host-dm', port: '0' }))).toMatch(/Port/);
        expect(checkPlayOptions(opts({ mode: 'join', address: 'a b' }))).toMatch(/Address/);
        expect(checkPlayOptions(opts({ name: 'x'.repeat(25) }))).toMatch(/name/);
        expect(() => launchEnv(opts({ mode: 'host-dm', port: 'x' }), null)).toThrow();
        expect(checkPlayOptions(opts())).toBeNull();
    });
});

describe('parsePlayOptions', () => {
    it('falls back to defaults on junk', () => {
        expect(parsePlayOptions(null)).toEqual(defaultPlayOptions);
        expect(parsePlayOptions('{nope')).toEqual(defaultPlayOptions);
        expect(parsePlayOptions('{"mode":"bogus","port":5}')).toEqual(defaultPlayOptions);
    });
    it('restores valid fields', () => {
        const o = parsePlayOptions(JSON.stringify({ mode: 'join', port: '1', autoStart: false }));
        expect(o.mode).toBe('join');
        expect(o.port).toBe('1');
        expect(o.autoStart).toBe(false);
    });
});

// What "Play" starts: the environment the game is launched with. Pure, so it is unit tested; the desktop shell only
// passes through the variables its allow-list knows (`src-tauri/src/paths.rs`, `allowed_env`).
// Variables are documented in docs/reference/env-flags.md and docs/reference/netcode.md.

export type PlayMode = 'single' | 'host-dm' | 'host-tdm' | 'host-coop' | 'join';

export interface PlayOptions {
    mode: PlayMode;
    /** UDP port to host on, or the port of `address` when it has none. */
    port: string;
    /** `host` or `host:port` to join. */
    address: string;
    /** Player name for multiplayer (blank = the game's default). */
    name: string;
    /** New Game loads the level being edited instead of Level 1A. */
    startLevel: boolean;
    /** Tap through the title, memory card prompts and intro (on a throw-away memory card). */
    autoStart: boolean;
    /** Keep the player at full health while testing (DRAKAN_PLAYTEST_GOD). */
    invincible: boolean;
}

export const defaultPlayOptions: PlayOptions = {
    mode: 'single',
    port: '27960',
    address: '127.0.0.1',
    name: '',
    startLevel: true,
    autoStart: true,
    invincible: false,
};

export const PLAY_MODES: { id: PlayMode; label: string }[] = [
    { id: 'single', label: 'Single player' },
    { id: 'host-dm', label: 'Host deathmatch (test)' },
    { id: 'host-tdm', label: 'Host team deathmatch (test)' },
    { id: 'host-coop', label: 'Host co-op (test)' },
    { id: 'join', label: 'Join a host (test)' },
];

/** First problem with the options, or null. */
export function checkPlayOptions(o: PlayOptions): string | null {
    const port = Number(o.port);
    const needsPort = o.mode.startsWith('host') || (o.mode === 'join' && !/:\d+$/.test(o.address.trim()));
    if (needsPort && !(Number.isInteger(port) && port >= 1 && port <= 65535)) return 'Port must be 1-65535.';
    if (o.mode === 'join' && !/^[^\s:]+(:\d+)?$/.test(o.address.trim()))
        return 'Address must look like host or host:port.';
    if (/[\r\n\0]/.test(o.name) || o.name.length > 24) return 'Player name is limited to 24 characters.';
    return null;
}

/** The level New Game opens first (the movie and the Drakan 2 opening cut scene). */
export const INTRO_LEVEL = 'Level 0 - Intro';

/**
 * Environment for the game. `level` is the long level name as the disc lists it (`Level 4A - Ravenshold`), or the
 * title of a project's level copy or converted Order of the Flame level, used for New Game when `startLevel` is on.
 * Throws on invalid options.
 *
 * Every level loads in place of the intro level itself (DRAKAN_START_LEVEL_FROM), so New Game goes straight into it with
 * no movie and no opening cut scene: the runtime rewrites the intro's level-load path to the level's own path, so the
 * engine knows the level by its real name and a mod pack's new level (copy, OotF) is opened like a disc one
 * (docs/journal/2026-11-02/editor-play-direct.md, level-copy-direct-play.md).
 *
 * With `autoStart` the game runs on a throw-away memory card (DRAKAN_PLAYTEST_CARD, made fresh by the desktop shell): on
 * the player's own card the title's first row is Continue, so the Cross taps loaded their latest save instead of the
 * level (and a play-test position from another level then dropped Rynn out of the world). The player's saves are never
 * read or written by a play test.
 */
export function launchEnv(o: PlayOptions, level: string | null): Record<string, string> {
    const bad = checkPlayOptions(o);
    if (bad) throw new Error(bad);
    const env: Record<string, string> = {};
    if (o.startLevel && level) {
        env.DRAKAN_START_LEVEL = level;
        if (level !== INTRO_LEVEL) env.DRAKAN_START_LEVEL_FROM = INTRO_LEVEL;
        // Which game the title runs: with a campaign pack installed the title first asks (the game card), and a tap
        // there picked whichever card had focus. Converted Order of the Flame levels load the same way under Drakan 2
        // (proven with an `ootf` campaign pack), so a single-level test always runs as Drakan 2 (d1 would add its own
        // campaign redirect and save folder).
        env.DRAKAN_GAME = 'd2';
    }
    if (o.autoStart) {
        env.DRAKAN_PLAYTEST_CARD = '1';
        // Boot logo fast-forwarded, New Game pressed as soon as the title is up, the play-test placement after ~1 s
        // (drakan_overrides / drakan_settings_host / drakan_playtest).
        env.DRAKAN_FAST_START = '1';
        env.DRAKAN_SKIP_MEMCARD_WARNING = '1';
        env.DRAKAN_AUTOPRESS = '3000';
        env.DRAKAN_AUTOPRESS_UNTIL = '120000';
        env.DRAKAN_INPUT_AUTOSTART = '1';
    }
    if (o.invincible) env.DRAKAN_PLAYTEST_GOD = '1';
    if (o.mode !== 'single') {
        env.DRAKAN_GHOST = '1';
        if (o.name.trim()) env.DRAKAN_NET_NAME = o.name.trim();
    }
    if (o.mode.startsWith('host-')) {
        env.DRAKAN_NET_HOST = String(Number(o.port));
        env.DRAKAN_NET_MODE = o.mode.slice(5);
    } else if (o.mode === 'join') {
        const a = o.address.trim();
        env.DRAKAN_NET_CONNECT = /:\d+$/.test(a) ? a : `${a}:${Number(o.port)}`;
    }
    return env;
}

/** Restore saved options, ignoring anything malformed. */
export function parsePlayOptions(text: string | null): PlayOptions {
    const out = { ...defaultPlayOptions };
    if (!text) return out;
    try {
        const v = JSON.parse(text) as Record<string, unknown>;
        if (PLAY_MODES.some((m) => m.id === v.mode)) out.mode = v.mode as PlayMode;
        for (const k of ['port', 'address', 'name'] as const) if (typeof v[k] === 'string') out[k] = v[k];
        for (const k of ['startLevel', 'autoStart', 'invincible'] as const)
            if (typeof v[k] === 'boolean') out[k] = v[k];
    } catch {
        // keep the defaults
    }
    return out;
}

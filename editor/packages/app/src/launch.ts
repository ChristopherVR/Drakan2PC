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
    /** Tap through the title, memory card prompts and intro. */
    autoStart: boolean;
}

export const defaultPlayOptions: PlayOptions = {
    mode: 'single',
    port: '27960',
    address: '127.0.0.1',
    name: '',
    startLevel: true,
    autoStart: true,
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

/**
 * Environment for the game. `level` is the long level name as the disc lists it (`Level 4A - Ravenshold`), used for
 * New Game when `startLevel` is on. Throws on invalid options.
 */
export function launchEnv(o: PlayOptions, level: string | null): Record<string, string> {
    const bad = checkPlayOptions(o);
    if (bad) throw new Error(bad);
    const env: Record<string, string> = {};
    if (o.startLevel && level) env.DRAKAN_START_LEVEL = level;
    if (o.autoStart) {
        env.DRAKAN_SKIP_MEMCARD_WARNING = '1';
        env.DRAKAN_AUTOPRESS = '3000';
        env.DRAKAN_AUTOPRESS_UNTIL = '120000';
        env.DRAKAN_INPUT_AUTOSTART = '1';
    }
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
        for (const k of ['startLevel', 'autoStart'] as const) if (typeof v[k] === 'boolean') out[k] = v[k];
    } catch {
        // keep the defaults
    }
    return out;
}

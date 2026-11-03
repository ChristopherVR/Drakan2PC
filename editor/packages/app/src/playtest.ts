// "Play test": run the game on the level (or multiplayer map) that is open, from where the author is looking.
//
// The document edits work on a *copy* of the document that goes into a temporary mod pack (`editor.playtest`), never on
// the author's own document or mod pack:
//   - Play from here   starts the level (New Game via DRAKAN_START_LEVEL; a disc level replaces the intro level, so
//                      there is no movie or opening cut scene; on a throw-away memory card, so no save is loaded) and
//                      asks the game to put the player at the clicked ground point facing the camera direction
//                      (DRAKAN_PLAYTEST_POS="x y z yaw", read by ps2xRuntime/src/lib/drakan_playtest.cpp). Moving the
//                      Rynn object in the level does not work: the game ignores it at New Game.
//   - Play cut scene   adds a big trigger volume (message Play Sequence) at the player start, linked to the chosen cut scene
//                      player (Stomp Player), and puts the player there.
//   - Play test arena  host a multiplayer map from its exported pack (the arena panel provides the pack through
//                      `setArenaPlayProvider`); optionally a second instance joins it.
// The temporary pack is switched off in mods.ini again when the game exits, so a later plain Play is not affected.
// The pure parts (document edits, yaw) are unit tested in test/playtest.test.ts; the rest needs the desktop shell.

import {
    ApplyTemplateCommand,
    LevelDocument,
    TEMPLATES,
    diffDocument,
    hasTerrainEdits,
    logicOf,
    updateModsIni,
    type AssetClass,
    type LevelObject,
    type LogicEnv,
    type ModMeta,
} from '@drakan/editor-core';
import { launchEnv, parsePlayOptions, type PlayOptions } from './launch.ts';
import { buildLevelPack, changeCount, playInGame, writePackToModsFolder, type LevelPack } from './modio.ts';
import * as platform from './platform.ts';
import type { LevelLoadResult } from './rpc.ts';

export const PLAYTEST_PACK_ID = 'editor.playtest';
export const DESKTOP_ONLY = 'Available in the desktop app';

/**
 * Yaw (radians, rotation about y) for a horizontal view direction. Measured in game (2026-11-01, Surdana farmer): yaw 0 faces
 * -z, yaw -pi/2 faces +x, yaw +pi/2 faces -x (an NPC at +x of the player showed on the right at yaw 0 and straight ahead at -pi/2).
 */
export function yawFromDirection(dx: number, dz: number): number {
    return Math.atan2(-dx, -dz);
}

/** Independent copy of a document (objects are cloned by `fromJSON`). */
export function copyDocument(doc: LevelDocument): LevelDocument {
    const c = LevelDocument.fromJSON(doc.toJSON());
    if (doc.bounds) c.bounds = { ...doc.bounds };
    return c;
}

/** How far from a pick a level object must be for the spot to count as known floor (about three terrain cells). */
export const FLOOR_REACH = 6000;

/**
 * Is `pos` (a ground pick) a place the player can be put? The game undoes a teleport onto ground it has no floor for (the
 * player ends at the start again), so a pick with no terrain under it, or far from every object of the level (creatures,
 * waypoints, items stand on floors), is refused with a sentence instead. `heightAt` is the editor's terrain height
 * (null = none).
 */
export function standableProblem(
    doc: LevelDocument,
    heightAt: (x: number, z: number) => number | null,
    pos: readonly number[],
): string | null {
    const [x = 0, , z = 0] = pos;
    const h = heightAt(x, z);
    if (h === null) return 'There is no terrain under that point. Pick a spot on the ground.';
    let near = false;
    for (const o of doc.objects()) {
        if (o.kind === 'camera') continue;
        const dx = o.transform.pos[0] - x;
        const dz = o.transform.pos[2] - z;
        if (dx * dx + dz * dz <= FLOOR_REACH * FLOOR_REACH && Math.abs(o.transform.pos[1] - h) <= FLOOR_REACH) {
            near = true;
            break;
        }
    }
    return near
        ? null
        : 'That spot is far from everything in the level, so the game may have no floor there. Pick a spot near objects or paths.';
}

/** Is this object a cut scene player (class 0x33, "Stomp Player")? */
export function isCutScene(o: LevelObject, env: LogicEnv): boolean {
    return logicOf(o, env)?.role === 'cutscene';
}

/** The object the game starts the player at in the level data: the Rynn player object. */
export function findPlayerStart(doc: LevelDocument): LevelObject | undefined {
    let any: LevelObject | undefined;
    for (const o of doc.objects()) {
        if (o.kind !== 'player' || !/^rynn$/i.test(o.class)) continue;
        any ??= o;
        if (o.origin !== undefined) return o;
    }
    return any;
}

/**
 * A copy of `doc` where cut scene player `id` plays when the level starts: a big trigger volume (message "Play Sequence",
 * Detect Which "Both", once) around the player start, linked to the cut scene player: the "cutscene-trigger" logic
 * template with a 10 x 8 x 10 cell volume, the setup proven in game on Surdana's #7809 (level-format.md section 14). A Timer
 * (Start Mode Run Instantly) was tried first and never came to life in the game. Returns the copy, or an error sentence.
 */
export function withCutScene(
    doc: LevelDocument,
    env: LogicEnv,
    catalog: readonly AssetClass[],
    id: number,
): { doc: LevelDocument } | { error: string } {
    const copy = copyDocument(doc);
    const o = copy.get(id);
    if (!o || !isCutScene(o, env)) return { error: `Object #${id} is not a cut scene player.` };
    const start = findPlayerStart(copy);
    if (!start) return { error: 'This level has no player start (Rynn) to put the trigger at.' };
    const tpl = TEMPLATES.find((t) => t.id === 'cutscene-trigger');
    if (!tpl) return { error: 'The cut scene trigger template is missing.' };
    const built = tpl.build(
        { doc: copy, env, catalog, at: [...start.transform.pos] as [number, number, number], selection: [] },
        { cutscene: id, once: true, w: 10, h: 8, d: 10 },
    );
    if ('error' in built) return { error: built.error };
    new ApplyTemplateCommand(built, 'Play cut scene').apply(copy);
    return { doc: copy };
}

// ---- the desktop flow -----------------------------------------------------------------------------------------------

/** What the arena editor offers for "Play test arena": its pack and the map to host. */
export interface ArenaPlay {
    meta: ModMeta;
    files: Record<string, Uint8Array>;
    /** Display name or id of the map (DRAKAN_MP_MAP). */
    map: string;
    mode: 'dm' | 'tdm';
    /** A custom game mode of the pack to host (DRAKAN_NET_GAME_MODE), and its name for the status line. */
    gameMode?: string;
    gameModeName?: string;
}
let arenaProvider: (() => ArenaPlay | { problem: string } | null) | null = null;
/**
 * The multiplayer map editor registers (or clears) how to get a pack for the open map: the pack, `{ problem }` when the
 * map has errors the author must fix first, or null when no map is open.
 */
export function setArenaPlayProvider(p: (() => ArenaPlay | { problem: string } | null) | null): void {
    arenaProvider = p;
}
/** Why the arena play test is not possible right now, or null when it is. */
export function arenaPlayProblem(): string | null {
    const a = arenaProvider?.() ?? null;
    if (!a) return 'Open a multiplayer map first';
    return 'problem' in a ? a.problem : null;
}
export const arenaPlayAvailable = (): boolean => arenaPlayProblem() === null;

const OPTIONS_KEY = 'drakan-editor:play-options';
function savedOptions(): PlayOptions {
    try {
        return parsePlayOptions(localStorage.getItem(OPTIONS_KEY));
    } catch {
        return parsePlayOptions(null);
    }
}

export interface PlayTestHost {
    /** The open document and where it came from; null while no disc level is open. */
    doc(): LevelDocument | null;
    src(): LevelLoadResult | null;
    env(): LogicEnv | null;
    catalog(): readonly AssetClass[];
    target(): ModMeta['target'] | null;
    toast(msg: string, kind?: 'info' | 'error' | 'ok' | 'warn'): void;
    status(msg: string, error?: boolean): void;
}

let watching = 0;

/** Switch the temporary pack off in mods.ini once the game has exited (polls; one watcher at a time). */
async function disableWhenDone(): Promise<void> {
    const token = ++watching;
    for (let i = 0; i < 6 * 60 * 6; i++) {
        await new Promise((r) => setTimeout(r, 5000));
        if (token !== watching) return;
        try {
            if (await platform.gameRunning()) continue;
            const ini = await platform.readModsIni();
            if (/editor\.playtest\s*=\s*on/i.test(ini))
                await platform.writeModPack(PLAYTEST_PACK_ID, {}, updateModsIni(ini, PLAYTEST_PACK_ID, false));
            return;
        } catch {
            return;
        }
    }
}

/**
 * A level copy of the open project has no disc LVL of its own: the play test needs a pack that adds the whole level. The
 * project installer registers how (`buildProjectPack` for that one level); null = `title` is not a project copy.
 */
type CopyPlayProvider = (
    title: string,
    doc: LevelDocument,
    meta: ModMeta,
) => Promise<Pick<LevelPack, 'meta' | 'files'> | null>;
let copyProvider: CopyPlayProvider | null = null;
export function setCopyPlayProvider(p: CopyPlayProvider | null): void {
    copyProvider = p;
}

/** The pack that adds the open level when it is a project's level copy (the disc has no LVL under its title); else null. */
export async function copyPlayPack(
    title: string,
    doc: LevelDocument,
    meta: ModMeta,
): Promise<Pick<LevelPack, 'meta' | 'files'> | null> {
    return copyProvider && title ? copyProvider(title, doc, meta) : null;
}

export async function playTestDoc(
    host: PlayTestHost,
    doc: LevelDocument,
    what: string,
    extraEnv: Record<string, string> = {},
): Promise<void> {
    const src = host.src();
    const target = host.target();
    if (!platform.isDesktop) throw new Error(DESKTOP_ONLY + '.');
    if (!src || !target) throw new Error('Open a level from the disc first.');
    const meta: ModMeta = {
        id: PLAYTEST_PACK_ID,
        name: 'Editor play test',
        version: '1.0.0',
        description: 'Temporary pack written by the Drakan editor for a play test; safe to delete.',
        target,
    };
    // The pack carries the author's edits (and the cut scene switch); without any edit the game starts unmodified.
    const copy = copyProvider && doc.level ? await copyProvider(doc.level, doc, meta) : null;
    const edited = changeCount(diffDocument(src.lvl.list, doc)) > 0 || hasTerrainEdits(doc);
    const pack = copy ?? (edited ? buildLevelPack(src, doc, meta) : null);
    const opts: PlayOptions = { ...savedOptions(), mode: 'single', startLevel: true };
    // Every level (disc level or copy) loads straight into the intro's slot: no movie, no opening cut scene.
    const msg = await playInGame(pack, opts, doc.level || null, extraEnv);
    if (msg === null) return;
    host.status(`${what}: ${msg}`);
    host.toast(`${what}: game started`, 'ok');
    void disableWhenDone();
}

export async function playFromHere(
    host: PlayTestHost,
    pos: readonly number[],
    yaw: number,
    what = 'Play from here',
): Promise<void> {
    const doc = host.doc();
    if (!doc) throw new Error('Open a level first.');
    const f = (n: number | undefined): string => String(Math.round((n ?? 0) * 1000) / 1000);
    await playTestDoc(host, doc, what, { DRAKAN_PLAYTEST_POS: `${f(pos[0])} ${f(pos[1])} ${f(pos[2])} ${f(yaw)}` });
}

export async function playCutScene(host: PlayTestHost, id: number): Promise<void> {
    const doc = host.doc();
    const env = host.env();
    if (!doc || !env) throw new Error('Open a level first.');
    const r = withCutScene(doc, env, host.catalog(), id);
    if ('error' in r) throw new Error(r.error);
    // Level objects only come to life near the player, and New Game starts her far from the Rynn object in Surdana (the
    // swamp), so she is put at the player start where the timer is created; there it runs at once.
    const at = findPlayerStart(r.doc)?.transform.pos;
    await playTestDoc(
        host,
        r.doc,
        'Play cut scene',
        at ? { DRAKAN_PLAYTEST_POS: `${Math.round(at[0])} ${Math.round(at[1] + 256)} ${Math.round(at[2])}` } : {},
    );
}

/** Host the open multiplayer map; with `joiner` a second instance joins it (needs the second-instance shell command). */
export async function playArena(host: PlayTestHost, joiner: boolean): Promise<void> {
    if (!platform.isDesktop) throw new Error(DESKTOP_ONLY + '.');
    const a = arenaProvider?.() ?? null;
    if (!a) throw new Error('Open a multiplayer map in the Multiplayer Map panel first.');
    if ('problem' in a) throw new Error(a.problem);
    const opts: PlayOptions = {
        ...savedOptions(),
        mode: a.mode === 'tdm' ? 'host-tdm' : 'host-dm',
        startLevel: false,
    };
    const env = launchEnv(opts, null);
    env.DRAKAN_MP_MAP = a.map;
    // With a campaign pack installed the title first shows "Choose your game" and waits there (the host runs behind
    // it); a multiplayer test is always Drakan 2.
    env.DRAKAN_GAME = 'd2';
    // The map's offered custom mode (its rules run on the server; the HUD shows its goal).
    if (a.gameMode) env.DRAKAN_NET_GAME_MODE = a.gameMode;
    if (await platform.gameRunning()) throw new Error('The game is already running. Close it first.');
    const where = await writePackToModsFolder({ meta: a.meta, files: a.files });
    if (where === null) return;
    await platform.launchGame(env);
    if (joiner) {
        const join: Record<string, string> = {
            DRAKAN_NET_CONNECT: `127.0.0.1:${Number(opts.port) || 27960}`,
            DRAKAN_GHOST: '1',
            DRAKAN_NET_NAME: 'Tester 2',
            DRAKAN_GAME: 'd2',
            DRAKAN_PLAYTEST_CARD: '1', // its own throw-away card: the title's Cross must be New Game, not Continue
            DRAKAN_SKIP_MEMCARD_WARNING: '1',
            DRAKAN_AUTOPRESS: '3000',
            DRAKAN_AUTOPRESS_UNTIL: '120000',
            DRAKAN_INPUT_AUTOSTART: '1',
        };
        await new Promise((r) => setTimeout(r, 15000)); // the host needs to be up before a client connects
        await platform.launchGame(join, true);
    }
    host.status(
        `Play test arena: hosting '${a.map}'${a.gameMode ? ` with the mode "${a.gameModeName ?? a.gameMode}"` : ''}${joiner ? ' with a second instance joining' : ''}.`,
    );
    host.toast('Arena play test started', 'ok');
}

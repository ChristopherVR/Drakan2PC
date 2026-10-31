import { strToU8 } from 'fflate';
import { buildModFiles, type ModMeta } from './mod.ts';
import { previewFileName, serializeMapFile, validateArena, type ArenaDraft, type ValidateArenaEnv } from './arena.ts';
import type { ModeDef } from './gamemodes.ts';

/**
 * Mod pack for a multiplayer map: `mod.toml`, `maps/<id>.ini` (the arena and the custom modes it uses) and, when a
 * picture was captured, `previews/<map name>.png` (the file the game's multiplayer menu looks a picture up by).
 * No level entries: the arena filter runs in the game, so the base level on the disc is used as it is.
 */
export function buildArenaPackFiles(
    draft: ArenaDraft,
    modes: readonly ModeDef[],
    meta: ModMeta,
    preview: Uint8Array | null,
): Record<string, Uint8Array> {
    const files = buildModFiles(meta, []);
    files[`maps/${draft.id}.ini`] = strToU8(serializeMapFile(draft, modes));
    if (preview && preview.length) files[`previews/${previewFileName(draft.name)}`] = preview;
    return files;
}

/** Errors that must stop an export (the runtime would drop the map or the pack would be unusable). */
export function exportBlockers(draft: ArenaDraft, env: ValidateArenaEnv): string[] {
    return validateArena(draft, env)
        .filter((i) => i.severity === 'error')
        .map((i) => i.message);
}

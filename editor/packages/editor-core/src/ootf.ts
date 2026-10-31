import { strToU8 } from 'fflate';
import {
    buildPs2Lvl,
    mangleComponent,
    ps2BlobBuilder,
    ps2BlobFor,
    ps2LevelTag,
    LVL_TYPE_LAYERS,
    pcLevelName,
    terrainHeightAt,
    type BlobBuilder,
    type PcLevel,
    type Ps2LevelParts,
    type SrscReader,
} from '@drakan/formats';
import { newDraft, serializeMapFile, slugify, type ArenaDraft, type Vec3 } from './arena.ts';
import { buildManifestToml, ModError, MOD_MAX_ENTRY, zipModFiles, type ModMeta } from './mod.ts';

/**
 * Pack a converted Order of the Flame level as a mod (docs/reference/modding.md "custom levels"): the LVL ships under
 * `files/ENGLISH/LEVELS/<mangled>/<mangled>.LVL` and `levels = [title]` registers it in the level list. The level keeps
 * the template's dependency table, so it loads the template's ODB / models / textures that already exist on the disc.
 */
export interface OotfLevelInput {
    /** Title shown in the level list; the folder and file name are its 8.3 mangling. */
    title: string;
    parts: Ps2LevelParts;
    /** Splat files of this level by extension; override the pack-wide `splats` per key. */
    splats?: Readonly<Record<string, Uint8Array>>;
    /** A multiplayer map on this level (`ootfArenaFromPc`): shipped as `maps/<mangled>.ini`, tagged Drakan 1. */
    arena?: ArenaDraft;
}

export interface OotfPack {
    meta: ModMeta;
    files: Record<string, Uint8Array>;
    /** `[title, mangled]` per level. */
    levels: { title: string; mangled: string; bytes: number }[];
    warnings: string[];
}

export interface OotfPackOptions {
    /**
     * The template level's splat files (`LEVEL1TS.DB` / `.TXD` for LEVEL1NA, keyed by extension). The game opens
     * `<name>_splats.*` for every level; a level without them crashes the guest (a jump to address 0 right after the
     * load), so they are shipped next to each LVL under the level's own `<first 6 chars>TS` name.
     */
    splats?: Readonly<Record<string, Uint8Array>>;
    /** Further pack files (shared asset container, new ODB / SSD / SDB entries, patched `.DB` files), shipped as they are. */
    extraFiles?: Readonly<Record<string, Uint8Array>>;
}

export async function buildOotfPack(
    meta: Omit<ModMeta, 'levels'>,
    template: SrscReader,
    inputs: readonly OotfLevelInput[],
    blob: BlobBuilder,
    options: OotfPackOptions = {},
): Promise<OotfPack> {
    if (!inputs.length) throw new ModError('no levels to pack');
    const warnings: string[] = [];
    if (blob === ps2BlobFor) {
        // The engine only draws a regenerated blob whose level tag is the template's own.
        const entry = template.entries.find((e) => e.type === LVL_TYPE_LAYERS);
        const tag = entry ? ps2LevelTag(await template.readRaw(entry)) : undefined;
        if (tag !== undefined) blob = ps2BlobBuilder(tag);
        else warnings.push('template level tag not found; terrain may not draw');
    }
    const files: Record<string, Uint8Array> = {};
    const levels: OotfPack['levels'] = [];
    const seen = new Map<string, string>();
    for (const input of inputs) {
        const title = input.title.trim();
        if (!title || /[\r\n]/.test(title)) throw new ModError('level title must be one non-empty line');
        const mangled = mangleComponent(title);
        const clash = seen.get(mangled);
        if (clash) throw new ModError(`"${title}" and "${clash}" both mangle to ${mangled}; rename one`);
        seen.set(mangled, title);
        const bytes = await buildPs2Lvl(template, input.parts, blob);
        files[`files/ENGLISH/LEVELS/${mangled}/${mangled}.LVL`] = bytes;
        levels.push({ title, mangled, bytes: bytes.length });
        if (input.arena) {
            const arena = { ...input.arena, level: title, game: input.arena.game || 'Drakan 1' };
            if (!arena.id) arena.id = slugify(arena.name || title) || mangled.toLowerCase();
            files[`maps/${mangled}.ini`] = strToU8(serializeMapFile(arena, []));
        }
        const stem = mangled.slice(0, 6) + 'TS';
        for (const [ext, data] of Object.entries({ ...options.splats, ...input.splats })) {
            files[`files/ENGLISH/LEVELS/${mangled}/${stem}.${ext.toUpperCase()}`] = data;
        }
        if (!options.splats) warnings.push(`${title}: no splat files given; the game crashes after loading without them`);
        for (const e of input.parts.report.warnings.slice(0, 3)) warnings.push(`${title}: ${e}`);
    }
    for (const [path, data] of Object.entries(options.extraFiles ?? {})) {
        if (path in files) warnings.push(`${path} is produced twice; the extra file wins`);
        files[path] = data;
    }
    // Entries past the whole-entry limit only warn for new files (modding.md): level data is streamed in chunks.
    for (const l of levels) {
        if (l.bytes > MOD_MAX_ENTRY * 64) warnings.push(`${l.title}: ${l.bytes} bytes is unusually large`);
    }
    const full: ModMeta = { ...meta, levels: levels.map((l) => l.title) };
    files['mod.toml'] = strToU8(buildManifestToml(full));
    return { meta: full, files, levels, warnings };
}

/**
 * The multiplayer map of a PC `Multiplayer/*` level: the Rynn launch pads (OotF's player starts) become the spawn points,
 * snapped over the terrain like the converted Rynn start (the engine lets her fall otherwise). Dragon launch pads are
 * the fallback for air maps. Null when the level is not a multiplayer one or has fewer than two starts. The menu lists
 * it next to the Drakan 2 maps with the `Drakan 1` tag (netcode.md "Multiplayer maps").
 */
export function ootfArenaFromPc(pc: PcLevel, title: string): ArenaDraft | null {
    if (!/^multiplayer[\\/]/i.test(pc.path)) return null;
    const pads = (match: RegExp): Vec3[] =>
        pc.list.objects
            .filter((o) => match.test(pc.classes.get(o.dbIndex)?.get(o.classId)?.name ?? ''))
            .map((o): Vec3 => {
                const g = terrainHeightAt(pc.terrain, o.pos[0], o.pos[2]);
                return [o.pos[0], Number.isFinite(g) ? g + 400 : o.pos[1], o.pos[2]];
            });
    let starts = pads(/rynn launch pad/i);
    if (starts.length < 2) starts = pads(/launch pad/i);
    if (starts.length < 2) return null;
    const name = pcLevelName(pc.header) || title;
    const d = newDraft(title);
    d.id = slugify(name);
    d.name = name;
    d.game = 'Drakan 1';
    d.description = 'A multiplayer map of Drakan: Order of the Flame.';
    d.players = Math.min(12, starts.length);
    for (const pos of starts) d.spawns.push({ uid: d.nextUid++, pos, yaw: 0, team: 0 });
    return d;
}

export const zipOotfPack = (pack: OotfPack): Uint8Array => zipModFiles(pack.files);

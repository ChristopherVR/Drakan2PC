import { strFromU8, strToU8, zipSync } from 'fflate';
import {
    PACK_BUILD_PATTERN,
    SrscReader,
    levelNameProblem,
    packBuildId,
    writeSrsc,
    type SrscEntry,
} from '@drakan/formats';

/**
 * Mod pack export in the runtime's format (docs/reference/modding.md, runtime/hle ModPack.cpp):
 * `mod.toml` + `overrides/<FILE>/<type hex>_<id>_<sub>.bin`. `FILE` is the resource file name without directories,
 * `type` is hex, `id` and `sub` are decimal (`parseEntryName` in runtime/hle/src/ResourceOverride.cpp).
 */
export const MOD_FORMAT_VERSION = 1;
export const MOD_ID_PATTERN = /^[A-Za-z0-9._-]{1,64}$/;
/** RSRCIOP fails whole-entry requests longer than this (modding.md "Size limits"). */
export const MOD_MAX_ENTRY = 0x40800;

export interface ModTarget {
    /** `pal`, `usa`, a list, or `*`. At least one of profile / hash is required. */
    profile?: string | string[];
    /** Disc profile hash, 16 hex digits. */
    hash?: string;
}

export interface ModMeta {
    id: string;
    name?: string;
    version?: string;
    /**
     * Save compatibility id (`build`): the game stores it with every save and refuses a save made with another build
     * of the pack. Leave it out and `zipModFiles` writes the content hash (`packBuildId`); pin it only when an update
     * keeps every save valid (textures or text only).
     */
    build?: string;
    author?: string;
    description?: string;
    target: ModTarget;
    dependencies?: string[];
    /** Custom level titles (`LEVELLST.TXT` names); their `files/ENGLISH/LEVELS/...` LVL ships in the pack. */
    levels?: string[];
    /** For a level copy: `Copy title = Disc level` lines, so the game names the copy's exits like the source's. */
    levelCopies?: string[];
    /**
     * `campaign = "<id>"`: the pack's `levels` are a whole campaign in play order, offered on the game's title card
     * ("Choose your game") next to Drakan. `"ootf"` is the converted Order of the Flame; any other id is a custom
     * campaign (CAMPAIGN_ID_PATTERN, not one of RESERVED_CAMPAIGN_IDS). New Game opens `campaignStart` (default the
     * first level), `campaignName` is the card title, `campaignDescription` its one-line text, `campaignSaves` the
     * memory card folder suffix (`<card>_<saves>`, default the id). The cover is `previews/campaign-<id>.png`.
     */
    campaign?: string;
    campaignName?: string;
    campaignDescription?: string;
    campaignStart?: string;
    campaignSaves?: string;
    /**
     * PS2MUSIC.SDB sound id the title menu plays while the campaign is the current game (the pack adds that track as
     * `new/PS2MUSIC.SDB/` entries; the OotF converter writes the rendered "gui" theme, ootfdmusic.ts).
     */
    campaignTitleMusic?: number;
    /**
     * Levels whose cut scenes are framed for one picture shape: level title -> aspect (`4:3`, `16:9` or a number).
     * While such a level is loaded the game renders and shows its world at that aspect instead of the window's
     * (manifest `level_aspect`; the OotF converter marks the "Ruined Village Book", whose paintings fill a 4:3 screen).
     * `4:3 fill` makes the level's width fill a wider window instead (top and bottom are cut, at most to 16:9).
     */
    levelAspect?: Record<string, string>;
    /**
     * Exits that close once they are taken and are never held by the runtime's arrival guard: `Level title @ x, y, z`
     * (manifest `one_way_exits`; the OotF converter marks the Ruined Village church stair to the Book, a point exit that
     * only its scene and a re-offer box fire, formats ootfexitseq.ts).
     */
    oneWayExits?: string[];
}

/** A `one_way_exits` entry: `Level title @ x, y, z` (numbers, the exit object's position). */
export const ONE_WAY_EXIT_PATTERN =
    /^[^@]*\S[^@]*@\s*-?[\d.]+(?:e[+-]?\d+)?\s*,\s*-?[\d.]+(?:e[+-]?\d+)?\s*,\s*-?[\d.]+(?:e[+-]?\d+)?\s*$/i;

/** An aspect a manifest accepts: `W:H` or a number, between 1 and 4. */
export const LEVEL_ASPECT_PATTERN = /^(\d+(\.\d+)?:\d+(\.\d+)?|\d+(\.\d+)?)( fill)?$/;

/** A campaign id: 1-32 of a-z 0-9 _ -, starting with a letter or digit. */
export const CAMPAIGN_ID_PATTERN = /^[a-z0-9][a-z0-9_-]{0,31}$/;
/** Save folder suffix of a campaign (`<card>_<saves>`). */
export const CAMPAIGN_SAVES_PATTERN = /^[A-Za-z0-9_-]{1,32}$/;
/** Words the game's `[general] game` setting uses itself: not campaign ids. */
export const RESERVED_CAMPAIGN_IDS: readonly string[] = ['d1', 'd2', 'drakan', 'ask'];
/** Longest campaign description the title card shows in full. */
export const CAMPAIGN_DESCRIPTION_MAX = 200;
/** Pack path of a campaign's cover picture. */
export const campaignCoverPath = (id: string): string => `previews/campaign-${id}.png`;

/** Plain-language problem with a campaign id, null when it is fine. */
export function campaignIdProblem(id: string): string | null {
    if (!CAMPAIGN_ID_PATTERN.test(id))
        return 'The campaign id may only hold small letters, digits, _ and - (1 to 32, starting with a letter or digit).';
    if (RESERVED_CAMPAIGN_IDS.includes(id)) return `"${id}" is a word the game uses itself; pick another campaign id.`;
    return null;
}

export interface ChangedEntry {
    /** Container file name, e.g. `LEVEL1NA.LVL`. */
    file: string;
    entry: Pick<SrscEntry, 'type' | 'id' | 'sub'>;
    data: Uint8Array;
}

export class ModError extends Error {}

const hex4 = (n: number): string => n.toString(16).padStart(4, '0');

export const overridePath = (file: string, e: ChangedEntry['entry']): string =>
    `overrides/${file}/${hex4(e.type)}_${e.id}_${e.sub}.bin`;

const tomlStr = (s: string): string => JSON.stringify(s);
const oneLine = (s: string): string => s.replace(/\s*[\r\n]+\s*/g, ' ').trim();

/** Validate the manifest fields the runtime requires; throws ModError with the reason. */
export function validateModMeta(m: ModMeta): void {
    if (!MOD_ID_PATTERN.test(m.id)) throw new ModError('mod id must be 1-64 characters of letters, digits . _ -');
    const profiles = m.target.profile === undefined ? [] : [m.target.profile].flat();
    if (profiles.length === 0 && !m.target.hash) throw new ModError('target needs a profile or a hash');
    if (m.target.hash !== undefined && !/^[0-9a-fA-F]{1,16}$/.test(m.target.hash)) {
        throw new ModError('target hash must be up to 16 hex digits');
    }
    if (m.version !== undefined && !/^\d+(\.\d+)*$/.test(m.version))
        throw new ModError('version must be dotted numbers');
    if (m.build !== undefined && !PACK_BUILD_PATTERN.test(m.build))
        throw new ModError('build must be 1-64 characters of letters, digits . _ -');
    if (m.campaign !== undefined) {
        const why = campaignIdProblem(m.campaign);
        if (why) throw new ModError(why);
    }
    if (m.campaignSaves !== undefined && !CAMPAIGN_SAVES_PATTERN.test(m.campaignSaves))
        throw new ModError('campaignSaves must be 1-32 characters of letters, digits _ -');
    if (
        m.campaignTitleMusic !== undefined &&
        (!Number.isInteger(m.campaignTitleMusic) || m.campaignTitleMusic < 1 || m.campaignTitleMusic > 0xffff)
    )
        throw new ModError('campaignTitleMusic must be a sound id 1-65535');
    for (const level of m.levels ?? []) {
        const why = levelNameProblem(level);
        // The runtime skips such a level with only a log line, which leaves a pack that loads but has no level.
        if (why) throw new ModError(`level "${level}": ${why}`);
    }
    for (const [level, aspect] of Object.entries(m.levelAspect ?? {})) {
        const [w, h] = aspect.replace(/ fill$/, '').split(':').map(Number);
        const value = h === undefined ? w! : w! / h;
        if (!LEVEL_ASPECT_PATTERN.test(aspect) || !(value >= 1 && value <= 4))
            throw new ModError(`levelAspect of "${level}" must be like 4:3 (between 1 and 4)`);
        if (level.includes('=') || !level.trim()) throw new ModError(`levelAspect: "${level}" is not a level title`);
    }
    for (const e of m.oneWayExits ?? [])
        if (!ONE_WAY_EXIT_PATTERN.test(e)) throw new ModError(`oneWayExits: "${e}" is not "Level title @ x, y, z"`);
    if (m.campaignStart !== undefined && !m.levels?.includes(m.campaignStart))
        throw new ModError('campaignStart must be one of the pack levels');
}

/** `mod.toml` text. Top-level keys first (the reader wants `dependencies` above the tables), then `[target]`. */
export function buildManifestToml(m: ModMeta): string {
    validateModMeta(m);
    const l: string[] = [`format = ${MOD_FORMAT_VERSION}`, `id = ${tomlStr(m.id)}`];
    l.push(`name = ${tomlStr(m.name ?? m.id)}`);
    if (m.version) l.push(`version = ${tomlStr(m.version)}`);
    if (m.build) l.push(`build = ${tomlStr(m.build)}`);
    if (m.author) l.push(`author = ${tomlStr(m.author)}`);
    if (m.description) l.push(`description = ${tomlStr(m.description.replace(/\s*[\r\n]+\s*/g, ' '))}`);
    if (m.dependencies?.length) l.push(`dependencies = [${m.dependencies.map(tomlStr).join(', ')}]`);
    if (m.levels?.length) l.push(`levels = [${m.levels.map(tomlStr).join(', ')}]`);
    if (m.levelCopies?.length) l.push(`level_copies = [${m.levelCopies.map(tomlStr).join(', ')}]`);
    if (m.campaign) l.push(`campaign = ${tomlStr(m.campaign)}`);
    if (m.campaign && m.campaignName) l.push(`campaign_name = ${tomlStr(m.campaignName)}`);
    if (m.campaign && m.campaignDescription)
        l.push(`campaign_description = ${tomlStr(oneLine(m.campaignDescription).slice(0, CAMPAIGN_DESCRIPTION_MAX))}`);
    if (m.campaign && m.campaignStart) l.push(`campaign_start = ${tomlStr(m.campaignStart)}`);
    if (m.campaign && m.campaignSaves) l.push(`campaign_saves = ${tomlStr(m.campaignSaves)}`);
    if (m.campaign && m.campaignTitleMusic) l.push(`campaign_title_music = ${m.campaignTitleMusic}`);
    const aspects = Object.entries(m.levelAspect ?? {});
    if (aspects.length) l.push(`level_aspect = [${aspects.map(([level, a]) => tomlStr(`${level} = ${a}`)).join(', ')}]`);
    if (m.oneWayExits?.length) l.push(`one_way_exits = [${m.oneWayExits.map(tomlStr).join(', ')}]`);
    l.push('', '[target]');
    if (m.target.profile !== undefined) {
        const p = m.target.profile;
        l.push(`profile = ${Array.isArray(p) ? `[${p.map(tomlStr).join(', ')}]` : tomlStr(p)}`);
    }
    if (m.target.hash) l.push(`hash = ${tomlStr(m.target.hash.toLowerCase())}`);
    return l.join('\n') + '\n';
}

/** All files of a pack, keyed by path inside the pack. Rejects empty entries and duplicate keys. */
export function buildModFiles(meta: ModMeta, entries: ChangedEntry[]): Record<string, Uint8Array> {
    const files: Record<string, Uint8Array> = { 'mod.toml': strToU8(buildManifestToml(meta)) };
    for (const c of entries) {
        if (c.data.length === 0) throw new ModError(`${c.file}: empty override entries are rejected by the runtime`);
        if (!/^[^\/:*?"<>|]+$/.test(c.file)) throw new ModError(`bad file name ${c.file}`);
        const path = overridePath(c.file, c.entry);
        if (path in files) throw new ModError(`duplicate override ${path}`);
        files[path] = c.data;
    }
    return files;
}

/** A pack as a zip (manifest at the zip root). The pack name for mods.ini is the zip file name without `.zip`. */
export function buildModZip(meta: ModMeta, entries: ChangedEntry[]): Uint8Array {
    return zipModFiles(buildModFiles(meta, entries));
}

/**
 * The finished pack with its build id: a manifest (`mod.toml` or `mod.json`) that names no `build` gets the content
 * hash of the pack's files (`packBuildId`, docs/reference/modding.md "Saves and pack builds"). A pinned build is kept.
 * Returns `files` unchanged when there is no manifest.
 */
export function stampPackBuild(files: Record<string, Uint8Array>): Record<string, Uint8Array> {
    const toml = files['mod.toml'];
    const json = files['mod.json'];
    if (toml) {
        const text = strFromU8(toml);
        const lines = text.split(/\r?\n/);
        const firstTable = lines.findIndex((l) => /^\s*\[/.test(l));
        const top = firstTable < 0 ? lines : lines.slice(0, firstTable);
        if (top.some((l) => /^\s*build\s*=/.test(l))) return files;
        const at = top.findIndex((l) => /^\s*id\s*=/.test(l));
        const nl = text.includes('\r\n') ? '\r\n' : '\n';
        lines.splice(at < 0 ? 0 : at + 1, 0, `build = ${tomlStr(packBuildId(files))}`);
        return { ...files, 'mod.toml': strToU8(lines.join(nl)) };
    }
    if (json) {
        const doc = JSON.parse(strFromU8(json)) as Record<string, unknown>;
        if (typeof doc !== 'object' || doc === null || Array.isArray(doc) || 'build' in doc) return files;
        return { ...files, 'mod.json': strToU8(JSON.stringify({ ...doc, build: packBuildId(files) }, null, 4) + '\n') };
    }
    return files;
}

/** The pack as a zip, stamped with its build id (`stampPackBuild`). */
export const zipModFiles = (files: Record<string, Uint8Array>, level: 0 | 1 | 6 = 6): Uint8Array =>
    zipSync(stampPackBuild(files), { level });

/** Check a replacement against the runtime's growth rule: crossing the whole-entry limit is rejected at apply time. */
export function checkEntrySize(file: string, originalSize: number, newSize: number): string | null {
    if (newSize > MOD_MAX_ENTRY && originalSize <= MOD_MAX_ENTRY) {
        return `${file}: entry would grow from ${originalSize} to ${newSize} bytes, past the ${MOD_MAX_ENTRY} byte limit; the runtime keeps the original`;
    }
    return null;
}

/**
 * Add or update `<pack> = on|off` in the `[mods]` section of mods.ini (created if missing). Existing lines, comments
 * and order are kept; a new pack goes last so it loads after the others.
 */
export function updateModsIni(text: string, pack: string, enabled = true): string {
    const nl = text.includes('\r\n') ? '\r\n' : '\n';
    const lines = text === '' ? [] : text.split(/\r?\n/);
    if (lines.length && lines[lines.length - 1] === '') lines.pop();
    const value = enabled ? 'on' : 'off';
    let inMods = false;
    let lastInMods = -1;
    let sectionSeen = false;
    for (let i = 0; i < lines.length; i++) {
        const t = lines[i]!.trim();
        const sec = /^\[(.+)\]$/.exec(t);
        if (sec) {
            inMods = sec[1]!.trim().toLowerCase() === 'mods';
            sectionSeen ||= inMods;
            if (inMods) lastInMods = i;
            continue;
        }
        if (!inMods) continue;
        if (t !== '') lastInMods = i;
        const eq = t.indexOf('=');
        if (eq > 0 && !t.startsWith(';') && !t.startsWith('#') && t.slice(0, eq).trim() === pack) {
            lines[i] = `${pack} = ${value}`;
            return lines.join(nl) + nl;
        }
    }
    if (!sectionSeen) {
        if (lines.length && lines[lines.length - 1] !== '') lines.push('');
        lines.push('[mods]', `${pack} = ${value}`);
    } else {
        lines.splice(lastInMods + 1, 0, `${pack} = ${value}`);
    }
    return lines.join(nl) + nl;
}

const same = (a: Uint8Array, b: Uint8Array): boolean => a.length === b.length && a.every((v, i) => v === b[i]);

/**
 * Self-test for the writer: an unmodified rewrite must equal the source byte for byte, and replacing the given
 * entries with their own bytes must leave every payload readable and identical. Gate for exporting real edits.
 */
export async function srscRoundTrips(reader: SrscReader, indices: number[]): Promise<boolean> {
    const orig = await reader.source.read(0, reader.source.size);
    if (!same(await writeSrsc(reader), orig)) return false;
    const rep = new Map<number, Uint8Array>();
    for (const i of indices) rep.set(i, await reader.readRaw(reader.entries[i]!));
    const again = await SrscReader.fromBytes(await writeSrsc(reader, rep));
    for (const i of indices) {
        if (!same(await reader.readRaw(reader.entries[i]!), await again.readRaw(again.entries[i]!))) return false;
    }
    return true;
}

/** The manifest text with another pack id (and name), so a copy of a pack can sit beside the original. */
export function retagManifestToml(toml: string, id: string, name: string): string {
    return toml
        .replace(/^id\s*=\s*".*"$/m, `id = "${id}"`)
        .replace(/^name\s*=\s*".*"$/m, `name = "${name}"`);
}

/**
 * `mods.ini` for one Play session with a temporary converted pack: packs whose name starts with `ootf` (the player's own
 * converted levels share files and class ids with the Play pack) are switched off and the Play pack on. The desktop
 * shell puts the original text back when the game closes.
 */
export function playPackIni(ini: string, playId: string): string {
    let out = ini;
    let inMods = false;
    for (const raw of ini.split(String.fromCharCode(10))) {
        const t = raw.trim();
        const sec = /^\[(.+)\]$/.exec(t);
        if (sec) {
            inMods = sec[1]!.trim().toLowerCase() === 'mods';
            continue;
        }
        const eq = t.indexOf('=');
        if (!inMods || eq <= 0 || t.startsWith(';') || t.startsWith('#')) continue;
        const name = t.slice(0, eq).trim();
        if (/^ootf/i.test(name) && name !== playId) out = updateModsIni(out, name, false);
    }
    return updateModsIni(out, playId, true);
}

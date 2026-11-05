import { strToU8, zipSync } from 'fflate';
import { SrscReader, levelNameProblem, writeSrsc, type SrscEntry } from '@drakan/formats';

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
}

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
    if (m.campaign !== undefined) {
        const why = campaignIdProblem(m.campaign);
        if (why) throw new ModError(why);
    }
    if (m.campaignSaves !== undefined && !CAMPAIGN_SAVES_PATTERN.test(m.campaignSaves))
        throw new ModError('campaignSaves must be 1-32 characters of letters, digits _ -');
    for (const level of m.levels ?? []) {
        const why = levelNameProblem(level);
        // The runtime skips such a level with only a log line, which leaves a pack that loads but has no level.
        if (why) throw new ModError(`level "${level}": ${why}`);
    }
    if (m.campaignStart !== undefined && !m.levels?.includes(m.campaignStart))
        throw new ModError('campaignStart must be one of the pack levels');
}

/** `mod.toml` text. Top-level keys first (the reader wants `dependencies` above the tables), then `[target]`. */
export function buildManifestToml(m: ModMeta): string {
    validateModMeta(m);
    const l: string[] = [`format = ${MOD_FORMAT_VERSION}`, `id = ${tomlStr(m.id)}`];
    l.push(`name = ${tomlStr(m.name ?? m.id)}`);
    if (m.version) l.push(`version = ${tomlStr(m.version)}`);
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

export const zipModFiles = (files: Record<string, Uint8Array>): Uint8Array => zipSync(files, { level: 6 });

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

import { strToU8 } from 'fflate';
import {
    buildPs2Lvl,
    encodePs2Texture,
    isOotfArenaLevel,
    isSkip,
    ootfLevelKey,
    latin1,
    mangleComponent,
    ps2BlobBuilder,
    ps2BlobFor,
    ps2LevelTag,
    LVL_TYPE_LAYERS,
    pcLevelName,
    SubtitleTable,
    classifySubtitleLines,
    detectOotfInstallLanguage,
    listObjectSounds,
    mergeSubtitleFiles,
    parseSubtitleFile,
    readPcTexture,
    SrscReader,
    type BlobBuilder,
    type OotfLanguage,
    type PcLevel,
    type PcFileSource,
    type SubtitleFile,
    type OotfLevelOut,
    type Ps2LevelParts,
} from '@drakan/formats';
import {
    newDraft,
    previewFileName,
    serializeMapFile,
    slugify,
    type ArenaDraft,
    type MarkerKind,
    type Vec3,
} from './arena.ts';
import { buildManifestToml, ModError, MOD_MAX_ENTRY, zipModFiles, type ModMeta } from './mod.ts';

/** Read the original credits text and artwork from the player's Dragon.rrc. */
export async function ootfCredits(src: PcFileSource): Promise<{
    files: Record<string, Uint8Array>;
    artwork?: Pick<ModMeta, 'campaignCreditsBackdrop' | 'campaignCreditsLogos'>;
}> {
    const raw = await src.read('Dragon.rrc');
    if (!raw) return { files: {} };
    const rrc = await SrscReader.fromBytes(raw);
    const first = rrc.find(0x401, 8804, 0);
    const second = rrc.find(0x401, 8805, 0);
    if (!first?.size || !second?.size) return { files: {} };
    const wrap = (text: string): string => {
        // The credits use a narrow native canvas. Break the PC's long lines at word boundaries.
        const lines: string[] = [];
        for (let line of text.split('\r\n')) {
            while (line.length > 44 && !line.startsWith('=')) {
                const space = line.lastIndexOf(' ', 44);
                if (space <= 0) break;
                lines.push(line.slice(0, space).trimEnd());
                line = line.slice(space + 1).trimStart();
            }
            lines.push(line);
        }
        return lines.join('\r\n');
    };
    const encode = ({ data }: { data: Uint8Array }): Uint8Array => {
        const lines = latin1(data)
            .split(/\r\n|\n|\r/)
            .map((rawLine) => {
                const line = rawLine.trim();
                if (line.endsWith(':')) return '=' + line.slice(0, -1);
                if (line && /^\t\t {3,}/.test(rawLine)) return '=' + line;
                return line.replace(/^(.+?):[ \t]*\t+(.+)$/, '=$1\r\n$2');
            });
        return Uint8Array.from(wrap(lines.join('\r\n')), (ch) => ch.charCodeAt(0));
    };
    const [firstBlock, secondBlock] = await Promise.all([rrc.read(first).then(encode), rrc.read(second).then(encode)]);
    const files: Record<string, Uint8Array> = {
        'credits/credits.txt': firstBlock,
        'credits/credits-2.txt': secondBlock,
    };
    const [left, right, publisher] = await Promise.all([8807, 8808, 8806].map((id) => readPcTexture(rrc, id)));
    if (!left || !right || !publisher || isSkip(left) || isSkip(right) || isSkip(publisher)) return { files };
    const logos = [32752, 32753, 32754] as const;
    for (const [index, logo] of [left, right, publisher].entries()) {
        files[`new/INTERFCE.TXD/0040_${logos[index]}_0.bin`] = encodePs2Texture(logo.image, {
            maxTruecolorPixels: 256 * 256,
        }).body;
    }
    // The PC credits clear to black. Use one black tile for all six parts of the console roll's backdrop.
    const black = 32755;
    const tileSize = 256;
    const rgba = new Uint8Array(tileSize * tileSize * 4);
    for (let i = 3; i < rgba.length; i += 4) rgba[i] = 255;
    files[`new/INTERFCE.TXD/0040_${black}_0.bin`] = encodePs2Texture({ width: tileSize, height: tileSize, rgba }).body;
    return {
        files,
        artwork: { campaignCreditsBackdrop: [black, black, black, black, black, black], campaignCreditsLogos: logos },
    };
}

/** Explicit transcripts take precedence over the install's own file and the matching bundled language. */
export async function ootfSubtitleTable(
    src: PcFileSource,
    {
        explicit,
        loadBundled,
        log,
    }: {
        explicit?: SubtitleFile[];
        loadBundled: (language: OotfLanguage) => Promise<unknown>;
        log?: (s: string) => void;
    },
) {
    if (!explicit) {
        const bytes = await src.read('ootf-subtitles.json');
        if (bytes) {
            explicit = [parseSubtitleFile(JSON.parse(new TextDecoder().decode(bytes)))];
        } else {
            const detected = await detectOotfInstallLanguage(src);
            const { language } = detected.language;
            const bundled = language === 'unknown' ? undefined : await loadBundled(language);
            if (bundled === undefined) {
                log?.(
                    `No matching bundled subtitles (install language: ${language}). Supply a transcript or generate one in setup.`,
                );
                return undefined;
            }
            const track = parseSubtitleFile(bundled);
            if (track.language !== language) {
                throw new Error(`Bundled subtitle file ${language}.json declares language "${track.language}".`);
            }
            explicit = [track];
        }
    }
    const merged = mergeSubtitleFiles(explicit);
    for (const l of classifySubtitleLines(merged, await listObjectSounds(src)))
        log?.(`subtitles: ${l.bank} #${l.id} (${l.name ?? ''}) is a bark, no subtitle: ${l.kindReason}`);
    const table = new SubtitleTable(merged);
    log?.(`subtitles: ${table.size} line(s) shown, ${table.withheld.length} withheld by their kind`);
    return table;
}

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
    /** A PNG picture of the arena (menu card), shipped as `previews/<previewFileName(arena name)>`; needs `arena`. */
    preview?: Uint8Array;
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

/**
 * The conversion notes of one level that go into the pack warnings: every class kept out on purpose and every real gap
 * (so the importer can list each with its reason), then the first three of the other notes.
 */
export function packLevelNotes(warnings: readonly string[]): string[] {
    const always = (e: string): boolean => /^(kept out on purpose: |no PS2 class for )/.test(e);
    return [...warnings.filter(always), ...warnings.filter((e) => !always(e)).slice(0, 3)];
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
            if (input.preview?.length) files[`previews/${previewFileName(arena.name || title)}`] = input.preview;
        } else if (input.preview?.length)
            warnings.push(`${title}: a picture was given but the level has no arena; ignored`);
        const stem = mangled.slice(0, 6) + 'TS';
        for (const [ext, data] of Object.entries({ ...options.splats, ...input.splats })) {
            files[`files/ENGLISH/LEVELS/${mangled}/${stem}.${ext.toUpperCase()}`] = data;
        }
        if (!options.splats)
            warnings.push(`${title}: no splat files given; the game crashes after loading without them`);
        for (const e of packLevelNotes(input.parts.report.warnings)) warnings.push(`${title}: ${e}`);
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

const OOTF_LOADOUT_CLUB = 0x3372b;

/**
 * Read the active launch pads and their transforms from the supplied PC level. Air matches use the dragon pads;
 * ground and mixed matches use the Rynn pads. A custom map outside the retail groups can use either kind of pad.
 */
export function ootfArenaFromPc(
    pc: PcLevel,
    title: string,
    converted?: Pick<OotfLevelOut, 'arenaPickups' | 'arenaOrder' | 'arenaLoadout' | 'arenaDragon'>,
): ArenaDraft | null {
    if (!isOotfArenaLevel(pc)) return null;
    const group = ootfLevelKey(pc).split('/')[1];
    const rules = group === 'air' || group === 'ground' || group === 'qod' ? group : undefined;
    const pads = (rfl: number, name: RegExp) =>
        pc.list.objects.filter((o) => {
            const cls = pc.classes.get(o.dbIndex)?.get(o.classId);
            return cls?.rflClass === rfl || (cls?.rflClass === undefined && name.test(cls?.name ?? ''));
        });
    const dragonPads = () => pads(127, /^dragon\s*launch pad/i);
    let starts = rules === 'air' ? dragonPads() : pads(18, /^rynn\s+launch pad/i);
    if (!rules && starts.length < 2) starts = dragonPads();
    if (starts.length < 2) return null;
    const name = pcLevelName(pc.header) || title;
    const d = newDraft(title);
    d.id = slugify(name);
    d.name = name;
    d.game = 'Drakan 1';
    d.ootfRules = rules;
    d.ootfOrder = converted?.arenaOrder;
    d.ootfDragon = converted?.arenaDragon;
    d.description = 'A multiplayer map of Drakan: Order of the Flame.';
    d.players = rules ? 12 : Math.min(12, starts.length);
    for (const pad of starts) d.spawns.push({ uid: d.nextUid++, pos: [...pad.pos], yaw: pad.rot[1], team: 0 });
    if (rules !== 'air') d.loadout = converted?.arenaLoadout ? [...converted.arenaLoadout] : [OOTF_LOADOUT_CLUB];
    for (const pickup of converted?.arenaPickups ?? [])
        d.pickups.push({
            uid: d.nextUid++,
            pos: [...pickup.pos],
            respawn: pickup.respawn,
            res: 0,
            label: pickup.name,
            rewards: pickup.rewards,
            visual: pickup.visual,
        });
    // PC-only multiplayer objects without a PS2 engine class (dragon launch pads, the Soul Crystal, dragon invulnerability)
    // are kept as authored markers for custom modes; the game does not read them yet.
    const marks: [RegExp, MarkerKind, string][] = [
        [/red\s*dragon\s*launch/i, 'base', 'Red dragon launch pad'],
        [/^dragon\s*launch/i, 'base', 'Dragon launch pad'],
        [/soul crystal/i, 'flag', 'Fire Dragon Soul Crystal'],
        [/dragon invun/i, 'zone', 'Dragon invulnerability'],
    ];
    for (const o of pc.list.objects) {
        const n = (pc.classes.get(o.dbIndex)?.get(o.classId)?.name ?? '').trim();
        const m = marks.find(([re]) => re.test(n));
        if (m)
            d.markers.push({
                uid: d.nextUid++,
                kind: m[1],
                pos: [...o.pos] as Vec3,
                radius: 400,
                team: 0,
                label: m[2],
            });
    }
    return d;
}

export const zipOotfPack = (pack: OotfPack): Uint8Array => zipModFiles(pack.files);

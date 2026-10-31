import { strToU8 } from 'fflate';
import {
    buildPs2Lvl,
    mangleComponent,
    type BlobBuilder,
    type Ps2LevelParts,
    type SrscReader,
} from '@drakan/formats';
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
}

export interface OotfPack {
    meta: ModMeta;
    files: Record<string, Uint8Array>;
    /** `[title, mangled]` per level. */
    levels: { title: string; mangled: string; bytes: number }[];
    warnings: string[];
}

export async function buildOotfPack(
    meta: Omit<ModMeta, 'levels'>,
    template: SrscReader,
    inputs: readonly OotfLevelInput[],
    blob: BlobBuilder,
): Promise<OotfPack> {
    if (!inputs.length) throw new ModError('no levels to pack');
    const files: Record<string, Uint8Array> = {};
    const levels: OotfPack['levels'] = [];
    const warnings: string[] = [];
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
        for (const e of input.parts.report.warnings.slice(0, 3)) warnings.push(`${title}: ${e}`);
    }
    // Entries past the whole-entry limit only warn for new files (modding.md): level data is streamed in chunks.
    for (const l of levels) {
        if (l.bytes > MOD_MAX_ENTRY * 64) warnings.push(`${l.title}: ${l.bytes} bytes is unusually large`);
    }
    const full: ModMeta = { ...meta, levels: levels.map((l) => l.title) };
    files['mod.toml'] = strToU8(buildManifestToml(full));
    return { meta: full, files, levels, warnings };
}

export const zipOotfPack = (pack: OotfPack): Uint8Array => zipModFiles(pack.files);

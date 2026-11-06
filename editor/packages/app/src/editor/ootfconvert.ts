// The heavy half of "Import Order of the Flame levels": reading the install, converting the levels and building the mod
// pack. It runs in `ootfworker.ts` so the editor stays responsive; the dialog only shows the progress and the result.

import { buildOotfPack, ootfArenaFromPc, type ModMeta, type OotfPack } from '@drakan/editor-core';
import {
    SrscReader,
    ps2BlobFor,
    convertOotfLevels,
    loadPcLevel,
    parseLevelHeader,
    pcLevelName,
    type PcFileSource,
} from '@drakan/formats';
import { OOTF_DIALOG_OPTIONS, ootfDiscContext } from './ootfdisc.ts';

/** One file of the player's install: its path below the chosen folder and the file itself. */
export interface InstallFile {
    path: string;
    file: File;
}

export interface ConvertRequest {
    files: InstallFile[];
    /** PC level paths to convert. */
    levels: string[];
    prefix: string;
    /** Template disc level (`ENGLISH\LEVELS\LEVEL0NA`) and its `.LVL`. */
    templateDir: string;
    templateBytes: Uint8Array;
    profile: string | null;
}

export interface LevelSummary {
    title: string;
    objectsOut: number;
    objectsIn: number;
    layers: number;
    unmappedClasses: number;
}

export interface ConvertResult {
    pack: OotfPack;
    summaries: LevelSummary[];
    /** Conversion warnings and pack warnings. */
    notes: string[];
    /** True when a campaign (not only arenas) was converted. */
    campaign: boolean;
}

export function filesSource(files: readonly InstallFile[]): PcFileSource {
    const byLower = new Map(files.map((f) => [f.path.toLowerCase(), f.file]));
    return {
        read: async (p) => {
            const f = byLower.get(p.toLowerCase());
            return f ? new Uint8Array(await f.arrayBuffer()) : null;
        },
        list: async () => files.map((f) => f.path),
    };
}

/**
 * Convert the requested levels. `readDisc` reads a language-relative file of the opened disc (the template's
 * dependencies); `progress` receives the converter's own log lines.
 */
export async function runConversion(
    req: ConvertRequest,
    readDisc: (path: string) => Promise<Uint8Array | null>,
    progress: (message: string) => void,
): Promise<ConvertResult> {
    const src = filesSource(req.files);
    const template = await SrscReader.fromBytes(req.templateBytes);
    const tplEntry = template.entries.find((e) => e.type === 0);
    if (!tplEntry) throw new Error('The template level has no header record; choose another template.');
    const header = parseLevelHeader((await template.read(tplEntry)).data);
    const { context, splats } = await ootfDiscContext(template, header, req.templateDir, readDisc);
    const result = await convertOotfLevels(src, req.levels, context, {
        ...OOTF_DIALOG_OPTIONS,
        name: (pc) => `${req.prefix}${pcLevelName(pc.header)}`.slice(0, 60),
        log: progress,
    });
    const failed = result.stages.filter((stage) => !stage.ok);
    if (failed.length) throw new Error(`Conversion failed: ${failed.map((s) => `${s.stage}: ${s.error}`).join('; ')}`);
    const inputs = [];
    const summaries: LevelSummary[] = [];
    for (const level of result.levels) {
        const pc = await loadPcLevel(src, level.pcPath);
        const R = level.parts.report;
        summaries.push({
            title: level.title,
            objectsOut: R.objectsOut,
            objectsIn: R.objectsIn,
            layers: R.layers,
            unmappedClasses: R.unmappedClasses.size,
        });
        inputs.push({
            title: level.title,
            parts: level.parts,
            splats: level.splats,
            arena: ootfArenaFromPc(pc, level.title) ?? undefined,
        });
    }
    const start = result.levels.find((l) => !l.arena);
    const meta: Omit<ModMeta, 'levels'> = {
        id: 'ootf-levels',
        name: 'Order of the Flame levels',
        description: 'Levels converted from Drakan: Order of the Flame.',
        target: { profile: req.profile ?? 'pal' },
        ...(start ? { campaign: 'ootf', campaignName: 'Drakan: Order of the Flame', campaignStart: start.title } : {}),
    };
    progress('Building the mod pack');
    const pack = await buildOotfPack(meta, template, inputs, ps2BlobFor, { splats, extraFiles: result.files });
    return { pack, summaries, notes: [...result.warnings, ...pack.warnings], campaign: !!start };
}

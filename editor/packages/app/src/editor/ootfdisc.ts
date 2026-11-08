import {
    resolveDependencyStem,
    type LvlHeader,
    type OotfPipelineOptions,
    type OotfPs2Context,
    SrscReader,
} from '@drakan/formats';

// The import dialog must build the same context as the in-game verified CLI (editor/scripts/ootf-convert.ts on the files
// scripts/harness/ootf_level_run.py extracts). Reading every dependency instead changed the pack: SYSTEM hosted models in a
// replaced System.mod, Rynn's SYSTEM.ADB clips were retargeted and FX classes got text ids without an FX.XDB.

/** Template ODBs whose own MOD / TXD may host converted models (OotfPs2Context.hostFiles). */
const HOST_ODBS = new Set(['LEVEL1ON', 'WORLDCON', 'FX', 'MOUNTAON']);
/** MOD / ADB containers beyond the host's (OotfPs2Context.mods / adbs): the creatures. SYSTEM (Rynn) stays as it is. */
const CREATURE_ODBS = new Set(['NPCS']);

/** Pipeline options of the import dialog; the CLI's defaults (sounds are opt-in in the pipeline). */
export const OOTF_DIALOG_OPTIONS = { sounds: true } as const satisfies OotfPipelineOptions;

/** Read conversion dependencies through the language-relative disc API. */
export async function ootfDiscContext(
    template: SrscReader,
    header: LvlHeader,
    templateDir: string,
    read: (path: string) => Promise<Uint8Array | null>,
): Promise<{ context: OotfPs2Context; splats: Record<string, Uint8Array> }> {
    const dir = templateDir.replace(/\\/g, '/').replace(/^\/?(?:ENGLISH|GERMAN|FRENCH|ITALIAN|SPANISH)\//i, '');
    const stem = dir.split('/').pop()!;
    const splats: Record<string, Uint8Array> = {};
    for (const ext of ['DB', 'TXD']) {
        const data = await read(`${dir}/${stem.slice(0, 6)}TS.${ext}`);
        if (!data) throw new Error(`Template ${stem} is missing its splat ${ext}; choose another template.`);
        splats[ext] = data;
    }
    const odbs = new Map<string, SrscReader>();
    const ssds = new Map<string, SrscReader>();
    const mods = new Map<string, SrscReader>();
    const adbs = new Map<string, SrscReader>();
    const dbFiles = new Map<string, { dir: string; text: string }>();
    const hostFiles = new Map<string, { mod: SrscReader; txd: SrscReader }>();
    const xdbFiles = new Set<string>();
    let hostMod: SrscReader | undefined, hostTxd: SrscReader | undefined;
    let hostAdb: SrscReader | undefined, hostSdb: SrscReader | undefined;
    let musicSdb: SrscReader | undefined;
    for (const dep of header.dependencies) {
        if (/_splats$/i.test(dep.path.text)) continue;
        const base = resolveDependencyStem(dir, dep.path.text).replace(/\\/g, '/');
        const name = base.split('/').pop()!;
        const readers = new Map<string, SrscReader>();
        for (const ext of ['ODB', 'SSD', 'MOD', 'TXD', 'ADB', 'SDB']) {
            const data = await read(`${base}.${ext}`);
            if (data) readers.set(ext, await SrscReader.fromBytes(data));
        }
        const odb = readers.get('ODB'),
            ssd = readers.get('SSD');
        if (odb) odbs.set(name, odb);
        if (ssd) ssds.set(name, ssd);
        if (CREATURE_ODBS.has(name)) {
            const m = readers.get('MOD'),
                a = readers.get('ADB');
            if (m) mods.set(name, m);
            if (a) adbs.set(name, a);
        }
        if (await read(`${base}.XDB`)) xdbFiles.add(`${name}.XDB`);
        const db = await read(`${base}.DB`);
        if (db)
            dbFiles.set(name, {
                dir: `ENGLISH/${base.slice(0, base.lastIndexOf('/'))}`,
                text: new TextDecoder('latin1').decode(db),
            });
        const mod = readers.get('MOD'),
            txd = readers.get('TXD');
        if (mod && txd && HOST_ODBS.has(name)) hostFiles.set(name, { mod, txd });
        if (name === 'RESOURES') {
            hostMod = mod;
            hostTxd = txd;
            hostAdb = readers.get('ADB');
            hostSdb = readers.get('SDB');
        }
        if (name === 'PS2MUSIC') musicSdb = readers.get('SDB'); // the rendered OotF music gets free ids of it
    }
    return {
        splats,
        context: {
            template,
            odbs,
            ssds,
            mods,
            adbs,
            dbFiles,
            hostFiles,
            xdbFiles,
            hostMod,
            hostTxd,
            hostSdb,
            hostAdb,
            musicSdb,
            splatTxd: splats['TXD'],
            procTxd: (await read('WORLDCON/PROCTEES/PROCTEES.TXD')) ?? undefined,
        },
    };
}

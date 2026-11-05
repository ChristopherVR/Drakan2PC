import { resolveDependencyStem, type LvlHeader, type OotfPs2Context, SrscReader } from '@drakan/formats';

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
    let hostTxd: SrscReader | undefined, hostSdb: SrscReader | undefined;
    for (const dep of header.dependencies) {
        if (/_splats$/i.test(dep.path.text)) continue;
        const base = resolveDependencyStem(dir, dep.path.text).replace(/\\/g, '/');
        const name = base.split('/').pop()!;
        const readers = new Map<string, SrscReader>();
        for (const ext of ['ODB', 'SSD', 'MOD', 'TXD', 'ADB', 'SDB']) {
            const data = await read(`${base}.${ext}`);
            if (data) readers.set(ext, await SrscReader.fromBytes(data));
        }
        for (const [ext, map] of [
            ['ODB', odbs],
            ['SSD', ssds],
            ['MOD', mods],
            ['ADB', adbs],
        ] as const) {
            const r = readers.get(ext);
            if (r) map.set(name, r);
        }
        const db = await read(`${base}.DB`);
        if (db)
            dbFiles.set(name, {
                dir: `ENGLISH/${base.slice(0, base.lastIndexOf('/'))}`,
                text: new TextDecoder('latin1').decode(db),
            });
        const mod = readers.get('MOD'),
            txd = readers.get('TXD');
        if (mod && txd) hostFiles.set(name, { mod, txd });
        if (name === 'RESOURES') {
            hostTxd = txd;
            hostSdb = readers.get('SDB');
        }
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
            hostMod: mods.get('RESOURES'),
            hostTxd,
            hostSdb,
            hostAdb: adbs.get('RESOURES'),
            splatTxd: splats['TXD'],
            procTxd: (await read('WORLDCON/PROCTEES/PROCTEES.TXD')) ?? undefined,
        },
    };
}

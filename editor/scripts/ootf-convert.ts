// Convert Order of the Flame levels to a PS2 mod pack (zip). Dev tool; the editor UI does the same from the browser.
//
//   node --experimental-transform-types --no-warnings editor/scripts/ootf-convert.ts \
//        --ootf <OotF install dir> --ps2 <dir with the extracted PS2 LEVEL*.LVL and *.ODB> \
//        --template LEVEL1NA --out build/ootf-pack.zip [--id ootf-levels] [--prefix "OotF - "] [--splats <dir with LEVEL1TS.DB and .TXD, default --ps2>] [--campaign 1] [--list] <pc level paths...>
//
// Level paths are relative to the install dir, e.g. "Multiplayer/Ground/Relic/Relic.lvl". Without paths every level.
import { existsSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { join, relative, sep } from 'node:path';
import { buildOotfPack, zipOotfPack } from '@drakan/editor-core';
import { SrscReader, convertOotfLevels, findPcLevels, ps2BlobFor, pcLevelName, type PcFileSource } from '@drakan/formats';

const args = process.argv.slice(2);
const opt: Record<string, string> = {};
const free: string[] = [];
let list = false;
for (let i = 0; i < args.length; i++) {
    const a = args[i]!;
    if (a === '--list') list = true;
    else if (a.startsWith('--')) opt[a.slice(2)] = args[++i] ?? '';
    else free.push(a);
}
const need = (k: string): string => opt[k] ?? (console.error(`missing --${k}`), process.exit(2));

const root = need('ootf');
const all: string[] = [];
(function walk(d: string) {
    for (const f of readdirSync(d)) {
        const p = join(d, f);
        statSync(p).isDirectory() ? walk(p) : all.push(relative(root, p).split(sep).join('/'));
    }
})(root);
const byLower = new Map(all.map((p) => [p.toLowerCase(), p]));
const src: PcFileSource = {
    read: async (p) => {
        const r = byLower.get(p.toLowerCase());
        return r ? new Uint8Array(readFileSync(join(root, r))) : null;
    },
    list: async () => all,
};

const levels = free.length ? free : await findPcLevels(src);
if (list) {
    for (const p of levels) console.log(p);
    process.exit(0);
}

const ps2 = need('ps2');
const tplName = need('template').toUpperCase();
const template = await SrscReader.fromBytes(new Uint8Array(readFileSync(join(ps2, `${tplName}.LVL`))));
const odbs = new Map<string, SrscReader>();
for (const f of readdirSync(ps2).filter((x) => /\.ODB$/i.test(x))) {
    odbs.set(f.slice(0, -4).toUpperCase(), await SrscReader.fromBytes(new Uint8Array(readFileSync(join(ps2, f)))));
}
const optReader = async (name: string): Promise<SrscReader | undefined> =>
    existsSync(join(ps2, name)) ? SrscReader.fromBytes(new Uint8Array(readFileSync(join(ps2, name)))) : undefined;
// Other PS2 MOD / ADB containers (NPCS.MOD ...): skinned templates of ODBs that list RESOURES, PS2 rigs and clips of name-mapped creatures.
const optMap = async (ext: string): Promise<Map<string, SrscReader>> => {
    const m = new Map<string, SrscReader>();
    for (const f of readdirSync(ps2).filter((x) => new RegExp(`^(?!RESOURES).*\.${ext}$`, 'i').test(x)))
        m.set(f.slice(0, -4).toUpperCase(), await SrscReader.fromBytes(new Uint8Array(readFileSync(join(ps2, f)))));
    return m;
};
const ssds = new Map<string, SrscReader>();
for (const f of readdirSync(ps2).filter((x) => /\.SSD$/i.test(x))) {
    ssds.set(f.slice(0, -4).toUpperCase(), await SrscReader.fromBytes(new Uint8Array(readFileSync(join(ps2, f)))));
}
// ODB stem -> disc directory (written by scripts/harness/ootf_level_run.py) so the pipeline can patch an ODB's .DB.
const dbFiles = new Map<string, { dir: string; text: string }>();
const pathsFile = join(ps2, 'paths.json');
if (existsSync(pathsFile)) {
    const dirs = JSON.parse(readFileSync(pathsFile, 'utf8')) as Record<string, string>;
    for (const [stem, dir] of Object.entries(dirs)) {
        const f = join(ps2, `${stem}.DB`);
        if (existsSync(f)) dbFiles.set(stem, { dir, text: readFileSync(f, 'latin1') });
    }
}

// Own MOD / TXD of template ODBs that host models of cloned doors / switches / flames (scripts/harness/ootf_level_run.py writes them
// next to the ps2 data dir as <ps2>/../hosts/<STEM>.MOD|TXD, see OotfPs2Context.hostFiles).
const hostFiles = new Map<string, { mod: SrscReader; txd: SrscReader }>();
{
    const hostsDir = opt['hosts'] ?? join(ps2, '..', 'hosts');
    if (existsSync(hostsDir)) {
        for (const f of readdirSync(hostsDir).filter((x) => /\.MOD$/i.test(x))) {
            const stem = f.slice(0, -4).toUpperCase();
            const t = join(hostsDir, `${stem}.TXD`);
            if (!existsSync(t)) continue;
            hostFiles.set(stem, {
                mod: await SrscReader.fromBytes(new Uint8Array(readFileSync(join(hostsDir, f)))),
                txd: await SrscReader.fromBytes(new Uint8Array(readFileSync(t))),
            });
        }
    }
}

// The template's splat files (LEVEL1TS.DB / .TXD): needed beside every converted LVL (the game crashes without them).
const splatDir = opt['splats'] ?? ps2;
const splatStem = opt['splat-stem'] ?? `${tplName.slice(0, 6)}TS`;
const splats: Record<string, Uint8Array> = {};
for (const ext of ['DB', 'TXD']) {
    const f = join(splatDir, `${splatStem}.${ext}`);
    if (existsSync(f)) splats[ext] = new Uint8Array(readFileSync(f));
}
if (!Object.keys(splats).length) console.warn(`warning: no ${splatStem}.DB/.TXD in ${splatDir} (--splats <dir>); the pack will crash the game`);

const prefix = opt['prefix'] ?? 'OotF - ';
const only = opt['only'] ? new Set(opt['only'].split(',')) : null; // stage switches: --only textures,models,...
const res = await convertOotfLevels(
    src,
    levels,
    { template, odbs, ssds, dbFiles, hostMod: await optReader('RESOURES.MOD'), hostTxd: await optReader('RESOURES.TXD'), hostAdb: await optReader('RESOURES.ADB'), hostSdb: await optReader('RESOURES.SDB'), mods: await optMap('MOD'), hostFiles, adbs: await optMap('ADB'), xdbFiles: readdirSync(ps2).some((f) => /\.XDB$/i.test(f)) ? new Set(readdirSync(ps2).filter((f) => /\.XDB$/i.test(f)).map((f) => f.toUpperCase())) : undefined, splatTxd: splats['TXD'], procTxd: existsSync(join(ps2, 'PROCTEES.TXD')) ? new Uint8Array(readFileSync(join(ps2, 'PROCTEES.TXD'))) : undefined },
    {
        name: (pc) => `${prefix}${pcLevelName(pc.header)}`,
        log: (m) => console.warn(m),
        textures: !only || only.has('textures'),
        terrainInCommon: opt['tex-common'] === '1',
        terrainDiscTextures: opt['tex-disc'] === '1',
        terrain: {
            firstId: opt['tex-first'] ? Number(opt['tex-first']) : undefined,
            maxSize: opt['tex-max'] ? Number(opt['tex-max']) : undefined,
            maxTextures: opt['tex-cap'] ? Number(opt['tex-cap']) : undefined,
        },
        models: !only || only.has('models'),
        classes: !only || only.has('classes'),
        singletons: !only || only.has('singletons'),
        maxNewClasses: opt['max-classes'] ? Number(opt['max-classes']) : undefined,
        skinned: opt['skinned'] !== '0',
        retarget: opt['retarget'] !== '0',
        preferCreatures: opt['creatures-first'] === '1',
        texts: !only || only.has('texts'),
        music: !only || only.has('music'),
        musicStartOn: opt['music-start-on'] === '1',
        sounds: !only || only.has('sounds'),
        sequences: !only || only.has('sequences'),
    },
);
for (const l of res.levels) {
    const R = l.parts.report;
    console.log(
        `${l.pcPath}: ${R.objectsOut}/${R.objectsIn} objects, ${R.unmappedClasses.size} classes unmapped, ${R.layers} layers, ${R.cellsMapped} cells textured, ${R.cellsFallback} on fallback`,
    );
}
for (const s of res.stages) if (!s.ok) console.warn(`STAGE FAILED ${s.stage}: ${s.error}`);
if (res.assets) {
    const a = res.assets;
    for (const c of a.creatures) console.log(`creature ${c.key}: ${c.joints} joints, ${c.groups} groups, ${c.anims} clips (${c.animsSkipped} skipped)`);
    console.log(`assets: ${a.models.converted} models, ${a.textures.converted} textures (${a.textures.bytes} bytes), ${a.skipped.size} skipped`);
}
if (res.classPlan) {
    console.log(`classes: ${res.classPlan.mapped.size} mapped, ${res.classPlan.synthesized.length} synthesized, ${res.classPlan.unsupported.size} unsupported`);
}
const pack = await buildOotfPack(
    {
        id: opt['id'] ?? 'ootf-levels',
        name: 'Order of the Flame levels',
        target: { profile: 'pal' },
        // --campaign: the levels are the campaign in play order (the title menu offers "Drakan: Order of the Flame")
        ...(opt['campaign'] !== undefined ? { campaign: 'ootf' as const, campaignName: 'Drakan: Order of the Flame' } : {}),
    },
    template,
    res.levels.map((l) => ({ title: l.title, parts: l.parts, splats: l.splats })),
    ps2BlobFor,
    { splats, extraFiles: res.files },
);
for (const w of [...res.warnings, ...pack.warnings]) console.warn('warning:', w);
writeFileSync(need('out'), zipOotfPack(pack));
console.log(`wrote ${opt['out']} (${pack.levels.length} levels, ${Object.keys(pack.files).length} files)`);

// Convert Order of the Flame levels to a PS2 mod pack (zip). Dev tool; the editor UI does the same from the browser.
//
//   node --experimental-transform-types --no-warnings editor/scripts/ootf-convert.ts \
//        --ootf <OotF install dir> --ps2 <dir with the extracted PS2 LEVEL*.LVL and *.ODB> \
//        --template LEVEL1NA --out build/ootf-pack.zip [--id ootf-levels] [--prefix "OotF - "] [--splats <dir with LEVEL1TS.DB and .TXD, default --ps2>] [--list] <pc level paths...>
//
// Level paths are relative to the install dir, e.g. "Multiplayer/Ground/Relic/Relic.lvl". Without paths every level.
import { existsSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { join, relative, sep } from 'node:path';
import { buildOotfPack, zipOotfPack } from '@drakan/editor-core';
import {
    SrscReader,
    buildOdbIndex,
    convertPcLevel,
    dominantTexture,
    findPcLevels,
    loadPcLevel,
    parsePs2Terrain,
    ps2BlobFor,
    pcLevelName,
    ps2ClassTables,
    readLvl,
    LVL_TYPE_LAYERS,
    type PcFileSource,
} from '@drakan/formats';

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
const tplBytes = new Uint8Array(readFileSync(join(ps2, `${tplName}.LVL`)));
const template = await SrscReader.fromBytes(tplBytes);
const tpl = await readLvl(template);
const odbs = new Map<string, SrscReader>();
for (const f of readdirSync(ps2).filter((x) => /\.ODB$/i.test(x))) {
    odbs.set(f.slice(0, -4).toUpperCase(), await SrscReader.fromBytes(new Uint8Array(readFileSync(join(ps2, f)))));
}
const classes = ps2ClassTables(tpl.header, await buildOdbIndex(odbs));
const layersEntry = template.entries.find((e) => e.type === LVL_TYPE_LAYERS)!;
const fallback = dominantTexture(parsePs2Terrain(await template.readRaw(layersEntry)));

const blob = ps2BlobFor;

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
const inputs = [];
for (const p of levels) {
    const pc = await loadPcLevel(src, p);
    const parts = convertPcLevel(pc, { template: { header: tpl.header, classes }, textureFallback: fallback });
    const R = parts.report;
    console.log(
        `${p}: ${R.objectsOut}/${R.objectsIn} objects, ${R.unmappedClasses.size} classes unmapped, ${R.layers} layers, ${R.cellsFallback} cells on fallback texture`,
    );
    inputs.push({ title: `${prefix}${pcLevelName(pc.header)}`.slice(0, 60), parts });
}
const pack = await buildOotfPack(
    { id: opt['id'] ?? 'ootf-levels', name: 'Order of the Flame levels', target: { profile: 'pal' } },
    template,
    inputs,
    blob,
    { splats },
);
for (const w of pack.warnings) console.warn('warning:', w);
writeFileSync(need('out'), zipOotfPack(pack));
console.log(`wrote ${opt['out']} (${pack.levels.length} levels)`);

// Build a mod pack that edits the terrain of a disc level with the editor's terrain writer (rebuildPs2Terrain): a hill, a
// repainted patch or an extended layer edge. Dev tool for the in-game proof (docs/journal/2026-11-02/terrain-editing-write.md).
//
//   node --experimental-transform-types --no-warnings editor/scripts/terrain-pack.ts --iso <game.iso> --out <mods dir> \
//        [--level LEVEL1NA] [--id terrain.proof] [--at "x z"] [--hill <height>[:<radius cells>]] [--paint <cells>[:<ref hex>]]
//        [--extend <rows>[:left|right|top|bottom]] [--new-layer <w>[:<h>[:<dy>]]] [--slots group|overlap|own] [--layer <index>] [--paint-new <ref hex>] [--info]
//
// `--at` defaults to a point 3 cells in front of the player start. Writes <out>/<id>/ and <out>/mods.ini enabling it, and prints
// a DRAKAN_PLAYTEST_POS that puts the player on the edit.
import { mkdirSync, openAsBlob, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { buildModFiles, documentFromLvl, updateModsIni } from '@drakan/editor-core';
import {
    CELL_SIZE,
    Iso9660,
    LVL_TYPE_LAYERS,
    SrscReader,
    TEX_HOLE,
    TEX_INVISIBLE,
    blobSource,
    buildOdbIndex,
    cloneTerrainLayer,
    odbStemForDependency,
    resolveDependencyStem,
    extendTerrainLayer,
    layerHeightAt,
    parsePs2Terrain,
    readLvl,
    rebuildPs2Terrain,
    sliceSource,
    terrainGroundBelow,
    type TerrainLayer,
} from '@drakan/formats';

const args = process.argv.slice(2);
const opt = new Map<string, string>();
for (let i = 0; i < args.length; i++) {
    const a = args[i]!;
    if (!a.startsWith('--')) continue;
    const v = args[i + 1] && !args[i + 1]!.startsWith('--') ? args[++i]! : '';
    opt.set(a.slice(2), v);
}
const one = (k: string, d?: string): string => opt.get(k) ?? d ?? (console.error(`missing --${k}`), process.exit(2));

const BS = String.fromCharCode(92);
const iso = await Iso9660.open(blobSource(await openAsBlob(one('iso'))));
const stem = one('level', 'LEVEL1NA').toUpperCase();
const dirPath = ['ENGLISH', 'LEVELS', stem].join(BS);
const lvlFile = (await iso.list(dirPath)).find((e) => !e.isDir && /\.LVL$/i.test(e.name));
if (!lvlFile) throw new Error(`no LVL in ${dirPath}`);
const reader = await SrscReader.open(sliceSource(iso.source, lvlFile.extent * 2048, lvlFile.size));
const lvl = await readLvl(reader);
const entry = reader.entries.find((x) => x.type === LVL_TYPE_LAYERS)!;
const original = await reader.readRaw(entry);
const layers = parsePs2Terrain(original);

// The player start: the first object whose class is Rynn.
const odbReaders = new Map<string, SrscReader>();
for (const d of lvl.header.dependencies) {
    const s = odbStemForDependency(d.path.text);
    if (odbReaders.has(s)) continue;
    const f = await iso.find(`${resolveDependencyStem(dirPath, d.path.text)}.ODB`);
    if (f && !f.isDir) odbReaders.set(s, await SrscReader.open(sliceSource(iso.source, f.extent * 2048, f.size)));
}
const { doc } = documentFromLvl(lvl, await buildOdbIndex(odbReaders), stem);
const startObj = [...doc.objects()].find((o) => /^rynn/i.test(o.class));
if (!startObj && !opt.has('at')) throw new Error('no Rynn object; pass --at');
const sp = startObj?.transform.pos ?? [0, 0, 0];
const start = { x: sp[0], y: sp[1], z: sp[2] };
const [ax, az] = opt.has('at') ? one('at').split(/\s+/).map(Number) : [start.x, start.z - 3 * CELL_SIZE];
const ground = opt.has('layer')
    ? layerHeightAt(layers[Number(one('layer'))]!, ax!, az!)
    : terrainGroundBelow(layers, ax!, az!, start.y + 2000);
// `--layer <index>` picks the layer to edit instead of the ground surface at the point.
const under = opt.has('layer')
    ? [layers[Number(one('layer'))]!]
    : layers.filter((L) => Math.abs(layerHeightAt(L, ax!, az!) - ground) < 1);
console.log(
    `start ${start.x.toFixed(0)} ${start.y.toFixed(0)} ${start.z.toFixed(0)}; edit at ${ax} ${az}, ground ${ground.toFixed(0)}`,
);
for (const L of under) {
    console.log(
        `  layer ${L.index} '${L.name}' ${L.width}x${L.height} type ${L.type} at ${L.originX},${L.originZ} flags 0x${L.flags.toString(16)}`,
    );
}
if (opt.has('info') || !under.length) process.exit(under.length ? 0 : 1);

const edited: TerrainLayer[] = [...layers];
const edit = (i: number): TerrainLayer => {
    if (edited[i] === layers[i]) edited[i] = cloneTerrainLayer(layers[i]!);
    return edited[i]!;
};
let pose = { x: ax!, z: az!, y: ground };

if (opt.has('hill')) {
    // Raise every floor surface that lies at the ground height near the point (shared seam vertices move together).
    const [hs, rs] = one('hill').split(':');
    const amp = Number(hs);
    const radius = Number(rs ?? 4) * CELL_SIZE;
    let moved = 0;
    for (const L of layers) {
        if (L.type !== 0) continue;
        const sw = L.width + 1;
        for (let z = 0; z <= L.height; z++) {
            for (let x = 0; x <= L.width; x++) {
                const wx = (L.originX + x) * CELL_SIZE;
                const wz = (L.originZ + z) * CELL_SIZE;
                const d = Math.hypot(wx - ax!, wz - az!);
                if (d >= radius) continue;
                const v = z * sw + x;
                if (Math.abs(L.heights[v]! - ground) > 1500) continue;
                const E = edit(L.index);
                E.heights[v] = E.heights[v]! + amp * 0.5 * (1 + Math.cos((Math.PI * d) / radius));
                moved++;
            }
        }
    }
    pose.y = ground + amp;
    console.log(`hill: ${moved} vertices raised up to ${amp}`);
}
if (opt.has('paint')) {
    const [ns, rs] = one('paint').split(':');
    const n = Number(ns);
    const L = under[0]!;
    const cx = Math.floor(ax! / CELL_SIZE - L.originX);
    const cz = Math.floor(az! / CELL_SIZE - L.originZ);
    const here = L.cellTexLeft[cz * L.width + cx]!;
    // Default: the most used ref of the level that differs from the cell's own.
    const uses = new Map<number, number>();
    for (const M of layers)
        for (const t of M.cellTexLeft)
            if (t !== TEX_HOLE && t !== TEX_INVISIBLE && t !== here) uses.set(t, (uses.get(t) ?? 0) + 1);
    const ref = rs ? parseInt(rs, 16) : [...uses].sort((a, b) => b[1] - a[1])[0]![0];
    const E = edit(L.index);
    let painted = 0;
    for (let z = cz - (n >> 1); z < cz - (n >> 1) + n; z++) {
        for (let x = cx - (n >> 1); x < cx - (n >> 1) + n; x++) {
            if (x < 0 || z < 0 || x >= L.width || z >= L.height) continue;
            const c = z * L.width + x;
            if (E.cellTexLeft[c] === TEX_HOLE) continue;
            E.cellTexLeft[c] = ref;
            E.cellTexRight[c] = ref;
            painted++;
        }
    }
    console.log(`paint: ${painted} cells of layer ${L.index} from 0x${here.toString(16)} to 0x${ref.toString(16)}`);
}
if (opt.has('extend')) {
    const [rs, side] = one('extend').split(':');
    const rows = Number(rs);
    const L = under[0]!;
    const sides = side ? [side] : ['bottom', 'top', 'right', 'left'];
    // Pick the side whose new strip no other layer covers.
    const free = (s: string): boolean => {
        for (let k = 1; k <= rows; k++) {
            for (let i = 0; i <= (s === 'left' || s === 'right' ? L.height : L.width); i++) {
                const x =
                    s === 'left'
                        ? L.originX - k + 0.5
                        : s === 'right'
                          ? L.originX + L.width + k - 0.5
                          : L.originX + Math.min(i, L.width - 1) + 0.5;
                const z =
                    s === 'top'
                        ? L.originZ - k + 0.5
                        : s === 'bottom'
                          ? L.originZ + L.height + k - 0.5
                          : L.originZ + Math.min(i, L.height - 1) + 0.5;
                if (layers.some((M) => M.type === 0 && !Number.isNaN(layerHeightAt(M, x * CELL_SIZE, z * CELL_SIZE))))
                    return false;
            }
        }
        return true;
    };
    const s = sides.find(free) ?? sides[0]!;
    const grow = { [s]: rows } as { left?: number; right?: number; top?: number; bottom?: number };
    edited[L.index] = extendTerrainLayer(edited[L.index]!, grow);
    const E = edited[L.index]!;
    if (opt.has('paint-new')) {
        // Paint only the new cells (makes the added strip easy to see in a screenshot).
        const ref = parseInt(one('paint-new'), 16);
        const l0 = s === 'left' ? rows : 0;
        const t0 = s === 'top' ? rows : 0;
        for (let z = 0; z < E.height; z++) {
            for (let x = 0; x < E.width; x++) {
                const old = x >= l0 && x < l0 + L.width && z >= t0 && z < t0 + L.height;
                const c = z * E.width + x;
                if (!old && E.cellTexLeft[c] !== TEX_HOLE) E.cellTexLeft[c] = E.cellTexRight[c] = ref;
            }
        }
    }
    // Pose: the middle of the new strip.
    const mx =
        s === 'left' ? E.originX + rows / 2 : s === 'right' ? E.originX + E.width - rows / 2 : E.originX + E.width / 2;
    const mz =
        s === 'top'
            ? E.originZ + rows / 2
            : s === 'bottom'
              ? E.originZ + E.height - rows / 2
              : E.originZ + E.height / 2;
    // Pose: the solid point of the new strip nearest its middle (copied edge cells may be holes).
    const along = s === 'left' || s === 'right' ? E.height : E.width;
    for (let k = 0; k <= along; k++) {
        const off = (k % 2 ? 1 : -1) * Math.ceil(k / 2);
        const px = (s === 'left' || s === 'right' ? mx : Math.floor(mx) + 0.5 + off) * CELL_SIZE;
        const pz = (s === 'left' || s === 'right' ? Math.floor(mz) + 0.5 + off : mz) * CELL_SIZE;
        const y = layerHeightAt(E, px, pz);
        if (!Number.isNaN(y)) {
            pose = { x: px, z: pz, y };
            break;
        }
    }
    console.log(
        `extend: layer ${L.index} by ${rows} on the ${s} (${free(s) ? 'free' : 'overlaps another layer'}) -> ${E.width}x${E.height} at ${E.originX},${E.originZ}`,
    );
}

if (opt.has('new-layer')) {
    // A new w x h layer centred on the point, `dy` above the ground, with the cell texture and lighting of the layer below.
    const [ws, hs, dys] = one('new-layer').split(':');
    const w = Number(ws);
    const h = Number(hs ?? ws);
    const dy = Number(dys ?? 1000);
    const U = under[0]!;
    const ox = Math.round(ax! / CELL_SIZE - w / 2);
    const oz = Math.round(az! / CELL_SIZE - h / 2);
    const tex =
        U.cellTexLeft[Math.floor(az! / CELL_SIZE - U.originZ) * U.width + Math.floor(ax! / CELL_SIZE - U.originX)]!;
    const index = edited.length;
    const nv = (w + 1) * (h + 1);
    const N: TerrainLayer = {
        ...U,
        index,
        id: Math.max(...edited.map((L) => L.id)) + 1,
        name: `Editor layer ${index}`,
        width: w,
        height: h,
        originX: ox,
        originZ: oz,
        worldHeight: Math.round(ground + dy),
        visible: [index, ...U.visible],
        heights: new Float32Array(nv).fill(Math.round(ground + dy)),
        vertexFlags: new Uint16Array(nv).fill(U.vertexFlags[0]!),
        vertexMask: new Uint8Array(nv),
        cellFlags: new Uint16Array(w * h).fill(0xff00),
        cellTexLeft: new Uint32Array(w * h).fill(tex),
        cellTexRight: new Uint32Array(w * h).fill(tex),
        cellUv: new Uint16Array(w * h * 8),
    };
    for (let c = 0; c < w * h; c++) N.cellUv.set([0, 0xffff, 0xffff, 0, 0, 0, 0xffff, 0xffff], c * 8);
    edited.push(N);
    // Visible from the layer below and from every layer that can see it.
    for (const L of [...edited]) {
        if (L === N || !(L.index === U.index || L.visible.includes(U.index))) continue;
        const E = edited[L.index] === layers[L.index] ? { ...L, visible: [...L.visible] } : edited[L.index]!;
        E.visible.push(index);
        edited[L.index] = E;
    }
    pose = { x: (ox + w / 2) * CELL_SIZE, z: (oz + h / 2) * CELL_SIZE, y: ground + dy };
    console.log(
        `new layer ${index} (id ${N.id}) ${w}x${h} at ${ox},${oz}, ${dy} above the ground, texture 0x${tex.toString(16)}`,
    );
}
const { payload, report } = rebuildPs2Terrain(original, edited, {
    slots: (opt.get('slots') || 'group') as 'group' | 'overlap' | 'own',
});
for (const [i, w] of report.changed) console.log(`  layer ${i}: ${w.join(', ')}`);
for (const w of report.warnings) console.log(`  warning: ${w}`);
const id = one('id', 'terrain.proof');
const files = buildModFiles({ id, name: `Terrain proof (${id})`, version: '1.0', target: { profile: 'pal' } }, [
    { file: lvlFile.name, entry: { type: entry.type, id: entry.id, sub: entry.sub }, data: payload },
]);
const out = one('out');
for (const [p, data] of Object.entries(files)) {
    const f = join(out, id, p);
    mkdirSync(dirname(f), { recursive: true });
    writeFileSync(f, data);
}
writeFileSync(join(out, 'mods.ini'), updateModsIni('', id, true));
console.log(`wrote ${join(out, id)} (${payload.length} bytes, disc ${original.length})`);
console.log(`DRAKAN_PLAYTEST_POS=${pose.x.toFixed(0)} ${(pose.y + 100).toFixed(0)} ${pose.z.toFixed(0)}`);

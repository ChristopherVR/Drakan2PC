// Prove the skeleton / animation decode against a PS2 disc image. Dev tool; reads the player's ISO, prints counts only.
//
//   node --experimental-transform-types --no-warnings editor/scripts/anim-probe.ts <iso> [--verbose]
//
// For every .MOD on the disc (one language tree: the others carry the same data): every model with a 0x0208 record is
// decoded to a rig (hierarchy, bind pose, weights) and the rest pose is skinned back onto the mesh (must reproduce the
// vertices). Every animation of the .ADB with the same stem is decoded; clips the model lists are checked against the
// model's joint count and posed at several times (finite, bounded); the translation base of each unkeyed track is
// compared with the local bind translation of the joint (informational).
import { fstatSync, openSync, readSync } from 'node:fs';
import {
    ADB_ANIMATION,
    ADB_KEYS,
    Iso9660,
    MOD_CHARACTER,
    SrscReader,
    buildRig,
    clipFitsRig,
    localToWorld,
    parseClip,
    parseModel,
    parseSkeleton,
    restPose,
    rootMotion,
    sampleClip,
    skinMatrices,
    skinVertices,
} from '@drakan/formats';

const iso = process.argv[2];
if (!iso) {
    console.error('usage: anim-probe.ts <iso> [--verbose]');
    process.exit(2);
}
const verbose = process.argv.includes('--verbose');
const fd = openSync(iso, 'r');
const disc = await Iso9660.open({
    size: fstatSync(fd).size,
    read: async (o: number, l: number) => {
        const b = Buffer.alloc(l);
        const n = readSync(fd, b, 0, l, o);
        return new Uint8Array(b.buffer, b.byteOffset, n);
    },
});
const files = (await disc.walk()).filter((e) => !e.isDir && /^ENGLISH\\/i.test(e.path));
const byPath = new Map(files.map((e) => [e.path.toUpperCase(), e]));

const tot = {
    mods: 0,
    skinnedModels: 0,
    skeletonFail: 0,
    restMismatch: 0,
    unweightedVerts: 0,
    droppedInfluence: 0,
    anims: 0,
    animFail: 0,
    listed: 0,
    listedMissing: 0,
    listedFit: 0,
    listedMismatch: 0,
    posed: 0,
    badPose: 0,
    tracks: 0,
    tracksWithOwnBase: 0,
    rootMoving: 0,
};
const fails: string[] = [];

for (const m of files.filter((e) => /\.MOD$/i.test(e.name))) {
    tot.mods++;
    const mod = await SrscReader.open(await disc.openFile(m));
    const adbEntry = byPath.get(m.path.replace(/\.MOD$/i, '.ADB').toUpperCase());
    const adb = adbEntry ? await SrscReader.open(await disc.openFile(adbEntry)) : null;
    const clipCache = new Map<number, ReturnType<typeof parseClip> | null>();
    const clip = async (id: number) => {
        if (clipCache.has(id)) return clipCache.get(id)!;
        let c: ReturnType<typeof parseClip> | null = null;
        const i = adb?.find(ADB_ANIMATION, id);
        const k = adb?.find(ADB_KEYS, id);
        if (i && k) c = parseClip(await adb!.readRaw(i), await adb!.readRaw(k));
        clipCache.set(id, c);
        return c;
    };
    if (adb) {
        const seen = new Set<number>();
        for (const e of adb.entries)
            if (e.type === ADB_ANIMATION && !seen.has(e.id)) {
                seen.add(e.id);
                tot.anims++;
                try {
                    await clip(e.id);
                    if (!clipCache.get(e.id)) throw new Error('no 0x0502');
                } catch (err) {
                    tot.animFail++;
                    fails.push(`${m.name} anim ${e.id}: ${(err as Error).message}`);
                }
            }
    }
    for (const e of mod.entries.filter((x) => x.type === MOD_CHARACTER)) {
        tot.skinnedModels++;
        try {
            const model = (await parseModel(mod, e.id))!;
            const sk = parseSkeleton(await mod.readRaw(e), 'ps2');
            const nv = model.positions.length / 3;
            const rig = buildRig(sk, nv);
            tot.unweightedVerts += rig.unweighted;
            tot.droppedInfluence += rig.droppedInfluences;
            // 1. rest pose reproduces the mesh
            const skin = skinMatrices(rig, localToWorld(rig, restPose(rig)));
            const out = new Float32Array(nv * 3);
            skinVertices(rig, skin, model.positions, null, 0, nv, out, null);
            let worst = 0;
            for (let i = 0; i < out.length; i++) worst = Math.max(worst, Math.abs(out[i]! - model.positions[i]!));
            if (!(worst < 1e-3)) {
                tot.restMismatch++;
                fails.push(`${m.name} model ${e.id}: rest pose error ${worst}`);
            }
            // 2. listed clips
            const size = Math.max(
                model.bounds.max[0] - model.bounds.min[0],
                model.bounds.max[1] - model.bounds.min[1],
                model.bounds.max[2] - model.bounds.min[2],
                1e-6,
            );
            for (const id of rig.animIds) {
                tot.listed++;
                const c = await clip(id).catch(() => null);
                if (!c) {
                    tot.listedMissing++;
                    continue;
                }
                if (!clipFitsRig(rig, c)) {
                    tot.listedMismatch++;
                    if (verbose)
                        fails.push(
                            `${m.name} model ${e.id} anim ${id}: ${c.tracks.length} tracks vs ${rig.jointCount} joints`,
                        );
                    continue;
                }
                tot.listedFit++;
                // track base translation == local bind translation
                // (tracks whose constant translation differs from the bind one, e.g. weapon and foot joints, are
                // authored that way: playback uses the track's own base)
                c.tracks.forEach((t, j) => {
                    if (t.hasPos) return;
                    tot.tracks++;
                    for (let k = 0; k < 3; k++)
                        if (Math.abs(t.base[k]! - rig.bindLocal[j * 12 + 9 + k]!) > 1e-3) {
                            tot.tracksWithOwnBase++;
                            break;
                        }
                });
                const rm = rootMotion(c, rig);
                if (Math.hypot(...rm.delta) > 0.05 * size) tot.rootMoving++;
                for (const f of [0, 0.31, 0.77, 1]) {
                    tot.posed++;
                    const w = localToWorld(rig, sampleClip(rig, c, f * c.duration));
                    skinVertices(rig, skinMatrices(rig, w), model.positions, null, 0, nv, out, null);
                    let ok = true;
                    for (let i = 0; i < out.length && ok; i++)
                        if (!Number.isFinite(out[i]!) || Math.abs(out[i]!) > size * 20) ok = false;
                    if (!ok) {
                        tot.badPose++;
                        if (verbose) fails.push(`${m.name} model ${e.id} anim ${id} t=${f}: unbounded vertices`);
                    }
                }
            }
        } catch (err) {
            tot.skeletonFail++;
            fails.push(`${m.name} model ${e.id}: ${(err as Error).message}`);
        }
    }
}
console.log(JSON.stringify(tot, null, 1));
for (const f of fails.slice(0, 40)) console.log(f);
if (fails.length > 40) console.log(`... ${fails.length - 40} more`);

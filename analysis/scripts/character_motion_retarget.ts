/** Private disc-derived multiplayer animations. Run through character_motion.py; never check outputs into git. */
import fs from 'node:fs';
import path from 'node:path';
import {
    parseSkeleton,
    parsePs2Anim,
    writePs2Anim,
} from '../../editor/packages/formats/src/ootfanim.ts';
import { buildRig } from '../../editor/packages/formats/src/skeleton.ts';
import {
    matchJoints,
    rigSize,
    retargetPs2Anim,
} from '../../editor/packages/formats/src/ootfretarget.ts';

const [scratch, output] = process.argv.slice(2);
if (!scratch || !output)
    throw new Error('scratch and output directories required');
const manifest = JSON.parse(
    fs.readFileSync(path.join(scratch, 'input.json'), 'utf8'),
);
const sourceSkeleton = parseSkeleton(
    fs.readFileSync(path.join(scratch, '3877.skel')),
    'ps2',
);
const source = buildRig(sourceSkeleton, 0);
const emit = (
    folder: string,
    type: number,
    id: number,
    sub: number,
    data: Uint8Array,
) => {
    const dir = path.join(output, folder);
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(
        path.join(
            dir,
            `${type.toString(16).padStart(4, '0')}_${id}_${sub}.bin`,
        ),
        data,
    );
};
// Extend only the animation-id array; preserve the rest of the original model record byte-for-byte.
function appendAnimationIds(raw: Buffer, ids: number[]): Buffer {
    let p = 64;
    const u16 = () => {
        const n = raw.readUInt16LE(p);
        p += 2;
        return n;
    };
    const lods = u16();
    for (let i = 1; i < lods; i++) {
        const n = u16();
        p += n;
    }
    const nodes = u16();
    p += nodes * 36 + 4;
    const joints = u16();
    for (let j = 0; j < joints; j++) {
        p += 60;
        for (let lod = 0; lod < lods; lod++) {
            const weights = u16();
            p += weights * 8;
        }
    }
    p += lods * 30;
    const countOffset = p;
    const count = u16();
    p += count * 4;
    const added = Buffer.alloc(ids.length * 4);
    ids.forEach((id, i) => added.writeUInt32LE(id, i * 4));
    const result = Buffer.concat([raw.subarray(0, p), added, raw.subarray(p)]);
    result.writeUInt16LE(count + ids.length, countOffset);
    return result;
}
const report = [];
for (const [modelIndex, model] of manifest.models.entries()) {
    const originalSkeleton = fs.readFileSync(
        path.join(scratch, `${model.id}.skel`),
    );
    const sk = parseSkeleton(originalSkeleton, 'ps2');
    const addedIds: number[] = [];
    const target = buildRig(sk, 0);
    const match = matchJoints(source, target);
    // The 57-joint villager rig uses a different hand/finger naming scheme but the same anatomical chain.
    const aliases: Record<string, string> = {
        HandLeft: 'Lhand',
        HandRight: 'Rhand',
        FingersLeft: 'L_Ring1',
        FingersRight: 'R_Ring1',
        FingerMiddleLeft: 'L_Middle1',
        FingerMiddleRight: 'R_Middle1',
        FingerIndexLeft: 'L_Point1',
        FingerIndexRight: 'R_Point1',
        ThumbLeft: 'L_Thumb1',
        ThumbRight: 'R_Thumb1',
    };
    for (const [d, name] of target.names.entries()) {
        if (aliases[name]) match.map[d] = source.names.indexOf(aliases[name]);
    }
    // Fingers use two unnamed descendants on this rig. Pair descendants only after their named base is matched.
    for (const d of target.order) {
        if (match.map[d] >= 0 || target.parents[d] < 0) continue;
        const parent = target.parents[d];
        const sp = match.map[parent];
        const children = source.parents.flatMap((p, i) =>
            p === sp ? [i] : [],
        );
        if (sp >= 0 && children.length === 1) match.map[d] = children[0];
    }
    for (const name of [
        'torso',
        'chest',
        'Rthigh',
        'Rcalf',
        'Rfoot',
        'Lthigh',
        'Lcalf',
        'Lfoot',
    ]) {
        const d = target.names.indexOf(name);
        if (d < 0 || match.map[d] < 0)
            throw new Error(`missing essential bone ${model.id}:${name}`);
    }
    // Held items attach to Rweapon / Lweapon and their models are Rynn's, authored on her weapon frames. NPC weapon
    // joints sit 55-150 degrees off hers at bind, so a bind-relative delta turned every held weapon by that much:
    // give these leaf joints Rynn's exact model-space orientation instead.
    const worldJoints = target.names.flatMap((name, d) =>
        (name === 'Rweapon' || name === 'Lweapon') &&
        match.map[d] >= 0 &&
        !target.parents.includes(d)
            ? [d]
            : [],
    );
    if (worldJoints.length !== 2)
        throw new Error(`weapon joints not matched on ${model.id}`);
    const scale = rigSize(target) / rigSize(source);
    let moving = 0;
    for (const [clipIndex, clip] of manifest.clips.entries()) {
        const original = parsePs2Anim(
            fs.readFileSync(path.join(scratch, `${clip.id}.info`)),
            fs.readFileSync(path.join(scratch, `${clip.id}.data`)),
        );
        const anim = retargetPs2Anim(original, source, target, match, {
            name: `MP_${model.id}_${original.name}`,
            channels: sk.channels.length,
            scale,
            inPlace: true,
            worldJoints,
        });
        if (
            !anim.tracks.every((t) =>
                t.keys.every((k) =>
                    [...k.q, ...(k.pos ?? [])].every(Number.isFinite),
                ),
            )
        )
            throw new Error('non-finite retarget');
        const id = 0x8000 + modelIndex * 256 + clipIndex;
        if (manifest.taken.includes(id))
            throw new Error(`animation id collision ${id}`);
        const encoded = writePs2Anim(anim);
        emit('new/NPCS.ADB', 0x501, id, 0, encoded.info);
        emit('new/NPCS.ADB', 0x502, id, 0, encoded.data);
        addedIds.push(id);
        if (anim.tracks.some((t) => t.keys.length > 1)) moving++;
    }
    emit(
        'overrides/NPCS.MOD',
        0x208,
        model.id,
        model.sub,
        appendAnimationIds(originalSkeleton, addedIds),
    );
    const channels = sk.channels.map((channel) => {
        let d = channel.joint;
        while (d >= 0 && match.map[d] < 0) d = target.parents[d];
        let s = d >= 0 ? match.map[d] : 0;
        while (s >= 0) {
            const index = sourceSkeleton.channels.findIndex(
                (c) => c.joint === s,
            );
            if (index >= 0) return index;
            s = source.parents[s];
        }
        return 0;
    });
    report.push({
        channels,
        model: model.id,
        joints: target.jointCount,
        matched: match.map.filter((i) => i >= 0).length,
        clips: manifest.clips.length,
        moving,
    });
}
fs.writeFileSync(
    path.join(output, 'motion-report.json'),
    JSON.stringify(report, null, 2),
);
console.log(JSON.stringify(report));

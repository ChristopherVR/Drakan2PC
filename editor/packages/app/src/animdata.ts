// Worker-side data for skinned playback: a model's rig and clip list, single clips, and which model a class uses.
// Plain data out (typed arrays and arrays), no three.js. Format notes: docs/reference/level-format.md section 11 / 12b.

import type { Iso9660 } from '@drakan/formats';
import {
    ADB_ANIMATION,
    ADB_KEYS,
    MOD_CHARACTER,
    parseClip,
    parseClipHeader,
    parseSkeleton,
    rigFromCharacterRecord,
    type Ps2Anim,
    type Rig,
} from '@drakan/formats';
import type { ModelData } from './scene/decode.ts';
import { depsOf, discFor, loadRefModel, reader } from './refdata.ts';

export interface ClipInfo {
    id: number;
    name: string;
    /** Seconds. */
    duration: number;
    looping: boolean;
    /** Track count; the clip fits a model whose rig has this many joints. */
    tracks: number;
}

export interface RefRig {
    stem: string;
    id: number;
    data: ModelData;
    /** Null when the model has no 0x0208 record (static) or it does not decode. */
    rig: Rig | null;
    /** The clips the model's 0x0208 record lists, resolved in the ADB of the same stem. */
    clips: ClipInfo[];
}

const rigCache = new Map<string, Promise<RefRig | null>>();

/** Model, rig and clip list of model `id` in MOD container `stem`. */
export function loadRefRig(iso: Iso9660, language: string, stem: string, id: number): Promise<RefRig | null> {
    const key = `${language}:${stem}:${id}`;
    let p = rigCache.get(key);
    if (!p) {
        p = build(iso, language, stem, id).catch(() => null);
        rigCache.set(key, p);
        if (rigCache.size > 16) rigCache.delete(rigCache.keys().next().value!);
    }
    return p;
}

async function build(iso: Iso9660, language: string, stem: string, id: number): Promise<RefRig | null> {
    const data = await loadRefModel(iso, language, stem, id);
    if (!data?.model) return null;
    const d = await discFor(iso, language);
    const mod = await reader(d, stem, 'MOD');
    const entry = mod?.find(MOD_CHARACTER, id);
    const rig = mod && entry ? rigFromCharacterRecord(await mod.readRaw(entry), data.model) : null;
    const clips: ClipInfo[] = [];
    if (rig) {
        const adb = await reader(d, stem, 'ADB');
        for (const cid of rig.animIds) {
            const e = adb?.find(ADB_ANIMATION, cid);
            if (!adb || !e) continue;
            const h = parseClipHeader(await adb.readRaw(e));
            clips.push({ id: cid, name: h.name, duration: h.duration, looping: (h.flags & 1) === 0, tracks: h.tracks });
        }
    }
    return { stem, id, data, rig, clips };
}

/** One clip of an ADB container; null when the entry is missing or does not decode. */
export async function loadRefClip(iso: Iso9660, language: string, stem: string, id: number): Promise<Ps2Anim | null> {
    const d = await discFor(iso, language);
    const adb = await reader(d, stem, 'ADB');
    const info = adb?.find(ADB_ANIMATION, id);
    const keys = adb?.find(ADB_KEYS, id);
    if (!adb || !info || !keys) return null;
    try {
        return parseClip(await adb.readRaw(info), await adb.readRaw(keys));
    } catch {
        return null;
    }
}

/** The model (container stem and id) of class `classId` in ODB `dbStem` (ODB 0x0102: `u16 group, u16 model_id, u16 model_db` after the name). */
export async function classModelRef(
    iso: Iso9660,
    language: string,
    dbStem: string,
    classId: number,
): Promise<{ stem: string; id: number } | null> {
    const d = await discFor(iso, language);
    const odb = await reader(d, dbStem, 'ODB');
    const rec = odb?.entries.find((x) => x.type === 0x0102 && x.id === classId);
    if (!odb || !rec) return null;
    const raw = await odb.readRaw(rec);
    const at = 2 + (raw[0]! | (raw[1]! << 8));
    if (at + 6 > raw.length) return null;
    const id = raw[at + 2]! | (raw[at + 3]! << 8);
    const db = raw[at + 4]! | (raw[at + 5]! << 8);
    if (id === 0) return null;
    const stem = db === 0 ? dbStem : (await depsOf(d, dbStem)).get(db);
    return stem ? { stem, id } : null;
}

/** First skinned model of `stem` whose rig has `tracks` joints (a stand-in when a class has no model of its own). */
export async function rigForTracks(
    iso: Iso9660,
    language: string,
    stem: string,
    tracks: number,
): Promise<{ stem: string; id: number } | null> {
    const d = await discFor(iso, language);
    const mod = await reader(d, stem, 'MOD');
    if (!mod) return null;
    for (const e of mod.entries) {
        if (e.type !== MOD_CHARACTER) continue;
        try {
            if (parseSkeleton(await mod.readRaw(e), 'ps2').joints.length === tracks) return { stem, id: e.id };
        } catch {
            /* skip an undecodable record */
        }
    }
    return null;
}

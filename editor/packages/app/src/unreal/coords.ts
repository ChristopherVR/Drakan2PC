// Coordinate and unit conversion for the Unreal export. Pure maths, no three.js and no browser APIs.
//
// Three spaces are involved (documented in docs/reference/unreal-export.md):
//
//  * Game / editor space: y up, game units. One game unit is 1 mm of "scene" (SCENE_SCALE 0.001 scene units per game
//    unit, so a terrain cell of 2048 units is 2.048 m). One model unit is one cell (2048 game units).
//  * glTF space (what the .glb stores): the editor's scene space, unchanged: y up, right handed, metres. A game unit
//    is 0.001 m and a model unit is 2.048 m. No axis change is applied, so a loaded glb looks exactly like the editor.
//  * Unreal space: Z up, left handed, centimetres. Unreal's glTF import maps glTF (x, y, z) to Unreal (X, Y, Z) =
//    (x, z, y) * 100 (an axis swap, determinant -1, so it also fixes the handedness), and this exporter assumes it.
//    The `ue` block in each node's extras is that same mapping applied to the instance transform, so the import
//    script can place actors without depending on how the importer converts node transforms.

import { SCENE_SCALE } from '../scene/terrain.ts';

/** Metres per game unit in the glb. */
export const GAME_UNIT_M = SCENE_SCALE;
/** Game units per model unit (one terrain cell). */
export const MODEL_UNIT_GAME = 2048;
/** Metres per model unit in the glb. */
export const MODEL_UNIT_M = MODEL_UNIT_GAME * GAME_UNIT_M;
/** Unreal centimetres per metre. */
export const UE_CM_PER_M = 100;
/** Unreal centimetres per game unit (0.1: a 2048 unit cell is 204.8 cm). */
export const UE_CM_PER_GAME_UNIT = GAME_UNIT_M * UE_CM_PER_M;

export type Vec3 = readonly [number, number, number];
export type Quat = readonly [number, number, number, number];

/**
 * The 3x3 matrix (row-major) taking glTF axes to Unreal axes: (x, y, z) -> (x, z, y). It is its own inverse.
 * Included so tests and the Python script share one definition of the conversion.
 */
export const UE_FROM_GLTF: readonly number[] = [1, 0, 0, 0, 0, 1, 0, 1, 0];

/** Game-unit position (y up) to glTF metres. */
export const gamePosToGltf = (p: Vec3): [number, number, number] => [
    p[0] * GAME_UNIT_M,
    p[1] * GAME_UNIT_M,
    p[2] * GAME_UNIT_M,
];

/** glTF metres position to Unreal centimetres. */
export const gltfPosToUe = (p: Vec3): [number, number, number] => [
    p[0] * UE_CM_PER_M,
    p[2] * UE_CM_PER_M,
    p[1] * UE_CM_PER_M,
];

/** Game-unit position to Unreal centimetres. */
export const gamePosToUe = (p: Vec3): [number, number, number] => gltfPosToUe(gamePosToGltf(p));

/**
 * glTF quaternion (x, y, z, w) to the Unreal quaternion of the same physical rotation. The axis swap has determinant
 * -1, so the rotation axis is swapped and negated: (x, y, z, w) -> (-x, -z, -y, w). Feed it to `unreal.Quat`.
 */
export const gltfQuatToUe = (q: Quat): [number, number, number, number] => [-q[0], -q[2], -q[1], q[3]];

/** glTF per-axis scale to Unreal (the local up axis becomes Z). */
export const gltfScaleToUe = (s: Vec3): [number, number, number] => [s[0], s[2], s[1]];

/** Quaternion of an Euler rotation in YXZ order (radians), the order the editor applies object rotations in. */
export function quatFromEulerYXZ(x: number, y: number, z: number): [number, number, number, number] {
    const c1 = Math.cos(x / 2);
    const c2 = Math.cos(y / 2);
    const c3 = Math.cos(z / 2);
    const s1 = Math.sin(x / 2);
    const s2 = Math.sin(y / 2);
    const s3 = Math.sin(z / 2);
    return [
        s1 * c2 * c3 + c1 * s2 * s3,
        c1 * s2 * c3 - s1 * c2 * s3,
        c1 * c2 * s3 - s1 * s2 * c3,
        c1 * c2 * c3 + s1 * s2 * s3,
    ];
}

/**
 * Rotation of an LVL object as stored: degrees, yaw = rot[1] - 90 (level-format.md section 3), applied in YXZ order.
 * Result is the glTF-space quaternion (identical to the editor's `instanceMatrix`).
 */
export function lvlRotToQuat(rotDeg: Vec3): [number, number, number, number] {
    const d = Math.PI / 180;
    return quatFromEulerYXZ(rotDeg[0] * d, (rotDeg[1] - 90) * d, rotDeg[2] * d);
}

/** Rotation of an editor document object (radians, yaw already minus 90 degrees), YXZ. */
export const docRotToQuat = (rot: Vec3): [number, number, number, number] => quatFromEulerYXZ(rot[0], rot[1], rot[2]);

/** Rotate a vector by a unit quaternion (x, y, z, w). */
export function rotateByQuat(q: Quat, v: Vec3): [number, number, number] {
    const [x, y, z, w] = q;
    // v' = v + 2 w (u x v) + 2 u x (u x v), u = (x, y, z)
    const tx = 2 * (y * v[2] - z * v[1]);
    const ty = 2 * (z * v[0] - x * v[2]);
    const tz = 2 * (x * v[1] - y * v[0]);
    return [v[0] + w * tx + (y * tz - z * ty), v[1] + w * ty + (z * tx - x * tz), v[2] + w * tz + (x * ty - y * tx)];
}

/** The Unreal transform of an instance placed in glTF space. */
export interface UeTransform {
    /** Centimetres. */
    location: [number, number, number];
    /** Unreal quaternion (x, y, z, w), for `unreal.Quat(x, y, z, w)`. */
    quat: [number, number, number, number];
    scale: [number, number, number];
}

export function gltfTransformToUe(t: { translation: Vec3; rotation: Quat; scale?: Vec3 }): UeTransform {
    return {
        location: gltfPosToUe(t.translation),
        quat: gltfQuatToUe(t.rotation),
        scale: gltfScaleToUe(t.scale ?? [1, 1, 1]),
    };
}

/** Joint-local row-vector rotation matrix (3x3, 9 floats, `Mat12` layout) as a glTF quaternion; translation is separate. */
export { mat12ToQuat } from '@drakan/formats';

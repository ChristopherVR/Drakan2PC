import * as THREE from 'three';
import { SCENE_SCALE, type TerrainScene } from './terrain.ts';

/** Ground height in game units under (x, z) game units, or NaN. `nearY` selects among stacked layers. */
export function groundHeightAt(terrain: TerrainScene, x: number, z: number, nearY?: number): number {
    return terrain.heightAt(x, z, nearY);
}

const ray = new THREE.Raycaster();
const ndc = new THREE.Vector2();

export interface SceneHit {
    /** Game units. */
    point: [number, number, number];
    distance: number;
    object: THREE.Object3D;
    /** InstancedMesh instance index, when the hit object is instanced. */
    instanceId?: number;
}

/**
 * Raycast from the camera through a normalised-device-coordinate point (`x`, `y` in -1..1) against `roots`
 * (terrain group, model groups). Nearest hit first.
 */
export function raycastScene(camera: THREE.Camera, x: number, y: number, roots: THREE.Object3D[]): SceneHit | null {
    ndc.set(x, y);
    ray.setFromCamera(ndc, camera);
    const hit = ray.intersectObjects(roots, true)[0];
    if (!hit) return null;
    return {
        point: [hit.point.x / SCENE_SCALE, hit.point.y / SCENE_SCALE, hit.point.z / SCENE_SCALE],
        distance: hit.distance / SCENE_SCALE,
        object: hit.object,
        instanceId: hit.instanceId,
    };
}

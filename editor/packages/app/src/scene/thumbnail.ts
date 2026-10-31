import * as THREE from 'three';
import type { Model } from '@drakan/formats';
import { buildModel } from './models.ts';
import type { TextureLookup } from './textures.ts';

let renderer: THREE.WebGLRenderer | null = null;
const cache = new Map<string, string>();

/**
 * Small PNG data URL of a model for the asset browser, rendered with a shared offscreen renderer from a fixed
 * three-quarter view; cached per `key` (class id or model key). Returns '' if WebGL is unavailable.
 */
export function classThumbnail(
    key: string,
    model: Model,
    lookup: TextureLookup,
    size = 96,
    geometryKey = `thumb:${key}`,
): string {
    const ck = `${key}@${size}`;
    const hit = cache.get(ck);
    if (hit !== undefined) return hit;
    let url = '';
    try {
        renderer ??= new THREE.WebGLRenderer({ antialias: true, alpha: true, preserveDrawingBuffer: true });
        renderer.setSize(size, size, false);
        const scene = new THREE.Scene();
        scene.add(new THREE.HemisphereLight(0xffffff, 0x445055, 1.6));
        const sun = new THREE.DirectionalLight(0xffffff, 1.4);
        sun.position.set(1, 2, 1.5);
        scene.add(sun);
        const mesh = buildModel(model, lookup, geometryKey);
        scene.add(mesh);
        const sphere = new THREE.Box3().setFromObject(mesh).getBoundingSphere(new THREE.Sphere());
        const cam = new THREE.PerspectiveCamera(35, 1, 0.01, 1000);
        const d = (sphere.radius || 1) / Math.sin((cam.fov * Math.PI) / 360);
        cam.position.copy(sphere.center).add(new THREE.Vector3(0.7, 0.5, 1).normalize().multiplyScalar(d));
        cam.lookAt(sphere.center);
        renderer.setClearColor(0x000000, 0);
        renderer.render(scene, cam);
        url = renderer.domElement.toDataURL('image/png');
    } catch {
        url = '';
    }
    cache.set(ck, url);
    return url;
}

export function clearThumbnails(): void {
    cache.clear();
}

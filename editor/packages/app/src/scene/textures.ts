import * as THREE from 'three';
import type { SceneTexture } from './decode.ts';

/** Looks a stored texture reference (`id | db << 16`) up; null when unknown (the builders then use a flat colour). */
export type TextureLookup = (ref: number) => THREE.Texture | null;

/**
 * Texture cache: one THREE.DataTexture per distinct texture, created once. Rows are stored bottom-up (GL order), so
 * `flipY` stays false. Repeat wrapping because model UVs may leave 0..1.
 */
export class TextureCache {
    private readonly byKey = new Map<string, THREE.DataTexture>();

    /** Add decoded textures under a namespace (e.g. 'terrain' or a model container); returns a lookup for that set. */
    add(namespace: string, textures: readonly SceneTexture[]): TextureLookup {
        for (const t of textures) {
            const key = `${namespace}:${t.ref}`;
            if (this.byKey.has(key)) continue;
            const data = new Uint8Array(t.rgba.buffer as ArrayBuffer, t.rgba.byteOffset, t.rgba.byteLength);
            const tex = new THREE.DataTexture(data, t.width, t.height, THREE.RGBAFormat);
            tex.colorSpace = THREE.SRGBColorSpace;
            tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
            tex.magFilter = THREE.LinearFilter;
            tex.minFilter = THREE.LinearMipmapLinearFilter;
            tex.generateMipmaps = true;
            tex.flipY = false;
            tex.needsUpdate = true;
            this.byKey.set(key, tex);
        }
        return (ref) => this.byKey.get(`${namespace}:${ref}`) ?? null;
    }

    get size(): number {
        return this.byKey.size;
    }

    dispose(): void {
        for (const t of this.byKey.values()) t.dispose();
        this.byKey.clear();
    }
}

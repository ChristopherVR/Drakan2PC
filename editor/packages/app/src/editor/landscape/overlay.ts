// Viewport drawing for Landscape mode: the brush circle laid on the ground, layer outlines and previews (ramp line,
// expand strip, new layer footprint). three.js lines drawn on top of the terrain.

import * as THREE from 'three';
import { CELL_SIZE, terrainHeightAt, type TerrainLayer } from '@drakan/formats';
import { SCENE_SCALE } from '../../scene/terrain.ts';

const S = SCENE_SCALE;
const RING = 96;
/** Lines float a little above the surface so they do not flicker into it. */
const LIFT = 24;

function lineMat(color: number, opacity: number): THREE.LineBasicMaterial {
    return new THREE.LineBasicMaterial({ color, transparent: true, opacity, depthTest: false, depthWrite: false });
}

function loop(color: number, opacity: number): THREE.LineLoop {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(RING * 3), 3));
    const l = new THREE.LineLoop(g, lineMat(color, opacity));
    l.frustumCulled = false;
    l.renderOrder = 20;
    l.visible = false;
    return l;
}

function segments(color: number, opacity: number, order = 19): THREE.LineSegments {
    const l = new THREE.LineSegments(new THREE.BufferGeometry(), lineMat(color, opacity));
    l.frustumCulled = false;
    l.renderOrder = order;
    l.visible = false;
    return l;
}

/** Perimeter of a layer as segments that follow its edge heights (world units in, scene units out). */
function perimeter(L: TerrainLayer, out: number[]): void {
    const s = L.width + 1;
    const pt = (vx: number, vz: number): [number, number, number] => [
        (L.originX + vx) * CELL_SIZE * S,
        (L.heights[vz * s + vx]! + LIFT) * S,
        (L.originZ + vz) * CELL_SIZE * S,
    ];
    const seg = (a: [number, number, number], b: [number, number, number]): void => void out.push(...a, ...b);
    for (let vx = 0; vx < L.width; vx++) {
        seg(pt(vx, 0), pt(vx + 1, 0));
        seg(pt(vx, L.height), pt(vx + 1, L.height));
    }
    for (let vz = 0; vz < L.height; vz++) {
        seg(pt(0, vz), pt(0, vz + 1));
        seg(pt(L.width, vz), pt(L.width, vz + 1));
    }
}

/** A flat rectangle (cell units) at height y (world units). */
function rect(x0: number, z0: number, x1: number, z1: number, y: number, out: number[]): void {
    const X0 = x0 * CELL_SIZE * S;
    const Z0 = z0 * CELL_SIZE * S;
    const X1 = x1 * CELL_SIZE * S;
    const Z1 = z1 * CELL_SIZE * S;
    const Y = (y + LIFT) * S;
    out.push(X0, Y, Z0, X1, Y, Z0, X1, Y, Z0, X1, Y, Z1, X1, Y, Z1, X0, Y, Z1, X0, Y, Z1, X0, Y, Z0);
}

const setSegs = (l: THREE.LineSegments, pts: number[]): void => {
    l.geometry.dispose();
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(pts, 3));
    l.geometry = g;
    l.visible = pts.length > 0;
};

export class LandscapeOverlay {
    readonly group = new THREE.Group();
    private readonly outer = loop(0xc6ff7a, 1);
    private readonly inner = loop(0xc6ff7a, 0.5);
    private readonly all = segments(0x9cc77a, 0.22, 18);
    private readonly active = segments(0xc8f08e, 1, 21);
    private readonly hover = segments(0xffffff, 0.55, 20);
    private readonly preview = segments(0xffc46b, 1, 22);

    constructor() {
        this.group.name = 'landscape-overlay';
        this.group.add(this.all, this.hover, this.active, this.outer, this.inner, this.preview);
    }

    /** Brush rings at (x, z) laid on the terrain surface nearest `y`. */
    brush(
        layers: readonly TerrainLayer[],
        x: number,
        y: number,
        z: number,
        radius: number,
        falloff: number,
        warm: boolean,
    ): void {
        const color = warm ? 0xffa040 : 0xc6ff7a;
        (this.outer.material as THREE.LineBasicMaterial).color.setHex(color);
        (this.inner.material as THREE.LineBasicMaterial).color.setHex(color);
        this.ring(this.outer, layers, x, y, z, radius);
        const ir = radius * (1 - falloff);
        if (falloff > 0.02 && ir > 1) this.ring(this.inner, layers, x, y, z, ir);
        else this.inner.visible = false;
    }

    private ring(l: THREE.LineLoop, layers: readonly TerrainLayer[], x: number, y: number, z: number, r: number): void {
        const a = l.geometry.getAttribute('position') as THREE.BufferAttribute;
        let prev = y;
        for (let i = 0; i < RING; i++) {
            const t = (i / RING) * Math.PI * 2;
            const px = x + Math.cos(t) * r;
            const pz = z + Math.sin(t) * r;
            const h = terrainHeightAt(layers, px, pz, prev);
            if (Number.isFinite(h)) prev = h;
            a.setXYZ(i, px * S, (Number.isFinite(h) ? h : prev) * S + LIFT * S, pz * S);
        }
        a.needsUpdate = true;
        l.geometry.computeBoundingSphere();
        l.visible = true;
    }

    hideBrush(): void {
        this.outer.visible = false;
        this.inner.visible = false;
    }

    /** Faint outline of every layer (null hides them). */
    outlines(layers: readonly TerrainLayer[] | null): void {
        const pts: number[] = [];
        if (layers) for (const L of layers) perimeter(L, pts);
        setSegs(this.all, pts);
    }

    activeLayer(L: TerrainLayer | null): void {
        const pts: number[] = [];
        if (L) perimeter(L, pts);
        setSegs(this.active, pts);
    }

    hoverLayer(L: TerrainLayer | null): void {
        const pts: number[] = [];
        if (L) perimeter(L, pts);
        setSegs(this.hover, pts);
    }

    /** Preview: a straight ramp line between two ground points (world units). */
    rampLine(a: [number, number, number] | null, b: [number, number, number] | null, width: number): void {
        if (!a || !b) return setSegs(this.preview, []);
        const dx = b[0] - a[0];
        const dz = b[2] - a[2];
        const len = Math.hypot(dx, dz) || 1;
        const nx = (-dz / len) * (width / 2);
        const nz = (dx / len) * (width / 2);
        const p = (x: number, y: number, z: number): number[] => [x * S, (y + LIFT) * S, z * S];
        setSegs(this.preview, [
            ...p(a[0], a[1], a[2]),
            ...p(b[0], b[1], b[2]),
            ...p(a[0] + nx, a[1], a[2] + nz),
            ...p(b[0] + nx, b[1], b[2] + nz),
            ...p(a[0] - nx, a[1], a[2] - nz),
            ...p(b[0] - nx, b[1], b[2] - nz),
        ]);
    }

    /** Preview: a cell rectangle (cell units) at height y, e.g. the strip an expand adds or a new layer. */
    previewRect(r: { x0: number; z0: number; x1: number; z1: number; y: number } | null): void {
        const pts: number[] = [];
        if (r) rect(r.x0, r.z0, r.x1, r.z1, r.y, pts);
        setSegs(this.preview, pts);
    }

    clearPreview(): void {
        setSegs(this.preview, []);
    }

    hideAll(): void {
        this.hideBrush();
        for (const l of [this.all, this.active, this.hover, this.preview]) setSegs(l, []);
    }

    dispose(): void {
        this.hideAll();
        for (const l of [this.outer, this.inner, this.all, this.active, this.hover, this.preview]) {
            l.geometry.dispose();
            (l.material as THREE.Material).dispose();
        }
    }
}

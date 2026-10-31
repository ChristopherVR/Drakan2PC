// Editor icons: screen-facing badges for helper classes (triggers, sounds, cameras, lights, waypoints, timers, spawns)
// instead of their placeholder models (textured boxes). One InstancedMesh and one draw call for all of them; the size is
// constant on screen (with a slight shrink and fade with distance), hover and selection recolour the rim.

import * as THREE from 'three';
import { ICON_ORDER, type HelperGroup } from './instances.ts';

export interface IconItem {
    id: number;
    /** Scene units. */
    pos: readonly [number, number, number];
    group: HelperGroup;
}

/** Atlas layout: 4 columns, 2 rows of 64 px cells, in `ICON_ORDER`. */
const COLS = 4;
const ROWS = 2;
const CELL = 64;

/** Token behind each kind's colour (tokens.css) and a fallback for when the stylesheet is not there (tests). */
const TINTS: Record<HelperGroup, [string, string]> = {
    trigger: ['--warn', '#e8b04a'],
    sound: ['--accent', '#3d8bfd'],
    camera: ['--ok', '#4cc38a'],
    light: ['--icon-light', '#f0d264'],
    waypoint: ['--fg-muted', '#a3a3ad'],
    timer: ['--axis-x', '#f06a62'],
    spawn: ['--play', '#3fb950'],
    barrier: ['--icon-barrier', '#b58cff'],
};

export function iconTint(g: HelperGroup): THREE.Color {
    let css = '';
    try {
        css = getComputedStyle(document.documentElement).getPropertyValue(TINTS[g][0]).trim();
    } catch {
        /* no DOM */
    }
    return new THREE.Color(css || TINTS[g][1]);
}

type Glyph = (c: CanvasRenderingContext2D) => void;

const poly = (c: CanvasRenderingContext2D, pts: number[]): void => {
    c.beginPath();
    c.moveTo(pts[0]!, pts[1]!);
    for (let i = 2; i < pts.length; i += 2) c.lineTo(pts[i]!, pts[i + 1]!);
    c.closePath();
    c.fill();
};

// Drawn in a 64 px cell centred on (32, 32), solid green: the shader turns green into white.
const GLYPHS: Record<HelperGroup, Glyph> = {
    trigger: (c) => poly(c, [36, 12, 20, 36, 30, 36, 26, 53, 44, 28, 33, 28]),
    sound: (c) => {
        poly(c, [14, 26, 24, 26, 34, 17, 34, 47, 24, 38, 14, 38]);
        c.lineWidth = 3.5;
        c.lineCap = 'round';
        c.beginPath();
        c.arc(34, 32, 9, -0.9, 0.9);
        c.stroke();
        c.beginPath();
        c.arc(34, 32, 16, -0.9, 0.9);
        c.stroke();
    },
    camera: (c) => {
        c.beginPath();
        c.roundRect(13, 22, 26, 21, 4);
        c.fill();
        poly(c, [42, 29, 52, 22, 52, 43, 42, 36]);
    },
    light: (c) => {
        c.beginPath();
        c.arc(32, 28, 12, 0, Math.PI * 2);
        c.fill();
        c.beginPath();
        c.roundRect(25, 41, 14, 4, 2);
        c.fill();
        c.beginPath();
        c.roundRect(27, 47, 10, 3.5, 2);
        c.fill();
    },
    waypoint: (c) => {
        c.beginPath();
        c.arc(32, 27, 12, 0, Math.PI * 2);
        c.fill();
        poly(c, [21, 33, 43, 33, 32, 52]);
        c.globalCompositeOperation = 'destination-out';
        c.beginPath();
        c.arc(32, 27, 5, 0, Math.PI * 2);
        c.fill();
        c.globalCompositeOperation = 'source-over';
    },
    timer: (c) => {
        c.lineWidth = 4;
        c.lineCap = 'round';
        c.beginPath();
        c.arc(32, 34, 15, 0, Math.PI * 2);
        c.stroke();
        c.beginPath();
        c.moveTo(32, 34);
        c.lineTo(32, 24);
        c.moveTo(32, 34);
        c.lineTo(39, 38);
        c.stroke();
        c.fillRect(28, 12, 8, 4);
    },
    barrier: (c) => {
        poly(c, [32, 12, 49, 20, 49, 36, 32, 53, 15, 36, 15, 20]);
        c.globalCompositeOperation = 'destination-out';
        poly(c, [32, 20, 42, 25, 42, 34, 32, 44, 22, 34, 22, 25]);
        c.globalCompositeOperation = 'source-over';
    },
    spawn: (c) => {
        c.beginPath();
        c.arc(32, 22, 8, 0, Math.PI * 2);
        c.fill();
        c.beginPath();
        c.moveTo(16, 50);
        c.quadraticCurveTo(32, 20, 48, 50);
        c.closePath();
        c.fill();
    },
};

function makeAtlas(): THREE.CanvasTexture {
    const cv = document.createElement('canvas');
    cv.width = COLS * CELL;
    cv.height = ROWS * CELL;
    const c = cv.getContext('2d')!;
    ICON_ORDER.forEach((g, i) => {
        c.save();
        c.translate((i % COLS) * CELL, Math.floor(i / COLS) * CELL);
        c.fillStyle = '#ff0000'; // disc body
        c.beginPath();
        c.arc(32, 32, 29, 0, Math.PI * 2);
        c.fill();
        c.strokeStyle = '#0000ff'; // rim
        c.lineWidth = 4;
        c.beginPath();
        c.arc(32, 32, 27, 0, Math.PI * 2);
        c.stroke();
        c.fillStyle = c.strokeStyle = '#00ff00'; // glyph
        GLYPHS[g](c);
        c.restore();
    });
    const t = new THREE.CanvasTexture(cv);
    t.colorSpace = THREE.NoColorSpace;
    t.generateMipmaps = true;
    t.minFilter = THREE.LinearMipmapLinearFilter;
    t.anisotropy = 4;
    return t;
}

const VERT = /* glsl */ `
attribute float aIcon;
attribute float aState;
attribute vec3 aTint;
uniform float uPx;
uniform float uViewH;
uniform float uNear;
uniform float uFar;
varying vec2 vUv;
varying float vState;
varying float vFade;
varying vec3 vTint;
void main() {
    vec4 c = modelViewMatrix * instanceMatrix * vec4(0.0, 0.0, 0.0, 1.0);
    float d = max(-c.z, 0.001);
    float t = clamp((d - uNear) / (uFar - uNear), 0.0, 1.0);
    float px = uPx * mix(1.0, 0.62, t) * (1.0 + 0.18 * aState);
    float size = px * 2.0 * d / (projectionMatrix[1][1] * uViewH);
    c.xy += position.xy * size;
    gl_Position = projectionMatrix * c;
    vec2 cell = vec2(mod(aIcon, ${COLS}.0), floor(aIcon / ${COLS}.0));
    vUv = vec2((cell.x + uv.x) / ${COLS}.0, 1.0 - (cell.y + 1.0 - uv.y) / ${ROWS}.0);
    vState = aState;
    vFade = mix(1.0, 0.4, t);
    vTint = aTint;
}`;

const FRAG = /* glsl */ `
uniform sampler2D uAtlas;
uniform vec3 uSel;
uniform vec3 uHot;
varying vec2 vUv;
varying float vState;
varying float vFade;
varying vec3 vTint;
void main() {
    vec4 t = texture2D(uAtlas, vUv);
    if (t.a < 0.02) discard;
    vec3 rim = vState > 1.5 ? uSel : (vState > 0.5 ? uHot : mix(vTint, vec3(1.0), 0.45));
    vec3 body = vTint * (vState > 0.5 ? 1.0 : 0.82);
    vec3 col = mix(body, rim, t.b);
    col = mix(col, vec3(1.0), t.g);
    float a = t.a * (vState > 0.5 ? 1.0 : vFade);
    gl_FragColor = vec4(col, a);
}`;

const tmpSize = new THREE.Vector2();

/** Kinds in `ICON_ORDER` index the atlas. */
export class IconLayer {
    readonly group = new THREE.Group();
    private mesh: THREE.InstancedMesh | null = null;
    private ids: number[] = [];
    private readonly material: THREE.ShaderMaterial;
    private readonly geometry = new THREE.PlaneGeometry(1, 1);
    private readonly atlas = makeAtlas();
    private state: THREE.InstancedBufferAttribute | null = null;
    private count_ = 0;
    private hover: number | null = null;
    private selected: ReadonlySet<number> = new Set();
    /** Icon size in CSS pixels. */
    sizePx = 26;

    constructor() {
        this.group.name = 'editor-icons';
        this.material = new THREE.ShaderMaterial({
            vertexShader: VERT,
            fragmentShader: FRAG,
            transparent: true,
            depthTest: false,
            depthWrite: false,
            side: THREE.DoubleSide,
            uniforms: {
                uAtlas: { value: this.atlas },
                uPx: { value: 26 },
                uViewH: { value: 900 },
                uNear: { value: 40 },
                uFar: { value: 900 },
                uSel: { value: new THREE.Color(0xffc83d) },
                uHot: { value: new THREE.Color(0x8fd3ff) },
            },
        });
    }

    /** Scene-space distance range over which icons shrink and fade. */
    setFade(near: number, far: number): void {
        this.material.uniforms.uNear!.value = near;
        this.material.uniforms.uFar!.value = Math.max(far, near + 1);
    }

    set(items: readonly IconItem[]): void {
        if (this.mesh) {
            this.group.remove(this.mesh);
            this.mesh.geometry.dispose();
            this.mesh.dispose();
            this.mesh = null;
        }
        this.ids = items.map((i) => i.id);
        this.count_ = items.length;
        if (!items.length) return;
        const geo = this.geometry.clone();
        const icon = new Float32Array(items.length);
        const tint = new Float32Array(items.length * 3);
        const tints = new Map<HelperGroup, THREE.Color>();
        const m = new THREE.Matrix4();
        const mesh = new THREE.InstancedMesh(geo, this.material, items.length);
        items.forEach((it, i) => {
            icon[i] = ICON_ORDER.indexOf(it.group);
            let c = tints.get(it.group);
            if (!c) tints.set(it.group, (c = iconTint(it.group)));
            tint.set([c.r, c.g, c.b], i * 3);
            mesh.setMatrixAt(i, m.setPosition(it.pos[0], it.pos[1], it.pos[2]));
        });
        mesh.instanceMatrix.needsUpdate = true;
        geo.setAttribute('aIcon', new THREE.InstancedBufferAttribute(icon, 1));
        geo.setAttribute('aTint', new THREE.InstancedBufferAttribute(tint, 3));
        this.state = new THREE.InstancedBufferAttribute(new Float32Array(items.length), 1);
        this.state.setUsage(THREE.DynamicDrawUsage);
        geo.setAttribute('aState', this.state);
        mesh.frustumCulled = false;
        mesh.renderOrder = 9;
        mesh.onBeforeRender = (r) => {
            const u = this.material.uniforms;
            const size = r.getDrawingBufferSize(tmpSize);
            u.uViewH!.value = size.y;
            u.uPx!.value = this.sizePx * r.getPixelRatio();
        };
        this.mesh = mesh;
        this.group.add(mesh);
        this.applyState();
    }

    setHover(id: number | null): void {
        if (id === this.hover) return;
        this.hover = id;
        this.applyState();
    }

    setSelection(ids: ReadonlySet<number>): void {
        this.selected = ids;
        this.applyState();
    }

    has(id: number): boolean {
        return this.ids.includes(id);
    }

    private applyState(): void {
        if (!this.state) return;
        const a = this.state.array as Float32Array;
        for (let i = 0; i < this.ids.length; i++) {
            const id = this.ids[i]!;
            a[i] = this.selected.has(id) ? 2 : id === this.hover ? 1 : 0;
        }
        this.state.needsUpdate = true;
    }

    get count(): number {
        return this.count_;
    }

    dispose(): void {
        this.set([]);
        this.material.dispose();
        this.geometry.dispose();
        this.atlas.dispose();
    }
}

export interface VolumeBox {
    /** Model space to scene (translation, rotation, scale). */
    matrix: THREE.Matrix4;
    min: readonly number[];
    max: readonly number[];
    group: HelperGroup;
    /** Selected (strong) or only hovered (faint). */
    strong: boolean;
}

/** Translucent boxes with a wire edge for the volume of a trigger or barrier icon (the box its model would be). */
export class VolumeBoxes {
    readonly group = new THREE.Group();
    private readonly unit = new THREE.BoxGeometry(1, 1, 1);
    private readonly edges = new THREE.EdgesGeometry(this.unit);

    constructor() {
        this.group.name = 'icon-volumes';
        this.group.renderOrder = 8;
    }

    set(list: readonly VolumeBox[]): void {
        for (const c of [...this.group.children]) {
            this.group.remove(c);
            const m = (c as THREE.Mesh).material as THREE.Material;
            m.dispose();
        }
        for (const b of list) {
            const tint = iconTint(b.group);
            const local = new THREE.Matrix4().compose(
                new THREE.Vector3((b.min[0]! + b.max[0]!) / 2, (b.min[1]! + b.max[1]!) / 2, (b.min[2]! + b.max[2]!) / 2),
                new THREE.Quaternion(),
                new THREE.Vector3(
                    Math.max(b.max[0]! - b.min[0]!, 1e-3),
                    Math.max(b.max[1]! - b.min[1]!, 1e-3),
                    Math.max(b.max[2]! - b.min[2]!, 1e-3),
                ),
            );
            const m = new THREE.Matrix4().multiplyMatrices(b.matrix, local);
            const fill = new THREE.Mesh(
                this.unit,
                new THREE.MeshBasicMaterial({
                    color: tint,
                    transparent: true,
                    opacity: b.strong ? 0.14 : 0.07,
                    depthWrite: false,
                    side: THREE.DoubleSide,
                }),
            );
            const wire = new THREE.LineSegments(
                this.edges,
                new THREE.LineBasicMaterial({ color: tint, transparent: true, opacity: b.strong ? 1 : 0.6, depthTest: false }),
            );
            for (const o of [fill, wire]) {
                o.matrixAutoUpdate = false;
                o.matrix.copy(m);
                o.frustumCulled = false;
                o.renderOrder = 8;
                this.group.add(o);
            }
        }
    }

    dispose(): void {
        this.set([]);
        this.unit.dispose();
        this.edges.dispose();
    }
}

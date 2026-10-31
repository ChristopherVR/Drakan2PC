// Synthetic data only; no game bytes.
import * as THREE from 'three';
import { describe, expect, it } from 'vitest';
import { buildRig, invertMat12, type Model, type Ps2Anim, type Skeleton } from '@drakan/formats';
import { AnimationLayer, type AnimObject, type AnimSource, type ClassRig } from '../src/scene/animlayer.ts';
import { MAX_ANIMATED, animRange, chooseActive, phaseOffset, poseInterval } from '../src/scene/animplan.ts';
import { ClipClock } from '../src/scene/skinned.ts';
import { DEFAULT_VIEW_OPTIONS, parseViewOptions } from '../src/scene/viewopts.ts';

describe('animation scheduling', () => {
    it('picks the nearest visible instances in range, capped', () => {
        const cands = [
            { id: 1, dist: 50, visible: true },
            { id: 2, dist: 10, visible: true },
            { id: 3, dist: 5, visible: false },
            { id: 4, dist: 900, visible: true },
            { id: 5, dist: 30, visible: true },
        ];
        expect(chooseActive(cands, 600)).toEqual([2, 5, 1]);
        expect(chooseActive(cands, 600, 2)).toEqual([2, 5]);
        const many = Array.from({ length: 200 }, (_, i) => ({ id: i, dist: i, visible: true }));
        expect(chooseActive(many, 1000)).toHaveLength(MAX_ANIMATED);
    });
    it('slows re-posing with distance and spreads phases', () => {
        expect(poseInterval(10)).toBeLessThan(poseInterval(100));
        expect(poseInterval(100)).toBeLessThan(poseInterval(1000));
        expect(phaseOffset(7, 2)).toBe(phaseOffset(7, 2));
        expect(phaseOffset(7, 2)).not.toBe(phaseOffset(8, 2));
        for (let i = 0; i < 50; i++) expect(phaseOffset(i, 3)).toBeLessThan(3);
        expect(phaseOffset(1, 0)).toBe(0);
        expect(animRange(3000)).toBe(600);
        expect(animRange(100)).toBe(40);
    });
    it('loops and holds clips by flag', () => {
        const c = new ClipClock(2);
        c.tick(2.5, true);
        expect(c.time).toBeCloseTo(0.5);
        const o = new ClipClock(2);
        o.tick(5, false);
        expect(o.time).toBe(2);
        expect(o.finished(false)).toBe(true);
    });
    it('has a Play animations view option on by default and parses it', () => {
        expect(DEFAULT_VIEW_OPTIONS.animate).toBe(true);
        expect(parseViewOptions({ animate: false }).animate).toBe(false);
        expect(parseViewOptions({ animate: 'no' }).animate).toBe(true);
    });
});

const I = [1, 0, 0, 0, 1, 0, 0, 0, 1];
function skeleton(): Skeleton {
    const childBind = [...I, 1, 0, 0];
    const joint = (xform: number[], firstChild: number, weights: { vertex: number; weight: number }[]) => ({
        xform,
        mesh: 0,
        firstChild,
        nextSibling: 0,
        weights: [weights],
    });
    return {
        platform: 'ps2',
        bounds: new Uint8Array(64),
        lodNames: [],
        nodes: [
            { name: 'root', joint: 0 },
            { name: 'arm', joint: 1 },
        ],
        totalVertices: 4,
        joints: [
            joint([...I, 0, 0, 0], 1, [
                { vertex: 0, weight: 1 },
                { vertex: 1, weight: 1 },
                { vertex: 3, weight: 1 },
            ]),
            joint(Array.from(invertMat12(childBind)), 0, [{ vertex: 2, weight: 1 }]),
        ],
        lods: [{ distance: 0, usage: 1, node: 0, firstVertex: 0, vertexCount: 4, firstPoly: 0, polyCount: 1 }],
        animIds: [7],
        channels: [],
        spheres: [],
        spheresPerLod: [],
    };
}
const model = (): Model => ({
    id: 1,
    name: 'creature',
    shading: 2,
    textures: [{ id: 1, db: 0 }],
    positions: new Float32Array([0, 0, 0, 1, 0, 0, 2, 0, 0, 0.5, -1, 0]),
    normals: new Float32Array([0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1]),
    indices: new Uint16Array([0, 1, 2, 0, 1, 3]),
    triTexture: new Uint16Array([0, 0]),
    uvs: new Float32Array(12),
    lods: [],
    skinned: true,
    bounds: { min: [0, 0, 0], max: [2, 1, 0] },
    lod0: { firstTri: 0, triCount: 2 },
});
const q0: [number, number, number, number] = [0, 0, 0, 1];
const q90: [number, number, number, number] = [0, 0, Math.SQRT1_2, Math.SQRT1_2];
const clip = (name: string): Ps2Anim => ({
    name,
    duration: 2,
    flags: 0,
    frames: 2,
    channels: 1,
    thresholds: { rotation: 0.2, scale: 1, translation: 1 },
    tracks: [
        {
            base: [0, 0, 0],
            hasPos: true,
            keys: [
                { time: 0, q: q0, pos: [0, 0, 0] },
                { time: 2, q: q0, pos: [0, 0, 0] },
            ],
        },
        {
            base: [1, 0, 0],
            hasPos: false,
            keys: [
                { time: 0, q: q0, pos: null },
                { time: 2, q: q90, pos: null },
            ],
        },
    ],
});

function setup() {
    const classRig: ClassRig = {
        model: model(),
        lookup: () => null,
        rig: buildRig(skeleton(), 4),
        clips: [
            { stem: 'S', id: 1, name: 'Idle', label: 'Base Idle' },
            { stem: 'S', id: 2, name: 'Walk', label: '' },
        ],
        idle: { stem: 'S', id: 1 },
    };
    const loads: string[] = [];
    const clipLoads: string[] = [];
    const src: AnimSource = {
        canAnimate: (r) => r === '1:5',
        load: async (r) => (loads.push(r), classRig),
        clip: async (c) => (clipLoads.push(`${c.stem}:${c.id}`), clip(c.id === 1 ? 'Idle' : 'Walk')),
    };
    const hidden = new Map<number, boolean>();
    const layer = new AnimationLayer((id, h) => hidden.set(id, h));
    layer.setSource(src);
    const cam = new THREE.PerspectiveCamera(50, 1, 0.1, 5000);
    cam.position.set(0, 3, 10);
    cam.lookAt(0, 0, 0);
    cam.updateMatrixWorld();
    const obj = (id: number, x: number, res = '1:5'): AnimObject => ({
        id,
        resource: res,
        transform: { pos: [x * 1000, 0, 0], rot: [0, 0, 0] },
    });
    const run = async (n: number): Promise<void> => {
        for (let i = 0; i < n; i++) {
            layer.update(1 / 60, cam);
            await new Promise((r) => setTimeout(r, 0));
        }
    };
    return { layer, cam, hidden, loads, clipLoads, obj, run };
}

describe('AnimationLayer', () => {
    it('animates idle on near instances, hides their bind-pose copy and shares clip data', async () => {
        const { layer, hidden, loads, clipLoads, obj, run } = setup();
        layer.setObjects([obj(1, 0), obj(2, 1), obj(3, 2), obj(9, 0, '1:9')]);
        await run(20);
        expect(loads).toEqual(['1:5']);
        expect(layer.stats.active).toBe(3);
        expect(hidden.get(1)).toBe(true);
        expect(hidden.has(9)).toBe(false);
        expect(clipLoads).toEqual(['S:1']);
        const p = layer.playing(1)!;
        expect(p.name).toBe('Idle');
        // Phases differ per instance, and time advances.
        expect(layer.playing(2)!.time).not.toBe(p.time);
        const t0 = layer.playing(1)!.time;
        await run(10);
        expect(layer.playing(1)!.time).not.toBe(t0);
    });
    it('plays a per-instance override and goes back to idle', async () => {
        const { layer, clipLoads, obj, run } = setup();
        layer.setObjects([obj(1, 0), obj(2, 1)]);
        await run(10);
        layer.setClip(1, { stem: 'S', id: 2 });
        await run(10);
        expect(layer.playing(1)!.name).toBe('Walk');
        expect(layer.playing(2)!.name).toBe('Idle');
        layer.setClip(1, null);
        await run(10);
        expect(layer.playing(1)!.name).toBe('Idle');
        expect([...clipLoads].sort()).toEqual(['S:1', 'S:2']);
    });
    it('keeps everything in bind pose when disabled and restores on enable', async () => {
        const { layer, hidden, obj, run } = setup();
        layer.setObjects([obj(1, 0)]);
        await run(10);
        expect(hidden.get(1)).toBe(true);
        layer.setEnabled(false);
        expect(hidden.get(1)).toBe(false);
        expect(layer.group.children).toHaveLength(0);
        await run(5);
        expect(layer.stats.active).toBe(0);
        layer.setEnabled(true);
        await run(10);
        expect(layer.stats.active).toBe(1);
    });
    it('pauses off-screen and far instances', async () => {
        const { layer, hidden, obj, run } = setup();
        layer.range = 50;
        layer.setObjects([obj(1, 0), obj(2, -500), obj(3, 0.5)]);
        await run(10);
        expect(layer.isAnimated(1)).toBe(true);
        expect(layer.isAnimated(2)).toBe(false);
        expect(hidden.get(2)).toBeUndefined();
        layer.setObjects([obj(1, 0)]);
        expect(layer.isAnimated(3)).toBe(false);
    });
    it('picks an animated instance by ray', async () => {
        const { layer, obj, run } = setup();
        layer.setObjects([obj(1, 0)]);
        await run(10);
        // Instance 1 sits at the origin, one model unit is 2.048 scene units: aim at its middle.
        const ray = new THREE.Ray(new THREE.Vector3(0.5, -0.3, 5), new THREE.Vector3(0, 0, -1));
        expect(layer.pick(ray)?.id).toBe(1);
        expect(layer.pick(ray, (id) => id === 1)).toBeNull();
    });
});

describe('pickIdle', () => {
    it('prefers a calm idle over death and cower idles', async () => {
        const { pickIdle } = await import('../src/editor/animpreview.ts');
        const names = ['Attack_Mid', 'death_idle_forward', 'cower_into', 'idle_base', 'idle_l'].map((name) => ({
            name,
        }));
        expect(pickIdle(names)?.name).toBe('idle_base');
        expect(pickIdle([{ name: 'x', label: 'Combat Ready, Base Idle' }, { name: 'y' }])?.name).toBe('x');
        expect(pickIdle([])).toBeNull();
    });
});

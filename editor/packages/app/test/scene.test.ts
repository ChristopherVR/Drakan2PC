import * as THREE from 'three';
import { describe, expect, it } from 'vitest';
import { LevelDocument, validateDocument, type LevelObject } from '@drakan/editor-core';
import type { Model } from '@drakan/formats';
import type { ModelData } from '../src/scene/decode.ts';
import { classesToLoad, helperGroupOf, partitionObjects } from '../src/scene/instances.ts';
import { ModelLayer } from '../src/scene/layer.ts';
import { MODEL_SCALE, docInstanceMatrix } from '../src/scene/models.ts';
import { clientToNdc, nearestHit, nearestScreenPoint, ndcToClient, pointsInRect } from '../src/scene/pickmath.ts';
import { TextureCache, pixelHash } from '../src/scene/textures.ts';
import { DEFAULT_VIEW_OPTIONS, parseViewOptions } from '../src/scene/viewopts.ts';

/** A single triangle in the xy plane, one unit wide, centred at the origin (model units). */
function triangleModel(): Model {
    return {
        id: 1,
        name: 'tri',
        shading: 2,
        textures: [],
        positions: new Float32Array([-0.5, 0, 0, 0.5, 0, 0, 0, 1, 0]),
        normals: new Float32Array([0, 0, 1, 0, 0, 1, 0, 0, 1]),
        indices: new Uint16Array([0, 1, 2]),
        triTexture: new Uint16Array([0]),
        uvs: new Float32Array([0, 0, 1, 0, 0.5, 1]),
        lods: [],
        skinned: false,
        bounds: { min: [-0.5, 0, 0], max: [0.5, 1, 0] },
        lod0: { firstTri: 0, triCount: 1 },
    };
}

const obj = (id: number, resource: string, cls: string, x = 0, z = 0): LevelObject => ({
    id,
    class: cls,
    type: -1,
    kind: 'static',
    resource,
    model: '',
    transform: { pos: [x, 0, z], rot: [0, 0, 0] },
    props: {},
});

describe('pick math', () => {
    const rect = { left: 100, top: 50, width: 400, height: 200 };
    it('maps client pixels to NDC and back', () => {
        expect(clientToNdc(rect, 300, 150)).toEqual([0, 0]);
        expect(clientToNdc(rect, 100, 50)).toEqual([-1, 1]);
        expect(clientToNdc(rect, 500, 250)).toEqual([1, -1]);
        const [nx, ny] = clientToNdc(rect, 180, 90);
        const [x, y] = ndcToClient(rect, nx, ny);
        expect(x).toBeCloseTo(180);
        expect(y).toBeCloseTo(90);
    });
    it('finds the nearest point inside the radius only', () => {
        const pts = [
            { id: 1, x: 10, y: 10 },
            { id: 2, x: 14, y: 10 },
            { id: 3, x: 100, y: 100 },
        ];
        expect(nearestScreenPoint(pts, 13, 10, 10)).toBe(2);
        expect(nearestScreenPoint(pts, 50, 50, 10)).toBeNull();
        expect(nearestScreenPoint([], 0, 0, 10)).toBeNull();
    });
    it('selects points in a rectangle given in any corner order', () => {
        const pts = [
            { id: 1, x: 5, y: 5 },
            { id: 2, x: 20, y: 20 },
            { id: 3, x: 40, y: 5 },
        ];
        expect(pointsInRect(pts, 0, 0, 25, 25)).toEqual([1, 2]);
        expect(pointsInRect(pts, 25, 25, 0, 0)).toEqual([1, 2]);
        expect(pointsInRect(pts, 40, 5, 40, 5)).toEqual([3]);
    });
    it('keeps the nearest hit', () => {
        expect(nearestHit([null, { distance: 5, id: 1 }, { distance: 2, id: 2 }, undefined])?.id).toBe(2);
        expect(nearestHit([])).toBeNull();
    });
});

describe('instance bookkeeping', () => {
    it('classifies helper classes by name', () => {
        expect(helperGroupOf('Trigger Detector')).toBe('trigger');
        expect(helperGroupOf('Triggered Sound')).toBe('sound');
        expect(helperGroupOf('Static (Vertex) Light')).toBe('light');
        expect(helperGroupOf('Boulder Wide (Light)')).toBeNull();
        expect(helperGroupOf('Light Chair 01 1A')).toBeNull();
        expect(helperGroupOf('Oak Tree')).toBeNull();
    });
    it('partitions objects into model batches, markers and hidden helpers', () => {
        const objs = [
            obj(1, '0x10001', 'Oak Tree'),
            obj(2, '0x10002', 'Mystery'),
            obj(3, '0x10001', 'Oak Tree'),
            obj(4, '0x10003', 'Trigger Detector'),
        ];
        const hasModel = (r: string): boolean => r !== '0x10002';
        const shown = partitionObjects(objs, hasModel, () => true);
        expect([...shown.byResource.keys()].sort()).toEqual(['0x10001', '0x10003']);
        expect(shown.byResource.get('0x10001')!.map((o) => o.id)).toEqual([1, 3]);
        expect(shown.markers.map((o) => o.id)).toEqual([2]);
        expect(shown.hidden).toEqual([]);
        const noTriggers = partitionObjects(objs, hasModel, (g) => g !== 'trigger');
        expect(noTriggers.hidden.map((o) => o.id)).toEqual([4]);
        expect(noTriggers.byResource.has('0x10003')).toBe(false);
    });
    it('lists each class to load once and skips known ones', () => {
        const list = classesToLoad(['0x10001', '0x10001', '0x20005', '0x10002'], (r) => r === '0x10002');
        expect(list).toEqual([
            { resource: '0x10001', dbIndex: 1, classId: 1 },
            { resource: '0x20005', dbIndex: 2, classId: 5 },
        ]);
    });
});

describe('ModelLayer', () => {
    const data: ModelData[] = [
        { classId: 1, dbIndex: 1, model: triangleModel(), textures: [] },
        { classId: 2, dbIndex: 1, model: null, textures: [] },
    ];

    it('registers models, remembers failures, and maps instances back to object ids', () => {
        const layer = new ModelLayer();
        const added = layer.addModelData(data, new TextureCache());
        expect(added).toEqual(['0x10001']);
        expect(layer.has('0x10001')).toBe(true);
        expect(layer.has('0x10002')).toBe(false);
        expect(layer.known('0x10002')).toBe(true);
        const a = obj(7, '0x10001', 'Oak', 0, 0);
        const b = obj(9, '0x10001', 'Oak', 5000, 0);
        layer.sync(new Map([['0x10001', [a, b]]]));
        expect(layer.idsOf('0x10001')).toEqual([7, 9]);
        expect(layer.instanceCount).toBe(2);
        // Dropping one object keeps the mesh and shrinks the batch.
        layer.sync(new Map([['0x10001', [b]]]));
        expect(layer.idsOf('0x10001')).toEqual([9]);
        // An empty map hides everything.
        layer.sync(new Map());
        expect(layer.instanceCount).toBe(0);
    });

    it('picks the instance under a ray and honours the skip filter', () => {
        const layer = new ModelLayer();
        layer.addModelData(data, new TextureCache());
        // Object 7 at the origin, object 9 two scene units behind it; both face +z.
        const a = obj(7, '0x10001', 'Oak', 0, 0);
        const b = obj(9, '0x10001', 'Oak', 0, -2000);
        layer.sync(new Map([['0x10001', [a, b]]]));
        const y = (MODEL_SCALE * 0.4) as number;
        const ray = new THREE.Ray(new THREE.Vector3(0, y, 10), new THREE.Vector3(0, 0, -1));
        const hit = layer.pick(ray);
        expect(hit?.id).toBe(7);
        expect(hit?.distance).toBeCloseTo(10, 5);
        expect(layer.pick(ray, (id) => id === 7)?.id).toBe(9);
        const miss = new THREE.Ray(new THREE.Vector3(5, y, 10), new THREE.Vector3(0, 0, -1));
        expect(layer.pick(miss)).toBeNull();
    });

    it('applies position, yaw and scale in the instance matrix', () => {
        const m = docInstanceMatrix({
            transform: { pos: [1000, 2000, 3000], rot: [0, Math.PI / 2, 0] },
            scale: [2, 2, 2],
        });
        const p = new THREE.Vector3(1, 0, 0).applyMatrix4(m);
        // +x of the model after a quarter turn about y points to -z; scale 2 and the model unit factor apply.
        expect(p.x).toBeCloseTo(1, 5);
        expect(p.y).toBeCloseTo(2, 5);
        expect(p.z).toBeCloseTo(3 - 2 * MODEL_SCALE, 5);
    });

    it('builds outlines and a drag ghost only for loaded classes', () => {
        const layer = new ModelLayer();
        layer.addModelData(data, new TextureCache());
        expect(layer.ghost('0x10001')).not.toBeNull();
        expect(layer.ghost('0x10002')).toBeNull();
        layer.setSelection([obj(1, '0x10001', 'Oak'), obj(2, '0x10002', 'x')]);
        layer.setHover([obj(1, '0x10001', 'Oak')]);
        layer.setSelection([]);
        layer.reset();
        expect(layer.has('0x10001')).toBe(false);
        expect(layer.known('0x10002')).toBe(false);
    });
});

describe('textures and options', () => {
    it('shares one GPU texture between classes with identical pixels', () => {
        const cache = new TextureCache();
        const px = (v: number) => new Uint8Array(4 * 4 * 4).fill(v);
        const a = cache.addShared([{ ref: 1, width: 4, height: 4, rgba: px(9) }]);
        const b = cache.addShared([
            { ref: 5, width: 4, height: 4, rgba: px(9) },
            { ref: 6, width: 4, height: 4, rgba: px(200) },
        ]);
        expect(a(1)).toBe(b(5));
        expect(b(6)).not.toBe(b(5));
        expect(a(2)).toBeNull();
        expect(cache.size).toBe(2);
        expect(pixelHash(px(1))).not.toBe(pixelHash(px(2)));
    });

    it('merges stored view options over the defaults and rejects bad values', () => {
        expect(parseViewOptions(null)).toEqual(DEFAULT_VIEW_OPTIONS);
        const o = parseViewOptions({
            wireframe: true,
            lighting: 'dusk',
            drawDistance: 99999,
            markers: 'x',
            helpers: { sound: true },
        });
        expect(o.wireframe).toBe(true);
        expect(o.lighting).toBe('dusk');
        expect(o.drawDistance).toBe(6000);
        expect(o.markers).toBe(DEFAULT_VIEW_OPTIONS.markers);
        expect(o.helpers.sound).toBe(true);
        expect(parseViewOptions({ lighting: 'nope' }).lighting).toBe('day');
    });
});

describe('placed class without an instance', () => {
    it('is flagged as default-body only on disc levels', () => {
        const doc = new LevelDocument();
        const fresh = obj(0, '0x10009', 'Lonely');
        doc.insert({ ...fresh, id: 50 });
        doc.insert({ ...obj(51, '0x10001', 'Oak'), origin: 51 });
        doc.insert({ ...obj(52, '0x10001', 'Oak'), template: 51 });
        const warn = validateDocument(doc, { sourceLevel: true }).filter((i) => i.code === 'default-body');
        expect(warn.map((i) => i.ids[0])).toEqual([50]);
        expect(validateDocument(doc).some((i) => i.code === 'default-body')).toBe(false);
    });
});

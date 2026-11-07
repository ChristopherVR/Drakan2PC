// Synthetic only: no game bytes. Classes without a model of their own (detectors, timers, generators, waypoints, level
// logic, break-away floors) are normal: they load, get an icon or a marker, are not reported as failures and survive a
// save. Converted Order of the Flame levels have dozens of them per level.
import { describe, expect, it } from 'vitest';
import {
    FIELD_ARRAY,
    FIELD_INT,
    FIELD_MODEL,
    FLAG_VISIBLE,
    Iso9660,
    bufferSource,
    parseLevelHeader,
    writeLevelHeader,
    writeObjectList,
    type Lvl,
    type LvlObject,
    type Model,
} from '@drakan/formats';
import { LevelDocument, applyDocumentToLvl, documentFromLvl, type LevelObject } from '@drakan/editor-core';
import { buildIso, buildSrsc } from '../../formats/test/fixtures.ts';
import { loadModelData, mergeModels, standInModelRefs, type ModelData } from '../src/scene/decode.ts';
import { helperGroupOfRfl, partitionObjects } from '../src/scene/instances.ts';
import { ModelLayer } from '../src/scene/layer.ts';
import { TextureCache } from '../src/scene/textures.ts';
import { modelsStatus } from '../src/sceneload.ts';

const u16 = (n: number): number[] => [n & 255, (n >> 8) & 255];
const u32 = (n: number): number[] => [n & 255, (n >>> 8) & 255, (n >>> 16) & 255, (n >>> 24) & 255];
const enc = (s: string): Uint8Array => Uint8Array.from([...s].map((c) => c.charCodeAt(0)));

/** A 0x0102 record: name field, then `u16 group, u16 model id, u16 model db`, the runtime class at +16 of the body. */
function classRecord(name: string, rfl: number, model?: { id: number; db: number }): Uint8Array {
    const body = Array(24).fill(0);
    if (model) body.splice(2, 4, ...u16(model.id), ...u16(model.db));
    body.splice(16, 4, ...u32(0x10000 | rfl));
    return Uint8Array.from([...u16(name.length + 1), ...enc(name), 0, ...body]);
}

const header = () =>
    parseLevelHeader(
        writeLevelHeader({
            name: { raw: enc('Test'), text: 'Test' },
            maxWidth: 1,
            maxHeight: 1,
            dependencies: [{ dbIndex: 2, zero: 0, path: { raw: enc('..\\..\\Common\\System\\System'), text: '' } }],
            trailer: new Uint8Array(0),
        }),
    );

/** A triangle `size` wide at x offset `dx`, one texture ref. */
function tri(dx: number, tex: { id: number; db: number }, id = 1): Model {
    return {
        id,
        name: `piece${id}`,
        shading: 2,
        textures: [tex],
        positions: new Float32Array([dx, 0, 0, dx + 1, 0, 0, dx, 1, 0]),
        normals: new Float32Array([0, 0, 1, 0, 0, 1, 0, 0, 1]),
        indices: new Uint16Array([0, 1, 2]),
        triTexture: new Uint16Array([0]),
        uvs: new Float32Array([0, 0, 1, 0, 0, 1]),
        lods: [],
        skinned: false,
        bounds: { min: [dx, 0, 0], max: [dx + 1, 1, 0] },
        lod0: { firstTri: 0, triCount: 1 },
    };
}

const obj = (id: number, resource: string, cls: string): LevelObject => ({
    id,
    class: cls,
    type: -1,
    kind: 'logic',
    resource,
    model: '',
    transform: { pos: [id * 100, 0, 0], rot: [0, 0, 0] },
    props: {},
});

describe('loading the models of a level with model-less classes', () => {
    async function disc(): Promise<Iso9660> {
        const odb = buildSrsc([
            { type: 0x0102, id: 1, sub: 0, data: classRecord('Orc Plate waypoint', 112) },
            { type: 0x0102, id: 2, sub: 0, data: classRecord('Soul Flare Generator', 63) },
            // Names a model, but there is no SYSTEM.MOD: a real failure.
            { type: 0x0102, id: 3, sub: 0, data: classRecord('Barrel', 16, { id: 40, db: 0 }) },
            // Names a model in a database the .DB does not list.
            { type: 0x0102, id: 4, sub: 0, data: classRecord('Crate', 16, { id: 41, db: 7 }) },
        ]);
        return Iso9660.open(bufferSource(buildIso([{ path: 'ENGLISH/COMMON/SYSTEM/SYSTEM.ODB', data: odb }])));
    }

    it('tells a class with no model of its own apart from a model that did not resolve', async () => {
        const fs = await disc();
        const want = [1, 2, 3, 4, 9].map((classId) => ({ classId, dbIndex: 2 }));
        const out = await loadModelData(fs, 'ENGLISH\\LEVELS\\TEST', header(), want);
        expect(out.map((d) => d.classId)).toEqual([1, 2, 3, 4, 9]);
        const by = (id: number): ModelData => out.find((d) => d.classId === id)!;
        expect(by(1)).toMatchObject({ model: null, noModel: true, rfl: 112 });
        expect(by(2)).toMatchObject({ model: null, noModel: true, rfl: 63 });
        expect(by(3).model).toBeNull();
        expect(by(3).noModel).toBeUndefined();
        expect(by(3).rfl).toBe(16);
        expect(by(4).noModel).toBeUndefined();
        // No class record at all: not "no model", the class is broken.
        expect(by(9)).toEqual({ classId: 9, dbIndex: 2, model: null, textures: [] });
    });

    it('counts only unresolved models as failures; model-less classes get icons by engine class', async () => {
        const fs = await disc();
        const want = [1, 2, 3, 4].map((classId) => ({ classId, dbIndex: 2 }));
        const layer = new ModelLayer();
        layer.addModelData(await loadModelData(fs, 'ENGLISH\\LEVELS\\TEST', header(), want), new TextureCache());
        expect([...layer.modellessResources].sort()).toEqual(['0x20001', '0x20002']);
        expect([...layer.failedResources].sort()).toEqual(['0x20003', '0x20004']);
        for (const r of ['0x20001', '0x20002', '0x20003', '0x20004']) expect(layer.known(r)).toBe(true);
        expect(layer.modellessGroup('0x20001')).toBe('waypoint');
        expect(layer.modellessGroup('0x20002')).toBe('spawn');
        expect(layer.modellessGroup('0x20003')).toBeUndefined();
        expect(modelsStatus(0, 4, 2, 2)).toBe(
            'Models: 0 of 4 classes decoded; 2 have no model of their own (shown as icons or markers); 2 could not be loaded (shown as markers).',
        );
        expect(modelsStatus(10, 12, 2, 0)).toBe(
            'Models: 10 of 12 classes decoded; 2 have no model of their own (shown as icons or markers).',
        );
        expect(modelsStatus(3, 3, 0, 0)).toBe('Models: 3 of 3 classes decoded.');
    });
});

describe('model layer bookkeeping', () => {
    it('treats a model with no polygons as no visible model, not as a failure', () => {
        const layer = new ModelLayer();
        const empty: Model = {
            ...tri(0, { id: 1, db: 0 }),
            triTexture: new Uint16Array(0),
            lod0: { firstTri: 0, triCount: 0 },
        };
        const added = layer.addModelData(
            [
                { classId: 1, dbIndex: 3, model: empty, textures: [], rfl: 35 },
                { classId: 2, dbIndex: 3, model: tri(0, { id: 1, db: 0 }), textures: [], noModel: true, rfl: 44 },
            ],
            new TextureCache(),
        );
        // A model-less class with a stand-in (a break-away floor's pieces) is drawn with it.
        expect(added).toEqual(['0x30002']);
        expect(layer.has('0x30002')).toBe(true);
        expect([...layer.modellessResources]).toEqual(['0x30001']);
        expect(layer.failedResources.size).toBe(0);
        expect(layer.modellessGroup('0x30001')).toBeNull();
        layer.reset();
        expect(layer.modellessCount).toBe(0);
        expect(layer.known('0x30001')).toBe(false);
    });

    it('draws model-less helpers as icons, hides them with their group and keeps the rest as markers', () => {
        const layer = new ModelLayer();
        layer.addModelData(
            [
                { classId: 1, dbIndex: 2, model: null, textures: [], noModel: true, rfl: 112 },
                { classId: 2, dbIndex: 2, model: null, textures: [], noModel: true, rfl: 121 },
                { classId: 3, dbIndex: 2, model: null, textures: [] },
            ],
            new TextureCache(),
        );
        const objs = [
            obj(1, '0x20001', 'Orc Plate waypoint'),
            obj(2, '0x20002', 'Visibility Toggler'),
            obj(3, '0x20003', 'Barrel'),
        ];
        const group = (r: string) => layer.modellessGroup(r) ?? null;
        const icons = partitionObjects(
            objs,
            (r) => layer.has(r),
            () => true,
            true,
            group,
        );
        expect(icons.icons.map((i) => [i.o.id, i.group])).toEqual([[1, 'waypoint']]);
        expect(icons.markers.map((o) => o.id)).toEqual([2, 3]);
        // Icons off: every model-less object is still drawn (and pickable) as a marker.
        const plain = partitionObjects(
            objs,
            (r) => layer.has(r),
            () => true,
            false,
            group,
        );
        expect(plain.markers.map((o) => o.id)).toEqual([1, 2, 3]);
        const noWaypoints = partitionObjects(
            objs,
            (r) => layer.has(r),
            (g) => g !== 'waypoint',
            true,
            group,
        );
        expect(noWaypoints.hidden.map((o) => o.id)).toEqual([1]);
    });

    it('maps only helper engine classes to icon groups', () => {
        expect(helperGroupOfRfl(61)).toBe('trigger');
        expect(helperGroupOfRfl(62)).toBe('timer');
        expect(helperGroupOfRfl(7)).toBe('timer');
        expect(helperGroupOfRfl(27)).toBe('camera');
        expect(helperGroupOfRfl(133)).toBe('light');
        expect(helperGroupOfRfl(129)).toBe('sound');
        // Break-away floor, Visibility Toggler, Level Properties: no icon of their own.
        expect(helperGroupOfRfl(44)).toBeNull();
        expect(helperGroupOfRfl(121)).toBeNull();
        expect(helperGroupOfRfl(54)).toBeNull();
    });
});

describe('stand-in models of model-less classes', () => {
    const fields = [
        { type: FIELD_INT, name: 'Damage' },
        { type: FIELD_MODEL | FIELD_ARRAY, name: 'Model List' },
    ];

    it('names every piece of a break-away floor and the first frame of a Morph Plug', () => {
        // data[1] = array of 3 words at offset 2
        expect(standInModelRefs(fields, [5, (3 << 16) | 2, 950, 951, 952])).toEqual([950, 951, 952]);
        const morph = [{ type: FIELD_MODEL | FIELD_ARRAY, name: 'Morphing Models' }];
        expect(standInModelRefs(morph, [(2 << 16) | 1, (2 << 16) | 70, (2 << 16) | 71])).toEqual([(2 << 16) | 70]);
        // Empty slots are skipped; other model fields (a generator's spawned debris) are not used.
        expect(standInModelRefs(fields, [5, (2 << 16) | 2, 0, 951])).toEqual([951]);
        expect(standInModelRefs([{ type: FIELD_MODEL, name: 'Debris Model' }], [960])).toEqual([]);
        expect(standInModelRefs([], [])).toEqual([]);
    });

    it('merges the pieces into one model with shared textures', () => {
        const a = tri(0, { id: 7, db: 0 }, 1);
        const b = tri(2, { id: 7, db: 0 }, 2);
        const c = tri(4, { id: 8, db: 0 }, 3);
        const m = mergeModels([a, b, c], 1, 'Brick')!;
        expect(m.lod0).toEqual({ firstTri: 0, triCount: 3 });
        expect(m.textures).toEqual([
            { id: 7, db: 0 },
            { id: 8, db: 0 },
        ]);
        expect([...m.triTexture]).toEqual([0, 0, 1]);
        expect([...m.indices]).toEqual([0, 1, 2, 3, 4, 5, 6, 7, 8]);
        expect(m.bounds).toEqual({ min: [0, 0, 0], max: [5, 1, 0] });
        expect(m.positions.length).toBe(27);
        expect(m.skinned).toBe(false);
        expect(mergeModels([a], 1, 'one')).toBe(a);
        expect(mergeModels([], 1, 'none')).toBeNull();
    });
});

describe('model-less objects in a saved level', () => {
    function mk(id: number, classId: number): LvlObject {
        return {
            id,
            classId,
            dbIndex: 2,
            prefix0: 0,
            prefix1: 0,
            pos: [id * 100, 5, id * 10],
            flags: FLAG_VISIBLE,
            initialEvents: 1,
            radius: null,
            links: [],
            rot: [0, 90, 0],
            scale: null,
            preSentinel: 0,
            sentinel: 0xffffffff,
            data: [],
            params: [],
        };
    }
    const odbs = new Map([
        [
            'SYSTEM',
            new Map([
                [1, { id: 1, name: 'Orc Plate waypoint', group: 0, runtimeClass: 0x10070, model: null, rflClass: 112 }],
                [2, { id: 2, name: 'Timer', group: 0, runtimeClass: 0x1003e, model: null, rflClass: 62 }],
            ]),
        ],
    ]);

    it('loads them, keeps every byte when nothing changed and writes a move', () => {
        const lvl: Lvl = { header: header(), list: { objects: [mk(4, 1), mk(5, 2)], warnings: [] } };
        const { doc, unresolved } = documentFromLvl(lvl, odbs, 'Test');
        expect(unresolved).toBe(0);
        expect([...doc.objects()].map((o) => o.class)).toEqual(['Orc Plate waypoint', 'Timer']);
        expect(writeObjectList(applyDocumentToLvl(lvl.list, doc).list)).toEqual(writeObjectList(lvl.list));
        // The autosave form carries them too.
        const back = LevelDocument.fromJSON(JSON.parse(JSON.stringify(doc.toJSON())));
        expect([...back.objects()].map((o) => o.id)).toEqual([4, 5]);
        doc.get(5)!.transform.pos[0] = 900;
        const out = applyDocumentToLvl(lvl.list, doc);
        expect(out.list.objects.find((o) => o.id === 5)!.pos[0]).toBe(900);
        expect(out.list.objects.find((o) => o.id === 4)!.pos[0]).toBe(400);
    });
});

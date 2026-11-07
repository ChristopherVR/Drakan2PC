// Synthetic only: no game bytes. A break-away floor draws the pieces its "Model List" names; an object that names its
// own pieces in that parameter is drawn with them, under a key of its own, while its class keeps the default pieces.
import { describe, expect, it } from 'vitest';
import {
    FIELD_ARRAY,
    FIELD_INT,
    FIELD_MODEL,
    Iso9660,
    buildPlainClassRecord,
    bufferSource,
    encodeFieldList,
    parseLevelHeader,
    writeLevelHeader,
    writePs2ModelRecords,
    type NeutralModel,
} from '@drakan/formats';
import type { LevelObject } from '@drakan/editor-core';
import { buildIso, buildSrsc } from '../../formats/test/fixtures.ts';
import { loadModelData } from '../src/scene/decode.ts';
import { classesToLoad, partitionObjects } from '../src/scene/instances.ts';
import { ModelLayer } from '../src/scene/layer.ts';
import {
    drawResource,
    modelRequest,
    objectStandInRefs,
    parseStandInResource,
    standInResource,
} from '../src/scene/standin.ts';
import { TextureCache } from '../src/scene/textures.ts';

const MODEL_LIST = FIELD_MODEL | FIELD_ARRAY;
const enc = (s: string): Uint8Array => Uint8Array.from([...s].map((c) => c.charCodeAt(0)));

/** A one-triangle piece at x offset `dx` (no textures). */
function piece(dx: number): NeutralModel {
    return {
        name: `piece${dx}`,
        shading: 2,
        distance: null,
        textures: [],
        positions: new Float32Array([dx, 0, 0, dx + 1, 0, 0, dx, 0, 1]),
        normals: new Int8Array([0, 127, 0, 0, 127, 0, 0, 127, 0]),
        polygons: [{ flags: 2, texture: 0, indices: [0, 1, 2], uvs: [0, 0, 1, 0, 0, 1] }],
        bounds: null,
    };
}

/** SYSTEM.ODB: floor class 5 (no model, "Model List" = pieces 10 and 11); SYSTEM.MOD: pieces 10, 11 and 12. */
async function disc(): Promise<Iso9660> {
    const fields = [
        { type: FIELD_INT, name: 'Break Away on Trigger Only' },
        { type: MODEL_LIST, name: 'Model List' },
    ];
    // data[1]: 2 refs at word 2
    const data = [0, (2 << 16) | 2, 10, 11];
    const odb = buildSrsc([
        {
            type: 0x0102,
            id: 5,
            sub: 0,
            data: buildPlainClassRecord({ name: 'Brick floor', group: 1, rfl: 44, icon: 1, fieldCount: 2, data }),
        },
        { type: 0x0103, id: 0, sub: 0, data: encodeFieldList(fields) },
    ]);
    const mod = buildSrsc(
        [10, 11, 12].flatMap((id, k) =>
            writePs2ModelRecords(piece(k * 4)).map((r) => ({ type: r.type, id, sub: 0, data: r.data })),
        ),
    );
    return Iso9660.open(
        bufferSource(
            buildIso([
                { path: 'ENGLISH/COMMON/SYSTEM/SYSTEM.ODB', data: odb },
                { path: 'ENGLISH/COMMON/SYSTEM/SYSTEM.MOD', data: mod },
            ]),
        ),
    );
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

const floor = (id: number, fields?: LevelObject['fields']): LevelObject => ({
    id,
    class: 'Brick floor',
    type: -1,
    kind: 'prop',
    resource: '0x20005',
    model: '',
    transform: { pos: [id * 100, 0, 0], rot: [0, 0, 0] },
    props: {},
    ...(fields ? { fields } : {}),
});

const ownPieces = [{ slot: 0, fieldIndex: 1, type: MODEL_LIST, name: 'Model List', value: [12] }];

describe('per-object stand-in models', () => {
    it('reads an object override of the pieces and keys it apart from the class', () => {
        expect(objectStandInRefs(undefined)).toBeNull();
        expect(objectStandInRefs([{ type: FIELD_INT, name: 'Model List', value: 3 }])).toBeNull(); // not a model field
        expect(objectStandInRefs(ownPieces)).toEqual([12]);
        expect(objectStandInRefs([{ ...ownPieces[0]!, value: [0, (3 << 16) | 70, 71] }])).toEqual([(3 << 16) | 70, 71]);
        expect(objectStandInRefs([{ ...ownPieces[0]!, value: [] }])).toBeNull(); // names nothing: the class default
        // A Morph Plug shows its first frame.
        expect(objectStandInRefs([{ type: MODEL_LIST, name: 'Morphing Models', value: [61, 62] }])).toEqual([61]);

        const key = standInResource('0x20005', [12, (3 << 16) | 70]);
        expect(parseStandInResource(key)).toEqual({ resource: '0x20005', refs: [12, (3 << 16) | 70] });
        expect(parseStandInResource('0x20005')).toEqual({ resource: '0x20005', refs: null });
        expect(modelRequest(key)).toEqual({ dbIndex: 2, classId: 5, standIn: [12, (3 << 16) | 70] });
        expect(modelRequest('0x20005')).toEqual({ dbIndex: 2, classId: 5 });
        expect(drawResource(floor(1))).toBe('0x20005');
        expect(drawResource(floor(2, ownPieces))).toBe(standInResource('0x20005', [12]));
        expect(classesToLoad([drawResource(floor(1)), drawResource(floor(2, ownPieces))], () => false)).toEqual([
            { resource: '0x20005', dbIndex: 2, classId: 5 },
            { resource: standInResource('0x20005', [12]), dbIndex: 2, classId: 5, standIn: [12] },
        ]);
    });

    it('loads the class default pieces and an object override, and draws each object with its own', async () => {
        const fs = await disc();
        const objects = [floor(1), floor(2, ownPieces), floor(3)];
        const want = classesToLoad(
            objects.map((o) => drawResource(o)),
            () => false,
        );
        const data = await loadModelData(fs, 'ENGLISH\\LEVELS\\TEST', header(), want);
        const [byClass, byObject] = data;
        // Class default: pieces 10 and 11 merged (two triangles); the override: piece 12 alone.
        expect(byClass).toMatchObject({ classId: 5, dbIndex: 2, noModel: true, rfl: 44 });
        expect(byClass!.standIn).toBeUndefined();
        expect(byClass!.model?.lod0.triCount).toBe(2);
        expect(byClass!.model?.bounds.min[0]).toBeCloseTo(0);
        expect(byObject).toMatchObject({ classId: 5, dbIndex: 2, noModel: true, standIn: [12] });
        expect(byObject!.model?.lod0.triCount).toBe(1);
        expect(byObject!.model?.bounds.min[0]).toBeCloseTo(8);

        const layer = new ModelLayer();
        const added = layer.addModelData(data, new TextureCache());
        const own = standInResource('0x20005', [12]);
        expect(added).toEqual(['0x20005', own]);
        expect(layer.entry(own)?.model.lod0.triCount).toBe(1);
        const part = partitionObjects(
            objects,
            (r) => layer.has(r),
            () => true,
        );
        expect([...part.byResource].map(([k, list]) => [k, list.map((o) => o.id)])).toEqual([
            ['0x20005', [1, 3]],
            [own, [2]],
        ]);
        expect(part.markers).toEqual([]);
        layer.sync(part.byResource);
        expect(layer.idsOf(own)).toEqual([2]);
        expect(layer.idsOf('0x20005')).toEqual([1, 3]);
    });
});

import { describe, expect, it } from 'vitest';
import { LevelDocument, type LevelObject, type LogicEnv } from '@drakan/editor-core';
import { copyDocument, standableProblem, withCutScene, yawFromDirection } from '../src/playtest.ts';

const obj = (
    id: number,
    cls: string,
    kind: LevelObject['kind'],
    pos: [number, number, number],
    origin?: number,
): LevelObject => ({
    id,
    class: cls,
    type: kind === 'player' ? 3 : 0,
    kind,
    resource: `0x2${id}`,
    model: '',
    transform: { pos, rot: [0, 0, 0] },
    props: {},
    ...(origin !== undefined ? { origin } : {}),
});

function doc(): LevelDocument {
    const d = new LevelDocument();
    d.insert(obj(1, 'Rynn', 'player', [10, 20, 30], 1));
    d.insert(obj(2, 'Arokh', 'player', [0, 0, 0], 2));
    d.insert(obj(3, 'Stomp Player', 'static', [5, 5, 5], 3));
    return d;
}

describe('play test document edits', () => {
    it('copies without sharing objects', () => {
        const d = doc();
        const c = copyDocument(d);
        c.get(3)!.transform.pos[0] = 99;
        expect(d.get(3)!.transform.pos[0]).toBe(5);
    });
    it('yaw 0 faces +x and positive yaw turns toward -z', () => {
        expect(yawFromDirection(1, 0)).toBeCloseTo(0);
        expect(yawFromDirection(0, -1)).toBeCloseTo(Math.PI / 2);
        expect(yawFromDirection(0, 1)).toBeCloseTo(-Math.PI / 2);
    });
    it('refuses an object that is not a cut scene player', () => {
        const env: LogicEnv = { rflOf: () => null, schemaFor: () => undefined };
        expect(withCutScene(doc(), env, [], 3)).toHaveProperty('error');
        expect(withCutScene(doc(), env, [], 99)).toHaveProperty('error');
    });
    it('refuses a pick without terrain or far from every object', () => {
        const d = doc();
        expect(standableProblem(d, () => null, [10, 0, 30])).toMatch(/no terrain/i);
        expect(standableProblem(d, () => 20, [90000, 20, 90000])).toMatch(/far from everything/i);
        expect(standableProblem(d, () => 20, [100, 20, 100])).toBeNull();
    });
});

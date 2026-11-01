// Synthetic only: the lane grouping and time snapping behind the cut scene timeline.
import { addActor, insertAction, newActionBytes, newSequence, setActionTime, parseActions } from '@drakan/formats';
import { describe, expect, it } from 'vitest';
import { lanesOf, timeAt } from '../src/story/timeline.ts';

function scene() {
    const seq = newSequence(7, 1, 'Test scene');
    const cam = addActor(seq, 'Camera', 5);
    const put = (a: typeof cam, bytes: Uint8Array): void => {
        const r = insertAction(a, bytes);
        a.actions = r.actions;
        a.actionCount = r.actionCount;
    };
    put(cam, newActionBytes(5, 0, { value: 0 }));
    put(cam, newActionBytes(0, 1, { x: 1, y: 2, z: 3 }));
    put(cam, newActionBytes(3, 2, { sound: 0x10001, params: 0x82710 }));
    put(cam, newActionBytes(5, 4, { value: 1 }));
    const npc = addActor(seq, 'Farmer', 9);
    put(npc, newActionBytes(7, 3, { message: 4, param: 0 }));
    return { seq, cam };
}

describe('timeline lanes', () => {
    it('makes one lane per actor and kind and skips markers', () => {
        const { seq } = scene();
        const lanes = lanesOf(seq);
        expect(lanes.map((l) => `${l.actor}:${l.kind}`)).toEqual([
            'Camera:transform',
            'Camera:sound',
            'Farmer:message',
        ]);
        expect(lanes[0]!.keys.map((k) => k.action.time)).toEqual([1]);
    });
    it('keeps the index of a key in the actor action list, so a drag edits the right one', () => {
        const { seq } = scene();
        const lane = lanesOf(seq).find((l) => l.kind === 'sound')!;
        const cam = seq.actors[0]!;
        const key = lane.keys[0]!;
        expect(parseActions(cam)![key.index]).toMatchObject({ kind: 'sound', time: 2 });
        cam.actions = setActionTime(cam, parseActions(cam)![key.index]!, 3.5);
        expect(lanesOf(seq).find((l) => l.kind === 'sound')!.keys[0]!.action.time).toBeCloseTo(3.5);
    });
});

describe('timeAt', () => {
    it('maps a pointer position to seconds, snapped and clamped', () => {
        expect(timeAt(0, 200, 10)).toBe(0);
        expect(timeAt(100, 200, 10)).toBe(5);
        expect(timeAt(103, 200, 10)).toBeCloseTo(5.15, 5);
        expect(timeAt(-20, 200, 10)).toBe(0);
        expect(timeAt(500, 200, 10)).toBe(10);
        expect(timeAt(33, 200, 10, 0.5)).toBe(1.5);
    });
});

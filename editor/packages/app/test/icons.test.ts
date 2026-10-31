import { describe, expect, it } from 'vitest';
import { CommandStack, LevelDocument, describeObjects } from '@drakan/editor-core';
import { ICON_ORDER, helperGroupOf, partitionObjects, HELPER_GROUPS } from '../src/scene/instances.ts';
import { DEFAULT_VIEW_OPTIONS, parseViewOptions } from '../src/scene/viewopts.ts';
import { addLinks, objectLabel } from '../src/editor/linking.ts';
import { contextItems, registerContextItem } from '../src/editor/registry.ts';
import type { Ctx } from '../src/editor/ctx.ts';

const o = (id: number, resource: string, cls: string) => ({ id, resource, class: cls });

describe('editor icons', () => {
    it('has one atlas cell per helper group', () => {
        expect([...ICON_ORDER].sort()).toEqual(HELPER_GROUPS.map((g) => g.id).sort());
        expect(ICON_ORDER.length).toBeLessThanOrEqual(8);
    });
    it('classifies waypoints, timers, spawns and barriers by name', () => {
        expect(helperGroupOf('Troll Way Point')).toBe('waypoint');
        expect(helperGroupOf('Skeleton Dragon AnimWay Point')).toBe('waypoint');
        expect(helperGroupOf('YellowOrc WP : Walk')).toBe('waypoint');
        expect(helperGroupOf('Timer')).toBe('timer');
        expect(helperGroupOf('Dragon Barrier')).toBe('barrier');
        expect(helperGroupOf('Player Spawn')).toBe('spawn');
        expect(helperGroupOf('Triggered Sound')).toBe('sound');
        expect(helperGroupOf('Oak Tree')).toBeNull();
    });
    it('routes helpers to icons when asked, with or without a model', () => {
        const objs = [o(1, '0x10001', 'Oak Tree'), o(2, '0x10002', 'Trigger Detector'), o(3, '0x10003', 'Timer')];
        const p = partitionObjects(
            objs,
            () => true,
            () => true,
            true,
        );
        expect(p.icons.map((i) => [i.o.id, i.group])).toEqual([
            [2, 'trigger'],
            [3, 'timer'],
        ]);
        expect([...p.byResource.keys()]).toEqual(['0x10001']);
        const legacy = partitionObjects(
            objs,
            () => true,
            () => true,
            false,
        );
        expect(legacy.icons).toEqual([]);
        expect(legacy.byResource.size).toBe(3);
    });
    it('switched-off kinds are hidden, not drawn as icons', () => {
        const p = partitionObjects(
            [o(2, '0x2', 'Looping Sound')],
            () => true,
            (g) => g !== 'sound',
            true,
        );
        expect(p.hidden.length).toBe(1);
        expect(p.icons).toEqual([]);
    });
    it('learns unnamed helper classes from their model', () => {
        const p = partitionObjects(
            [o(1, '0x5', 'Wren_2'), o(2, '0x6', 'Oak Tree')],
            () => true,
            () => true,
            true,
            (r) => (r === '0x5' ? 'sound' : null),
        );
        expect(p.icons.map((i) => i.o.id)).toEqual([1]);
    });
    it('stores the editor icons option and ignores stale shapes', () => {
        expect(DEFAULT_VIEW_OPTIONS.editorIcons).toBe(true);
        expect(parseViewOptions({ editorIcons: false }).editorIcons).toBe(false);
        expect(parseViewOptions({ editorIcons: 'no', helpers: { barrier: false } }).helpers.barrier).toBe(false);
        expect(parseViewOptions({ editorIcons: 'no' }).editorIcons).toBe(true);
    });
});

describe('history names', () => {
    it('describes one object by name and several by count', () => {
        expect(describeObjects([{ class: 'Orc', id: 123 }])).toBe('Orc #123');
        expect(
            describeObjects([
                { class: 'Orc', id: 1 },
                { class: 'Orc', id: 2 },
            ]),
        ).toBe('2 objects');
    });
    it('names the objects in the stack labels of duplicate', () => {
        const doc = new LevelDocument();
        const obj = doc.insert({
            id: 0,
            class: 'Orc',
            resource: '0x1',
            kind: 'creature',
            transform: { pos: [0, 0, 0], rot: [0, 0, 0] },
            props: {},
        } as never);
        expect(objectLabel(obj)).toBe(`Orc #${obj.id}`);
        const stack = new CommandStack(doc);
        stack.duplicate([obj.id]);
        expect(stack.history().labels).toEqual([`Duplicate Orc #${obj.id}`]);
    });
});

describe('addLinks', () => {
    function ctxFor(doc: LevelDocument, labels: string[]): Ctx {
        return {
            doc: () => doc,
            edit: (label: string, ids: readonly number[], fn: (x: never) => void) => {
                labels.push(label);
                for (const i of ids) fn(doc.get(i) as never);
            },
        } as unknown as Ctx;
    }
    const mk = (doc: LevelDocument, cls: string) =>
        doc.insert({
            id: 0,
            class: cls,
            resource: '0x1',
            kind: 'usable',
            transform: { pos: [0, 0, 0], rot: [0, 0, 0] },
            props: {},
        } as never);
    it('links sources to a target once, as one named edit', () => {
        const doc = new LevelDocument();
        const a = mk(doc, 'Trigger Detector');
        const b = mk(doc, 'Timer');
        const labels: string[] = [];
        const ctx = ctxFor(doc, labels);
        expect(addLinks(ctx, [a.id], b.id)).toBe(1);
        expect(doc.get(a.id)!.links).toEqual([b.id]);
        expect(labels).toEqual([`Link Trigger Detector #${a.id} to Timer #${b.id}`]);
        expect(addLinks(ctx, [a.id], b.id)).toBe(0);
        expect(addLinks(ctx, [b.id], b.id)).toBe(0);
        expect(labels.length).toBe(1);
    });
});

describe('context menu registry', () => {
    it('filters entries by what the menu was opened on', () => {
        const off = registerContextItem({ id: 't.world', label: 'W', where: 'world', run: () => {} });
        const off2 = registerContextItem({ id: 't.obj', label: 'O', where: 'object', run: () => {} });
        expect(contextItems({ ids: [], ground: [1, 2, 3] }).map((x) => x.id)).toContain('t.world');
        expect(contextItems({ ids: [], ground: [1, 2, 3] }).map((x) => x.id)).not.toContain('t.obj');
        expect(contextItems({ ids: [4], ground: null }).map((x) => x.id)).toContain('t.obj');
        expect(contextItems({ ids: [4], ground: null }).map((x) => x.id)).not.toContain('t.world');
        off();
        off2();
        expect(contextItems({ ids: [4], ground: [0, 0, 0] }).map((x) => x.id)).not.toContain('t.obj');
    });
});

import { describe, expect, it } from 'vitest';
import { newCampaign, newProject, withExitOverrides, type ExitIndex } from '@drakan/editor-core';
import {
    WIZARD_STEPS,
    autoLinkable,
    copyProjectLevel,
    copyTitleProblem,
    defaultCopyTitle,
    fixFor,
    linkPlans,
    stepsDone,
    wizardExits,
} from '../src/editor/project/wizardplan.ts';

const make = () => {
    const p = newProject('Test campaign');
    p.levels = [
        { title: 'Surdana Copy', copyOf: 'Surdana' },
        { title: 'Kragmor Copy', copyOf: 'Kragmor' },
    ];
    p.campaign = { ...newCampaign(p), start: 'Surdana Copy' };
    return p;
};
const disc: ExitIndex = {
    levels: new Map([
        [
            'Surdana',
            [
                { id: 1, name: 'Road', dest: 'Kragmor' },
                { id: 2, name: 'Portal', dest: 'portal', portal: true },
            ],
        ],
        ['Kragmor', [{ id: 3, name: 'Return', dest: 'Surdana' }]],
    ]),
};

describe('campaign wizard plan', () => {
    it('has a stable route from name to shipping', () => {
        expect(WIZARD_STEPS.map((s) => s.id)).toEqual(['about', 'levels', 'exits', 'check', 'play', 'ship']);
    });
    it('rejects unsafe/empty/colliding folder titles and makes bounded unique defaults', () => {
        for (const t of ['', '../bad', 'CON', 'LPT1.txt', '...', 'last.', 'a/b', 'x'.repeat(49)])
            expect(copyTitleProblem(t, [])).not.toBeNull();
        expect(copyTitleProblem('surdana-copy', ['Surdana Copy'])).not.toBeNull();
        expect(copyTitleProblem('Valley (2)', [])).toBeNull();
        expect(defaultCopyTitle('Level 1A - Surdana', ['Surdana Copy'])).toBe('Surdana Copy 2');
        expect(copyTitleProblem(defaultCopyTitle('x'.repeat(100), []), [])).toBeNull();
    });
    it('suggests forward and return links, then only the missing return', () => {
        const p = make();
        const data = { disc, current: withExitOverrides(p, disc, []) };
        const plan = linkPlans(p, data)[0]!;
        expect(plan.state).toBe('suggested');
        expect(plan.forward.map((e) => e.id)).toEqual([1]);
        expect(plan.back.map((e) => e.id)).toEqual([3]);
        expect(autoLinkable([plan])).toHaveLength(1);
        data.current = withExitOverrides(p, disc, [{ level: 'Surdana Copy', exitId: 1, dest: 'Kragmor Copy' }]);
        expect(linkPlans(p, data)[0]!.state).toBe('linked');
        expect(autoLinkable(linkPlans(p, data))).toHaveLength(1);
        data.current = withExitOverrides(p, disc, [
            { level: 'Surdana Copy', exitId: 1, dest: 'Kragmor Copy' },
            { level: 'Kragmor Copy', exitId: 3, dest: 'Surdana Copy' },
        ]);
        expect(autoLinkable(linkPlans(p, data))).toEqual([]);
    });
    it('never claims an unknown route or a disc-level edit is auto-linkable', () => {
        const p = make();
        expect(linkPlans(p, null)[0]!.state).toBe('unknown');
        p.levels[0] = { title: 'Surdana' };
        expect(linkPlans(p, { disc, current: disc })[0]!.state).toBe('disc');
    });
    it('keeps an explicitly cleared destination rather than falling back to the disc', () => {
        const p = make();
        const current: ExitIndex = { levels: new Map([['Surdana Copy', [{ id: 1, name: 'Road', dest: null }]]]) };
        expect(wizardExits(p.levels[0]!, { disc, current })).toEqual([
            { id: 1, name: 'Road', discDest: 'Kragmor', dest: null },
        ]);
    });
    it('offers a retarget fix for an exit whose destination has a project copy', () => {
        const p = make();
        const fix = fixFor(
            p,
            {
                code: 'campaign-exit-out',
                severity: 'warning',
                level: 'Surdana Copy',
                message: 'Surdana Copy: exit "Road" leads into Drakan\'s own "Kragmor"',
            },
            { disc, current: withExitOverrides(p, disc, []) },
        );
        expect(fix).toMatchObject({ kind: 'retarget', level: 'Surdana Copy', exitId: 1, to: 'Kragmor Copy' });
    });
    it('copies project metadata without mutating the source or losing starts/layout', () => {
        const p = make();
        p.levels[0] = { title: 'Surdana' };
        p.start = 'Surdana';
        p.campaign!.start = 'Surdana';
        p.mapLayout = { Surdana: [1, 2] };
        const out = copyProjectLevel(p, 'Surdana', 'New Valley');
        expect(out.levels[0]).toEqual({ title: 'New Valley', copyOf: 'Surdana' });
        expect(out.start).toBe('New Valley');
        expect(out.campaign!.start).toBe('New Valley');
        expect(out.mapLayout).toEqual({ 'New Valley': [1, 2] });
        expect(p.mapLayout).toEqual({ Surdana: [1, 2] });
        expect(p.levels[0]!.title).toBe('Surdana');
    });
    it('does not mark an unchecked campaign ready and permits a single-level campaign', () => {
        const p = make();
        const unchecked = { issues: null, played: false, exported: false };
        expect(stepsDone(p, unchecked, []).check).toBe(false);
        expect(stepsDone(p, unchecked, []).exits).toBe(false);
        p.levels = p.levels.slice(0, 1);
        expect(stepsDone(p, { ...unchecked, issues: [] }, [])).toMatchObject({
            levels: true,
            check: true,
            exits: true,
        });
    });
});

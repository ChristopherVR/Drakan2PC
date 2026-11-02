// Synthetic only: titles and exit tables are made up.
import { describe, expect, it } from 'vitest';
import {
    WORKFLOWS,
    addLevelCopy,
    addDiscLevel,
    exitDestinationText,
    levelExits,
    moveLevel,
    newProject,
    nextStep,
    parseProject,
    parseProjectFile,
    serializeProjectFile,
    removeLevel,
    routeOrder,
    serializeProject,
    slugId,
    stepStates,
    suggestCopyTitle,
    validateProject,
    type ExitIndex,
    type StepFacts,
} from '../src/index.ts';

const exits: ExitIndex = {
    levels: new Map([
        ['Level A - One', [{ id: 5, name: 'To Two', dest: 'Level B - Two', portal: false }]],
        ['Level B - Two', [{ id: 9, name: 'Back', dest: 'Level A - One', portal: false }]],
        [
            'Level C - Three',
            [
                { id: 2, name: 'Nowhere', dest: 'Level Z - Gone', portal: false },
                { id: 3, name: 'Portal', dest: 'portal', portal: false },
            ],
        ],
    ]),
};
const disc = ['Level A - One', 'Level B - Two', 'Level C - Three'];

describe('project file', () => {
    it('slugs ids and round-trips', () => {
        expect(slugId('My Map!')).toBe('my-map');
        expect(slugId('   ')).toBe('my-mod');
        let p = newProject('My Map', 'multiplayer', 1);
        p = addDiscLevel(p, 'Level A - One');
        p = addLevelCopy(p, 'Level A - One', 'Level 9X - Copy');
        expect(parseProject(serializeProject(p))).toEqual(p);
    });
    it('keeps level edits in the project file', () => {
        const p = addDiscLevel(newProject('Edits', null, 1), 'Level A - One');
        const doc = { level: 'Level A - One', objects: [] } as never;
        const text = serializeProjectFile(p, { 'Level A - One': { savedAt: 5, doc, bookmarks: [] } });
        const back = parseProjectFile(text);
        expect(back.project).toEqual(p);
        expect(back.edits['Level A - One']?.savedAt).toBe(5);
        expect(parseProject(text)).toEqual(p);
        expect(parseProjectFile(serializeProject(p)).edits).toEqual({});
        expect(parseProjectFile('{"format":1,"id":"x","edits":{"a":5,"b":{"doc":{}}}}').edits).toHaveProperty('b');
    });
    it('rejects other files', () => {
        expect(() => parseProject('nope')).toThrow(/not a project/);
        expect(() => parseProject('{"kind":"level"}')).toThrow(/not a project/);
        expect(() => parseProject('{"format":99,"id":"x"}')).toThrow(/newer/);
    });
    it('edits the level list', () => {
        let p = newProject('x');
        p = addDiscLevel(addDiscLevel(p, 'Level A - One'), 'Level B - Two');
        expect(addDiscLevel(p, 'level a - one').levels).toHaveLength(2);
        expect(moveLevel(p, 'Level B - Two', -1).levels[0]!.title).toBe('Level B - Two');
        expect(() => addLevelCopy(p, 'Level A - One', 'Level A - One')).toThrow(/already/);
        p = { ...p, start: 'Level B - Two' };
        expect(removeLevel(p, 'Level B - Two').start).toBeNull();
        expect(suggestCopyTitle(p, 'Level A - One', disc)).toBe('Level A - One (copy)');
    });
});

describe('campaign checks', () => {
    it('lists exits; a copy arrives at the way back to its source', () => {
        let p = addDiscLevel(newProject('x'), 'Level A - One');
        p = addLevelCopy(p, 'Level A - One', 'Level 9X - Copy');
        const e = levelExits(p, p.levels[0]!, exits);
        expect(e).toHaveLength(1);
        expect(e[0]!.problem).toBeNull();
        expect(e[0]!.inProject).toBe(false);
        const c = levelExits(p, p.levels[1]!, exits);
        expect(c[0]!.dest).toBe('Level B - Two');
        // Level B leads back to A, not to the copy: the game arrives at that marker (the copy's source), no problem.
        expect(c[0]!.problem).toBeNull();
    });
    it('flags destinations that are not on the disc', () => {
        const p = addDiscLevel(newProject('x'), 'Level C - Three');
        const issues = validateProject(p, { discLevels: disc, exits });
        expect(issues.filter((i) => i.code === 'exit' && i.severity === 'warning')).toHaveLength(1);
        // the portal exit (destination text "portal") is not a level and raises nothing
        expect(issues.some((i) => i.exitId === 3)).toBe(false);
    });
    it('orders by route and reports unreachable levels', () => {
        let p = newProject('x');
        for (const t of ['Level C - Three', 'Level B - Two', 'Level A - One']) p = addDiscLevel(p, t);
        p = { ...p, start: 'Level A - One' };
        expect(routeOrder(p, exits).map((l) => l.title)).toEqual(['Level A - One', 'Level B - Two', 'Level C - Three']);
        const issues = validateProject(p, { discLevels: disc, exits });
        expect(issues.filter((i) => i.code === 'unreachable').map((i) => i.level)).toEqual(['Level C - Three']);
    });
    it('validates ids, titles and file name clashes', () => {
        let p = { ...newProject('x'), id: 'bad id', version: 'v1' };
        p = addLevelCopy(p, 'Level A - One', 'Level 9X - My Map');
        p = addLevelCopy(p, 'Level A - One', 'Level 9X - Other Map');
        const codes = validateProject(p, { discLevels: disc, exits: null }).map((i) => i.code);
        expect(codes).toContain('id');
        expect(codes).toContain('version');
        expect(codes).toContain('title-clash');
    });
});

describe('workflows', () => {
    const none: StepFacts = {
        disc: true,
        levelOpen: false,
        levelEdits: false,
        errors: 0,
        projectLevels: 0,
        projectCopies: 0,
        campaignErrors: 0,
        exitsChecked: true,
        mpActive: false,
        mpNamed: false,
        mpSpawns: 0,
        mpPickups: 0,
        mpErrors: 0,
        storyChanges: 0,
        saveOpen: false,
        saveEdited: false,
        exported: false,
        played: false,
    };
    it('ticks steps from facts and by hand', () => {
        const w = WORKFLOWS.multiplayer;
        expect(nextStep(stepStates(w, none, []))).toBe(0);
        const f = { ...none, mpActive: true, mpNamed: true, mpSpawns: 3 };
        const s = stepStates(w, f, ['export']);
        expect(s.slice(0, 3)).toEqual([true, true, true]);
        expect(s[s.length - 1]).toBe(true);
        expect(nextStep(s)).toBe(3);
    });
    it('every command-less step has a way to be ticked and ids are unique', () => {
        for (const w of Object.values(WORKFLOWS)) {
            expect(new Set(w.steps.map((s) => s.id)).size).toBe(w.steps.length);
        }
    });
});

describe('exit destination text', () => {
    it('never shows the raw word portal', () => {
        expect(exitDestinationText({ dest: 'portal', portal: false })).toBe('Portal inside this level');
        expect(exitDestinationText({ dest: ' Portal ', portal: true })).toBe('Portal inside this level');
        expect(exitDestinationText({ dest: null, portal: true })).toBe('Portal inside this level');
        expect(exitDestinationText({ dest: null, portal: false })).toBe('unknown destination');
        expect(exitDestinationText({ dest: 'Level B - Two', portal: false })).toBe('Level B - Two');
    });
});

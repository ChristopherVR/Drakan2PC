import { describe, expect, it } from 'vitest';
import {
    TEMPLATES,
    availableTemplates,
    primaryLabel,
    primaryNote,
    startPlan,
    suggestedLevels,
    templateById,
    templateStepStates,
} from '../src/editor/starttemplates.ts';
import type { StepFacts } from '@drakan/editor-core';

const t = (id: string) => templateById(id)!;
const none = { disc: false, levelOpen: false };
const disc = { disc: true, levelOpen: false };
const level = { disc: true, levelOpen: true };

const facts = (over: Partial<StepFacts> = {}): StepFacts => ({
    disc: true,
    levelOpen: false,
    levelEdits: false,
    errors: 0,
    projectLevels: 0,
    projectCopies: 0,
    campaignErrors: 0,
    exitsChecked: false,
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
    ...over,
});

describe('start page templates', () => {
    it('every template has a title, a line, steps and a unique id', () => {
        const ids = new Set(TEMPLATES.map((x) => x.id));
        expect(ids.size).toBe(TEMPLATES.length);
        for (const x of TEMPLATES) {
            expect(x.title.length).toBeGreaterThan(0);
            expect(x.line.length).toBeGreaterThan(0);
            expect(x.steps.length).toBeGreaterThanOrEqual(2);
            expect(x.button.length).toBeGreaterThan(0);
            if (x.needs === 'level' || x.id === 'newlevel') expect(x.pickTitle).toBeTruthy();
        }
    });

    it('asks for the disc first, then a level, then runs', () => {
        expect(startPlan(t('arena'), none)).toEqual(['disc', 'pick', 'run']);
        expect(startPlan(t('arena'), disc)).toEqual(['pick', 'run']);
        expect(startPlan(t('arena'), level)).toEqual(['run']);
        expect(startPlan(t('story'), none)).toEqual(['disc', 'run']);
        expect(startPlan(t('story'), disc)).toEqual(['run']);
        // A save needs no disc; a copy always picks the level to copy.
        expect(startPlan(t('save'), none)).toEqual(['run']);
        expect(startPlan(t('newlevel'), level)).toEqual(['pick', 'run']);
    });

    it('labels the button with what it does next', () => {
        expect(primaryLabel(t('puzzle'), none)).toBe('Open your disc to start');
        expect(primaryLabel(t('puzzle'), disc)).toBe('Choose a level');
        expect(primaryLabel(t('puzzle'), level)).toBe('Start in the open level');
        expect(primaryLabel(t('save'), none)).toBe('Open the Save Editor');
        expect(primaryNote(t('puzzle'), none)).toMatch(/disc image .* then a level/);
        expect(primaryNote(t('story'), none)).toMatch(/carries on/);
    });

    it('hides templates whose feature is not installed', () => {
        const ids = availableTemplates(() => false).map((x) => x.id);
        expect(ids).not.toContain('vendor');
        expect(ids).not.toContain('menus');
        expect(ids).not.toContain('ootf');
        expect(availableTemplates(() => true).length).toBe(TEMPLATES.length);
    });

    it('suggests Surdana for a shop', () => {
        const levels = [{ name: 'Level 0 - Intro' }, { name: 'Level 1A - Surdana' }];
        expect(suggestedLevels(t('vendor'), levels)).toEqual([{ name: 'Level 1A - Surdana' }]);
        expect(suggestedLevels(t('arena'), levels)).toEqual([]);
    });

    it('ticks steps off from what the editor sees and from hand ticks', () => {
        const p = t('puzzle');
        expect(templateStepStates(p, facts(), [])).toEqual([false, false, false, false]);
        expect(templateStepStates(p, facts({ levelOpen: true, levelEdits: true }), [])).toEqual([
            true,
            true,
            false,
            false,
        ]);
        const m = t('menus');
        expect(templateStepStates(m, facts(), ['open'])[0]).toBe(true);
    });
});

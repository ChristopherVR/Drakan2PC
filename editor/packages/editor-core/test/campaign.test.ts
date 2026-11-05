// Synthetic only: titles and exit tables are made up.
import { describe, expect, it } from 'vitest';
import { unzlibSync } from 'fflate';
import {
    COVER_PALETTES,
    buildManifestToml,
    campaignIdProblem,
    campaignMeta,
    campaignSlug,
    newCampaign,
    newProject,
    parseProject,
    pngSize,
    removeLevel,
    renderCoverPng,
    serializeProject,
    validateCampaign,
    validateModMeta,
    withExitOverrides,
    type ExitIndex,
    type ModProject,
} from '../src/index.ts';

const disc = ['Level A - One', 'Level B - Two', 'Level C - Three'];
const exits: ExitIndex = {
    levels: new Map([
        ['Level A - One', [{ id: 5, name: 'To Two', dest: 'Level B - Two', portal: false }]],
        ['Level B - Two', [{ id: 9, name: 'Back', dest: 'Level A - One', portal: false }]],
        ['Level C - Three', []],
    ]),
};

function campaignProject(): ModProject {
    const p = newProject('The Test Valley');
    p.levels = [
        { title: 'One Copy', copyOf: 'Level A - One' },
        { title: 'Two Copy', copyOf: 'Level B - Two' },
    ];
    p.campaign = newCampaign(p, 7);
    return p;
}
const codes = (p: ModProject, ex: ExitIndex | null = null): string[] =>
    validateCampaign(p, { discLevels: disc, exits: ex }).map((i) => `${i.severity}:${i.code}`);

describe('campaign manifest keys', () => {
    it('accepts custom ids and refuses the game words', () => {
        expect(campaignIdProblem('test-valley')).toBeNull();
        expect(campaignIdProblem('ootf')).toBeNull();
        for (const bad of ['d1', 'd2', 'drakan', 'ask', '', '-x', 'Big', 'a'.repeat(33), 'a b'])
            expect(campaignIdProblem(bad)).not.toBeNull();
        expect(campaignSlug('The Lost Valley!')).toBe('the-lost-valley');
        expect(campaignSlug('Drakan')).toBe('drakan-campaign');
        expect(campaignSlug('***')).toBe('my-campaign');
    });
    it('writes campaign, name, description, start and saves', () => {
        const toml = buildManifestToml({
            id: 'test.campaign',
            target: { profile: 'pal' },
            levels: ['One Copy', 'Two Copy'],
            campaign: 'test-valley',
            campaignName: 'The Test Valley',
            campaignDescription: 'Two valleys,\nnew paths.',
            campaignStart: 'One Copy',
            campaignSaves: 'testvalley',
        });
        expect(toml).toContain('campaign = "test-valley"');
        expect(toml).toContain('campaign_name = "The Test Valley"');
        expect(toml).toContain('campaign_description = "Two valleys, new paths."');
        expect(toml).toContain('campaign_start = "One Copy"');
        expect(toml).toContain('campaign_saves = "testvalley"');
        expect(toml.indexOf('campaign_saves')).toBeLessThan(toml.indexOf('[target]'));
        expect(() => validateModMeta({ id: 'x', target: { profile: 'pal' }, campaign: 'd2' })).toThrow();
        expect(() =>
            validateModMeta({ id: 'x', target: { profile: 'pal' }, campaign: 'ok', campaignSaves: 'a/b' }),
        ).toThrow();
        // the Order of the Flame converter's keys still work
        expect(buildManifestToml({ id: 'o', target: { profile: 'pal' }, levels: ['L'], campaign: 'ootf' })).toContain(
            'campaign = "ootf"',
        );
    });
    it('refuses level names the runtime would skip (a localized title with umlauts loads as no level)', () => {
        expect(() =>
            validateModMeta({ id: 'x', target: { profile: 'pal' }, levels: ['OotF 04 - Grimstone-Höhlen'] }),
        ).toThrow(/Grimstone-Höhlen.*not plain ASCII/);
        expect(() => validateModMeta({ id: 'x', target: { profile: 'pal' }, levels: ['A: B'] })).toThrow(/disc path/);
        expect(() =>
            validateModMeta({ id: 'x', target: { profile: 'pal' }, levels: ['OotF 04 - Grimstone-Hoehlen'] }),
        ).not.toThrow();
    });
});

describe('project campaign settings', () => {
    it('round-trips through the project file; old files have none', () => {
        const p = campaignProject();
        const back = parseProject(serializeProject(p));
        expect(back.campaign).toEqual(p.campaign);
        expect(parseProject(serializeProject(newProject('x'))).campaign).toBeUndefined();
        expect(p.campaign).toMatchObject({ id: 'the-test-valley', start: 'One Copy', saves: 'the-test-valley' });
    });
    it('gives manifest keys only when switched on', () => {
        const p = campaignProject();
        expect(campaignMeta(p)).toMatchObject({ campaign: 'the-test-valley', campaignStart: 'One Copy' });
        expect(campaignMeta(p).campaignSaves).toBeUndefined(); // same as the id: left out
        p.campaign!.enabled = false;
        expect(campaignMeta(p)).toEqual({});
    });
    it('forgets a removed start level', () => {
        expect(removeLevel(campaignProject(), 'One Copy').campaign!.start).toBeNull();
    });
});

describe('validateCampaign', () => {
    it('checks the settings without exits', () => {
        const p = campaignProject();
        expect(codes(p).filter((c) => c.startsWith('error'))).toEqual([]);
        p.campaign!.start = null;
        p.campaign!.saves = 'bad folder';
        p.campaign!.title = ' ';
        expect(codes(p)).toEqual(
            expect.arrayContaining(['error:campaign-start', 'error:campaign-saves', 'error:campaign-title']),
        );
        const q = campaignProject();
        q.levels.push({ title: 'Level C - Three' });
        q.campaign!.start = 'Level C - Three';
        expect(codes(q)).toEqual(expect.arrayContaining(['error:campaign-start', 'warning:campaign-disc-level']));
    });
    it('finds unreachable levels and exits that lead out', () => {
        const p = campaignProject();
        // As on the disc, One Copy's exit still leads to the disc level, so Two Copy is not reached.
        const plain = codes(p, withExitOverrides(p, exits, []));
        expect(plain).toEqual(expect.arrayContaining(['error:campaign-unreachable', 'warning:campaign-exit-out']));
        // Point that exit at the copy: everything is reached; Two Copy's way back still leaves the campaign.
        const linked = withExitOverrides(p, exits, [{ level: 'One Copy', exitId: 5, dest: 'Two Copy' }]);
        const c = codes(p, linked);
        expect(c).not.toContain('error:campaign-unreachable');
        expect(c.filter((x) => x === 'warning:campaign-exit-out')).toHaveLength(1);
        const nowhere = withExitOverrides(p, exits, [{ level: 'One Copy', exitId: 5, dest: 'Nowhere Land' }]);
        expect(codes(p, nowhere)).toContain('error:campaign-exit-nowhere');
    });
    it('says nothing when the campaign is off', () => {
        const p = campaignProject();
        p.campaign!.enabled = false;
        expect(codes(p, exits)).toEqual([]);
    });
});

describe('cover pictures', () => {
    it('makes a 960 x 540 PNG, the same for the same seed', () => {
        const a = renderCoverPng({ palette: 'dusk', seed: 3, width: 96, height: 54 });
        const b = renderCoverPng({ palette: 'dusk', seed: 3, width: 96, height: 54 });
        const c = renderCoverPng({ palette: 'dusk', seed: 4, width: 96, height: 54 });
        expect(pngSize(a)).toEqual({ width: 96, height: 54 });
        expect(a).toEqual(b);
        expect(a).not.toEqual(c);
        // IDAT inflates to (1 + 96 * 3) * 54 bytes
        const len = new DataView(a.buffer, a.byteOffset).getUint32(33);
        expect(unzlibSync(a.subarray(41, 41 + len)).length).toBe((1 + 96 * 3) * 54);
        for (const pal of COVER_PALETTES)
            expect(renderCoverPng({ palette: pal.id, seed: 1, width: 32, height: 18 }).length).toBeGreaterThan(60);
        expect(pngSize(renderCoverPng({ palette: 'night', seed: 9 }))).toEqual({ width: 960, height: 540 });
    });
});

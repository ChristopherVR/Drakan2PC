// Synthetic only: issue texts in validateProject's wording, level titles made up in the disc's style.
import { addDiscLevel, addLevelCopy, newProject, validateProject, type ProjectIssue } from '@drakan/editor-core';
import { describe, expect, it } from 'vitest';
import {
    countIssues,
    describeIssue,
    exitSummary,
    groupIssues,
    parseExitIssue,
    reorder,
    shortLevelName,
    worstSeverity,
} from '../src/editor/project/checks.ts';

const exitIssue = (level: string, name: string, dest: string, problem: string, exitId = 7): ProjectIssue => ({
    severity: 'warning',
    code: 'exit',
    message: `${level}: exit "${name}" to ${dest}: ${problem}.`,
    level,
    exitId,
});

const NO_WAY_BACK = (dest: string, from: string): string =>
    `"${dest}" has no exit leading back to "${from}", so the player arrives with no marker (and falls)`;

describe('project checks', () => {
    it('shortens disc level titles', () => {
        expect(shortLevelName('Level 1B - Kragmor')).toBe('Kragmor');
        expect(shortLevelName('Level 3A - Andrellian Isles')).toBe('Andrellian Isles');
        expect(shortLevelName('Surdana Copy')).toBe('Surdana Copy');
    });

    it('parses an exit issue back into name, destination and problem', () => {
        const i = exitIssue('Level 1A - One', 'Transition from 1(A) to 1 (B)', 'Level 1B - Two', NO_WAY_BACK('B', 'A'));
        expect(parseExitIssue(i)).toEqual({
            name: 'Transition from 1(A) to 1 (B)',
            dest: 'Level 1B - Two',
            problem: NO_WAY_BACK('B', 'A'),
        });
        const unnamed = exitIssue('X', '#12', '?', 'the destination of this exit could not be read');
        expect(parseExitIssue(unnamed)).toMatchObject({ name: '', dest: null });
        expect(parseExitIssue({ severity: 'error', code: 'id', message: 'x' })).toBeNull();
    });

    it('writes a short line with the long text kept as details', () => {
        const i = exitIssue(
            'Surdana Copy',
            'To 1B',
            'Level 1B - Kragmor',
            NO_WAY_BACK('Level 1B - Kragmor', 'Surdana Copy'),
        );
        const d = describeIssue(i);
        expect(d.short).toBe('No way back from Kragmor');
        expect(d.detail).toContain('Level 1B - Kragmor has no exit leading back to Surdana Copy');
        expect(d.detail).toContain('falls');

        expect(
            describeIssue(
                exitIssue(
                    'A',
                    'p',
                    'Level 4A - Ravenshold',
                    '"Level 4A - Ravenshold" has no object named "portal" to arrive at',
                ),
            ).short,
        ).toBe('No portal to arrive at in Ravenshold');
        expect(
            describeIssue(
                exitIssue('A', 'x', 'Level 9 - Gone', 'destination level "Level 9 - Gone" is not on this disc'),
            ).short,
        ).toBe('Gone is not on the disc');
        expect(describeIssue({ severity: 'info', code: 'unreachable', message: 'long', level: 'A' })).toEqual({
            short: 'No exit leads here yet',
            detail: 'long',
        });
        expect(
            describeIssue({ severity: 'error', code: 'version', message: 'The version must be dotted numbers.' }).short,
        ).toBe('The version is not numbers like 1.0.0');
        expect(describeIssue({ severity: 'warning', code: 'new-code', message: 'something odd.' }).short).toBe(
            'Something odd',
        );
    });

    it('prefers the exit itself when it is given', () => {
        const i = exitIssue('A', 'weird "quoted" name', 'B', 'mangled');
        const d = describeIssue(i, {
            name: 'Gate',
            dest: 'Level 2 - Shadowmire',
            problem: NO_WAY_BACK('Level 2 - Shadowmire', 'A'),
        });
        expect(d.short).toBe('No way back from Shadowmire');
        expect(d.detail).toContain('Exit "Gate"');
    });

    it('groups by level in campaign order with the mod settings first, and filters', () => {
        const issues: ProjectIssue[] = [
            { severity: 'info', code: 'unreachable', message: 'u', level: 'B' },
            exitIssue('B', 'e1', 'Level 1A - A', NO_WAY_BACK('Level 1A - A', 'B')),
            { severity: 'error', code: 'missing-level', message: 'm', level: 'B' },
            exitIssue('A', 'e2', 'Level 1B - B', NO_WAY_BACK('Level 1B - B', 'A')),
            { severity: 'error', code: 'id', message: 'id' },
            { severity: 'warning', code: 'exit', message: 'odd', level: 'Gone' },
        ];
        const g = groupIssues(issues, ['A', 'B']);
        expect(g.map((x) => x.key)).toEqual(['project', 'level:A', 'level:B', 'level:Gone']);
        expect(g[0]!.label).toBe('Mod settings');
        expect(g[2]!.items.map((m) => m.issue.severity)).toEqual(['error', 'warning', 'info']);
        expect(g[2]!.counts).toEqual({ error: 1, warning: 1, info: 1 });

        const errorsOnly = groupIssues(issues, ['A', 'B'], { error: true, warning: false, info: false });
        expect(errorsOnly.map((x) => x.key)).toEqual(['project', 'level:B']);
        expect(errorsOnly[1]!.items).toHaveLength(1);
        expect(errorsOnly[1]!.counts.warning).toBe(1);

        expect(countIssues(issues)).toEqual({ error: 2, warning: 3, info: 1 });
        expect(worstSeverity({ error: 0, warning: 2, info: 1 })).toBe('warning');
        expect(worstSeverity({ error: 0, warning: 0, info: 0 })).toBeNull();
    });

    it('keeps message keys stable and distinct', () => {
        const a = groupIssues([exitIssue('A', 'x', 'B', 'p', 1), exitIssue('A', 'x', 'B', 'p', 2)], ['A']);
        const keys = a[0]!.items.map((m) => m.key);
        expect(new Set(keys).size).toBe(2);
        expect(groupIssues([exitIssue('A', 'x', 'B', 'p', 1)], ['A'])[0]!.items[0]!.key).toBe(keys[0]);
    });

    it('reads the real validateProject wording', () => {
        let p = newProject('T');
        p = addDiscLevel(p, 'Level 1A - One');
        p = addLevelCopy(p, 'Level 1A - One', 'One Copy');
        p = { ...p, version: 'v1' };
        const exits = {
            levels: new Map([
                ['Level 1A - One', [{ id: 3, name: 'Gate', dest: 'Level 1B - Two', portal: false }]],
                ['Level 1B - Two', [{ id: 4, name: 'Back', dest: 'Level 1C - Three', portal: false }]],
            ]),
        };
        const issues = validateProject(p, { discLevels: ['Level 1A - One', 'Level 1B - Two'], exits });
        const shorts = groupIssues(
            issues,
            p.levels.map((l) => l.title),
        ).flatMap((g) => g.items.map((m) => m.short));
        expect(shorts).toContain('The version is not numbers like 1.0.0');
        expect(shorts).toContain('No way back from Two');
        expect(shorts).toContain('No exit leads here yet');
    });

    it('reorders by drop position', () => {
        expect(reorder(['a', 'b', 'c', 'd'], 0, 2)).toEqual(['b', 'a', 'c', 'd']);
        expect(reorder(['a', 'b', 'c', 'd'], 3, 0)).toEqual(['d', 'a', 'b', 'c']);
        expect(reorder(['a', 'b', 'c', 'd'], 1, 4)).toEqual(['a', 'c', 'd', 'b']);
        expect(reorder(['a', 'b'], 1, 1)).toEqual(['a', 'b']);
        expect(reorder(['a', 'b'], 5, 0)).toEqual(['a', 'b']);
    });

    it('summarises exits', () => {
        expect(exitSummary([])).toEqual({ count: 0, problems: 0, text: 'No exits' });
        expect(exitSummary([{ problem: null }, { problem: 'x' }]).text).toBe('2 exits, 1 to fix');
        expect(exitSummary([{ problem: null }]).text).toBe('1 exit');
    });
});

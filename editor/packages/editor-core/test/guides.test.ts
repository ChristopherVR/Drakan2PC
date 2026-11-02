import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import { GUIDES, WORKFLOWS, guideById, guideWebUrl, guidesMarkdown, searchGuides } from '../src/index.ts';

const page = fileURLToPath(new URL('../../../../docs/reference/editor-guide.md', import.meta.url));

describe('guides', () => {
    it('have unique ids and every workflow guide exists', () => {
        expect(new Set(GUIDES.map((g) => g.id)).size).toBe(GUIDES.length);
        for (const w of Object.values(WORKFLOWS)) {
            expect(guideById(w.guide), w.id).toBeDefined();
            for (const s of w.steps) if (s.guide) expect(guideById(s.guide), `${w.id}/${s.id}`).toBeDefined();
        }
    });
    it('have ids that are anchors on the published page', () => {
        const md = guidesMarkdown();
        for (const g of GUIDES) {
            expect(g.id).toMatch(/^[a-z][a-z0-9-]*$/);
            expect(md).toContain(`<a id="${g.id}"></a>`);
            expect(guideWebUrl(g.id)).toMatch(new RegExp(`/guides/editor-tasks\.html#${g.id}$`));
        }
        // The site publishes the page from the first anchor on (pages.json start_at), so "start" must stay first.
        expect(GUIDES[0]!.id).toBe('start');
    });
    it('search by every word, title matches first', () => {
        expect(searchGuides('')).toHaveLength(GUIDES.length);
        expect(searchGuides('shop')[0]!.id).toBe('vendors');
        expect(searchGuides('campaign map').map((g) => g.id)).toContain('campaign');
        expect(searchGuides('zzzz nothing')).toEqual([]);
    });
    it('are plain text', () => {
        const all = JSON.stringify(GUIDES);
        expect(all).not.toMatch(/[\u{1F300}-\u{1FAFF}\u2600-\u27BF]/u);
    });
    it('match docs/reference/editor-guide.md', () => {
        const want = guidesMarkdown();
        if (process.env['UPDATE_GUIDE']) writeFileSync(page, want);
        expect(readFileSync(page, 'utf8').replace(/\r\n/g, '\n')).toBe(want);
    });
});

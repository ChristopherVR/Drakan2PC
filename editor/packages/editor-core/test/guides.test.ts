import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import { GUIDES, WORKFLOWS, guideById, guidesMarkdown } from '../src/index.ts';

const page = fileURLToPath(new URL('../../../../docs/reference/editor-guide.md', import.meta.url));

describe('guides', () => {
    it('have unique ids and every workflow guide exists', () => {
        expect(new Set(GUIDES.map((g) => g.id)).size).toBe(GUIDES.length);
        for (const w of Object.values(WORKFLOWS)) {
            expect(guideById(w.guide), w.id).toBeDefined();
            for (const s of w.steps) if (s.guide) expect(guideById(s.guide), `${w.id}/${s.id}`).toBeDefined();
        }
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

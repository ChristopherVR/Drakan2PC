import { describe, expect, it } from 'vitest';
import { GUIDES } from '@drakan/editor-core';
import { GUIDE_ICONS } from '../src/editor/project/help.ts';

describe('help guides', () => {
    it('every guide has an icon of its own kind, not the generic question mark', () => {
        for (const g of GUIDES) expect(GUIDE_ICONS[g.id], g.id).toBeTruthy();
    });
});

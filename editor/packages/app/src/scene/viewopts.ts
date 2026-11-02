// Viewport display options for the level editor (pure data; applied by `LevelView`, shown in the Display menu).

import type { HelperGroup } from './instances.ts';

/** Story overlays drawn over the level (`story/storyview.ts`): cut scenes, shops, quest areas and talking characters. */
export type StoryGroup = 'cutscene' | 'vendor' | 'quest' | 'talk';

export const STORY_GROUPS: { id: StoryGroup; label: string; hint: string }[] = [
    { id: 'cutscene', label: 'Cut scenes', hint: 'Cut scene players, their camera paths, actors and trigger boxes' },
    { id: 'vendor', label: 'Shops', hint: 'Shopkeepers with their name and how many items they sell' },
    { id: 'quest', label: 'Quest areas', hint: 'Where quests start and finish, with the quest title' },
    { id: 'talk', label: 'Conversations', hint: 'Characters the player can talk to' },
];

export type MarkerMode = 'fallback' | 'all' | 'off';
export type LightingPreset = 'day' | 'overcast' | 'dusk' | 'flat';

export interface ViewOptions {
    models: boolean;
    /** Play skinned animations on placed characters (idle by default). */
    animate: boolean;
    markers: MarkerMode;
    terrain: boolean;
    wireframe: boolean;
    /** Which helper classes are drawn. */
    helpers: Record<HelperGroup, boolean>;
    /** Which story overlays are drawn (Show menu, "Story"). */
    story: Record<StoryGroup, boolean>;
    /** Draw helper classes as billboard icons (off: their placeholder models). */
    editorIcons: boolean;
    lighting: LightingPreset;
    fog: boolean;
    /** Camera far plane in scene units (1 unit = 1000 game units). */
    drawDistance: number;
    keepAboveGround: boolean;
}

export const DEFAULT_VIEW_OPTIONS: ViewOptions = {
    models: true,
    animate: true,
    markers: 'fallback',
    terrain: true,
    wireframe: false,
    helpers: {
        trigger: true,
        sound: true,
        camera: true,
        light: true,
        waypoint: true,
        timer: true,
        spawn: true,
        barrier: true,
    },
    story: { cutscene: true, vendor: true, quest: true, talk: true },
    editorIcons: true,
    lighting: 'day',
    fog: false,
    drawDistance: 3000,
    keepAboveGround: true,
};

export interface LightingSpec {
    label: string;
    sky: number;
    ground: number;
    hemi: number;
    sun: number;
    sunDir: [number, number, number];
}

export const LIGHTING: Record<LightingPreset, LightingSpec> = {
    day: { label: 'Day', sky: 0xffffff, ground: 0x6b7a58, hemi: 1.5, sun: 1.6, sunDir: [1, 2, 0.8] },
    overcast: { label: 'Overcast', sky: 0xdfe6ee, ground: 0x7d848c, hemi: 2.4, sun: 0.5, sunDir: [0.3, 2, 0.3] },
    dusk: { label: 'Dusk', sky: 0xffb48a, ground: 0x2c2f4a, hemi: 1.0, sun: 1.9, sunDir: [-2, 0.7, 0.6] },
    flat: { label: 'Flat (unlit look)', sky: 0xffffff, ground: 0xffffff, hemi: 3.0, sun: 0, sunDir: [0, 1, 0] },
};

const BOOLS = ['models', 'animate', 'terrain', 'wireframe', 'fog', 'keepAboveGround', 'editorIcons'] as const;

/** Merge a stored (possibly partial or stale) value over the defaults; unknown keys and bad types are dropped. */
export function parseViewOptions(raw: unknown): ViewOptions {
    const d = DEFAULT_VIEW_OPTIONS;
    const o: ViewOptions = { ...d, helpers: { ...d.helpers }, story: { ...d.story } };
    if (!raw || typeof raw !== 'object') return o;
    const r = raw as Record<string, unknown>;
    for (const k of BOOLS) {
        const v = r[k];
        if (typeof v === 'boolean') o[k] = v;
    }
    if (r.markers === 'fallback' || r.markers === 'all' || r.markers === 'off') o.markers = r.markers;
    if (typeof r.lighting === 'string' && r.lighting in LIGHTING) o.lighting = r.lighting as LightingPreset;
    if (typeof r.drawDistance === 'number' && Number.isFinite(r.drawDistance)) {
        o.drawDistance = Math.min(6000, Math.max(20, r.drawDistance));
    }
    if (r.helpers && typeof r.helpers === 'object') {
        const h = r.helpers as Record<string, unknown>;
        for (const g of Object.keys(o.helpers) as HelperGroup[]) {
            const v = h[g];
            if (typeof v === 'boolean') o.helpers[g] = v;
        }
    }
    if (r.story && typeof r.story === 'object') {
        const st = r.story as Record<string, unknown>;
        for (const g of Object.keys(o.story) as StoryGroup[]) {
            const v = st[g];
            if (typeof v === 'boolean') o.story[g] = v;
        }
    }
    return o;
}

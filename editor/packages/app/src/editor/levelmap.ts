// The map picture of a converted level, as the import dialog edits it: which picture, which part of the world it shows and which
// part of the picture that is. Pure rules (no DOM) so they are unit tested; the panel in `ootfmaps.ts` draws them.

import {
    mapPixelOf,
    type LevelMapOverride,
    type LevelMapSpec,
    type LevelRect,
    type PictureRect,
    type TopDownImage,
} from '@drakan/formats';

/** Where the picture comes from. */
export type MapSource =
    | { kind: 'default' } // the level's own Order of the Flame parchment
    | { kind: 'level'; path: string } // the parchment of another level
    | { kind: 'file'; name: string; image: TopDownImage }; // a picture of the player's own

/** What the player sees and edits for one level. */
export interface MapChoice {
    source: MapSource;
    /** The part of the world the picture shows, in level units. */
    world: LevelRect;
    /** The part of the picture that is, as fractions (0 to 1). */
    picture: PictureRect;
}

/** A spot to draw on the preview: Rynn's start, or the place of a quest marker. */
export interface MapPoint {
    kind: 'start' | 'quest';
    label: string;
    /** Picture pixels (0..255) of the 256 x 256 map texture. */
    px: number;
    py: number;
}

export const MIN_WORLD_SPAN = 2;
export const MIN_PICTURE_SPAN = 0.05;

const round2 = (v: number): number => Math.round(v * 100) / 100;

/** Keep a rectangle valid: ordered edges with at least `min` between them. */
export function normalizeWorld(r: LevelRect): LevelRect {
    const x0 = Math.min(r.x0, r.x1 - MIN_WORLD_SPAN);
    const z0 = Math.min(r.z0, r.z1 - MIN_WORLD_SPAN);
    return { x0, z0, x1: Math.max(r.x1, x0 + MIN_WORLD_SPAN), z1: Math.max(r.z1, z0 + MIN_WORLD_SPAN) };
}

export function normalizePicture(r: PictureRect): PictureRect {
    const c = (v: number): number => Math.max(0, Math.min(1, v));
    const x0 = Math.min(c(r.x0), c(r.x1) - MIN_PICTURE_SPAN);
    const y0 = Math.min(c(r.y0), c(r.y1) - MIN_PICTURE_SPAN);
    return { x0: round2(x0), y0: round2(y0), x1: round2(Math.max(c(r.x1), x0 + MIN_PICTURE_SPAN)), y1: round2(Math.max(c(r.y1), y0 + MIN_PICTURE_SPAN)) };
}

const sameRect = (a: object, b: object): boolean => JSON.stringify(a) === JSON.stringify(b);

/** The choice that shows the converter's own map (what you get when you change nothing). */
export function defaultChoice(spec: Pick<LevelMapSpec, 'world' | 'picture'>): MapChoice {
    return { source: { kind: 'default' }, world: { ...spec.world }, picture: { ...spec.picture } };
}

/**
 * The converter option for a choice: only what differs from the default, so an untouched level converts exactly as before.
 * `parchmentOf` gives the picture of another level's map.
 */
export function choiceToOverride(
    choice: MapChoice,
    base: Pick<LevelMapSpec, 'world' | 'picture'>,
    parchmentOf: (path: string) => TopDownImage | null,
): LevelMapOverride | undefined {
    const o: LevelMapOverride = {};
    if (choice.source.kind === 'file') o.image = choice.source.image;
    else if (choice.source.kind === 'level') {
        const img = parchmentOf(choice.source.path);
        if (img) o.image = img;
    }
    if (!sameRect(choice.world, base.world)) o.world = normalizeWorld(choice.world);
    // a new picture starts from the whole picture unless the player set the rectangle
    if (!sameRect(choice.picture, base.picture) || (o.image && choice.source.kind !== 'default'))
        o.picture = normalizePicture(choice.picture);
    return o.image || o.world || o.picture ? o : undefined;
}

/** Rectangle of the picture that holds the lit parchment, or the whole picture for a plain image of the player's own. */
export function pictureRectFor(source: MapSource, parchment: PictureRect): PictureRect {
    return source.kind === 'file' ? { x0: 0, y0: 0, x1: 1, y1: 1 } : { ...parchment };
}

/** Rynn's start and the quest markers on the 256 x 256 map texture, through the choice's calibration. */
export function previewPoints(
    choice: Pick<MapChoice, 'world' | 'picture'>,
    start: { x: number; z: number } | null,
    quests: readonly { label: string; x: number; z: number }[],
): MapPoint[] {
    const out: MapPoint[] = [];
    if (start) {
        const [px, py] = mapPixelOf(choice, start.x, start.z);
        out.push({ kind: 'start', label: 'Rynn starts here', px, py });
    }
    for (const q of quests) {
        const [px, py] = mapPixelOf(choice, q.x, q.z);
        out.push({ kind: 'quest', label: q.label, px, py });
    }
    return out;
}

/** True when a world point lies outside the rectangle the map shows (its marker would sit on the edge). */
export function isOffMap(world: LevelRect, xWorld: number, zWorld: number, cell = 2048): boolean {
    const x = xWorld / cell;
    const z = zWorld / cell;
    return x < world.x0 || x > world.x1 || z < world.z0 || z > world.z1;
}

/** Fit an image of any size into at most `max` pixels on its long side (keeps the proportions); the picture is shown stretched square in game. */
export function limitImage(img: TopDownImage, max = 1024): TopDownImage {
    const k = Math.min(1, max / Math.max(img.width, img.height));
    if (k === 1) return img;
    const w = Math.max(1, Math.round(img.width * k));
    const h = Math.max(1, Math.round(img.height * k));
    const out = new Uint8Array(w * h * 4);
    for (let y = 0; y < h; y++)
        for (let x = 0; x < w; x++) {
            const s = (Math.min(img.height - 1, Math.floor(y / k)) * img.width + Math.min(img.width - 1, Math.floor(x / k))) * 4;
            out.set(img.rgba.subarray(s, s + 4), (y * w + x) * 4);
        }
    return { width: w, height: h, rgba: out };
}

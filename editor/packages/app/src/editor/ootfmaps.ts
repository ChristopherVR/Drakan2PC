// "Map screen" section of the Order of the Flame import dialog: the picture the in-game map shows for each converted level,
// which part of the world it covers, and a live preview with Rynn's start and the quest markers on it. The default is the level's
// own parchment from the install; a level can show another level's parchment or a picture of your own. The choice goes to the
// converter (`OotfPipelineOptions.levelMaps`), which writes the pack entries the same way for every source.

import {
    defaultLevelMap,
    loadOotfMapPicture,
    pictureContent,
    OOTF_MAP_SETS_BY_LEVEL,
    pickRrcPath,
    SrscReader,
    loadPcLevel,
    type LevelMapOverride,
    type LevelMapSpec,
    type PcFileSource,
    type PcLevel,
    type TopDownImage,
} from '@drakan/formats';
import { h } from './dom.ts';
import {
    choiceToOverride,
    defaultChoice,
    isOffMap,
    limitImage,
    normalizePicture,
    normalizeWorld,
    pictureRectFor,
    previewPoints,
    type MapChoice,
} from './levelmap.ts';

export interface MapLevel {
    path: string;
    title: string;
}

interface Loaded {
    pc: PcLevel;
    spec: LevelMapSpec | null;
    start: { x: number; z: number } | null;
    quests: { label: string; x: number; z: number }[];
    choice: MapChoice | null;
}

/** The name a quest marker object shows on the map (the converter's rule: its object name without the "Map Trigger" prefix). */
export function markerLabel(name: string): string {
    return name
        .replace(/^map\s*(trigger|object)?\s*[-:]*\s*/i, '')
        .replace(/\s+icon$/i, '')
        .trim();
}

/** Decode a picture file of the player's own into pixels, top row first. */
export async function readPictureFile(file: File): Promise<TopDownImage> {
    const bmp = await createImageBitmap(file);
    const c = document.createElement('canvas');
    c.width = bmp.width;
    c.height = bmp.height;
    const g = c.getContext('2d')!;
    g.drawImage(bmp, 0, 0);
    const d = g.getImageData(0, 0, c.width, c.height);
    return limitImage({ width: c.width, height: c.height, rgba: new Uint8Array(d.data.buffer.slice(0)) });
}

const PREVIEW_W = 320;
const PREVIEW_H = 240;

/**
 * The dialog section. `setLevels` is called when the ticked levels change; `overrides()` gives the converter option (only levels
 * the player changed).
 */
export function levelMapsPanel(getSource: () => PcFileSource | null): {
    el: HTMLElement;
    setLevels(levels: MapLevel[], all: MapLevel[]): void;
    overrides(): Record<string, LevelMapOverride>;
} {
    const cards = h('div', { class: 'od-maps' });
    const note = h('div', { class: 'od-dim' }, 'Tick a level above to see its map.');
    const loaded = new Map<string, Loaded>();
    const parchments = new Map<string, TopDownImage | null>();
    let rrc: SrscReader | null | undefined;

    const rrcReader = async (): Promise<SrscReader | null> => {
        if (rrc !== undefined) return rrc;
        const src = getSource();
        rrc = null;
        if (src) {
            const { path } = pickRrcPath(await src.list());
            const bytes = path ? await src.read(path) : null;
            if (bytes) rrc = await SrscReader.fromBytes(bytes);
        }
        return rrc;
    };

    const parchmentOf = (path: string): TopDownImage | null => parchments.get(path.toLowerCase()) ?? null;
    const loadParchment = async (path: string): Promise<TopDownImage | null> => {
        const key = path.toLowerCase();
        if (parchments.has(key)) return parchments.get(key)!;
        const base = OOTF_MAP_SETS_BY_LEVEL.get(key);
        const r = await rrcReader();
        const img = base !== undefined && r ? await loadOotfMapPicture(r, base) : null;
        parchments.set(key, img);
        return img;
    };

    const load = async (level: MapLevel): Promise<Loaded> => {
        const have = loaded.get(level.path);
        if (have) return have;
        const src = getSource()!;
        const pc = await loadPcLevel(src, level.path);
        const r = await rrcReader();
        const spec = r ? await defaultLevelMap(pc, r) : null;
        if (spec) parchments.set(level.path.toLowerCase(), spec.image);
        const rynn = pc.list.objects.find((o) => pc.classes.get(o.dbIndex)?.get(o.classId)?.rflClass === 9);
        const quests: Loaded['quests'] = [];
        for (const o of pc.list.objects) {
            const c = pc.classes.get(o.dbIndex)?.get(o.classId);
            if (c?.rflClass !== 104) continue;
            const label = markerLabel(c.name);
            if (label) quests.push({ label, x: o.pos[0], z: o.pos[2] });
        }
        const out: Loaded = {
            pc,
            spec,
            start: rynn ? { x: rynn.pos[0], z: rynn.pos[2] } : null,
            quests,
            choice: spec ? defaultChoice(spec) : null,
        };
        loaded.set(level.path, out);
        return out;
    };

    function drawPreview(canvas: HTMLCanvasElement, l: Loaded, img: TopDownImage | null): void {
        const g = canvas.getContext('2d')!;
        g.fillStyle = '#14161a';
        g.fillRect(0, 0, canvas.width, canvas.height);
        if (!img || !l.choice) return;
        const t = document.createElement('canvas');
        t.width = img.width;
        t.height = img.height;
        t.getContext('2d')!.putImageData(new ImageData(new Uint8ClampedArray(img.rgba), img.width, img.height), 0, 0);
        // the game stretches the picture over a square texture and shows it 4:3: the preview shows the same
        g.drawImage(t, 0, 0, canvas.width, canvas.height);
        const p = l.choice.picture;
        g.strokeStyle = 'rgba(240,200,90,.9)';
        g.setLineDash([5, 4]);
        g.strokeRect(p.x0 * canvas.width, p.y0 * canvas.height, (p.x1 - p.x0) * canvas.width, (p.y1 - p.y0) * canvas.height);
        g.setLineDash([]);
        const pts = previewPoints(l.choice, l.start, l.quests);
        for (const pt of pts) {
            const x = (pt.px / 256) * canvas.width;
            const y = (pt.py / 256) * canvas.height;
            g.beginPath();
            g.fillStyle = pt.kind === 'start' ? '#e5483a' : '#4fb0ff';
            g.arc(x, y, pt.kind === 'start' ? 6 : 4, 0, Math.PI * 2);
            g.fill();
            g.strokeStyle = '#000';
            g.stroke();
        }
    }

    async function card(level: MapLevel, all: MapLevel[]): Promise<HTMLElement> {
        const l = await load(level);
        if (!l.spec || !l.choice) {
            return h('div', { class: 'od-map' }, h('b', {}, level.title), h('div', { class: 'od-dim' }, 'This level has no map picture in the install; it keeps the map of the template level.'));
        }
        const spec = l.spec;
        const choice = l.choice;
        const canvas = h('canvas', { width: PREVIEW_W, height: PREVIEW_H, style: `width:${PREVIEW_W}px;height:${PREVIEW_H}px;border-radius:6px;flex:none` });
        const warn = h('div', { class: 'od-dim' });
        const redraw = async (): Promise<void> => {
            let img: TopDownImage | null = spec.image;
            if (choice.source.kind === 'file') img = choice.source.image;
            else if (choice.source.kind === 'level') img = await loadParchment(choice.source.path);
            drawPreview(canvas, l, img);
            const off = [...(l.start ? [{ label: 'Rynn starts here', ...l.start }] : []), ...l.quests].filter((q) => isOffMap(choice.world, q.x, q.z));
            warn.textContent = off.length
                ? `${off.length} spot${off.length === 1 ? '' : 's'} lie outside the area this map shows and sit on its edge.`
                : 'Red dot: where Rynn starts. Blue dots: quest places.';
        };

        const sel = h('select', {});
        sel.append(h('option', { value: 'default' }, 'The level\'s own Order of the Flame map'));
        for (const o of all)
            if (OOTF_MAP_SETS_BY_LEVEL.has(o.path.toLowerCase()) && o.path !== level.path)
                sel.append(h('option', { value: `level:${o.path}` }, `Map of ${o.title}`));
        sel.append(h('option', { value: 'file' }, 'A picture of my own...'));
        const fileIn = h('input', { type: 'file', accept: 'image/png,image/jpeg,image/webp', hidden: true });
        const fileName = h('span', { class: 'od-dim' });
        const apply = (): void => {
            choice.world = normalizeWorld(choice.world);
            choice.picture = normalizePicture(choice.picture);
            void redraw();
        };
        sel.addEventListener('change', () => {
            if (sel.value === 'file') {
                fileIn.click();
                return;
            }
            choice.source = sel.value === 'default' ? { kind: 'default' } : { kind: 'level', path: sel.value.slice(6) };
            void (async () => {
                const img = sel.value === 'default' ? spec.image : await loadParchment(sel.value.slice(6));
                const rect = img ? pictureContent(img) : spec.picture;
                choice.picture = pictureRectFor(choice.source, rect);
                syncInputs();
                apply();
            })();
        });
        fileIn.addEventListener('change', () => {
            const f = fileIn.files?.[0];
            if (!f) {
                sel.value = choice.source.kind === 'file' ? 'file' : sel.value;
                return;
            }
            void readPictureFile(f).then(
                (image) => {
                    choice.source = { kind: 'file', name: f.name, image };
                    choice.picture = pictureRectFor(choice.source, spec.picture);
                    fileName.textContent = `${f.name} (${image.width} x ${image.height})`;
                    syncInputs();
                    apply();
                },
                () => {
                    fileName.textContent = 'That file could not be read as a picture.';
                },
            );
        });

        const num = (get: () => number, set: (v: number) => void, step: number): HTMLInputElement => {
            const i = h('input', { type: 'number', step: String(step), style: 'width:5.5em' });
            i.value = String(get());
            i.addEventListener('change', () => {
                const v = Number(i.value);
                if (Number.isFinite(v)) set(v);
                apply();
                syncInputs();
            });
            return i;
        };
        const inputs: { i: HTMLInputElement; get: () => number }[] = [];
        const field = (label: string, get: () => number, set: (v: number) => void, step: number): HTMLElement => {
            const i = num(get, set, step);
            inputs.push({ i, get });
            return h('label', { class: 'od-field' }, h('span', { class: 'od-dim' }, label), i);
        };
        const pct = (get: () => number, set: (v: number) => void): [() => number, (v: number) => void] => [
            () => Math.round(get() * 100),
            (v) => set(v / 100),
        ];
        const pe = (k: 'x0' | 'y0' | 'x1' | 'y1'): [() => number, (v: number) => void] =>
            pct(() => choice.picture[k], (v) => (choice.picture = { ...choice.picture, [k]: v }));
        const we = (k: 'x0' | 'z0' | 'x1' | 'z1'): [() => number, (v: number) => void] => [
            () => choice.world[k],
            (v) => (choice.world = { ...choice.world, [k]: v }),
        ];
        function syncInputs(): void {
            for (const x of inputs) x.i.value = String(x.get());
        }
        const reset = h(
            'button',
            {
                class: 'btn',
                type: 'button',
                onclick: () => {
                    Object.assign(choice, defaultChoice(spec));
                    sel.value = 'default';
                    fileName.textContent = '';
                    syncInputs();
                    apply();
                },
            },
            'Reset to the default',
        );
        const worldRow = h(
            'div',
            { class: 'od-row' },
            field('West edge', ...we('x0'), 1),
            field('East edge', ...we('x1'), 1),
            field('North edge', ...we('z0'), 1),
            field('South edge', ...we('z1'), 1),
        );
        const pictureRow = h(
            'div',
            { class: 'od-row' },
            field('Left %', ...pe('x0'), 1),
            field('Right %', ...pe('x1'), 1),
            field('Top %', ...pe('y0'), 1),
            field('Bottom %', ...pe('y1'), 1),
        );
        void redraw();
        return h(
            'div',
            { class: 'od-map', style: 'display:flex;gap:12px;flex-wrap:wrap;border-top:1px solid var(--border);padding-top:8px' },
            canvas,
            h(
                'div',
                { style: 'flex:1;min-width:280px;display:grid;gap:6px' },
                h('b', {}, level.title),
                h('div', { class: 'od-row' }, sel, fileIn, fileName),
                h('div', { class: 'od-dim' }, 'Area of the world the map shows (map squares, 1 square = 1 terrain cell):'),
                worldRow,
                h('div', { class: 'od-dim' }, 'Part of the picture that area covers (dashed frame):'),
                pictureRow,
                warn,
                h('div', { class: 'od-row' }, reset),
            ),
        );
    }

    let token = 0;
    const el = h(
        'details',
        { class: 'od-mapspanel' },
        h('summary', {}, 'Map screen'),
        h(
            'div',
            { class: 'od-dim' },
            'The map you open in game shows a picture of the level with Rynn and the quest places on it. Each level starts with its own Order of the Flame map; pick another picture or adjust which area it covers if a dot lands in the wrong place.',
        ),
        note,
        cards,
    );

    return {
        el,
        setLevels(levels, all) {
            const mine = ++token;
            note.textContent = levels.length ? '' : 'Tick a level above to see its map.';
            cards.replaceChildren();
            if (!getSource()) return;
            void (async () => {
                for (const lv of levels) {
                    const c = await card(lv, all).catch((e: unknown) =>
                        h('div', { class: 'od-bad' }, `${lv.title}: ${e instanceof Error ? e.message : String(e)}`),
                    );
                    if (mine !== token) return;
                    cards.append(c);
                }
            })();
        },
        overrides() {
            const out: Record<string, LevelMapOverride> = {};
            for (const [path, l] of loaded) {
                if (!l.spec || !l.choice) continue;
                const o = choiceToOverride(l.choice, l.spec, parchmentOf);
                if (o) out[path.toLowerCase()] = o;
            }
            return out;
        },
    };
}


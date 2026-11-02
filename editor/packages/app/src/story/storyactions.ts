// One-step story placement in the level: a cut scene at a point (or at an object), a quest start / finish area next to a
// quest, and the list of what can be placed (`storyPlaceables`) for the Place panel. Each runs as one undo step through the
// existing logic templates (`cutscene-trigger`, `volume-message`) or `buildNewShop`, so nothing here invents level data.
//
// For other panels (the left Place panel, menus):
//   storyPlaceables()          the entries ("Cut scenes" > "Cut scene", "Quests" > "Quest start area" ...) with their
//                              command id; `runCommand(entry.command)` places one at the viewport centre ground point.
//   storyPlacer(command)       the same placement at a given point (a drop from the Place panel) and why it cannot be
//                              placed right now ("Select one quest first"); `register.ts` provides them.
//   commands                   `story.place.cutscene`, `story.place.quest-start`, `story.place.quest-finish`,
//                              `story.place.vendor`
//   right-click menu           "Add cut scene here", "Add cut scene at this object", "Add quest start area here",
//                              "Add shop here" ...

import {
    ApplyTemplateCommand,
    TEMPLATES,
    VENDOR_RFL_CLASS,
    buildNewShop,
    isError,
    logicOf,
    type LevelObject,
    type ParamValues,
} from '@drakan/editor-core';
import type { Ctx } from '../editor/ctx.ts';
import type { ContentSession } from './content.ts';
import { starterCameraPoints, type Vec3 } from './storymap.ts';

const err = (e: unknown): string => (e instanceof Error ? e.message : String(e));

/**
 * Story folders the open level may use, best first: its own file stem, then the ODBs it depends on. The story files are
 * per chapter (Surdana, `LEVEL1NA.LVL`, keeps its sequences in `LEVEL1ON`, one of its ODBs).
 */
export function storyStemCandidates(ctx: Ctx): string[] {
    try {
        const src = ctx.src();
        if (!src) return [];
        const own = src.file.replace(/\.LVL$/i, '').toUpperCase();
        return [own, ...[...src.odbs.keys()].map((k) => k.toUpperCase()).filter((k) => k !== own)];
    } catch {
        return [];
    }
}

/** The story folder of the open level among `stems` (the disc's story folders), or null. */
export function levelStem(ctx: Ctx, stems: readonly string[]): string | null {
    return storyStemCandidates(ctx).find((c) => stems.includes(c)) ?? null;
}

/** Make `session` show the open level's story folder (lists the folders first when needed). */
export async function openLevelStory(ctx: Ctx, session: ContentSession): Promise<void> {
    if (!session.stems.length) {
        const fs = session.host.fs();
        if (!fs) throw new Error('Open a disc first.');
        session.stems = await fs.stems();
    }
    const stem = levelStem(ctx, session.stems);
    if (stem && session.stem !== stem) await session.ensure(stem);
    else if (!session.story) await session.ensure();
}

/** Where a click-free placement goes: the ground at the viewport centre, else the orbit target. */
export function cameraTarget(ctx: Ctx): Vec3 {
    const v = ctx.view();
    const g = v.groundAtCentre();
    if (g) return [g[0], g[1], g[2]];
    const t = v.orbit.target;
    return [t.x * 1000, t.y * 1000, t.z * 1000];
}

/** The folder most of the level's sequences are listed in (new ones join them). */
function usualFolder(session: ContentSession): number {
    const n = new Map<number, number>();
    for (const s of session.sequences()) n.set(s.folder, (n.get(s.folder) ?? 0) + 1);
    const best = [...n].sort((a, b) => b[1] - a[1])[0]?.[0];
    return best ?? session.story?.folders[0]?.id ?? 0;
}

function freeName(session: ContentSession): string {
    const used = new Set(session.sequences().map((s) => s.seq?.name ?? ''));
    for (let i = 1; ; i++) if (!used.has(`New cut scene ${i}`)) return `New cut scene ${i}`;
}

export interface MadeCutscene {
    sequence: number;
    /** The new cut scene player and trigger box. */
    player: number;
    trigger: number;
    note: string;
}

/**
 * Make a cut scene at `at`: a new sequence (camera moving in on the spot when the level has a Camera object), a cut scene
 * player that plays it and a trigger box that starts it. With `actor`, the camera looks at that object and it is added
 * to the sequence as an actor. Reads the level's story folder first when the Story editors are on another level.
 */
export async function makeCutsceneAt(
    ctx: Ctx,
    session: ContentSession,
    at: Vec3,
    actor?: LevelObject,
): Promise<MadeCutscene> {
    const env = ctx.schemas();
    if (!env || !ctx.doc().size) throw new Error('Open a level from the disc first.');
    if (!ctx.view().policy.structural) throw new Error('This view cannot add objects.');
    await openLevelStory(ctx, session);
    if (!session.story) throw new Error('The level’s story files could not be read.');
    const cameraRef = session.cameraRef();
    const v = ctx.view();
    const name = freeName(session);
    const target: Vec3 = actor ? [...actor.transform.pos] : at;
    const seq = session.addCutscene({
        folder: usualFolder(session),
        name,
        cameraRef,
        camera: cameraRef ? starterCameraPoints(target, v.cameraGamePos()) : [],
        steps: [],
        duration: 4,
    });
    if (actor) session.addSequenceActor(seq, actor.class, actor.id);
    const t = TEMPLATES.find((x) => x.id === 'cutscene-trigger')!;
    const vals: ParamValues = { sequence: seq, w: 3, h: 2, d: 3, once: true, who: '2' };
    const r = t.build({ doc: ctx.doc(), env, catalog: ctx.catalog(), at, selection: [] }, vals);
    if (isError(r)) {
        session.revertSequence(seq);
        throw new Error(r.error);
    }
    const cmd = new ApplyTemplateCommand(r, `Add cut scene "${name}"`);
    v.run(cmd);
    const player = r.objects.find((o) => logicOf(o, env)?.role === 'cutscene')?.id ?? cmd.ids[0]!;
    const trigger = r.objects.find((o) => o.id !== player)?.id ?? player;
    v.select([player]);
    const note =
        `Cut scene "${name}" (sequence ${seq}) added: a player and a trigger box.` +
        (cameraRef ? '' : ' This level has no Camera object in its other cut scenes, so it has no camera move yet.');
    return { sequence: seq, player, trigger, note };
}

/** Quest objects (journal entries / quest markers) among `ids`. */
export function questObjects(ctx: Ctx, ids: readonly number[]): LevelObject[] {
    const env = ctx.schemas();
    if (!env) return [];
    return ids.map((id) => ctx.doc().get(id)).filter((o): o is LevelObject => !!o && logicOf(o, env)?.role === 'quest');
}

/** A trigger box at `at` that starts (Quest Initiate) or finishes (Quest Complete) the quest object `quest`. */
export function addQuestArea(ctx: Ctx, quest: LevelObject, at: Vec3, kind: 'start' | 'finish'): number[] {
    const env = ctx.schemas();
    if (!env) throw new Error('Open a level from the disc first.');
    const t = TEMPLATES.find((x) => x.id === 'volume-message')!;
    const message = kind === 'start' ? 'Quest Initiate' : 'Quest Complete';
    const r = t.build(
        { doc: ctx.doc(), env, catalog: ctx.catalog(), at, selection: [quest.id] },
        { targets: [quest.id], message, w: 3, h: 2, d: 3, once: true, who: '2' },
    );
    if (isError(r)) throw new Error(r.error);
    const cmd = new ApplyTemplateCommand(r, `Add quest ${kind} area for ${quest.class}`);
    ctx.view().run(cmd);
    ctx.view().select(cmd.ids);
    return cmd.ids;
}

/** The one selected shop (its stock is copied by a new shop), if exactly one shop is selected. */
export function selectedShop(ctx: Ctx): LevelObject | undefined {
    const env = ctx.schemas();
    if (!env) return undefined;
    const sel = [...ctx.view().selection].map((id) => ctx.doc().get(id));
    return sel.length === 1 && sel[0] && env.rflOf(sel[0].resource) === VENDOR_RFL_CLASS ? sel[0] : undefined;
}

/** Why no shop can be made in the open level (it has no shop class), or null. */
export function noShopClass(ctx: Ctx): string | null {
    return ctx.catalog().some((c) => c.rflClass === VENDOR_RFL_CLASS)
        ? null
        : 'This level has no shop class (Surdana and levels 3, 4 and 6 have one)';
}

export interface MadeShop {
    shop: number;
    keeper: number;
    note: string;
}

/**
 * A new shop at `at`, one undo step: a shopkeeper (a copy of one of the level's shopkeepers), the shop with its class's
 * stock (or the stock of `like`) and the box that switches the keeper on (`buildNewShop`). Walking up to the keeper opens
 * the shop. The new shop is selected so the Vendor section shows its stock.
 */
export function addShopAt(ctx: Ctx, at: Vec3, like?: LevelObject): MadeShop {
    const env = ctx.schemas();
    if (!env || !ctx.doc().size) throw new Error('Open a level from the disc first.');
    if (!ctx.view().policy.structural) throw new Error('This view cannot add objects.');
    const r = buildNewShop(
        { doc: ctx.doc(), env, catalog: ctx.catalog(), at, selection: [] },
        like ? { like: like.id } : {},
    );
    if (isError(r)) throw new Error(r.error);
    const cmd = new ApplyTemplateCommand(r, 'Add shop');
    ctx.view().run(cmd);
    const [shop, keeper] = cmd.ids as [number, number];
    ctx.view().select([shop]);
    const name = r.objects[0]!.class;
    const note =
        `Shop "${name}" added with its shopkeeper; walking up to the keeper opens it. Edit its stock in Vendors.` +
        (r.warnings.length ? ` ${r.warnings.join(' ')}` : '');
    return { shop, keeper, note };
}

export interface StoryPlaceable {
    id: string;
    /** Place panel category. */
    category: 'Cut scenes' | 'Quests' | 'Shops';
    title: string;
    hint: string;
    /** Story overlay group (for the badge glyph, `storyGlyph`). */
    glyph: 'cutscene' | 'quest' | 'vendor';
    /** Registry command that places it at the viewport centre. */
    command: string;
    /** Why it cannot be placed (shown greyed out), or null. */
    unavailable: string | null;
}

/** A story placement the Place panel can run at a point (a drop) or at the view centre (`at` null). */
export interface StoryPlacer {
    place(at: Vec3 | null): void;
    /** Why it cannot be placed now (no level, no quest selected ...), or null. */
    unavailable(): string | null;
}

const placers = new Map<string, StoryPlacer>();

/** Register the placement behind a `story.place.*` command (register.ts). */
export function registerStoryPlacer(command: string, placer: StoryPlacer): void {
    placers.set(command, placer);
}

export const storyPlacer = (command: string): StoryPlacer | undefined => placers.get(command);

/**
 * Why `entry` cannot be placed now, or null: its fixed reason first, then the placer's (none registered means there is
 * no level editor to place into).
 */
export function placeableReason(entry: StoryPlaceable, placer: StoryPlacer | undefined): string | null {
    if (entry.unavailable) return entry.unavailable;
    if (!placer) return 'Open a level from the disc first';
    return placer.unavailable();
}

/** What the Story workspace can place, for the left Place panel. */
export function storyPlaceables(): readonly StoryPlaceable[] {
    return [
        {
            id: 'story.cutscene',
            category: 'Cut scenes',
            title: 'Cut scene',
            hint: 'A cut scene player, the box that starts it and a camera move. Opens its timeline.',
            glyph: 'cutscene',
            command: 'story.place.cutscene',
            unavailable: null,
        },
        {
            id: 'story.quest-start',
            category: 'Quests',
            title: 'Quest start area',
            hint: 'A box that starts the selected quest when the player walks in.',
            glyph: 'quest',
            command: 'story.place.quest-start',
            unavailable: null,
        },
        {
            id: 'story.quest-finish',
            category: 'Quests',
            title: 'Quest finish area',
            hint: 'A box that finishes the selected quest when the player walks in.',
            glyph: 'quest',
            command: 'story.place.quest-finish',
            unavailable: null,
        },
        {
            id: 'story.vendor',
            category: 'Shops',
            title: 'Shop',
            hint: 'A shopkeeper, the shop with its default stock and the box that switches the keeper on. With one shop selected, the new shop copies its stock.',
            glyph: 'vendor',
            command: 'story.place.vendor',
            unavailable: null,
        },
    ];
}

/** Run an async placement and report the result as a toast. */
export function report(ctx: Ctx, p: Promise<{ note: string }>): void {
    p.then(
        (r) => ctx.toast(r.note, 'ok'),
        (e) => ctx.toast(err(e), 'error'),
    );
}

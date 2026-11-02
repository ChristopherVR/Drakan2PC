import type { AssetClass } from './catalog.ts';
import type { EntityKind } from './world.ts';

/**
 * Display categories for objects and classes: what the Outliner groups by, the Content Browser chips and folders, the
 * Show menu toggles and the Place sidebar. Purely for display. The stored `LevelObject.kind` (marker colour, world-dump
 * type) is not touched, so nothing written to a file depends on these.
 *
 * The category comes from the engine class (the RFL id, `AssetClass.rflClass`) first: every class with the same RFL id
 * behaves the same in the game, whatever the designers named it. The table was built from a census of every class and
 * placed object on the PAL disc (docs/journal/2026-11-02/object-categories.md). When the RFL id is unknown (a world
 * dump, a missing ODB) the class name decides, then the old `kind`.
 */
export type ObjectCategory =
    | 'player'
    | 'character'
    | 'waypoint'
    | 'item'
    | 'breakable'
    | 'mechanism'
    | 'trigger'
    | 'cutscene'
    | 'quest'
    | 'exit'
    | 'barrier'
    | 'light'
    | 'sound'
    | 'effect'
    | 'sky'
    | 'settings'
    | 'camera'
    | 'scenery'
    | 'other';

export interface CategoryInfo {
    id: ObjectCategory;
    /** Plain name shown in the UI. */
    label: string;
    /** One sentence for tooltips. */
    hint: string;
    /** Dot / marker colour (0xRRGGBB). */
    color: number;
    /** Icon name in the app's icon set (`dom.ts` PATHS). */
    icon: string;
}

/** Every category in display order (Outliner groups, chips, Show menu). */
export const OBJECT_CATEGORIES: readonly CategoryInfo[] = [
    { id: 'player', label: 'Player', hint: 'Rynn, Arokh and the player start', color: 0x4ade80, icon: 'character' },
    {
        id: 'character',
        label: 'Characters & creatures',
        hint: 'Enemies, villagers, dragons, animals and turrets',
        color: 0xef4444,
        icon: 'character',
    },
    {
        id: 'waypoint',
        label: 'Paths & waypoints',
        hint: 'Patrol points that characters walk or fly between',
        color: 0xfb923c,
        icon: 'link',
    },
    {
        id: 'item',
        label: 'Items & pick-ups',
        hint: 'Weapons, armour, potions, gold, keys and quest items',
        color: 0xfacc15,
        icon: 'item',
    },
    {
        id: 'breakable',
        label: 'Chests & breakables',
        hint: 'Chests, barrels, crates, pots and floors that break',
        color: 0xd97706,
        icon: 'box',
    },
    {
        id: 'mechanism',
        label: 'Doors & mechanisms',
        hint: 'Doors, gates, levers, buttons, pressure plates, locks, lifts, ladders and traps',
        color: 0xa78bfa,
        icon: 'door',
    },
    {
        id: 'trigger',
        label: 'Triggers & logic',
        hint: 'Trigger volumes, timers, counters, message relays, show/hide togglers and groupers',
        color: 0xf472b6,
        icon: 'trigger',
    },
    {
        id: 'cutscene',
        label: 'Cut scenes',
        hint: 'Cut scene players, movies and screen fades',
        color: 0x22d3ee,
        icon: 'play',
    },
    {
        id: 'quest',
        label: 'Quests & dialogue',
        hint: 'Conversations, journal entries, quest markers and shops',
        color: 0x34d399,
        icon: 'info',
    },
    {
        id: 'exit',
        label: 'Level exits & teleporters',
        hint: 'Level transitions, portals and teleporters with their targets',
        color: 0x60a5fa,
        icon: 'external',
    },
    {
        id: 'barrier',
        label: 'Barriers & zones',
        hint: 'Dragon barriers, level boundaries, air currents and invisible walls',
        color: 0x94a3b8,
        icon: 'magnet',
    },
    {
        id: 'light',
        label: 'Lights',
        hint: 'Vertex lights, dynamic lights, torches and candles',
        color: 0xfde047,
        icon: 'light',
    },
    {
        id: 'sound',
        label: 'Sound & music',
        hint: 'Looping and triggered sounds, ambience changes and music',
        color: 0x818cf8,
        icon: 'sound',
    },
    {
        id: 'effect',
        label: 'Effects',
        hint: 'Particles, smoke, mist, drips, lightning and other visual effects',
        color: 0xe879f9,
        icon: 'effect',
    },
    {
        id: 'sky',
        label: 'Sky & weather',
        hint: 'Skies, clouds, fog and day/night switchers, rain and snow',
        color: 0x7dd3fc,
        icon: 'sun',
    },
    {
        id: 'settings',
        label: 'Level settings',
        hint: 'Level properties, menu launchers, maps, materials and other engine settings',
        color: 0x64748b,
        icon: 'sliders',
    },
    { id: 'camera', label: 'Cameras', hint: 'The game camera', color: 0x38bdf8, icon: 'camera' },
    {
        id: 'scenery',
        label: 'Scenery',
        hint: 'Trees, rocks, grass, buildings, walls and other props that do not move',
        color: 0x7a8699,
        icon: 'tree',
    },
    {
        id: 'other',
        label: 'Other',
        hint: 'Classes the editor cannot place in a category yet',
        color: 0x888888,
        icon: 'generic',
    },
];

const INFO = new Map(OBJECT_CATEGORIES.map((c) => [c.id, c]));

export function categoryInfo(id: ObjectCategory): CategoryInfo {
    return INFO.get(id) ?? INFO.get('other')!;
}

const R = (cat: ObjectCategory, ids: number[]): [number, ObjectCategory][] => ids.map((id) => [id, cat]);

/** Engine class (RFL id) to category. Comments name the classes found on the disc. */
const RFL_CATEGORY = new Map<number, ObjectCategory>([
    // Rynn, Dragon (Arokh).
    ...R('player', [0x09, 0x1c]),
    // Humanoid NPCs (orcs, trogs, skeletons, Wartoks), Scavenger, Spider, flyers and dragons, villagers, archers and
    // shamans, Soulless, golems, giants and yetis, Storm Djinn, Desert Lord, mages, mounted Northmen, catapults, Stone
    // Golem, Sea Dragon, dragon egg, animated dummies, ambient wildlife (butterflies, fish, bats), turrets (Ballista,
    // Evil Tree Eye, Dart Gen).
    ...R(
        'character',
        [
            0x31, 0x29, 0x28, 0x1e, 0x9d, 0xad, 0x44, 0xb0, 0x3b, 0xc1, 0xbc, 0x52, 0x42, 0xa5, 0xbd, 0xc2, 0x75, 0x0c,
            0x41, 0x25, 0xbb,
        ],
    ),
    // Waypoint, AI cover marker, AI repositioning and stealth attack helpers.
    ...R('waypoint', [0x70, 0xc4, 0x5c, 0xe3]),
    // Potions, weapons, armour, bows, shields, special potions, treasure, mana, quest items and keys, dragon power-ups,
    // spell unlockers, books, dragon health, helmets, quivers, arrows, enemy shields.
    ...R(
        'item',
        [0x0a, 0x0e, 0x2d, 0x4f, 0x90, 0x59, 0x6d, 0xab, 0x21, 0x39, 0xb2, 0xaa, 0x8a, 0xc7, 0x27, 0x5f, 0x8b],
    ),
    // Chest, breakable props (barrels, crates, bricks, pots), break-away floors.
    ...R('breakable', [0xc5, 0x11, 0x2c]),
    // Door, lever / button, pressure plate, lock plate, combo lock, mechanism (spikes, lift parts, lids), elevator
    // handler, ladder, pushable block.
    ...R('mechanism', [0x13, 0x0d, 0x1d, 0x22, 0xdf, 0x23, 0xe1, 0x0f, 0x10]),
    // Trigger detector, timer, countdown, message relay, vis toggler, object grouper, line of sight.
    ...R('trigger', [0x3d, 0x3e, 0x07, 0xe0, 0x79, 0x4e, 0xd0]),
    // Cut scene player ("Stomp Player"), movie player, screen fader.
    ...R('cutscene', [0x33, 0x16, 0x88]),
    // Conversation handler, journal entry, quest marker, proximity dialog, shop, shop addition, dialogue player and lines.
    ...R('quest', [0xb4, 0x68, 0xdc, 0xb5, 0xac, 0xb7, 0x9f, 0xba]),
    // Level transition / portal, teleporter, teleporter target.
    ...R('exit', [0x37, 0xc6, 0x48]),
    // Dragon barrier, boundary / air current, death box.
    ...R('barrier', [0x76, 0x6f, 0xb8]),
    // Static (vertex) light, dynamic light, torches and candles.
    ...R('light', [0x84, 0x85, 0x14]),
    // Sounds, ambient transitions, music triggers, EAX reverb, music tracks.
    ...R('sound', [0x06, 0x83, 0x81, 0x74, 0xbf]),
    // Particle generators and fountains, mist, drips, morphs and light rays, lightning rods and generators, warps,
    // land morph / shake, FX, lasers, force shields, tornados, and the spell, missile, flash, fragment, splatter, halo,
    // trail and blur classes the game spawns.
    ...R(
        'effect',
        [
            0x3f, 0x8c, 0x2b, 0x92, 0x63, 0xd5, 0xce, 0xbe, 0x35, 0x86, 0xc3, 0x4a, 0x7c, 0x01, 0x08, 0x3a, 0x7d, 0x32,
            0x87, 0x03, 0x05, 0x20, 0xd2, 0x53, 0xcb, 0xc0, 0x5a, 0x9e, 0x43, 0x47, 0x5d, 0x7a, 0x15, 0xb1, 0xae, 0xd6,
            0xb9, 0xb6, 0x30, 0xa6, 0xa0, 0x6a, 0x99, 0x69, 0x2a, 0x55, 0x6c, 0x77, 0x46, 0x3c, 0xda, 0x6b, 0x1f,
        ],
    ),
    // Sky layers, day/night and fog switchers, environmental effects (rain, snow), day/night cycles, wind.
    ...R('sky', [0x1a, 0xca, 0x8f, 0x61, 0xc9, 0xdd]),
    // Level properties, menu launcher, resource prefetcher, UI properties and backgrounds, level maps, inventory,
    // crosshair, materials, lava / water properties, procedural textures and water.
    ...R('settings', [0x36, 0x8d, 0xe5, 0x62, 0xe4, 0x66, 0x2e, 0xd9, 0x4b, 0x5e, 0xcd, 0xd1, 0x72]),
    ...R('camera', [0x1b]),
    // Static props, grass and flowers, structures (stairs, bridges, platforms), flags, hanging bags and cables.
    ...R('scenery', [0xd3, 0xd8, 0xd4, 0xe2, 0x49]),
    // DEBUG.ODB helpers (bounding node / collision response / waypoint generators, box, spheres, AI debug): never placed.
    ...R('other', [0x18, 0x91, 0xcc, 0x17, 0x19]),
]);

/** Name rules for scenery-like engine classes that are really invisible blockers. */
const INVISIBLE = /\binvisible\b|\bjump plug\b/i;

/** Name rules when the engine class is unknown. First match wins; order matters ("Sound Trigger" is a sound). */
const NAME_RULES: [RegExp, ObjectCategory][] = [
    [/^(rynn|player)\b|^dragon$/i, 'player'],
    [/^camera\b/i, 'camera'],
    [/sound|music|ambient|\beax\b/i, 'sound'],
    [/way ?point|\bwp\b/i, 'waypoint'],
    [/level transition|portal|teleport/i, 'exit'],
    [/barrier|boundry|boundary|invisible|air current/i, 'barrier'],
    [/stomp|cut ?scene|movie|fader/i, 'cutscene'],
    [/conversation|journal|quest|proximity|store\b|shop/i, 'quest'],
    [/timer|trigger|detector|count ?down|counter|relay|vis ?toggl|grouper|line of sight/i, 'trigger'],
    [/\(vertex\) light|vertex light|dynamic light|torch|candle|sconce|firepot|firepit/i, 'light'],
    [/\bsky\b|fog|day ?night|weather|\brain\b|snow object|clouds?\b/i, 'sky'],
    [/level properties|prefetch/i, 'settings'],
    [
        /door|gate|portcullis|lever|switch|button|elevator|\blift\b|ladder|\block\b|pressure plate|mechanism/i,
        'mechanism',
    ],
    [/chest|barrel|crate|break ?away|\bpot\b/i, 'breakable'],
    [/potion|sword|axe|club|mace|bow\b|shield|armou?r|gold|key\b|pwrup|power ?up/i, 'item'],
    [/particle|smoke|mist|drip|spark|fountain|\bgen\b|generator|lightning|\bfx\b|effect/i, 'effect'],
];

const KIND_CATEGORY: Record<EntityKind, ObjectCategory> = {
    player: 'player',
    creature: 'character',
    weapon: 'item',
    static: 'scenery',
    camera: 'camera',
    usable: 'other',
    other: 'other',
};

export interface CategoryInput {
    /** Class name. */
    name: string;
    /** RFL id (low 16 bits of the runtime class), null or absent when unknown. */
    rfl?: number | null;
    /** The stored marker kind, the last fallback. */
    kind?: EntityKind;
}

/** Category of a class or object: engine class first, then the name, then the stored kind. */
export function categoryOf(x: CategoryInput): ObjectCategory {
    const byRfl = x.rfl === null || x.rfl === undefined ? undefined : RFL_CATEGORY.get(x.rfl);
    if (byRfl) {
        if ((byRfl === 'scenery' || byRfl === 'breakable') && INVISIBLE.test(x.name)) return 'barrier';
        return byRfl;
    }
    for (const [re, cat] of NAME_RULES) if (re.test(x.name)) return cat;
    return x.kind ? KIND_CATEGORY[x.kind] : 'other';
}

/** Category of a catalog class. */
export function classCategory(c: Pick<AssetClass, 'name' | 'rflClass' | 'kind'>): ObjectCategory {
    return categoryOf({ name: c.name, rfl: c.rflClass, kind: c.kind });
}

/** True when the engine class has a category of its own (not decided by name or kind). */
export function hasRflCategory(rfl: number | null | undefined): boolean {
    return rfl !== null && rfl !== undefined && RFL_CATEGORY.has(rfl);
}

export interface CategorizedObject {
    class: string;
    resource: string;
    kind: EntityKind;
}

/**
 * A cached object-to-category function for one catalog (the open level's classes). Objects whose resource is not in
 * the catalog (world dumps) fall back on their class name and kind.
 */
export function categoryIndex(
    catalog: Iterable<Pick<AssetClass, 'resource' | 'rflClass'>>,
): (o: CategorizedObject) => ObjectCategory {
    const rfl = new Map<string, number | null>();
    for (const c of catalog) rfl.set(c.resource, c.rflClass);
    const cache = new Map<string, ObjectCategory>();
    return (o) => {
        const key = `${o.resource}\u0000${o.class}\u0000${o.kind}`;
        let cat = cache.get(key);
        if (!cat) cache.set(key, (cat = categoryOf({ name: o.class, rfl: rfl.get(o.resource) ?? null, kind: o.kind })));
        return cat;
    };
}

/** Counts per category in display order, empty categories left out. */
export function countByCategory<T>(
    items: Iterable<T>,
    categoryOfItem: (x: T) => ObjectCategory,
): { info: CategoryInfo; count: number }[] {
    const n = new Map<ObjectCategory, number>();
    for (const x of items) {
        const c = categoryOfItem(x);
        n.set(c, (n.get(c) ?? 0) + 1);
    }
    return OBJECT_CATEGORIES.filter((c) => n.has(c.id)).map((info) => ({ info, count: n.get(info.id)! }));
}

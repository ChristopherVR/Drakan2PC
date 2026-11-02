/**
 * The Place panel's categories (editor/placepanel.ts): every class the open level can use sorted into plain groups a
 * modder would look for, plus the "make one" actions (cut scene, quest, vendor, puzzle ...) under the group they
 * belong to. Pure: the panel and the tests share it. The class's display category (editor-core categories.ts, the same
 * one the Outliner and Content Browser use) is the input; this only splits a few of them further for the sidebar
 * (characters and creatures, quests, conversations and shops).
 */
import { classCategory, type AssetClass, type ObjectCategory } from '@drakan/editor-core';

export type PlaceCat =
    | 'characters'
    | 'creatures'
    | 'items'
    | 'breakables'
    | 'doors'
    | 'lights'
    | 'logic'
    | 'cutscenes'
    | 'conversations'
    | 'quests'
    | 'vendors'
    | 'exits'
    | 'sound'
    | 'effects'
    | 'sky'
    | 'scenery'
    | 'other';

export interface PlaceCatInfo {
    id: PlaceCat;
    title: string;
    icon: string;
    /** One line shown at the top of the group. */
    hint: string;
}

/** Sidebar order. */
export const PLACE_CATS: readonly PlaceCatInfo[] = [
    { id: 'characters', title: 'Characters', icon: 'character', hint: 'People, Rynn, Arokh and the player start' },
    { id: 'creatures', title: 'Creatures', icon: 'character', hint: 'Enemies, monsters, dragons and animals' },
    { id: 'items', title: 'Items', icon: 'item', hint: 'Weapons, armour, potions, gold, keys and other pick-ups' },
    { id: 'breakables', title: 'Chests & breakables', icon: 'box', hint: 'Chests, barrels, crates and pots' },
    { id: 'doors', title: 'Doors & mechanisms', icon: 'door', hint: 'Doors, gates, levers, plates, lifts and traps' },
    { id: 'lights', title: 'Lights', icon: 'light', hint: 'Torches, candles and other light sources' },
    {
        id: 'logic',
        title: 'Triggers & logic',
        icon: 'trigger',
        hint: 'Areas the player walks into, timers, paths, barriers and ready-made puzzles',
    },
    { id: 'cutscenes', title: 'Cut scenes', icon: 'camera', hint: 'Cut scenes, cameras and screen fades' },
    { id: 'conversations', title: 'Conversations', icon: 'chat', hint: 'Lines characters say, with subtitles' },
    { id: 'quests', title: 'Quests', icon: 'check-circle', hint: 'Quests, journal entries and their markers' },
    { id: 'vendors', title: 'Vendors', icon: 'shop', hint: 'Shops: what they sell and for how much' },
    { id: 'exits', title: 'Level exits', icon: 'external', hint: 'Ways to other levels, portals and teleporters' },
    { id: 'sound', title: 'Sound', icon: 'sound', hint: 'Sounds, ambience and music' },
    { id: 'effects', title: 'Effects', icon: 'effect', hint: 'Fire, smoke, magic and other effects' },
    { id: 'sky', title: 'Sky & weather', icon: 'sun', hint: 'Skies, clouds, fog, rain and snow' },
    { id: 'scenery', title: 'Scenery', icon: 'tree', hint: 'Trees, rocks, buildings and props' },
    { id: 'other', title: 'Other', icon: 'generic', hint: 'Level settings and everything else the level can use' },
];

/** Display category (editor-core categories.ts) to sidebar group; characters and quests are split further below. */
const FROM_CATEGORY: Record<ObjectCategory, PlaceCat> = {
    player: 'characters',
    character: 'characters',
    waypoint: 'logic',
    item: 'items',
    breakable: 'breakables',
    mechanism: 'doors',
    trigger: 'logic',
    cutscene: 'cutscenes',
    quest: 'quests',
    exit: 'exits',
    barrier: 'logic',
    light: 'lights',
    sound: 'sound',
    effect: 'effects',
    sky: 'sky',
    settings: 'other',
    camera: 'cutscenes',
    scenery: 'scenery',
    other: 'other',
};

const CREATURE =
    /dragon|arokh|orc|goblin|troll|ogre|spider|scavenger|\brats?\b|\bbats?\b|wolf|boar|skeleton|beast|monster|wyvern|lizard|golem|giant|yeti|djinn|soul ?less|wartok|trog|creature|enemy|undead|zombie|demon|wraith|ghoul|insect|worm|bird|fish|snake|butterfl|moth|firefl|chicken|scorpion|crab|ballista|catapult|turret|evil tree/i;
const CONVERSATION = /convers|dialog|talk|speech|subtitle/i;
const VENDOR = /shop|vendor|merchant|trader|store/i;
/** Spell parts and particle generators the census left in "other" ("!! Ice Burst !!", "Arrow Test Gen"). */
const EFFECT_NAME =
    /!!|\bgen\b|generator|missile|\bblur\b|\bflash\b|\bbeams?\b|\bcloud\b|\bsparks?\b|explosion|dmgfx|\bfx\b|\btrail|\bmorph\b|polysuck|\bgib\b/i;

/** Where a class is listed in the Place panel. */
export function placeCategory(c: Pick<AssetClass, 'name' | 'kind' | 'rflClass'>): PlaceCat {
    const cat = classCategory(c);
    if (cat === 'character') return CREATURE.test(c.name) ? 'creatures' : 'characters';
    if (cat === 'player' && /dragon|arokh/i.test(c.name)) return 'creatures';
    if (cat === 'quest')
        return CONVERSATION.test(c.name) ? 'conversations' : VENDOR.test(c.name) ? 'vendors' : 'quests';
    if (cat === 'other' && EFFECT_NAME.test(c.name)) return 'effects';
    return FROM_CATEGORY[cat];
}

/** Quick Add rows (quickadd.ts) that make something placeable, and the group they are listed in. */
export const ACTION_CAT: Readonly<Record<string, PlaceCat>> = {
    cutscene: 'cutscenes',
    dialogue: 'conversations',
    quest: 'quests',
    vendor: 'vendors',
};

/** Ready-made logic set-ups (editor-core logic-templates.ts) listed outside Triggers and logic. */
export const TEMPLATE_CAT: Readonly<Record<string, PlaceCat>> = {
    'cutscene-trigger': 'cutscenes',
    'quest-object': 'quests',
};
export const templateCategory = (id: string): PlaceCat => TEMPLATE_CAT[id] ?? 'logic';

/**
 * List order: by name ignoring case and leading punctuation, with names that start with punctuation or a digit
 * ("!! Spike !!", "4:Snow Orc Center": internal parts and test classes) after the plain ones.
 */
export function compareNames(a: string, b: string): number {
    const odd = (s: string): number => (/^[a-z]/i.test(s.trim()) ? 0 : 1);
    const key = (s: string): string => s.replace(/^[^a-z]+/i, '');
    return odd(a) - odd(b) || key(a).localeCompare(key(b), undefined, { sensitivity: 'base', numeric: true });
}

/** Case-insensitive match of every word of `query` against the text. */
export function matchesQuery(text: string, query: string): boolean {
    const t = text.toLowerCase();
    return query
        .toLowerCase()
        .split(/\s+/)
        .filter(Boolean)
        .every((w) => t.includes(w));
}

/** The recently placed list after placing `key`: newest first, no repeats, at most `max`. */
export function pushRecent(list: readonly string[], key: string, max = 12): string[] {
    return [key, ...list.filter((k) => k !== key)].slice(0, max);
}

/** Recently placed keys read back from storage (anything that is not a list of strings is dropped). */
export function readRecent(raw: unknown, max = 12): string[] {
    return Array.isArray(raw) ? raw.filter((k): k is string => typeof k === 'string').slice(0, max) : [];
}

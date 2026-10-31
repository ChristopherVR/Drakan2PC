import { odbStemForDependency, type LvlHeader, type OdbIndex } from '@drakan/formats';
import { cloneObject, type LevelDocument, type LevelObject } from './document.ts';
import type { EntityKind } from './world.ts';

/**
 * Asset catalog: every object class the level can use. A class is a (db_index, class_id) pair; the dependency table
 * maps db_index to an ODB, whose 0x0102 records name the classes (level-format.md section 5). The PC Order of the Flame
 * class id sits in the low 16 bits of `runtimeClass` (section 9) and is kept as `rflClass`.
 */
export interface AssetClass {
    /** `0x<db_index<<16 | class_id>`: the same string as `LevelObject.resource`. */
    resource: string;
    classId: number;
    dbIndex: number;
    /** ODB stem, e.g. `NPCS`. */
    db: string;
    name: string;
    /** ODB group id (0x0101 group names are not decoded). */
    group: number;
    runtimeClass: number | null;
    rflClass: number | null;
    /** Display group: the ODB's friendly name. */
    category: string;
    /** Marker colour group and icon. */
    kind: EntityKind;
    /** Icon family chosen from the class name. */
    icon: AssetIcon;
    /** How many objects of this class the level already has. */
    instances: number;
}

export type AssetIcon =
    | 'character'
    | 'tree'
    | 'rock'
    | 'light'
    | 'door'
    | 'item'
    | 'trigger'
    | 'sound'
    | 'camera'
    | 'effect'
    | 'building'
    | 'generic';

export const ICON_ORDER: AssetIcon[] = [
    'character',
    'item',
    'tree',
    'rock',
    'building',
    'door',
    'light',
    'effect',
    'trigger',
    'sound',
    'camera',
    'generic',
];

const ICON_RULES: [RegExp, AssetIcon][] = [
    [/camera/i, 'camera'],
    [/sound|music|ambient|audio|noise/i, 'sound'],
    [/trigger|volume|zone|switch|lever|button|message|waypoint|path|spawn|timer|counter|relay|marker/i, 'trigger'],
    [/torch|light|lamp|candle|lantern|brazier|glow|flame|fire/i, 'light'],
    [/fx|effect|particle|spark|smoke|splash|explosion|magic|spell|bolt|beam|mist|fog/i, 'effect'],
    [/tree|bush|plant|grass|flower|fern|palm|vine|shrub|branch|log|mushroom/i, 'tree'],
    [/rock|stone|boulder|cliff|crystal|pillar|column|rubble/i, 'rock'],
    [/door|gate|portcullis|bridge|elevator|platform|lift|ladder|stair/i, 'door'],
    [
        /chest|potion|key|coin|gold|health|weapon|sword|axe|shield|bow|arrow|spear|armor|scroll|gem|ring|pickup|item|food/i,
        'item',
    ],
    [/house|hut|tower|wall|building|castle|tent|ruin|temple|fence|barrel|crate|table|chair|statue/i, 'building'],
    [
        /rynn|arokh|dragon|player|npc|soldier|guard|orc|skeleton|wizard|knight|bat|spider|rat|beast|monster|enemy|villager|wolf|boar|creature|golem|goblin|troll|ogre|lizard|wyvern|mage|priest|bandit/i,
        'character',
    ],
];

export function iconForName(name: string): AssetIcon {
    for (const [re, icon] of ICON_RULES) if (re.test(name)) return icon;
    return 'generic';
}

const KIND_FOR_ICON: Record<AssetIcon, EntityKind> = {
    character: 'creature',
    item: 'weapon',
    tree: 'static',
    rock: 'static',
    building: 'static',
    door: 'usable',
    light: 'usable',
    effect: 'other',
    trigger: 'usable',
    sound: 'other',
    camera: 'camera',
    generic: 'other',
};

/** Marker kind for a class name (the LVL has no entity type; this is a naming heuristic only). */
export function kindOfClassName(name: string): EntityKind {
    if (/^(rynn|player)/i.test(name)) return 'player';
    return KIND_FOR_ICON[iconForName(name)];
}

const DB_LABELS: Record<string, string> = {
    SYSTEM: 'System',
    RESOURCES: 'Resources',
    FX: 'Effects',
    NPCS: 'Characters',
    MAGIC: 'Magic',
    SOUND: 'Sound',
    MUSIC: 'Music',
    INTERFACE: 'Interface',
    DEBUG: 'Debug',
};

export function categoryForDb(stem: string, dependencyPath: string): string {
    const known = DB_LABELS[stem.toUpperCase()];
    if (known) return known;
    const last = dependencyPath.split(/[\\/]/).filter(Boolean).pop() ?? stem;
    return last.replace(/[_-]+/g, ' ').replace(/\b\w/g, (c) => c.toUpperCase());
}

export function resourceString(dbIndex: number, classId: number): string {
    return `0x${(((dbIndex << 16) | classId) >>> 0).toString(16)}`;
}

/** Split a `resource` string back into db index and class id. */
export function resourceParts(resource: string): { dbIndex: number; classId: number } {
    const n = Number(resource) >>> 0;
    return { dbIndex: n >>> 16, classId: n & 0xffff };
}

/** Build the catalog from the level's dependency table and the ODB class tables; `objects` supplies instance counts. */
export function buildCatalog(header: LvlHeader, odbs: OdbIndex, objects: Iterable<{ resource: string }>): AssetClass[] {
    const counts = new Map<string, number>();
    for (const o of objects) counts.set(o.resource, (counts.get(o.resource) ?? 0) + 1);
    const out: AssetClass[] = [];
    // The last dependency with a given db_index wins, like `dependencyPath`.
    const byIndex = new Map(header.dependencies.map((d) => [d.dbIndex, d]));
    for (const [dbIndex, dep] of byIndex) {
        const db = odbStemForDependency(dep.path.text);
        const classes = odbs.get(db);
        if (!classes) continue;
        const category = categoryForDb(db, dep.path.text);
        for (const c of classes.values()) {
            if (!c.name) continue;
            const resource = resourceString(dbIndex, c.id);
            const icon = iconForName(c.name);
            out.push({
                resource,
                classId: c.id,
                dbIndex,
                db,
                name: c.name,
                group: c.group,
                runtimeClass: c.runtimeClass,
                rflClass: c.runtimeClass === null ? null : c.runtimeClass & 0xffff,
                category,
                kind: kindOfClassName(c.name),
                icon,
                instances: counts.get(resource) ?? 0,
            });
        }
    }
    return out.sort((a, b) => a.category.localeCompare(b.category) || a.name.localeCompare(b.name));
}

/** Case-insensitive match on name, ODB stem, category or id (hex with or without 0x). */
export function searchCatalog(list: readonly AssetClass[], query: string): AssetClass[] {
    const q = query.trim().toLowerCase();
    if (!q) return [...list];
    const hex = q.replace(/^0x/, '');
    return list.filter(
        (c) =>
            c.name.toLowerCase().includes(q) ||
            c.category.toLowerCase().includes(q) ||
            c.db.toLowerCase().includes(q) ||
            c.resource.includes(hex) ||
            String(c.classId) === q,
    );
}

export interface CatalogGroup {
    title: string;
    items: AssetClass[];
}

/** Groups for display: Favourites, In this level, then one per category. Empty groups are dropped. */
export function groupCatalog(list: readonly AssetClass[], favourites: ReadonlySet<string>): CatalogGroup[] {
    const groups: CatalogGroup[] = [
        { title: 'Favourites', items: list.filter((c) => favourites.has(c.resource)) },
        {
            title: 'In this level',
            items: list.filter((c) => c.instances > 0).sort((a, b) => b.instances - a.instances),
        },
    ];
    const cats = new Map<string, AssetClass[]>();
    for (const c of list) {
        let l = cats.get(c.category);
        if (!l) cats.set(c.category, (l = []));
        l.push(c);
    }
    for (const [title, items] of cats) groups.push({ title, items });
    return groups.filter((g) => g.items.length);
}

/**
 * A new object of `cls` at `pos` (game units) with yaw `yawRad`, ready for `AddCommand`. If the document has an
 * instance of the class its scale, fields and radius carry over and it is the export template; otherwise the object
 * has no fields and no scale (every field takes the class default; unverified in game).
 */
export function newObjectFromClass(
    doc: LevelDocument,
    cls: AssetClass,
    pos: readonly [number, number, number],
    yawRad = 0,
): LevelObject {
    const tpl = [...doc.objects()].find((o) => o.resource === cls.resource && o.origin !== undefined);
    const base: LevelObject = tpl
        ? cloneObject(tpl)
        : {
              id: 0,
              class: cls.name,
              type: -1,
              kind: cls.kind,
              resource: cls.resource,
              model: '',
              transform: { pos: [0, 0, 0], rot: [0, 0, 0] },
              props: {},
          };
    base.id = 0;
    base.class = cls.name;
    base.kind = cls.kind;
    base.resource = cls.resource;
    base.transform = { pos: [pos[0], pos[1], pos[2]], rot: [0, yawRad, 0] };
    base.props = { ...(tpl?.props ?? {}), db: cls.db };
    base.links = [];
    delete base.origin;
    if (tpl) base.template = tpl.origin;
    else {
        delete base.template;
        delete base.fields;
        delete base.scale;
    }
    return base;
}

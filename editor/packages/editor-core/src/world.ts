/** World JSON dump (runtime/world/src/Json.cpp, docs/reference/world-model.md) adapter. Thin on purpose. */

export type EntityKind = 'player' | 'camera' | 'creature' | 'weapon' | 'usable' | 'static' | 'other';

export interface WorldEntityJson {
    handle?: string;
    ref?: number;
    class?: string;
    type?: number;
    resIndex?: number;
    resId?: string;
    model?: string;
    flags?: string;
    pos?: number[];
    yaw?: number;
    hp?: number;
    hpMax?: number;
    hpKind?: string;
}

export interface WorldDump {
    levelName: string;
    levelPath: string;
    sequence: number;
    entities: WorldEntityJson[];
}

export function parseWorldDump(text: string): WorldDump {
    const j = JSON.parse(text) as Partial<WorldDump>;
    if (!j || !Array.isArray(j.entities)) throw new Error('not a world dump: no "entities" array');
    return {
        levelName: String(j.levelName ?? ''),
        levelPath: String(j.levelPath ?? ''),
        sequence: Number(j.sequence ?? 0),
        entities: j.entities,
    };
}

/** Marker category from the registry type (world-model.md type table; mostly inferred). */
export function classifyType(type: number | undefined, hasHp = false): EntityKind {
    switch (type) {
        case 3:
            return 'player';
        case 4:
            return 'camera';
        case 6:
        case 7:
            return hasHp ? 'creature' : 'other';
        case 8:
            return 'static';
        case 10:
            return 'weapon';
        case 11:
            return 'usable';
        default:
            return 'other';
    }
}

import { cloneDraft, cloneMode, newDraft, type ArenaDraft, type ModeDef, type Vec3 } from '@drakan/editor-core';
import type { Command, LevelDocument } from '@drakan/editor-core';
import type { LevelView } from '../../level.ts';
import { store as local } from '../dom.ts';

export type ArenaTool = 'select' | 'spawn' | 'pickup' | 'flag' | 'base' | 'zone' | 'bounds';
export type ArenaSel = { kind: 'spawn' | 'pickup' | 'marker'; uid: number } | { kind: 'bounds'; corner: 0 | 1 } | null;

/** One undo step: swaps the whole draft (it is small), so a gizmo drag or a list edit is one history row. */
class ArenaCommand implements Command {
    constructor(
        private readonly owner: ArenaStore,
        readonly label: string,
        private readonly before: ArenaDraft,
        private readonly after: ArenaDraft,
    ) {}
    apply(_doc: LevelDocument): void {
        this.owner.replace(cloneDraft(this.after));
    }
    revert(_doc: LevelDocument): void {
        this.owner.replace(cloneDraft(this.before));
    }
}

export type ArenaEvent = 'draft' | 'selection' | 'tool' | 'mode' | 'modes' | 'filter';

const DRAFT_KEY = 'mp-draft';
const MODES_KEY = 'mp-modes';

/**
 * State of the "Multiplayer map" document mode: the arena draft, the selected item, the placement tool and the custom
 * modes. Edits go through the level view's command stack so undo, redo and the History panel cover them.
 */
export class ArenaStore {
    active = false;
    draft: ArenaDraft = newDraft('');
    sel: ArenaSel = null;
    tool: ArenaTool = 'select';
    /** Custom game modes the user made or imported (shared by every map, saved in this browser). */
    modes: ModeDef[] = [];
    showFilter = true;
    private live: ArenaDraft | null = null;
    private readonly listeners = new Set<(e: ArenaEvent) => void>();
    private saveTimer: ReturnType<typeof setTimeout> | undefined;

    constructor(private readonly view: () => LevelView) {
        const saved = local.json<unknown>(MODES_KEY, []);
        if (Array.isArray(saved)) {
            this.modes = (saved as ModeDef[]).filter((m) => m && typeof m.id === 'string').map(cloneMode);
        }
    }

    on(fn: (e: ArenaEvent) => void): () => void {
        this.listeners.add(fn);
        return () => this.listeners.delete(fn);
    }
    emit(e: ArenaEvent): void {
        for (const l of this.listeners) l(e);
    }

    /** Begin a map on `level` (a fresh draft, or `init` from an imported arena). */
    start(level: string, init?: ArenaDraft): void {
        this.draft = init ? cloneDraft(init) : newDraft(level);
        if (init) this.draft.level = level;
        this.active = true;
        this.sel = null;
        this.tool = 'select';
        this.emit('mode');
        this.emit('draft');
    }
    stop(): void {
        this.active = false;
        this.sel = null;
        this.tool = 'select';
        this.emit('mode');
    }

    /** Swap in a draft (undo / redo / load); not an undo step itself. */
    replace(d: ArenaDraft): void {
        this.draft = d;
        if (this.sel && this.sel.kind !== 'bounds') {
            const s = this.sel;
            const list = s.kind === 'spawn' ? d.spawns : s.kind === 'pickup' ? d.pickups : d.markers;
            if (!list.some((x) => x.uid === s.uid)) this.sel = null;
        }
        this.changed();
    }

    /** One undoable edit of the draft. */
    commit(label: string, fn: (d: ArenaDraft) => void): void {
        const before = cloneDraft(this.draft);
        const after = cloneDraft(this.draft);
        fn(after);
        this.view().run(new ArenaCommand(this, label, before, after));
    }

    /** A drag in progress: `liveEdit` changes the draft at once, `endLive` records the undo step. */
    beginLive(): void {
        this.live = cloneDraft(this.draft);
    }
    liveEdit(fn: (d: ArenaDraft) => void): void {
        fn(this.draft);
        this.changed(false);
    }
    endLive(label: string): void {
        const before = this.live;
        this.live = null;
        if (!before) return;
        const after = cloneDraft(this.draft);
        if (JSON.stringify(before) === JSON.stringify(after)) return;
        this.draft = cloneDraft(before);
        this.view().run(new ArenaCommand(this, label, before, after));
    }

    select(sel: ArenaSel): void {
        this.sel = sel;
        this.emit('selection');
    }
    setTool(t: ArenaTool): void {
        this.tool = t;
        this.emit('tool');
    }
    setShowFilter(on: boolean): void {
        this.showFilter = on;
        this.emit('filter');
    }

    private changed(save = true): void {
        this.emit('draft');
        if (save) {
            clearTimeout(this.saveTimer);
            this.saveTimer = setTimeout(() => this.persist(), 500);
        }
    }

    persist(): void {
        if (this.active) local.set(DRAFT_KEY, JSON.stringify(this.draft));
    }
    saved(): ArenaDraft | null {
        const d = local.json<ArenaDraft | null>(DRAFT_KEY, null);
        return d && Array.isArray(d.spawns) ? d : null;
    }

    // ---- custom modes ----

    setModes(modes: ModeDef[]): void {
        this.modes = modes.map(cloneMode);
        local.set(MODES_KEY, JSON.stringify(this.modes));
        this.emit('modes');
    }

    selected(): { pos: Vec3 } | null {
        const s = this.sel;
        if (!s) return null;
        if (s.kind === 'bounds') return null;
        const list =
            s.kind === 'spawn' ? this.draft.spawns : s.kind === 'pickup' ? this.draft.pickups : this.draft.markers;
        return list.find((x) => x.uid === s.uid) ?? null;
    }
}

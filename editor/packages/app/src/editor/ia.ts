/**
 * The editor's information architecture in one table: where each registered editor opens and what it is called.
 * Features register their panel with `registerPanel` (registry.ts) and stay unaware of the shell layout; the shell
 * (workspace.ts) reads this table to put the panel in the right place. See
 * docs/journal/2026-11-01/editor-information-architecture.md.
 *
 *   tab   an editor of its own: a closable tab in the top bar next to the Level Editor (like an Unreal asset editor)
 *   dock  a closable tab in the Level Editor's bottom area, beside the viewport (editors that work on what is in view)
 *   side  a tab beside Details in the Level Editor's right column (per-level settings, like Unreal's World Settings)
 *
 * A `tab` or `dock` editor can be moved to the other place by the user ("Dock in Level Editor" / "Open in own tab");
 * the choice is remembered. Opening an editor from the viewport (double-click a story badge, "Add cut scene here")
 * always docks it below the viewport unless it is open in its own tab right now (`Workspace.showBeside`), the way
 * Unreal's Sequencer opens in the bottom drawer while the level stays in view.
 */
export type Home = 'tab' | 'dock' | 'side';

export interface Placement {
    home: Home;
    /** Name in the tab and the Window menu (overrides the panel's own title). */
    title?: string;
    /** One line under the title in the editor header and the Window menu. */
    summary?: string;
    /** Palette title of the command that opens it. */
    command?: string;
    /** Bound key (shown in menus and the palette; see shortcuts.ts). */
    keys?: string;
    /** Can be moved between `tab` and `dock`. */
    movable?: boolean;
    /** Docked, it grows the bottom drawer to at least this share of the Level Editor's height (0..1) when it opens. */
    dockShare?: number;
}

export const PLACEMENT: Record<string, Placement> = {
    project: {
        home: 'tab',
        title: 'Project Settings',
        summary: 'Name, author, version, the levels of the mod in campaign order and their exits',
        command: 'Open Project Settings',
        keys: 'Ctrl+,',
    },
    mod: {
        home: 'tab',
        title: 'Package and Play',
        summary: 'What the mod changes, where it was written and how to play it',
        command: 'Open Package and Play',
    },
    story: {
        // Beside the viewport by default (cut scenes, quest areas and talkers are drawn in it); can be popped out.
        home: 'dock',
        title: 'Story',
        summary: 'Quests, conversations, cut scenes and text',
        command: 'Open the Story editor',
        keys: 'Ctrl+Shift+Y',
        movable: true,
        dockShare: 0.5,
    },
    logic: {
        home: 'dock',
        title: 'Logic',
        summary: 'Wire triggers, doors, timers and cut scenes of the open level',
        command: 'Open the Logic graph',
        keys: 'Ctrl+Shift+K',
        movable: true,
    },
    menus: {
        home: 'tab',
        title: 'Menus',
        summary: 'Menu entries, theme colours and the HUD',
        command: 'Open the Menus editor',
        movable: true,
    },
    vendors: {
        home: 'tab',
        title: 'Vendors',
        summary: 'What each shop of the open level sells, and for how much',
        command: 'Open the Vendors editor',
        movable: true,
    },
    multiplayer: {
        home: 'side',
        title: 'World Settings',
        summary: 'Multiplayer arena of the open level: spawns, pickups, bounds and modes',
        command: 'Open World Settings (multiplayer arena)',
    },
};

/** Tabs of the bottom area that are always there (the rest open on demand and can be closed). */
export const FIXED_DOCK = ['assets', 'output', 'validation'] as const;

export const placementOf = (id: string): Placement => PLACEMENT[id] ?? { home: 'tab', movable: true };

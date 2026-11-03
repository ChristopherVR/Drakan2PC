import type { LevelUi } from '../../levelui.ts';
import type { Ctx } from '../ctx.ts';
import { registerCommand, registerPanel } from '../registry.ts';
import { addValidationProvider } from '../panels.ts';
import { setArenaPlayProvider } from '../../playtest.ts';
import { buildArenaPackFiles, exportBlockers } from '@drakan/editor-core';
import { createArenaPanel } from './panel.ts';
import { ArenaStore, arenaTestMode } from './store.ts';
import './arena.css';

export interface ArenaInstallHost {
    ui: LevelUi;
    download(name: string, data: BlobPart, type: string): void;
    setStatus(msg: string, error?: boolean): void;
}

/**
 * Add the Multiplayer Map mode to the level editor: a dock tab, palette commands and Validation issues. Called once
 * from `main.ts`; everything else (overlay, store, export) hangs off the panel.
 */
export function installMultiplayerMaps(host: ArenaInstallHost): { store: ArenaStore } {
    const ctx: Ctx = host.ui.ctx();
    const store = new ArenaStore(() => host.ui.view());
    const panel = createArenaPanel(ctx, store, {
        levels: () => host.ui.levels().map((l) => ({ name: l.name, dirPath: l.dirPath })),
        openLevel: (p) => host.ui.openLevel(p),
        visualsReady: () => host.ui.visualsReady(),
        target: () => host.ui.modTarget(),
        download: host.download,
        setStatus: host.setStatus,
    });
    registerPanel({
        id: 'multiplayer',
        title: 'Multiplayer Map',
        icon: 'rocket',
        panel,
        command: {
            title: 'Open the Multiplayer Map editor',
            keywords: 'arena pvp deathmatch spawn pickup mode',
            hint: 'Build a PvP arena from a campaign level',
        },
    });
    registerCommand({
        id: 'mp.spawn',
        title: 'Place spawn points',
        category: 'Multiplayer',
        icon: 'character',
        enabled: () => store.active,
        run: () => store.setTool('spawn'),
    });
    registerCommand({
        id: 'mp.pickup',
        title: 'Place pickups',
        category: 'Multiplayer',
        icon: 'item',
        enabled: () => store.active,
        run: () => store.setTool('pickup'),
    });
    registerCommand({
        id: 'mp.bounds',
        title: 'Draw arena bounds',
        category: 'Multiplayer',
        icon: 'top',
        enabled: () => store.active,
        run: () => store.setTool('bounds'),
    });
    // "Play test arena" (editor/playtest.ts): the open map as a pack, built on demand (no preview picture).
    setArenaPlayProvider(() => {
        if (!store.active) return null;
        const d = store.draft;
        const v = ctx.view();
        const blockers = exportBlockers(d, {
            groundAt: (x, z) => v.groundHeightAt(x, z),
            hasGround: v.hasGround,
            modes: store.modes,
            items: [],
            reach: false,
        });
        if (blockers.length) return { problem: `Fix the map first: ${blockers[0]}` };
        const target = host.ui.modTarget();
        if (!target) return { problem: 'The disc profile is unknown; open the ISO first' };
        const meta = {
            id: `mp-${d.id}`.slice(0, 64),
            name: d.name || d.id,
            version: '1.0.0',
            description: `Multiplayer map ${d.name} (play test from the Drakan editor).`,
            target,
        };
        const custom = arenaTestMode(d, store.modes);
        return {
            meta,
            files: buildArenaPackFiles(d, store.modes, meta, null),
            map: d.id,
            mode: custom
                ? custom.base
                : d.modes.includes('dm') || !d.modes.includes('tdm')
                  ? ('dm' as const)
                  : ('tdm' as const),
            ...(custom ? { gameMode: custom.id, gameModeName: custom.name || custom.id } : {}),
        };
    });
    addValidationProvider({
        issues: () =>
            store.active
                ? panel
                      .issues()
                      .filter((i) => i.severity !== 'info')
                      .map((i) => ({
                          severity: i.severity === 'error' ? ('error' as const) : ('warning' as const),
                          code: `mp-${i.code}`,
                          message: `Multiplayer map: ${i.message}`,
                          run: () => panel.reveal(i),
                      }))
                : [],
        onChange: (fn) => panel.onIssues(fn),
    });
    return { store };
}

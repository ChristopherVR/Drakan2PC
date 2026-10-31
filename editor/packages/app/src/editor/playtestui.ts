// Play test entries in the editor shell: right-click menus (viewport and Outliner), palette commands, F5 and a small
// menu button beside the toolbar's Play. The work itself is in `../playtest.ts`; this file only wires it to the shell.
// In the browser build the entries are shown disabled with the hint "Available in the desktop app".

import type { LevelObject } from '@drakan/editor-core';
import type { LevelUi } from '../levelui.ts';
import {
    DESKTOP_ONLY,
    arenaPlayAvailable,
    arenaPlayProblem,
    isCutScene,
    playArena,
    playCutScene,
    playFromHere,
    standableProblem,
    yawFromDirection,
    type PlayTestHost,
} from '../playtest.ts';
import { isDesktop } from '../platform.ts';
import { registerCommand, registerContextItem, type ContextTarget } from './registry.ts';
import { contextMenu, type MenuItem } from './ui.ts';

/** Player is placed a little above the clicked surface and drops onto the floor (the arena does the same). */
const LIFT = 256;

export function installPlayTest(ui: LevelUi, status: (msg: string, error?: boolean) => void): void {
    const ctx = ui.ctx();
    const host: PlayTestHost = {
        doc: () => {
            try {
                const d = ctx.doc();
                return d.size ? d : null;
            } catch {
                return null;
            }
        },
        src: () => ctx.src(),
        env: () => ctx.schemas(),
        catalog: () => ctx.catalog(),
        target: () => ui.modTarget(),
        toast: (m, k) => ctx.toast(m, k),
        status,
    };
    const fail = (e: unknown): void => {
        const msg = e instanceof Error ? e.message : String(e);
        status(msg, true);
        ctx.toast(msg, 'error');
    };
    const run = (p: Promise<void>): void => void p.catch(fail);

    const cameraYaw = (): number => {
        const cam = ui.view().camera;
        const d = cam.getWorldDirection(cam.position.clone());
        return yawFromDirection(d.x, d.z);
    };
    const object = (id: number): LevelObject | undefined => host.doc()?.get(id);
    const cutScene = (t: ContextTarget): number | null => {
        const env = host.env();
        const o = t.ids.length === 1 ? object(t.ids[0]!) : undefined;
        return o && env && isCutScene(o, env) ? o.id : null;
    };
    const desk = (): string | null => (isDesktop ? null : DESKTOP_ONLY);
    const noLevel = (): string | null => desk() ?? (host.doc() && host.src() ? null : 'Open a level first');

    /** Play from a ground pick; objects are floors by definition, a pick on the terrain is checked first. */
    const here = (pos: readonly number[], what: string, checked = true): void => {
        const d = host.doc();
        const view = ui.view();
        const bad = checked && d ? standableProblem(d, (x, z) => view.groundHeightAt(x, z), pos) : null;
        if (bad) return fail(new Error(bad));
        run(playFromHere(host, [pos[0]!, pos[1]! + LIFT, pos[2]!], cameraYaw(), what));
    };
    const fromCamera = (): void => {
        const p = ui.view().groundAtCentre();
        if (!p) return fail(new Error('Nothing to stand on in the middle of the view.'));
        here(p, 'Play from camera');
    };

    registerContextItem({
        id: 'play.here',
        label: 'Play from here',
        icon: 'play',
        where: 'world',
        unavailable: noLevel,
        run: (t) => t.ground && here(t.ground, 'Play from here'),
    });
    registerContextItem({
        id: 'play.object',
        label: 'Play from this object',
        icon: 'play',
        where: 'object',
        when: (t) => t.ids.length === 1,
        unavailable: noLevel,
        run: (t) => {
            const o = object(t.ids[0]!);
            if (o) here(o.transform.pos, 'Play from object', false);
        },
    });
    registerContextItem({
        id: 'play.cutscene',
        label: 'Play this cut scene',
        icon: 'play',
        where: 'object',
        when: (t) => cutScene(t) !== null,
        unavailable: noLevel,
        run: (t) => {
            const id = cutScene(t);
            if (id !== null) run(playCutScene(host, id));
        },
    });
    const arenaWhy = (): string | null => desk() ?? arenaPlayProblem();
    registerContextItem({
        id: 'play.arena',
        label: 'Play test arena (host)',
        icon: 'play',
        where: 'any',
        when: () => arenaPlayAvailable() || !isDesktop,
        unavailable: arenaWhy,
        run: () => run(playArena(host, false)),
    });
    registerContextItem({
        id: 'play.arena2',
        label: 'Play test arena (host + second player)',
        icon: 'play',
        where: 'any',
        when: () => arenaPlayAvailable() || !isDesktop,
        unavailable: arenaWhy,
        run: () => run(playArena(host, true)),
    });

    registerCommand({
        id: 'play.camera',
        title: 'Play from camera',
        category: 'Play',
        icon: 'play',
        keys: 'F5',
        hint: 'Start the game on this level with the player where the camera looks (desktop app)',
        keywords: 'test run launch here',
        enabled: () => isDesktop && !!host.doc(),
        run: fromCamera,
    });
    registerCommand({
        id: 'play.arena',
        title: 'Play test arena',
        category: 'Play',
        icon: 'play',
        hint: 'Host the open multiplayer map locally (desktop app)',
        enabled: () => isDesktop && arenaPlayAvailable(),
        run: () => run(playArena(host, false)),
    });
    registerCommand({
        id: 'play.arena2',
        title: 'Play test arena with a second player',
        category: 'Play',
        icon: 'play',
        hint: 'Host the map and start a second instance that joins it (desktop app)',
        enabled: () => isDesktop && arenaPlayAvailable(),
        run: () => run(playArena(host, true)),
    });

    // F5: play from the camera (desktop only: in a browser F5 keeps reloading the page).
    if (isDesktop)
        window.addEventListener('keydown', (e) => {
            if (e.key !== 'F5' || e.ctrlKey || e.shiftKey || e.altKey) return;
            const t = e.target as HTMLElement | null;
            if (t && /^(input|textarea|select)$/i.test(t.tagName)) return;
            if (!host.doc()) return;
            e.preventDefault();
            fromCamera();
        });

    // A drop-down beside the toolbar's Play button with the same entries.
    const play = document.getElementById('ed-play');
    if (play && !document.getElementById('ed-play-menu')) {
        const btn = document.createElement('button');
        btn.id = 'ed-play-menu';
        btn.type = 'button';
        btn.className = 'ui-btn sm';
        btn.title = 'Play test options';
        btn.setAttribute('aria-label', 'Play test options');
        btn.textContent = '▾';
        btn.addEventListener('click', () => {
            const r = btn.getBoundingClientRect();
            const sel = [...ctx.view().selection];
            const t: ContextTarget = { ids: sel, ground: ctx.view().groundAtCentre() };
            const item = (label: string, why: string | null, fn: () => void): MenuItem => ({
                label,
                icon: 'play',
                run: fn,
                ...(why ? { disabled: true, hint: why } : {}),
            });
            const items: MenuItem[] = [
                { heading: 'Play test' },
                item('Play from camera', noLevel(), fromCamera),
                item('Play from selected object', noLevel() ?? (sel.length === 1 ? null : 'Select one object'), () => {
                    const o = object(sel[0]!);
                    if (o) here(o.transform.pos, 'Play from object', false);
                }),
                item(
                    'Play selected cut scene',
                    noLevel() ?? (cutScene(t) !== null ? null : 'Select a cut scene player'),
                    () => {
                        const id = cutScene(t);
                        if (id !== null) run(playCutScene(host, id));
                    },
                ),
                { sep: true },
                item('Play test arena (host)', arenaWhy(), () => run(playArena(host, false))),
                item('Play test arena (host + second player)', arenaWhy(), () => run(playArena(host, true))),
            ];
            contextMenu(r.left, r.bottom + 4, items);
        });
        play.after(btn);
    }
}

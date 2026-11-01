/**
 * The editor workspace: the tab strip in the top bar and where every editor opens. Modelled on Unreal's editor: the
 * Level Editor is the main tab; content editors (Story, Menus, Vendors, Project Settings, ...) open as their own
 * closable tabs next to it; editors that work on what is in the viewport (Logic) open in the Level Editor's bottom
 * area; per-level settings (the multiplayer arena) sit in World Settings beside Details. The disc file browser is a
 * tool tab; the save game editor is an application of its own (save.html). Placement comes from `ia.ts`; panels
 * register through `registry.ts` and never touch
 * the layout themselves.
 */
import type { Panel } from './ctx.ts';
import { fill, h, icon, store } from './dom.ts';
import { FIXED_DOCK, placementOf, type Home } from './ia.ts';
import type { Layout } from './layout.ts';
import type { Dock } from './panels.ts';
import type { PanelSpec } from './registry.ts';
import { openAppWindow } from '../platform.ts';
import './workspace.css';

/** `level`, `inspect` (disc files) or the id of an editor open in its own tab (`save` opens the save editor app). */
export type Area = string;

export interface WorkspaceDeps {
    /** #level-main: the frame that holds the Level Editor and the editor tabs (its styles apply to both). */
    frame: HTMLElement;
    toolbar: HTMLElement;
    body: HTMLElement;
    /** Host for editors open in their own tab (inside the frame). */
    doc: HTMLElement;
    /** The lower half of the right column (Details) and the inspector shown there. */
    details: HTMLElement;
    inspector: HTMLElement;
    dock: Dock;
    layout: () => Layout | null;
    /** Every panel that gets selection / change / load events (the workspace adds the editors it hosts). */
    panels: Panel[];
    /** The Level Editor became visible again (resize the viewport). */
    levelShown: () => void;
}

export interface Workspace {
    add(spec: PanelSpec): void;
    /** Show an editor where it lives (opening its tab first). */
    show(id: string): void;
    /** Show it in a given place (`tab` or `dock`); `remember` keeps the choice for later sessions too. */
    openIn(id: string, home: 'tab' | 'dock', remember?: boolean): void;
    close(id: string): void;
    showArea(a: Area): void;
    area(): Area;
    /** The Level Editor (viewport, Outliner, Details) is what is on screen. */
    levelActive(): boolean;
    /** The disc files are on screen (no editor keyboard shortcuts at all). */
    toolActive(): boolean;
    /** Editors that are registered: id, title, icon, where they open. */
    editors(): { id: string; title: string; icon: string; home: Home; open: boolean }[];
    onArea(fn: (a: Area) => void): void;
    /** A small floating window inside the editor frame (Undo History). Calling it again toggles it. */
    toggleWindow(id: string, title: string, ico: string, panel: Panel): void;
}

let current: Workspace | null = null;
const pendingArea: Area[] = [];
/** The workspace once the Level Editor built it (main.ts and the desktop shell switch areas through it). */
export const workspace = (): Workspace | null => current;
/** Switch area; before the workspace exists the request waits. */
export function showArea(a: Area): void {
    if (current) current.showArea(a);
    else pendingArea.push(a);
}

const TOOL_TABS: { id: string; title: string; icon: string; tip: string; closable: boolean }[] = [
    {
        id: 'inspect',
        title: 'Disc Files',
        icon: 'folder',
        tip: 'Advanced: browse the raw files on your disc',
        closable: true,
    },
];

export function createWorkspace(d: WorkspaceDeps): Workspace {
    const specs = new Map<string, PanelSpec>();
    const where = new Map<string, Home>();
    const openTabs: string[] = [];
    let filesOpen = false;
    let active: Area = 'level';
    const listeners = new Set<(a: Area) => void>();
    const strip = document.getElementById('tabs');

    // ---- the editor-tab host (inside the frame) ------------------------------------------------------------------
    const docHead = h('div', { class: 'ed-doc-head' });
    const docBody = h('div', { class: 'ed-doc-body' });
    d.doc.append(docHead, docBody);
    /** Panels that are not shown anywhere wait here (hidden) so their DOM keeps its state. */
    const park = (p: Panel): void => {
        p.el.hidden = true;
        if (p.el.parentElement !== docBody) docBody.append(p.el);
    };

    // ---- World Settings beside Details ---------------------------------------------------------------------------
    const sideStrip = h('div', {
        class: 'side-tabs',
        role: 'tablist',
        'aria-label': 'Details and settings',
        hidden: true,
    });
    const sideBody = h('div', { class: 'side-body' });
    d.details.prepend(sideStrip);
    let sideActive = 'details';
    const sideIds: string[] = [];
    function renderSide(): void {
        sideStrip.hidden = sideIds.length === 0;
        const btn = (id: string, title: string, ico: string): HTMLElement =>
            h(
                'button',
                {
                    type: 'button',
                    role: 'tab',
                    class: `side-tab${sideActive === id ? ' active' : ''}`,
                    'aria-selected': String(sideActive === id),
                    'data-side': id,
                    onclick: () => selectSide(id),
                },
                icon(ico, 13),
                h('span', {}, title),
            );
        sideStrip.replaceChildren(
            btn('details', 'Details', 'details'),
            ...sideIds.map((id) => btn(id, specs.get(id)!.title, specs.get(id)!.icon)),
        );
    }
    function selectSide(id: string): void {
        sideActive = id;
        d.inspector.hidden = id !== 'details';
        for (const s of sideIds) specs.get(s)!.panel.el.hidden = s !== id;
        d.details.classList.toggle('side-other', id !== 'details');
        renderSide();
    }

    // ---- placement -------------------------------------------------------------------------------------------------
    const homeOf = (id: string): Home => {
        const known = where.get(id);
        if (known) return known;
        const p = placementOf(id);
        let h0 = p.home;
        if (p.movable) {
            const saved = store.get(`ws-place:${id}`);
            if (saved === 'tab' || saved === 'dock') h0 = saved;
        }
        where.set(id, h0);
        return h0;
    };

    function add(spec: PanelSpec): void {
        if (specs.has(spec.id)) return;
        specs.set(spec.id, spec);
        d.panels.push(spec.panel);
        spec.panel.el.classList.add('ws-editor');
        if (homeOf(spec.id) === 'side') {
            sideIds.push(spec.id);
            spec.panel.el.hidden = true;
            sideBody.append(spec.panel.el);
            if (!sideBody.parentElement) d.details.append(sideBody);
            renderSide();
        } else park(spec.panel);
        renderStrip();
    }

    function dockTab(id: string): void {
        const s = specs.get(id)!;
        if (d.dock.has(id)) return;
        d.dock.addTab({
            id,
            title: s.title,
            icon: s.icon,
            panel: s.panel,
            onClose: () => close(id),
            ...(placementOf(id).movable ? { onPopOut: () => openIn(id, 'tab') } : {}),
        });
    }

    function show(id: string): void {
        const s = specs.get(id);
        if (!s) return;
        const home = homeOf(id);
        if (home === 'side') {
            showArea('level');
            const l = d.layout();
            if (l && !l.isOpen('right')) l.toggle('right', true);
            selectSide(id);
            return;
        }
        if (home === 'dock') {
            showArea('level');
            dockTab(id);
            d.layout()?.openBottom();
            d.dock.show(id);
            return;
        }
        if (!openTabs.includes(id)) openTabs.push(id);
        showArea(id);
    }

    function close(id: string): void {
        const s = specs.get(id);
        if (!s) return;
        if (d.dock.has(id)) {
            d.dock.removeTab(id);
            park(s.panel);
        }
        const i = openTabs.indexOf(id);
        if (i >= 0) {
            openTabs.splice(i, 1);
            if (active === id) showArea(openTabs[Math.max(0, i - 1)] ?? 'level');
            else renderStrip();
        }
    }

    function openIn(id: string, home: 'tab' | 'dock', remember = true): void {
        if (!specs.has(id)) return;
        if (homeOf(id) !== home || !(openTabs.includes(id) || d.dock.has(id))) close(id);
        where.set(id, home);
        if (remember) store.set(`ws-place:${id}`, home);
        show(id);
    }

    // ---- areas -----------------------------------------------------------------------------------------------------
    const isEditor = (a: Area): boolean => specs.has(a) && openTabs.includes(a);

    function showArea(a: Area): void {
        // The save game editor is its own application (save.html, its own desktop window).
        if (a === 'save') {
            void openAppWindow('save');
            return;
        }
        if (a === 'inspect') filesOpen = true;
        if (a !== 'level' && a !== 'save' && a !== 'inspect' && !isEditor(a)) {
            if (specs.has(a)) return show(a);
            a = 'level';
        }
        const was = active;
        active = a;
        const editor = isEditor(a);
        d.frame.hidden = !(a === 'level' || editor);
        document.getElementById('inspect-main')?.toggleAttribute('hidden', a !== 'inspect');
        d.toolbar.hidden = editor;
        d.body.hidden = editor;
        d.doc.hidden = !editor;
        d.frame.classList.toggle('doc-mode', editor);
        if (editor) {
            const s = specs.get(a)!;
            for (const c of docBody.children) (c as HTMLElement).hidden = c !== s.panel.el;
            if (s.panel.el.parentElement !== docBody) docBody.append(s.panel.el);
            s.panel.el.hidden = false;
            renderHead(s);
        }
        renderStrip();
        if (a === 'level' && was !== 'level') d.levelShown();
        if (was !== a) listeners.forEach((l) => l(a));
    }

    function renderHead(s: PanelSpec): void {
        const p = placementOf(s.id);
        fill(
            docHead,
            h('span', { class: 'ed-doc-ico' }, icon(s.icon, 16)),
            h('h2', { class: 'ed-doc-title' }, s.title),
            p.summary ? h('span', { class: 'ed-doc-sum' }, p.summary) : null,
            h('span', { class: 'spacer' }),
            p.movable
                ? h(
                      'button',
                      {
                          class: 'btn sm',
                          type: 'button',
                          title: 'Show this editor below the viewport of the Level Editor',
                          onclick: () => openIn(s.id, 'dock'),
                      },
                      icon('dock-bottom', 13),
                      'Dock in Level Editor',
                  )
                : null,
            h(
                'button',
                {
                    class: 'tool sm',
                    type: 'button',
                    title: `Close ${s.title}`,
                    'aria-label': `Close ${s.title}`,
                    onclick: () => close(s.id),
                },
                icon('x', 13),
            ),
        );
    }

    // ---- the tab strip in the top bar ------------------------------------------------------------------------------
    function tabButton(
        id: string,
        domId: string,
        title: string,
        ico: string,
        tip: string,
        onClose: (() => void) | null,
        extra = '',
    ): HTMLElement {
        const b = h(
            'button',
            {
                id: domId,
                type: 'button',
                role: 'tab',
                class: `ws-tab${extra}${active === id ? ' active' : ''}`,
                'aria-selected': String(active === id),
                title: tip,
                onclick: () => showArea(id),
            },
            icon(ico, 13),
            h('span', { class: 'ws-tab-label' }, title),
        );
        if (onClose) {
            b.append(
                h(
                    'span',
                    {
                        class: 'ws-tab-x',
                        role: 'button',
                        title: `Close ${title}`,
                        'aria-label': `Close ${title}`,
                        onclick: (e: Event) => {
                            e.stopPropagation();
                            onClose();
                        },
                    },
                    icon('x', 10),
                ),
            );
            b.addEventListener('auxclick', (e) => {
                if (e.button === 1) onClose();
            });
        }
        return b;
    }

    function renderStrip(): void {
        if (!strip) return;
        strip.setAttribute('role', 'tablist');
        const kids: HTMLElement[] = [
            tabButton('level', 'tab-level', 'Level Editor', 'world', 'Build levels: viewport, Outliner, Details', null),
        ];
        for (const id of openTabs) {
            const s = specs.get(id)!;
            kids.push(
                tabButton(id, `tab-ed-${id}`, s.title, s.icon, placementOf(id).summary ?? s.title, () => close(id)),
            );
        }
        kids.push(h('span', { class: 'ws-tab-sep', 'aria-hidden': 'true' }));
        for (const t of TOOL_TABS) {
            if (t.id === 'inspect' && !filesOpen) continue;
            kids.push(
                tabButton(
                    t.id,
                    `tab-${t.id}`,
                    t.title,
                    t.icon,
                    t.tip,
                    t.closable
                        ? () => {
                              filesOpen = false;
                              if (active === t.id) showArea('level');
                              else renderStrip();
                          }
                        : null,
                    ' tool',
                ),
            );
        }
        strip.replaceChildren(...kids);
    }

    // ---- floating windows ------------------------------------------------------------------------------------------
    const windows = new Map<string, HTMLElement>();
    function toggleWindow(id: string, title: string, ico: string, panel: Panel): void {
        const open = windows.get(id);
        if (open) {
            open.remove();
            windows.delete(id);
            return;
        }
        if (active !== 'level') showArea('level');
        const head = h(
            'div',
            { class: 'ed-window-head' },
            icon(ico, 13),
            h('strong', {}, title),
            h('span', { class: 'spacer' }),
            h(
                'button',
                {
                    class: 'tool sm',
                    type: 'button',
                    title: `Close ${title}`,
                    'aria-label': `Close ${title}`,
                    onclick: () => toggleWindow(id, title, ico, panel),
                },
                icon('x', 12),
            ),
        );
        const win = h('div', { class: 'ed-window', role: 'dialog', 'aria-label': title, 'data-window': id }, head);
        panel.el.hidden = false;
        win.append(panel.el);
        d.body.append(win);
        windows.set(id, win);
        panel.onLoad?.();
        // Drag by the title bar.
        head.addEventListener('pointerdown', (e) => {
            if ((e.target as HTMLElement).closest('button')) return;
            const r = win.getBoundingClientRect();
            const host = d.body.getBoundingClientRect();
            const dx = e.clientX - r.left;
            const dy = e.clientY - r.top;
            head.setPointerCapture(e.pointerId);
            const move = (ev: PointerEvent): void => {
                win.style.left = `${Math.max(0, Math.min(host.width - 80, ev.clientX - host.left - dx))}px`;
                win.style.top = `${Math.max(0, Math.min(host.height - 40, ev.clientY - host.top - dy))}px`;
                win.style.right = 'auto';
            };
            const up = (): void => {
                head.removeEventListener('pointermove', move);
                head.removeEventListener('pointerup', up);
            };
            head.addEventListener('pointermove', move);
            head.addEventListener('pointerup', up);
        });
    }

    // The fixed bottom-area tabs are the dock's own; nothing to do for them here.
    void FIXED_DOCK;

    const ws: Workspace = {
        add,
        show,
        openIn,
        close,
        showArea,
        area: () => active,
        levelActive: () => active === 'level',
        toolActive: () => active === 'inspect',
        editors: () =>
            [...specs.values()].map((s) => ({
                id: s.id,
                title: s.title,
                icon: s.icon,
                home: homeOf(s.id),
                open: openTabs.includes(s.id) || d.dock.has(s.id),
            })),
        onArea: (fn) => void listeners.add(fn),
        toggleWindow,
    };
    current = ws;
    renderStrip();
    for (const a of pendingArea.splice(0)) showArea(a);
    return ws;
}

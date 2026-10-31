import {
    AddCommand,
    CompositeCommand,
    LevelDocument,
    SchemaProvider,
    alignPositions,
    buildCatalog,
    copyObjects,
    diffDocument,
    distributePositions,
    documentFromLvl,
    editObjects,
    moveCommand,
    newObjectFromClass,
    parseClipboard,
    parseWorldDump,
    pasteObjects,
    snapToGround,
    type AlignMode,
    type AssetClass,
    type Axis,
    type EntityKind,
    type LevelObject,
    type ModMeta,
    type WorldComparison,
    compareWithWorld,
    type LevelDocumentJson,
} from '@drakan/editor-core';
import type { DiscProfile } from '@drakan/formats';
import { KINDS, LevelView, type ToolMode } from './level.ts';
import { changeCount } from './modio.ts';
import type { Api, LevelInfo, LevelLoadResult, Method } from './rpc.ts';
import { createSceneLoader } from './sceneload.ts';
import { HELPER_GROUPS, helperGroupOf, type HelperGroup } from './scene/instances.ts';
import { parseViewOptions, type ViewOptions } from './scene/viewopts.ts';
import { createAssets, type AssetsPanel } from './editor/assets.ts';
import { autosaveKey, clearAutosave, loadAutosave, saveAutosave } from './editor/autosave.ts';
import type { Ctx } from './editor/ctx.ts';
import { fill, h, icon, store } from './editor/dom.ts';
import { createInspector } from './editor/inspector.ts';
import { initLayout, type Layout } from './editor/layout.ts';
import { createOutliner } from './editor/outliner.ts';
import { createDock, createHistory, createModPanel, createValidation } from './editor/panels.ts';
import { helpOverlay, initShortcuts } from './editor/shortcuts.ts';
import { VIEW_COLORS, applyTheme, currentTheme, onThemeChange, toggleTheme } from './editor/theme.ts';
import { createToolbar, type ToolbarActions } from './editor/toolbar.ts';
import './editor/editor.css';

const $ = <T extends HTMLElement>(id: string): T => {
    const el = document.getElementById(id);
    if (!el) throw new Error(`missing #${id}`);
    return el as T;
};

export interface LevelUiHost {
    call<M extends Method>(method: M, ...args: Parameters<Api[M]>): Promise<ReturnType<Api[M]>>;
    setStatus(msg: string, error?: boolean): void;
}

function download(name: string, data: BlobPart, type: string): void {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([data], { type }));
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 10_000);
}

export interface DiscContext {
    isoName: string;
    levels: LevelInfo[];
    profile: DiscProfile | null;
}

export interface LevelUi {
    view: () => LevelView;
    loadFile(f: File): Promise<void>;
    setDisc(d: DiscContext | null): void;
    openLevel(dirPath: string): Promise<void>;
    /** Open an already loaded level (the worker result); also used by tests with synthetic data. */
    openLoaded(r: LevelLoadResult, displayName: string): Promise<void>;
    /** The Asset Browser panel (thumbnail provider hook for the model renderer). */
    assets: () => AssetsPanel;
    ctx: () => Ctx;
    /** Resolves when terrain and models of the last `openLevel` finished loading (tests, harnesses). */
    visualsReady: () => Promise<void>;
    /** Class name and model render for a resource string (Save mode inventory); null when no such class is loaded. */
    classInfo(resource: string): { name: string; thumb: string | null } | null;
}

const AUTOSAVE_DELAY = 1500;

/**
 * The Level tab: a docked editor (toolbar, Outliner, viewport, Inspector, Asset Browser / History / Validation / Mod
 * panels, status bar). This file wires the panels (`./editor/*`) to the view (`level.ts`) and the document
 * (`@drakan/editor-core`).
 */
export function initLevelUi(host: LevelUiHost): LevelUi {
    const root = $('level-main');
    const empty = $('lv-empty');
    let view: LevelView | null = null;
    let levelName = 'level';
    let disc: DiscContext | null = null;
    /** The level the document was loaded from (LVL source); null for a world dump or a saved document. */
    let src: LevelLoadResult | null = null;
    let catalog: AssetClass[] = [];
    let schemas: SchemaProvider | null = null;
    let worldDoc: LevelDocument | null = null;
    let comparison: WorldComparison | null = null;
    let ghosts = true;
    let clipboard: ReturnType<typeof copyObjects> = null;
    let savedTop: unknown = undefined;
    let restoredDirty = false;
    let autosaveTimer = 0;
    let lastAutosave = 0;
    const hiddenKinds = new Set<EntityKind>();
    let gridSnapOn = true;
    let viewOpts: ViewOptions = parseViewOptions(store.json('view-options', null));
    let visuals: Promise<void> = Promise.resolve();

    // ---- shell ----------------------------------------------------------------------------------------------------

    applyTheme(currentTheme());
    const tip = h('div', { class: 'tip', hidden: true });
    const toasts = h('div', { class: 'toasts', 'aria-live': 'polite' });
    document.body.append(tip, toasts);
    const help = helpOverlay();
    document.body.append(help.el);

    const statusMsg = h('span', { class: 'sb-msg' }, 'Ready');
    const sbSel = h('span', { class: 'sb-item', id: 'sb-sel' });
    const sbCam = h('span', { class: 'sb-item mono', id: 'sb-cam' });
    const sbSnap = h('span', { class: 'sb-item', id: 'sb-snap' });
    const sbSave = h('span', { class: 'sb-item', id: 'sb-save' });
    const sbFps = h('span', { class: 'sb-item mono dim', id: 'sb-fps' });
    fill($('ed-status'), statusMsg, h('span', { class: 'spacer' }), sbSel, sbSnap, sbCam, sbSave, sbFps);

    function toast(msg: string, kind: 'info' | 'error' | 'ok' | 'warn' = 'info'): void {
        const t = h('div', { class: `toast ${kind}`, role: 'status' }, msg);
        toasts.append(t);
        window.setTimeout(() => t.classList.add('out'), kind === 'error' || kind === 'warn' ? 6000 : 2800);
        window.setTimeout(() => t.remove(), kind === 'error' || kind === 'warn' ? 6400 : 3200);
        statusMsg.textContent = msg;
        statusMsg.classList.toggle('error', kind === 'error');
    }
    const fail = (e: unknown): void => {
        const msg = e instanceof Error ? e.message : String(e);
        host.setStatus(msg, true);
        toast(msg, 'error');
    };

    // ---- context for the panels -------------------------------------------------------------------------------------

    const ctx: Ctx = {
        view: () => getView(),
        doc: () => getView().doc,
        catalog: () => catalog,
        schemas: () => schemas,
        classThumb(cls) {
            loader.wantThumbnail(cls);
            return loader.thumbnail(cls.resource);
        },
        src: () => src,
        toast,
        locate(ids) {
            const v = getView();
            v.select(ids.filter((i) => v.doc.has(i)));
            v.frameSelection();
        },
        placeClass(cls, pos) {
            const v = getView();
            if (!v.policy.structural) return;
            const at = pos ?? snapPos(v.groundAtCentre() ?? v.cameraGamePos());
            const o = newObjectFromClass(v.doc, cls, at, 0);
            const cmd = new AddCommand([o], `Add ${cls.name}`);
            v.run(cmd);
            v.select(cmd.ids);
            void loader.ensureModels([cls.resource]);
            if (src && !o.template) {
                toast(
                    `${cls.name} has no instance in this level, so #${cmd.ids[0]} gets generic class defaults (not verified in game). Prefer a class the level already uses.`,
                    'warn',
                );
            } else toast(`Added ${cls.name} #${cmd.ids[0]}`, 'ok');
        },
        edit(label, ids, fn) {
            const v = getView();
            const cmd = editObjects(
                v.doc,
                ids.filter((i) => !v.lockedIds.has(i)),
                label,
                fn,
            );
            if (cmd) v.run(cmd);
        },
    };

    const snapPos = (p: [number, number, number]): [number, number, number] => {
        const v = getView();
        return v.snap.grid
            ? [
                  Math.round(p[0] / v.snap.gridSize) * v.snap.gridSize,
                  p[1],
                  Math.round(p[2] / v.snap.gridSize) * v.snap.gridSize,
              ]
            : p;
    };

    // ---- panels --------------------------------------------------------------------------------------------------

    const outliner = createOutliner(ctx);
    const inspector = createInspector(ctx);
    const assets = createAssets(ctx);
    const history = createHistory(ctx);
    let dockRef: ReturnType<typeof createDock> | null = null;
    const validation = createValidation(ctx, (e, w) =>
        dockRef?.setBadge('validation', e + w ? String(e + w) : '', e ? 'bad' : w ? 'warn' : ''),
    );
    const modPanel = createModPanel(ctx, {
        target: () => target(),
        targetText: () => {
            const p = disc?.profile;
            return p
                ? `Target: ${p.serial}, profile ${p.profile ?? 'unknown'}, hash ${p.hash ?? 'unknown'}`
                : 'Target: open a disc image to derive the disc profile and hash.';
        },
        download,
        setStatus: host.setStatus,
    });
    const dock = createDock([
        { id: 'assets', title: 'Asset Browser', icon: 'box', panel: assets },
        { id: 'history', title: 'History', icon: 'history', panel: history },
        { id: 'validation', title: 'Validation', icon: 'warn', panel: validation },
        { id: 'mod', title: 'Mod', icon: 'download', panel: modPanel },
    ]);
    dockRef = dock;
    $('ed-left').append(outliner.el);
    $('ed-right').append(inspector.el);
    $('ed-bottom').append(dock.el);
    const panels = [outliner, inspector, assets, history, validation, modPanel];

    // ---- real visuals: terrain, models, thumbnails ------------------------------------------------------------------
    const loader = createSceneLoader(host.call, () => getView(), {
        status: (m) => host.setStatus(m),
        toast: (m, k) => toast(m, k),
        thumbnail: (res, url) => assets.applyThumbnail(res, url),
        modelsChanged: () => {
            syncToolbar();
            updateStatus();
        },
    });
    assets.setThumbnailProvider((cls) => loader.thumbnail(cls.resource));
    assets.setThumbnailLoader((cls) => loader.wantThumbnail(cls));

    // ---- toolbar --------------------------------------------------------------------------------------------------

    let layout: Layout | null = null;
    const actions: ToolbarActions = {
        tool: (m: ToolMode) => {
            const v = getView();
            if (m === 'scale' && v.selection.size && ![...v.selection].some((id) => v.doc.get(id)?.scale)) {
                toast('None of the selected objects has a scale (flag 0x100); scale cannot be added.');
            }
            v.setMode(m);
            syncToolbar();
        },
        toggleSpace: () => {
            const v = getView();
            v.setSpace(v.space === 'world' ? 'local' : 'world');
            syncToolbar();
        },
        toggleGridSnap: () => {
            const v = getView();
            gridSnapOn = !v.snap.grid;
            v.setSnap({ grid: gridSnapOn });
            toast(`Grid snap ${gridSnapOn ? `on (${v.snap.gridSize})` : 'off'}`);
            syncToolbar();
        },
        toggleAngleSnap: () => {
            const v = getView();
            v.setSnap({ angle: !v.snap.angle });
            toast(`Angle snap ${v.snap.angle ? `on (${v.snap.angleStep}°)` : 'off'}`);
            syncToolbar();
        },
        setGridSize: (n) => {
            getView().setSnap({ gridSize: n });
            syncToolbar();
        },
        setAngleStep: (n) => {
            getView().setSnap({ angleStep: n });
            syncToolbar();
        },
        undo: () => getView().undo(),
        redo: () => getView().redo(),
        duplicate: () => getView().duplicateSelected(),
        remove: () => getView().deleteSelected(),
        copy: () => copySelection(),
        paste: () => void pasteFromClipboard(null),
        frame: () => getView().frameSelection(),
        frameAll: () => getView().frameAll(),
        topView: () => getView().topView(),
        toSnapGround: () => snapSelectionToGround(),
        align: (axis, mode) => alignSelection(axis, mode),
        distribute: (axis) => distributeSelection(axis),
        selectAll: () => getView().selectVisible(),
        invert: () => getView().invertSelection(),
        sameClass: () => getView().selectSameClass(),
        hide: () => {
            const v = getView();
            v.setHidden([...v.selection], true);
        },
        showAll: () => {
            const v = getView();
            v.setHidden([...v.hiddenIds], false);
        },
        lock: () => {
            const v = getView();
            const ids = [...v.selection];
            v.setLocked(ids, !ids.every((i) => v.lockedIds.has(i)));
        },
        save: () => saveDocument(),
        exportMod: () => {
            layout?.openBottom();
            dock.show('mod');
            void modPanel.exportMod(false).catch(fail);
        },
        help: () => help.toggle(),
        theme: () => {
            toggleTheme();
        },
        openLevel: (p) => void openLevel(p).catch(fail),
        openDumpFile: (f) => void loadFile(f).catch(fail),
        toggleGridVisible: () => {
            const v = getView();
            v.gridVisible = !v.gridVisible;
            syncToolbar();
        },
        setLinks: (m) => {
            getView().setShowLinks(m);
            syncToolbar();
        },
        toggleKind: (k, shown) => {
            if (shown) hiddenKinds.delete(k as EntityKind);
            else hiddenKinds.add(k as EntityKind);
            getView().setFilters({ hidden: new Set(hiddenKinds) });
            syncToolbar();
        },
        setViewOptions: (patch, live) => {
            viewOpts = { ...viewOpts, ...patch };
            store.set('view-options', JSON.stringify(viewOpts));
            getView().setViewOptions(viewOpts);
            if (!live) syncToolbar();
        },
        toggleGhosts: (on) => {
            ghosts = on;
            updateOverlay();
            syncToolbar();
        },
        setFlySpeed: (s) => {
            getView().flySpeed = s;
        },
        saveBookmark: () => {
            const b = getView().saveBookmark();
            toast(`Saved view "${b.name}" (Shift+${getView().bookmarks.length})`, 'ok');
            scheduleAutosave();
        },
        gotoBookmark: (i) => getView().gotoBookmark(i),
        removeBookmark: (i) => {
            getView().bookmarks.splice(i, 1);
            scheduleAutosave();
        },
        resetLayout: () => layout?.reset(),
        togglePanel: (k) => layout?.toggle(k),
    };
    const toolbar = createToolbar($('ed-toolbar'), actions, () => toolbarState());

    function toolbarState(): Parameters<typeof toolbar.sync>[0] {
        const v = view;
        return {
            mode: v?.mode ?? 'translate',
            space: v?.space ?? 'world',
            gridSnap: v?.snap.grid ?? true,
            angleSnap: v?.snap.angle ?? true,
            gridSize: v?.snap.gridSize ?? 256,
            angleStep: v?.snap.angleStep ?? 15,
            links: v?.showLinks ?? 'selection',
            gridVisible: v?.gridVisible ?? true,
            flySpeed: v?.flySpeed ?? 1,
            hiddenKinds,
            bookmarks: v?.bookmarks ?? [],
            ghosts: worldDoc && src ? ghosts : null,
            viewOptions: viewOpts,
            helperCounts: helperCounts(),
            hasLevels: !!disc?.levels.length,
            levels: disc?.levels ?? [],
            canUndo: v?.stack.canUndo ?? false,
            canRedo: v?.stack.canRedo ?? false,
            selCount: v?.selection.size ?? 0,
            hasDoc: (v?.doc.size ?? 0) > 0,
            hasSource: src !== null && (v?.doc.size ?? 0) > 0,
            structural: v?.policy.structural ?? true,
        };
    }
    const syncToolbar = (): void => toolbar.sync(toolbarState());

    /** Objects per helper class group (Display menu). Cheap: one pass over class names, cached per document size. */
    let helperKey = '';
    let helperCache = {} as Record<HelperGroup, number>;
    function helperCounts(): Record<HelperGroup, number> {
        const v = view;
        const key = `${v?.doc.size ?? 0}:${src?.file ?? ''}`;
        if (key === helperKey) return helperCache;
        helperKey = key;
        helperCache = Object.fromEntries(HELPER_GROUPS.map((g) => [g.id, 0])) as Record<HelperGroup, number>;
        const byClass = new Map<string, HelperGroup | null>();
        for (const o of v?.doc.objects() ?? []) {
            let g = byClass.get(o.class);
            if (g === undefined) byClass.set(o.class, (g = helperGroupOf(o.class)));
            if (g) helperCache[g]++;
        }
        return helperCache;
    }

    // ---- viewport -------------------------------------------------------------------------------------------------

    const getView = (): LevelView => {
        if (!view) {
            view = new LevelView($('lv-viewport'), {
                onSelection: () => onSelection(),
                onChange: () => onChange(),
                onHover: (id, x, y) => onHover(id, x, y),
                onVisibility: () => {
                    for (const p of panels) p.onVisibility?.();
                    syncToolbar();
                    updateStatus();
                },
                onCamera: () => updateStatus(),
                onDropAsset: (key, pos) => {
                    const cls = catalog.find((c) => c.resource === key);
                    if (cls) ctx.placeClass(cls, pos);
                },
            });
            view.setEditorControls(true);
            view.setViewOptions(viewOpts);
            view.setSnap({});
            view.setDocument(new LevelDocument());
            view.setActive(true);
            applyViewTheme();
            window.setInterval(updateStatus, 500);
        }
        return view;
    };

    function applyViewTheme(): void {
        const c = VIEW_COLORS[currentTheme()];
        view?.setBackground(c.bg);
        view?.setGridColors(c.major, c.minor);
    }
    onThemeChange(() => {
        applyViewTheme();
        const b = document.getElementById('ed-theme');
        if (b) b.replaceChildren(icon(currentTheme() === 'dark' ? 'sun' : 'moon'));
    });

    function onSelection(): void {
        for (const p of panels) p.onSelection?.();
        syncToolbar();
        updateStatus();
    }

    function dirty(): boolean {
        return restoredDirty || (view?.stack.top ?? undefined) !== savedTop;
    }

    function onChange(): void {
        const v = getView();
        empty.hidden = v.doc.size > 0;
        for (const p of panels) p.onChange?.();
        syncToolbar();
        updateStatus();
        scheduleAutosave();
    }

    function onHover(id: number | null, x: number, y: number): void {
        const o = id === null ? undefined : getView().doc.get(id);
        if (!o) {
            tip.hidden = true;
            return;
        }
        fill(
            tip,
            h('strong', {}, o.class || 'Object'),
            h('span', { class: 'dim' }, ` #${o.id}`),
            h('div', { class: 'dim small' }, `${o.kind}${getView().lockedIds.has(o.id) ? ', locked' : ''}`),
        );
        tip.hidden = false;
        const r = $('lv-viewport').getBoundingClientRect();
        tip.style.left = `${Math.min(x + 14, r.right - 170)}px`;
        tip.style.top = `${Math.min(y + 16, r.bottom - 54)}px`;
    }

    function updateStatus(): void {
        if (!view) return;
        const v = view;
        sbSel.textContent = `${v.selection.size} selected, ${v.visibleCount}/${v.doc.size} shown`;
        const c = v.cameraGamePos().map((n) => Math.round(n / 100) / 10);
        sbCam.textContent = `cam ${c.join(', ')}k`;
        sbSnap.textContent = `${v.snap.grid ? `grid ${v.snap.gridSize}` : 'grid off'} | ${v.snap.angle ? `${v.snap.angleStep}°` : 'angle off'} | fly x${v.flySpeed.toFixed(2)}`;
        const st = v.stats();
        sbFps.textContent = `${v.fps} fps | ${st.calls} draws | ${Math.round(st.triangles / 1000)}k tris`;
        const d = $('ed-dirty');
        const isDirty = dirty();
        d.className = `dirty${isDirty ? ' on' : ''}`;
        d.textContent = !v.doc.size ? '' : isDirty ? 'Unsaved changes' : 'Saved';
        d.title = isDirty
            ? 'The document has changes that were not saved to a file (autosave keeps a copy in this browser)'
            : 'No unsaved changes';
        sbSave.textContent = lastAutosave ? `autosaved ${new Date(lastAutosave).toLocaleTimeString()}` : '';
        const hud = document.getElementById('lv-hud');
        if (hud) hud.textContent = '';
    }

    // ---- editing actions ------------------------------------------------------------------------------------------

    function copySelection(): boolean {
        const v = getView();
        clipboard = copyObjects(v.doc, [...v.selection]);
        if (!clipboard) return false;
        const text = JSON.stringify(clipboard);
        navigator.clipboard?.writeText(text).catch(() => undefined);
        toast(`Copied ${clipboard.objects.length} object(s)`);
        return true;
    }

    async function pasteFromClipboard(text: string | null): Promise<void> {
        const v = getView();
        if (!v.policy.structural) return;
        let clip = text ? parseClipboard(text) : null;
        if (!clip && text === null) {
            try {
                clip = parseClipboard((await navigator.clipboard?.readText()) ?? '');
            } catch {
                clip = null;
            }
        }
        clip ??= clipboard;
        if (!clip) {
            toast('Nothing to paste: copy objects first.');
            return;
        }
        const at = v.pointerGround();
        const r = pasteObjects(clip, v.doc, catalog, at ? snapPos(at) : null);
        if (!r.objects.length) {
            toast(
                `Cannot paste: ${[...new Set(r.skipped)].slice(0, 3).join(', ')} not available in this level.`,
                'error',
            );
            return;
        }
        const cmd = new AddCommand(r.objects, `Paste ${r.objects.length} object(s)`);
        v.run(cmd);
        v.select(cmd.ids);
        toast(
            r.skipped.length
                ? `Pasted ${r.objects.length}; skipped ${r.skipped.length} (class not in this level)`
                : `Pasted ${r.objects.length} object(s)`,
            r.skipped.length ? 'info' : 'ok',
        );
    }

    function snapSelectionToGround(): void {
        const v = getView();
        const m = snapToGround(v.doc, v.editable(), (x, z) => v.groundHeightAt(x, z));
        if (!m.size) {
            toast(
                v.hasGround
                    ? 'Nothing to move: the selection is already on the ground.'
                    : 'No terrain is loaded yet, so there is no ground to snap to.',
            );
            return;
        }
        const cmd = moveCommand(v.doc, new Map([...m].map(([id, t]) => [id, t.pos])), 'Snap to ground');
        if (cmd) v.run(cmd);
    }

    function alignSelection(axis: Axis, mode: AlignMode): void {
        const v = getView();
        const cmd = moveCommand(v.doc, alignPositions(v.doc, v.editable(), axis, mode), `Align ${'XYZ'[axis]} ${mode}`);
        if (cmd) v.run(cmd);
        else toast('Nothing to align.');
    }

    function distributeSelection(axis: Axis): void {
        const v = getView();
        const cmd = moveCommand(v.doc, distributePositions(v.doc, v.editable(), axis), `Distribute ${'XYZ'[axis]}`);
        if (cmd) v.run(cmd);
        else toast('Nothing to distribute (needs 3 or more objects).');
    }

    function saveDocument(): void {
        const v = getView();
        if (!v.doc.size) return;
        const json: LevelDocumentJson & { bookmarks?: unknown } = v.doc.toJSON();
        download(`${levelName}.level.json`, JSON.stringify(json, null, 1), 'application/json');
        savedTop = v.stack.top;
        restoredDirty = false;
        updateStatus();
        toast(`Saved ${levelName}.level.json`, 'ok');
    }

    // ---- shortcuts --------------------------------------------------------------------------------------------------

    const v0 = (): LevelView => getView();
    initShortcuts(
        {
            select: () => actions.tool('select'),
            move: () => actions.tool('translate'),
            rotate: () => actions.tool('rotate'),
            scale: () => actions.tool('scale'),
            space: () => actions.toggleSpace(),
            snap: () => actions.toggleGridSnap(),
            angleSnap: () => actions.toggleAngleSnap(),
            ground: () => snapSelectionToGround(),
            frame: () => v0().frameSelection(),
            frameAll: () => v0().frameAll(),
            topView: () => v0().topView(),
            bookmark: (_e, i) => {
                const v = v0();
                const b = { ...v.saveBookmark(`View ${(i ?? 0) + 1}`) };
                // Slot i: replace instead of append.
                v.bookmarks.pop();
                v.bookmarks[i ?? 0] = b;
                toast(`Saved view ${(i ?? 0) + 1}`, 'ok');
                scheduleAutosave();
            },
            gotoBookmark: (_e, i) => {
                if (!v0().gotoBookmark(i ?? 0))
                    toast(`No saved view ${(i ?? 0) + 1} (Ctrl+Shift+${(i ?? 0) + 1} saves one)`);
            },
            delete: () => v0().deleteSelected(),
            duplicate: () => v0().duplicateSelected(),
            undo: () => v0().undo(),
            redo: () => v0().redo(),
            selectAll: () => v0().selectVisible(),
            invert: () => v0().invertSelection(),
            sameClass: () => v0().selectSameClass(),
            hide: () => actions.hide(),
            showAll: () => actions.showAll(),
            lock: () => actions.lock(),
            deselect: () => {
                const v = v0();
                if (v.cancelPick()) {
                    toast('Pick cancelled');
                    return;
                }
                if (!document.getElementById('ed-bottom')?.contains(document.activeElement)) toolbar.closeMenus();
                v.select([]);
            },
            save: () => saveDocument(),
            export: () => actions.exportMod(),
            help: () => help.toggle(),
            theme: () => actions.theme(),
        },
        () => !root.hidden && !(view?.isFlying ?? false),
    );
    // Browser clipboard events: copy / cut / paste of objects (text/plain JSON, works across tabs and levels).
    const editorActive = (e: Event): boolean => {
        const t = e.target as HTMLElement | null;
        return !root.hidden && !(t && /^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName));
    };
    document.addEventListener('copy', (e) => {
        if (!editorActive(e) || !view?.selection.size) return;
        if (copySelection()) {
            e.clipboardData?.setData('text/plain', JSON.stringify(clipboard));
            e.preventDefault();
        }
    });
    document.addEventListener('cut', (e) => {
        if (!editorActive(e) || !view?.selection.size) return;
        if (copySelection()) {
            e.clipboardData?.setData('text/plain', JSON.stringify(clipboard));
            e.preventDefault();
            view.deleteSelected();
        }
    });
    document.addEventListener('paste', (e) => {
        if (!editorActive(e)) return;
        e.preventDefault();
        void pasteFromClipboard(e.clipboardData?.getData('text/plain') ?? '').catch(fail);
    });
    window.addEventListener('keydown', (e) => {
        if (root.hidden) return;
        // Ctrl+Shift+N style helpers are not needed; just keep the page from trapping Backspace navigation.
        if (e.key === 'Backspace' && !(e.target as HTMLElement | null)?.closest?.('input,textarea')) e.preventDefault();
    });

    // ---- layout ---------------------------------------------------------------------------------------------------

    layout = initLayout(root, () => window.dispatchEvent(new Event('resize')));

    // ---- disc / level -------------------------------------------------------------------------------------------

    function target(): ModMeta['target'] | null {
        const p = disc?.profile;
        if (!p || (!p.profile && !p.hash)) return null;
        return { ...(p.profile ? { profile: p.profile } : {}), ...(p.hash ? { hash: p.hash } : {}) };
    }

    function setDisc(d: DiscContext | null): void {
        disc = d;
        syncToolbar();
        modPanel.onLoad?.();
    }

    async function openLevel(dirPath: string): Promise<void> {
        const info = disc?.levels.find((l) => l.dirPath === dirPath);
        host.setStatus(`Loading ${info?.name ?? dirPath}...`);
        toast(`Loading ${info?.name ?? dirPath}...`);
        const r = await host.call('loadLevel', dirPath);
        await openLoaded(r, info?.name ?? r.file);
        const resources = new Set<string>();
        for (const o of getView().doc.objects()) resources.add(o.resource);
        visuals = loader.loadLevel(dirPath, resources).catch(fail);
    }

    async function openLoaded(r: LevelLoadResult, displayName: string): Promise<void> {
        loader.clear();
        getView().setGround({});
        const { doc, unresolved } = documentFromLvl(r.lvl, r.odbs, displayName);
        src = r;
        levelName = r.file.replace(/\.lvl$/i, '');
        catalog = buildCatalog(r.lvl.header, r.odbs, doc.objects());
        schemas = new SchemaProvider(r.lvl.header, r.odbs, r.odbDeps);
        const v = getView();
        let used = doc;
        let restoreMsg = '';
        // Autosaved edits for this level?
        const rec = await loadAutosave(autosaveKey(displayName));
        v.setDocument(used);
        savedTop = v.stack.top;
        restoredDirty = false;
        if (rec) {
            const age = new Date(rec.savedAt).toLocaleString();
            const banner = h(
                'div',
                { class: 'restore' },
                h('span', {}, `Autosaved edits from ${age} found for this level.`),
                h(
                    'button',
                    {
                        class: 'btn primary sm',
                        type: 'button',
                        onclick: () => {
                            restore(rec);
                            banner.remove();
                        },
                    },
                    'Restore',
                ),
                h(
                    'button',
                    {
                        class: 'btn sm',
                        type: 'button',
                        onclick: () => {
                            void clearAutosave(autosaveKey(displayName));
                            banner.remove();
                        },
                    },
                    'Discard',
                ),
                h(
                    'button',
                    { class: 'tool sm', type: 'button', 'aria-label': 'Dismiss', onclick: () => banner.remove() },
                    icon('x', 12),
                ),
            );
            $('lv-viewport').append(banner);
            restoreMsg = ' Autosaved edits are available to restore.';
        }
        used = doc;
        modPanel.suggest(`editor.${levelName.toLowerCase()}-edit`, `${displayName} edit`);
        restoreSettings(displayName);
        for (const p of panels) p.onLoad?.();
        updateOverlay();
        empty.hidden = true;
        syncToolbar();
        updateStatus();
        host.setStatus(
            `${displayName}: ${used.size} objects from ${r.file}, ${catalog.length} classes` +
                (unresolved ? `, ${unresolved} with unresolved class names` : '') +
                (r.missingOdbs.length ? `; missing ODBs: ${r.missingOdbs.join(', ')}` : '') +
                `.${restoreMsg}`,
        );
        statusMsg.textContent = `${displayName}: ${used.size} objects, ${catalog.length} classes.${restoreMsg}`;
    }

    function restore(rec: NonNullable<Awaited<ReturnType<typeof loadAutosave>>>): void {
        const v = getView();
        const doc = LevelDocument.fromJSON(rec.doc);
        if (src) {
            const b = src.lvl.header;
            if (!doc.bounds && b.maxWidth > 0) doc.bounds = v.doc.bounds;
        }
        v.setDocument(doc);
        v.bookmarks = rec.bookmarks ?? [];
        modPanel.restore(rec.mod ?? {});
        restoredDirty = true;
        for (const p of panels) p.onLoad?.();
        updateOverlay();
        syncToolbar();
        updateStatus();
        toast('Restored autosaved edits', 'ok');
    }

    function restoreSettings(level: string): void {
        const v = getView();
        v.bookmarks = store.json(`bookmarks:${level}`, [] as typeof v.bookmarks);
    }

    // ---- autosave ---------------------------------------------------------------------------------------------------

    function scheduleAutosave(): void {
        window.clearTimeout(autosaveTimer);
        autosaveTimer = window.setTimeout(() => void doAutosave(), AUTOSAVE_DELAY);
    }

    async function doAutosave(): Promise<void> {
        const v = view;
        if (!v || !v.doc.size) return;
        const key = autosaveKey(v.doc.level || levelName);
        store.set(`bookmarks:${v.doc.level || levelName}`, JSON.stringify(v.bookmarks));
        if (v.stack.depth === 0 && !restoredDirty) {
            await clearAutosave(key);
            return;
        }
        const ok = await saveAutosave({
            key,
            level: v.doc.level,
            savedAt: Date.now(),
            doc: v.doc.toJSON(),
            bookmarks: v.bookmarks,
            mod: modPanel.values(),
        });
        if (ok) {
            lastAutosave = Date.now();
            updateStatus();
        }
    }

    // ---- world dump overlay ---------------------------------------------------------------------------------------

    /** Pair the disc level with a loaded world dump and draw the unmatched dump entities as outlines. */
    function updateOverlay(): void {
        const v = getView();
        if (!src || !worldDoc) {
            comparison = null;
            v.setGhost([]);
            return;
        }
        comparison = compareWithWorld(v.doc, worldDoc);
        const matched = new Set(comparison.pairs.values());
        v.setGhost(ghosts ? [...worldDoc.objects()].filter((o) => !matched.has(o.id)) : []);
    }

    async function loadFile(f: File): Promise<void> {
        const text = await f.text();
        const j = JSON.parse(text) as { format?: string };
        const v = getView();
        if (j.format === 'drakan-level-document') {
            const doc = LevelDocument.fromJSON(text);
            src = null;
            catalog = [];
            levelName = doc.level || f.name.replace(/\.json$/i, '');
            v.setDocument(doc);
            savedTop = v.stack.top;
            restoredDirty = false;
            for (const p of panels) p.onLoad?.();
            updateOverlay();
            syncToolbar();
            host.setStatus(
                `${f.name}: ${doc.size} objects in "${doc.level}" (saved document, no LVL source: export disabled).`,
            );
            return;
        }
        const world = LevelDocument.fromWorld(parseWorldDump(text));
        worldDoc = world;
        if (src) {
            updateOverlay();
            const c = comparison!;
            host.setStatus(
                `${f.name}: ${world.size} live entities overlaid on the disc level: ${c.matched} matched, ${c.lvlOnly} level objects not live, ${c.worldOnly} entities not in the list.`,
            );
        } else {
            levelName = world.level || f.name.replace(/\.json$/i, '');
            v.setDocument(world);
            savedTop = v.stack.top;
            for (const p of panels) p.onLoad?.();
            host.setStatus(`${f.name}: ${world.size} objects in "${world.level}".`);
        }
        syncToolbar();
    }

    // ---- boot -----------------------------------------------------------------------------------------------------

    // The theme button icon reflects the current theme.
    $('ed-theme').replaceChildren(icon(currentTheme() === 'dark' ? 'sun' : 'moon'));
    syncToolbar();
    void KINDS;
    void CompositeCommand;
    void diffDocument;
    void changeCount;
    return {
        view: getView,
        loadFile,
        setDisc,
        openLevel,
        openLoaded,
        assets: () => assets,
        ctx: () => ctx,
        visualsReady: () => visuals,
        /** Class name and model render for a resource string (used by Save mode's inventory); null without a level. */
        classInfo(resource: string): { name: string; thumb: string | null } | null {
            const cls = catalog.find((c) => c.resource === resource);
            if (!cls) return null;
            loader.wantThumbnail(cls);
            return { name: cls.name, thumb: loader.thumbnail(cls.resource) };
        },
    };
}

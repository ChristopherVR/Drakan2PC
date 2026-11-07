import { createAnimSource } from './editor/animsource.ts';
import {
    AddCommand,
    CompositeCommand,
    LevelDocument,
    SchemaProvider,
    alignPositions,
    buildCatalog,
    categoryIndex,
    copyObjects,
    countByCategory,
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
    type ObjectCategory,
    type Axis,
    type LevelObject,
    type ModMeta,
    type WorldComparison,
    compareWithWorld,
    type LevelDocumentJson,
} from '@drakan/editor-core';
import type { DiscProfile } from '@drakan/formats';
import { KINDS, LevelView, type ToolMode } from './level.ts';
import { PLAY_PACK_ID, changeCount, setConvertedPackProvider, setDesktopOnlyNotice } from './modio.ts';
import type { Api, LevelInfo, LevelLoadResult, Method } from './rpc.ts';
import { createSceneLoader } from './sceneload.ts';
import { HELPER_GROUPS, helperGroupOf, type HelperGroup } from './scene/instances.ts';
import { parseViewOptions, type ViewOptions } from './scene/viewopts.ts';
import { createAssets, type AssetsPanel } from './editor/assets.ts';
import { autosaveKey, clearAutosave, editsAtRisk, loadAutosave, saveAutosave } from './editor/autosave.ts';
import type { Ctx } from './editor/ctx.ts';
import { fill, h, icon, store } from './editor/dom.ts';
import { createRefs } from './refs.ts';
import { createInspector } from './editor/inspector.ts';
import { initLayout, type Layout } from './editor/layout.ts';
import { createPlacePanel } from './editor/placepanel.ts';
import { installLandscape } from './editor/landscape/landscape.ts';
import { tourBridge } from './editor/tour/index.ts';
import { createOutliner } from './editor/outliner.ts';
import { buildObjectMenu } from './editor/objectmenu.ts';
import { installLinking } from './editor/linking.ts';
import { projects } from './editor/project/store.ts';
import { rememberLoadedLevel } from './editor/project/export.ts';
import { createDock, createHistory, createModPanel, createValidation } from './editor/panels.ts';
import { helpOverlay, initShortcuts } from './editor/shortcuts.ts';
import { VIEW_COLORS, applyTheme, currentTheme, onThemeChange, toggleTheme } from './editor/theme.ts';
import { createToolbar, type ToolbarActions } from './editor/toolbar.ts';
import { createOutputLog } from './editor/output.ts';
import { createNavGizmo } from './editor/navgizmo.ts';
import { contextMenu, emptyState, installTooltips } from './editor/ui.ts';
import { createPalette } from './editor/palette.ts';
import { bindPanelHost, registerCommand, registerPanel, runCommand } from './editor/registry.ts';
import { createLoadProgress } from './editor/loadprogress.ts';
import { LoadCancelled, isCancelled } from './editor/loadsteps.ts';
import { createWorkspace, type Workspace } from './editor/workspace.ts';
import { installLogic } from './editor/logicpanel.ts';
import { registerShellCommands } from './editor/shellcommands.ts';
import { exportContentPackage, type ContentExportHost } from './editor/contentexport.ts';
import { showPlayNeedsDesktop } from './editor/needsdesktop.ts';
import { registerOotfImport } from './editor/ootfimport.ts';
import { createStartPage, rememberLevel } from './editor/startpage.ts';
import { createViewportHint } from './editor/hint.ts';
import { downloadFile, isDesktop } from './platform.ts';
import { friendlyMessage } from './shared/friendly.ts';
import { registerUnrealExport } from './editor/unrealexport.ts';
import './editor/editor.css';

setDesktopOnlyNotice(showPlayNeedsDesktop);

const $ = <T extends HTMLElement>(id: string): T => {
    const el = document.getElementById(id);
    if (!el) throw new Error(`missing #${id}`);
    return el as T;
};

export interface LevelUiHost {
    call<M extends Method>(method: M, ...args: Parameters<Api[M]>): Promise<ReturnType<Api[M]>>;
    setStatus(msg: string, error?: boolean): void;
}

/** Browser download, or the desktop app's native save dialog (see platform.ts). */
const download = downloadFile;

export interface DiscContext {
    isoName: string;
    levels: LevelInfo[];
    profile: DiscProfile | null;
}

export interface LevelUi {
    view: () => LevelView;
    loadFile(f: File): Promise<void>;
    setDisc(d: DiscContext | null): void;
    /** Open a disc level; `asTitle` opens it under another title (a project's level copy keeps its own edits). */
    openLevel(dirPath: string, asTitle?: string): Promise<void>;
    /** Open an already loaded level (the worker result); also used by tests with synthetic data. */
    openLoaded(r: LevelLoadResult, displayName: string): Promise<void>;
    /** The Asset Browser panel (thumbnail provider hook for the model renderer). */
    assets: () => AssetsPanel;
    ctx: () => Ctx;
    /** Resolves when terrain and models of the last `openLevel` finished loading (tests, harnesses). */
    visualsReady: () => Promise<void>;
    /** Class name and model render for a resource string (Save mode inventory); null when no such class is loaded. */
    classInfo(resource: string): { name: string; thumb: string | null } | null;
    /** The disc's levels (empty without a disc). */
    levels(): LevelInfo[];
    /** The mod target of the open disc, or null when it is unknown. */
    modTarget(): ModMeta['target'] | null;
    /** Write the open level's edits to the autosave now (the project folder is saved from it). */
    flushAutosave(): Promise<void>;
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
    const palette = createPalette();
    const requestOpenIso = (): void => document.getElementById('iso-input')?.click();
    const startPage = createStartPage({
        openIso: requestOpenIso,
        openDocument: (f) => void loadFile(f).catch(fail),
        openLevel: (p) => {
            const r = openLevel(p);
            r.catch(fail);
            return r;
        },
        levelOpen: () => !!src,
        openLevelName: () => (src ? shownName : null),
        close: () => {
            if (src) empty.hidden = true;
        },
        openPalette: () => palette.open(),
        openHelp: () => help.toggle(true),
    });
    empty.classList.add('has-start');
    empty.append(startPage.el);
    let view: LevelView | null = null;
    let levelName = 'level';
    /** Display name of the open document ("Level 1A - Surdana"). */
    let shownName = '';
    /** Disc directory of the open disc level (for the Unreal export); empty for documents without one. */
    let currentDirPath = '';
    let disc: DiscContext | null = null;
    // Play on a converted Order of the Flame level installs the converted pack first (modio.ts).
    setConvertedPackProvider(async (level) => {
        const info = disc?.levels.find((l) => l.virtual && l.name === level);
        const zip = info ? await host.call('convertedPackZip', PLAY_PACK_ID) : null;
        return info && zip ? { zip, language: info.dirPath.split(/[\\/]/)[0] ?? '' } : null;
    });
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
    /** Edits exist that no autosave holds yet (the timer is running, or the last write failed): closing now would lose them. */
    let autosaveAtRisk = false;
    let lastAutosave = 0;
    /** Display categories switched off in the Show menu (`ObjectCategory` ids). */
    const hiddenCategories = new Set<string>();
    /** Category of an object for the open catalog (memoised per catalog). */
    let catFor: AssetClass[] | null = null;
    let catFn = categoryIndex([]);
    const objectCategory = (o: LevelObject): ObjectCategory => {
        if (catFor !== catalog) {
            catFor = catalog;
            catFn = categoryIndex(catalog);
        }
        return catFn(o);
    };
    let catCountKey = '';
    let catCountCache: { id: string; label: string; hint: string; color: number; count: number }[] = [];
    function categoryCounts(): typeof catCountCache {
        const v = view;
        const key = `${v?.doc.size ?? 0}:${src?.file ?? ''}:${catalog.length}:${v?.helperRevision ?? 0}`;
        if (key === catCountKey) return catCountCache;
        catCountKey = key;
        catCountCache = v
            ? countByCategory(v.doc.objects(), objectCategory).map(({ info, count }) => ({ ...info, count }))
            : [];
        return catCountCache;
    }
    let gridSnapOn = true;
    let viewOpts: ViewOptions = parseViewOptions(store.json('view-options', null));
    let visuals: Promise<void> = Promise.resolve();
    /** Bumped by every disc level open; an older open that finishes late is dropped. */
    let openToken = 0;
    /** A level is being read: the start page stays away (an empty document would show it again). */
    let opening = false;

    // ---- shell ----------------------------------------------------------------------------------------------------

    applyTheme(currentTheme());
    installTooltips();
    const tip = h('div', { class: 'tip', hidden: true });
    const toasts = h('div', { class: 'toasts', 'aria-live': 'polite' });
    document.body.append(tip, toasts);
    const help = helpOverlay();
    document.body.append(help.el);

    const statusMsg = h('span', { class: 'sb-msg', role: 'status', 'aria-live': 'polite' }, 'Ready');
    const sbSel = h('span', { class: 'sb-item', id: 'sb-sel' });
    const sbCam = h('span', { class: 'sb-item mono', id: 'sb-cam' });
    const sbSnap = h('span', { class: 'sb-item', id: 'sb-snap' });
    const sbSave = h('span', { class: 'sb-item', id: 'sb-save' });
    const sbFps = h('span', { class: 'sb-item mono dim', id: 'sb-fps' });
    const sbDrawer = (id: string, ico: string, label: string, onclick: () => void): HTMLButtonElement =>
        h('button', { class: 'sb-btn', type: 'button', id, onclick }, icon(ico, 13), label);
    const sbLogBadge = h('span', { class: 'ui-badge', hidden: true });
    const sbBrowser = sbDrawer('sb-browser', 'folder-open', 'Content Browser', () => toggleBottom('assets'));
    const sbOutput = sbDrawer('sb-output', 'terminal', 'Output Log', () => toggleBottom('output'));
    sbOutput.append(sbLogBadge);
    fill(
        $('ed-status'),
        sbBrowser,
        sbOutput,
        h('span', { class: 'sb-sep' }),
        statusMsg,
        h('span', { class: 'spacer' }),
        sbSel,
        sbSnap,
        sbCam,
        sbSave,
        sbFps,
    );
    const output = createOutputLog((warn, error) => {
        sbLogBadge.hidden = !(warn + error);
        sbLogBadge.textContent = String(warn + error);
        sbLogBadge.className = `ui-badge ${error ? 'bad' : 'warn'}`;
        dockRef?.setBadge('output', warn + error ? String(warn + error) : '', error ? 'bad' : 'warn');
    });
    /** Drawer behaviour for the status bar buttons: a second click on the active tab folds the bottom dock. */
    function toggleBottom(tab: string): void {
        if (ws && !ws.levelActive()) {
            ws.showArea('level');
            layout?.openBottom();
            dockRef?.show(tab);
        } else if (layout?.isOpen('bottom') && dockRef?.active() === tab) layout.toggle('bottom', false);
        else {
            layout?.openBottom();
            dockRef?.show(tab);
        }
    }

    function toast(raw: string, kind: 'info' | 'error' | 'ok' | 'warn' = 'info'): void {
        const msg = kind === 'error' ? friendlyMessage(raw) : raw;
        // an error is announced at once (role alert), everything else politely
        const t = h('div', { class: `toast ${kind}`, role: kind === 'error' ? 'alert' : 'status' }, msg);
        toasts.append(t);
        window.setTimeout(() => t.classList.add('out'), kind === 'error' || kind === 'warn' ? 6000 : 2800);
        window.setTimeout(() => t.remove(), kind === 'error' || kind === 'warn' ? 6400 : 3200);
        statusMsg.textContent = msg;
        statusMsg.classList.toggle('error', kind === 'error');
        output.log(msg, kind === 'info' ? 'info' : kind);
    }
    window.addEventListener('drakan:toast', (e) => {
        const d = (e as CustomEvent<{ msg: string; kind: 'info' | 'error' | 'ok' | 'warn' }>).detail;
        toast(d.msg, d.kind);
    });
    const fail = (e: unknown): void => {
        // Cancelled on purpose: the loading card already went away.
        if (isCancelled(e)) return;
        const msg = e instanceof Error ? e.message : String(e);
        host.setStatus(msg, true);
        toast(msg, 'error');
    };

    // ---- context for the panels -------------------------------------------------------------------------------------

    const refs = createRefs((method, ...args) => host.call(method, ...args));
    const ctx: Ctx = {
        view: () => getView(),
        refs: () => refs,
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
                toast(`Added ${cls.name} #${cmd.ids[0]} with default settings.`, 'warn');
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
            return p ? `Disc: ${p.serial}` : 'Open a disc image first.';
        },
        setStatus: host.setStatus,
        exportMod: () => projects.handlers.export?.(),
        exportContent: () => void exportContentPackage(contentExportHost),
    });
    const contentExportHost: ContentExportHost = {
        call: (method, ...args) => host.call(method, ...args),
        doc: () => getView().doc,
        schemas: () => schemas,
        catalog: () => catalog,
        src: () => src,
        profile: () => disc?.profile ?? null,
        toast,
        setStatus: host.setStatus,
    };
    const dock = createDock([
        { id: 'assets', title: 'Content Browser', icon: 'folder-open', panel: assets },
        { id: 'output', title: 'Output Log', icon: 'terminal', panel: output },
        { id: 'validation', title: 'Validation', icon: 'warn', panel: validation },
    ]);
    dockRef = dock;
    $('ed-outliner').append(outliner.el);
    $('ed-details').append(inspector.el);
    $('ed-bottom').append(dock.el);
    // Place sidebar on the left (editor/placepanel.ts).
    const place = createPlacePanel(ctx, {
        hide: () => layout?.toggle('left', false),
        toggle: () => layout?.toggle('left'),
    });
    $('ed-left').append(place.el);
    // Landscape mode: its tools panel takes the Place panel's spot while it is on (editor/landscape/landscape.ts).
    const landscape = installLandscape(ctx, {
        leftEl: $('ed-left'),
        placeEl: place.el,
        showLeft: () => layout?.toggle('left', true),
        changed: () => syncToolbar(),
    });
    const panels = [outliner, inspector, assets, history, validation, place, tourBridge(ctx), landscape.panel];
    // Where every editor opens (its own tab, the bottom area, or World Settings beside Details): editor/workspace.ts.
    const ws: Workspace = createWorkspace({
        frame: root,
        toolbar: $('ed-toolbar'),
        body: $('ed-body'),
        doc: $('ed-doc'),
        details: $('ed-details'),
        inspector: inspector.el,
        dock,
        layout: () => layout,
        panels,
        levelShown: () => window.dispatchEvent(new Event('resize')),
    });
    bindPanelHost({ add: (spec) => ws.add(spec), show: (id) => ws.show(id) });
    registerPanel({
        id: 'mod',
        title: 'Mod',
        icon: 'download',
        panel: modPanel,
        command: {
            keywords: 'mod changes pack export play package status target disc engine neutral',
            hint: 'What the open level changes, where the mod was written and how to play it',
        },
    });
    installLogic(ctx, (only) => host.call('exitIndex', ...(only ? [only] : [])));

    // ---- real visuals: terrain, models, thumbnails ------------------------------------------------------------------
    const loader = createSceneLoader(host.call, () => getView(), {
        status: (m) => host.setStatus(m),
        toast: (m, k) => toast(m, k),
        thumbnail: (res, url) => {
            assets.applyThumbnail(res, url);
        },
        modelsChanged: () => {
            syncToolbar();
            updateStatus();
        },
    });
    assets.setThumbnailProvider((cls) => loader.thumbnail(cls.resource));
    const loadCard = createLoadProgress($('lv-viewport'));

    // ---- the start page, again ---------------------------------------------------------------------------------------
    registerCommand({
        id: 'view.startPage',
        title: 'Start page',
        category: 'File',
        icon: 'home',
        keywords: 'home templates new what do you want to make begin welcome',
        hint: 'Templates, your disc, the mod project and recent levels. The open level stays open.',
        run: () => {
            runCommand('mode.level');
            startPage.show();
            empty.hidden = false;
            empty.scrollTo({ top: 0 });
            empty.querySelector<HTMLElement>('.sp-tile.on, .sp-go')?.focus();
        },
    });
    assets.setThumbnailLoader((cls) => loader.wantThumbnail(cls));

    // ---- toolbar --------------------------------------------------------------------------------------------------

    let layout: Layout | null = null;
    const actions: ToolbarActions = {
        editorMode: (m) => landscape.setActive(m === 'landscape'),
        tool: (m: ToolMode) => {
            const v = getView();
            if (m === 'scale' && v.selection.size && ![...v.selection].some((id) => v.doc.get(id)?.scale)) {
                toast('The selection cannot be scaled.');
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
        play: () => {
            // Like Play in Unreal: with a level open the game starts on it with the player where the camera looks
            // (editor/playtestui.ts, F5). Without a level: the Mod panel's Play (the whole mod, from New Game).
            if (isDesktop && src && runCommand('play.camera')) return;
            if (isDesktop) {
                ws.show('mod');
                void modPanel.play().catch(fail);
                return;
            }
            showPlayNeedsDesktop();
        },
        exportMod: () => {
            projects.handlers.export?.();
        },
        exportContent: () => void exportContentPackage(contentExportHost),
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
        toggleCategory: (c, shown) => {
            if (shown) hiddenCategories.delete(c);
            else hiddenCategories.add(c);
            getView().setFilters({ categories: new Set(hiddenCategories) });
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
    const toolbar = createToolbar($('ed-toolbar'), actions, () => toolbarState(), {
        left: $('vp-bar-left'),
        right: $('vp-bar-right'),
    });
    const navGizmo = createNavGizmo($('vp-gizmo'), () => getView().topView());

    function toolbarState(): Parameters<typeof toolbar.sync>[0] {
        const v = view;
        return {
            mode: v?.mode ?? 'translate',
            editorMode: landscape.active() ? 'landscape' : 'objects',
            space: v?.space ?? 'world',
            gridSnap: v?.snap.grid ?? true,
            angleSnap: v?.snap.angle ?? true,
            gridSize: v?.snap.gridSize ?? 256,
            angleStep: v?.snap.angleStep ?? 15,
            links: v?.showLinks ?? 'selection',
            gridVisible: v?.gridVisible ?? true,
            flySpeed: v?.flySpeed ?? 1,
            hiddenCategories,
            categoryCounts: categoryCounts(),
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
        const key = `${v?.doc.size ?? 0}:${src?.file ?? ''}:${v?.helperRevision ?? 0}`;
        if (key === helperKey) return helperCache;
        helperKey = key;
        helperCache = v
            ? v.helperCounts()
            : (Object.fromEntries(HELPER_GROUPS.map((g) => [g.id, 0])) as Record<HelperGroup, number>);
        return helperCache;
    }

    // ---- viewport -------------------------------------------------------------------------------------------------

    const getView = (): LevelView => {
        if (!view) {
            view = new LevelView($('lv-viewport'), {
                onSelection: () => onSelection(),
                onChange: () => onChange(),
                onNotice: (m) => toast(m, 'warn'),
                onHover: (id, x, y) => onHover(id, x, y),
                onContextMenu: (id, x, y, ground) => {
                    const items = buildObjectMenu(ctx, id === null ? [] : [id], { ground });
                    if (items.length) contextMenu(x, y, items);
                },
                onVisibility: () => {
                    for (const p of panels) p.onVisibility?.();
                    syncToolbar();
                    updateStatus();
                },
                onCamera: () => {
                    updateStatus();
                    if (view) navGizmo.update(view.camera.quaternion);
                },
                onDropAsset: (key, pos) => {
                    const cls = catalog.find((c) => c.resource === key);
                    if (cls) ctx.placeClass(cls, pos);
                },
            });
            view.setEditorControls(true);
            view.setFilters({ categoryOf: objectCategory });
            view.models.anim.setSource(createAnimSource(refs, () => schemas));
            view.setViewOptions(viewOpts);
            view.setSnap({});
            view.setDocument(new LevelDocument());
            view.setActive(true);
            installLinking(ctx);
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
        if (!opening) empty.hidden = v.doc.size > 0;
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
        // A project folder is saved in place (project file + this level's edits); no dialog.
        if (projects.folder && projects.handlers.save) {
            projects.handlers.save();
            savedTop = v.stack.top;
            restoredDirty = false;
            updateStatus();
            return;
        }
        if (!v.doc.size) return;
        const json: LevelDocumentJson & { bookmarks?: unknown } = v.doc.toJSON();
        const name = `${levelName}.level.json`;
        const top = v.stack.top;
        const done = (): void => {
            savedTop = top;
            restoredDirty = false;
            updateStatus();
            toast(`Saved ${name}`, 'ok');
        };
        if (isDesktop) {
            // A cancelled save dialog leaves the document marked as unsaved.
            void download(name, JSON.stringify(json, null, 1), 'application/json').then((p) => p && done(), fail);
        } else {
            void download(name, JSON.stringify(json, null, 1), 'application/json');
            done();
        }
    }

    // ---- shortcuts --------------------------------------------------------------------------------------------------

    const v0 = (): LevelView => getView();
    initShortcuts(
        {
            palette: () => palette.open(),
            commands: () => palette.open('>'),
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
            projectSettings: () => runCommand('panel.project'),
            contentBrowser: () => toggleBottom('assets'),
            story: () => runCommand('panel.story'),
            logic: () => runCommand('panel.logic'),
            history: () => ws.toggleWindow('history', 'Undo History', 'history', history),
            landscape: () => landscape.toggle(),
        },
        () => !root.hidden && !(view?.isFlying ?? false),
        // In an editor tab only the global keys (palette, save, export, help, Window) work: the viewport is not shown.
        () => !ws.levelActive(),
    );
    registerOotfImport({
        call: (method, ...args) => host.call(method, ...args),
        levels: () => (disc?.levels ?? []).filter((l) => l.present),
        toast,
        profile: () => disc?.profile?.profile ?? null,
        addLevels: async (levels, files) => {
            const all = await host.call('addVirtualLevels', levels, files);
            if (disc) setDisc({ ...disc, levels: all });
            // The project panel checks its levels against this list.
            window.dispatchEvent(new CustomEvent('drakan:levels-changed'));
        },
        openLevel: (dirPath) => openLevel(dirPath),
        clearLevels: async () => {
            const all = await host.call('clearVirtualLevels');
            if (disc) setDisc({ ...disc, levels: all });
            window.dispatchEvent(new CustomEvent('drakan:levels-changed'));
        },
    });
    registerUnrealExport({
        collect: () =>
            src && view && currentDirPath
                ? {
                      call: (method, ...args) => host.call(method, ...args),
                      refs,
                      schemas,
                      catalog,
                      objects: [...view.doc.objects()],
                      dirPath: currentDirPath,
                      level: levelName,
                      status: (m) => host.setStatus(m),
                  }
                : null,
        toast,
    });
    registerShellCommands({
        actions,
        ctx,
        view: getView,
        levels: () => disc?.levels ?? [],
        openIso: requestOpenIso,
        openPanel: (tab) => {
            ws.showArea('level');
            layout?.openBottom();
            dock.show(tab);
        },
        toggleHistory: () => ws.toggleWindow('history', 'Undo History', 'history', history),
        hasLevel: () => (view?.doc.size ?? 0) > 0,
        selCount: () => view?.selection.size ?? 0,
        canUndo: () => view?.stack.canUndo ?? false,
        canRedo: () => view?.stack.canRedo ?? false,
        canExport: () => (src !== null && (view?.doc.size ?? 0) > 0) || projects.get() !== null,
        catalog: () => catalog,
        clearSelection: () => getView().select([]),
        openPalette: (q) => palette.open(q),
    });
    createViewportHint($('lv-viewport'), {
        shown: () => (view?.doc.size ?? 0) > 0 && (view?.selection.size ?? 0) === 0,
        openPalette: () => palette.open(),
    });

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
        if (d !== disc) refs.reset();
        disc = d;
        startPage.refresh(d && d.levels.length ? d.levels : null, d?.isoName);
        syncToolbar();
        modPanel.onLoad?.();
    }

    /**
     * Open a disc level. The viewport shows it at once: the start page goes, the loading card names the level and its
     * steps, the objects appear as soon as the level file is read, then the terrain and the models fill in.
     */
    async function openLevel(dirPath: string, asTitle?: string): Promise<void> {
        const info = disc?.levels.find((l) => l.dirPath === dirPath);
        const shown = asTitle ?? info?.name ?? dirPath;
        const tk = ++openToken;
        const hadLevel = !!src;
        const startWasShown = !empty.hidden;
        let cancel: (e: LoadCancelled) => void = () => undefined;
        const cancelled = new Promise<never>((_, rej) => (cancel = rej));
        cancelled.catch(() => undefined);
        // A level that was still filling in stops; this one takes the viewport.
        loader.stop();
        opening = true;
        empty.hidden = true;
        loadCard.begin(shown, {
            covering: !hadLevel,
            converted: !!info?.virtual,
            onCancel: () => {
                openToken++;
                opening = false;
                loadCard.hide();
                if (startWasShown || !hadLevel) empty.hidden = false;
                host.setStatus(`Opening ${shown} was cancelled.`);
                cancel(new LoadCancelled());
            },
            onStop: () => {
                loader.stop();
                loadCard.step('models', 'stopped');
                loadCard.finish();
                host.setStatus(`Stopped loading models of ${shown}; the rest show as markers.`);
            },
        });
        host.setStatus(`Loading ${shown}...`);
        let r: LevelLoadResult;
        try {
            r = await Promise.race([host.call('loadLevel', dirPath), cancelled]);
        } catch (e) {
            if (tk === openToken && !isCancelled(e)) {
                opening = false;
                loadCard.step('read', 'failed');
                loadCard.set({ error: `${shown} could not be read.` });
                if (startWasShown || !hadLevel) empty.hidden = false;
                window.setTimeout(() => loadCard.hide(), 2500);
            }
            throw e;
        }
        if (tk !== openToken) throw new LoadCancelled();
        loadCard.step('read', 'done');
        loadCard.step('objects', 'active');
        // The export compares edits against this read: keeping it spares a second parse in the busy worker.
        rememberLoadedLevel(dirPath, r);
        try {
            await openLoaded(r, asTitle ?? info?.name ?? r.file);
        } finally {
            opening = false;
        }
        currentDirPath = dirPath;
        if (info && !asTitle) rememberLevel({ name: info.name, dirPath });
        const resources = new Set<string>();
        for (const o of getView().doc.objects()) resources.add(o.resource);
        loadCard.set({ objects: getView().doc.size });
        loadCard.step('objects', 'done');
        visuals = loader
            .loadLevel(dirPath, resources, (p) => {
                if (tk !== openToken) return;
                if (p.step === 'terrain') {
                    if (p.layers !== undefined) loadCard.set({ layers: p.layers });
                    loadCard.step('terrain', p.state);
                } else {
                    loadCard.set({ models: { done: p.done, total: p.total, missing: p.missing ?? 0 } });
                    loadCard.step('models', p.state);
                }
            })
            .then(() => {
                if (tk === openToken && loadCard.state()?.steps.models !== 'stopped') loadCard.finish();
            })
            .catch(fail);
    }

    async function openLoaded(r: LevelLoadResult, displayName: string): Promise<void> {
        loader.clear();
        getView().setGround({});
        const { doc, unresolved } = documentFromLvl(r.lvl, r.odbs, displayName);
        src = r;
        shownName = displayName;
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
        const auto = !!rec && projects.autoRestore.delete(displayName);
        if (rec && auto) {
            restore(rec);
            restoreMsg = ' Edits from the project were restored.';
        } else if (rec) {
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
        // only real edits are at risk: opening a level, moving the camera or a selection change schedule a write too
        autosaveAtRisk = !!view && editsAtRisk(view.doc.size, view.stack.depth, restoredDirty);
        autosaveTimer = window.setTimeout(() => void doAutosave(), AUTOSAVE_DELAY);
    }

    async function doAutosave(): Promise<void> {
        const v = view;
        if (!v || !v.doc.size) {
            autosaveAtRisk = false; // nothing open, nothing to lose
            return;
        }
        const key = autosaveKey(v.doc.level || levelName);
        store.set(`bookmarks:${v.doc.level || levelName}`, JSON.stringify(v.bookmarks));
        if (v.stack.depth === 0 && !restoredDirty) {
            autosaveAtRisk = false;
            await clearAutosave(key);
            projects.edited();
            return;
        }
        const ok = await saveAutosave({
            key,
            level: v.doc.level,
            savedAt: Date.now(),
            doc: v.doc.toJSON(),
            bookmarks: v.bookmarks,
        });
        if (ok) {
            autosaveAtRisk = false;
            lastAutosave = Date.now();
            updateStatus();
            projects.edited();
        }
    }

    // Edits are kept by the autosave; the browser only asks before closing while a copy is still unwritten.
    window.addEventListener('beforeunload', (e) => {
        if (!autosaveAtRisk) return;
        e.preventDefault();
        e.returnValue = '';
    });

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
        flushAutosave: async () => {
            window.clearTimeout(autosaveTimer);
            await doAutosave();
        },
        levels: () => disc?.levels ?? [],
        modTarget: () => target(),
        /** Class name and model render for a resource string (used by Save mode's inventory); null without a level. */
        classInfo(resource: string): { name: string; thumb: string | null } | null {
            const cls = catalog.find((c) => c.resource === resource);
            if (!cls) return null;
            loader.wantThumbnail(cls);
            return { name: cls.name, thumb: loader.thumbnail(cls.resource) };
        },
    };
}

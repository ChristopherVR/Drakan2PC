import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { TransformControls } from 'three/examples/jsm/controls/TransformControls.js';
import {
    CommandStack,
    DEFAULT_SNAP,
    DeleteCommand,
    LevelDocument,
    SetPropCommand,
    TransformCommand,
    describeObjects,
    editObjects,
    nearestObjectPlaneY,
    snapValue,
    type EntityKind,
    type GroundFn,
    type LevelObject,
    type SnapSettings,
    type Transform,
} from '@drakan/editor-core';
import { IconLayer, VolumeBoxes, type VolumeBox } from './scene/icons.ts';
import { ModelLayer } from './scene/layer.ts';
import { animRange } from './scene/animplan.ts';
import { HELPER_GROUPS, helperGroupOf, partitionObjects, type HelperGroup } from './scene/instances.ts';
import { docInstanceMatrix } from './scene/models.ts';
import { clientToNdc, nearestScreenPoint, pointsInRect, type ScreenPoint } from './scene/pickmath.ts';
import { DEFAULT_VIEW_OPTIONS, LIGHTING, type ViewOptions } from './scene/viewopts.ts';
import { canvasSize, inBurst, sameSize, settled, type CanvasSize } from './scene/viewsize.ts';

/** Game units to scene units. World coordinates run to about 7.4e5; y is up. */
const S = 0.001;
export const KINDS: { kind: EntityKind; color: number; label: string }[] = [
    { kind: 'player', color: 0x4ade80, label: 'Player' },
    { kind: 'creature', color: 0xef4444, label: 'Creature' },
    { kind: 'weapon', color: 0xfacc15, label: 'Item' },
    { kind: 'usable', color: 0xa78bfa, label: 'Usable' },
    { kind: 'static', color: 0x7a8699, label: 'Static' },
    { kind: 'camera', color: 0x38bdf8, label: 'Camera' },
    { kind: 'other', color: 0xf472b6, label: 'Other' },
];
/** What each kind means, shown as a tooltip on the Place chips, the Content Browser chips and the Outliner groups. */
export const KIND_HINT: Record<EntityKind, string> = {
    player: 'Player characters, their start points and level transitions',
    creature: 'Enemies, NPCs, dragons, barriers and waypoints: anything the game treats as a creature',
    weapon: 'Pick-ups: weapons, armour, potions and other items',
    usable: 'Things the player or a trigger operates: doors, levers, chests, timers and detectors',
    static: 'Scenery that does not move: trees, rocks, walls, props',
    camera: 'Cameras for cut scenes',
    other: 'Everything else: sounds, lights, level settings',
};
const DRAG_PX = 4;
const PICK_PX = 10;
/** Half the editor icon size on screen plus a little slack. */
const ICON_PICK_PX = 15;

const tmpM = new THREE.Matrix4();
const tmpQ = new THREE.Quaternion();
const tmpE = new THREE.Euler();
const tmpV = new THREE.Vector3();
const tmpS = new THREE.Vector3();

export type ToolMode = 'select' | 'translate' | 'rotate' | 'scale';
export type ToolSpace = 'world' | 'local';

export interface CameraBookmark {
    name: string;
    pos: [number, number, number];
    target: [number, number, number];
}

export interface LevelViewEvents {
    onSelection(ids: number[]): void;
    onChange(): void;
    /** Hover over an object (or off, `id` null); `x`/`y` are client coordinates. */
    onHover?(id: number | null, x: number, y: number): void;
    /** Right click without dragging (a plain right button press is the fly camera): object under it and the ground point. */
    onContextMenu?(id: number | null, x: number, y: number, ground: [number, number, number] | null): void;
    /** Hidden/locked sets or filters changed. */
    onVisibility?(): void;
    /** Camera moved (for the status bar). */
    onCamera?(): void;
    /** A class was dropped from the asset browser; `pos` is in game units. */
    onDropAsset?(key: string, pos: [number, number, number]): void;
}

export interface GroundSource {
    /** Terrain height under (x, z) in game units. */
    height?: GroundFn;
    /** Meshes to raycast for ground placement (terrain). */
    meshes?: THREE.Object3D[];
}

/** three.js level view: instanced markers per entity kind, picking, gizmo, filters and the undoable document. */
export class LevelView {
    readonly renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
    readonly scene = new THREE.Scene();
    readonly camera = new THREE.PerspectiveCamera(50, 1, 0.5, 20000);
    readonly orbit: OrbitControls;
    readonly gizmo: TransformControls;
    private readonly pivot = new THREE.Object3D();
    private readonly raycaster = new THREE.Raycaster();
    private meshes = new Map<EntityKind, { mesh: THREE.InstancedMesh; ids: number[] }>();
    private selMesh: THREE.InstancedMesh | null = null;
    private hoverMesh: THREE.Mesh;
    /** Terrain and model instances (real visuals); filled by the level loader. */
    readonly models = new ModelLayer();
    viewOptions: ViewOptions = {
        ...DEFAULT_VIEW_OPTIONS,
        helpers: { ...DEFAULT_VIEW_OPTIONS.helpers },
        story: { ...DEFAULT_VIEW_OPTIONS.story },
    };
    /** Called after every rendered frame (overlays that position screen labels, `story/storyview.ts`). */
    readonly afterFrame = new Set<() => void>();
    private readonly hemi = new THREE.HemisphereLight(0xffffff, 0x445066, 1.6);
    private readonly sun = new THREE.DirectionalLight(0xffffff, 1.2);
    private bgColor = 0x14161a;
    /** Objects that are drawn right now (marker or model), and the subset drawn as markers. */
    private drawn: LevelObject[] = [];
    private markerObjs: LevelObject[] = [];
    /** Helper-class objects drawn as editor icons (billboards), and their ids. */
    private iconObjs: LevelObject[] = [];
    private iconIds = new Set<number>();
    private iconGroups = new Map<number, HelperGroup>();
    readonly icons = new IconLayer();
    private readonly iconVolumes = new VolumeBoxes();
    private ghostKey: string | null = null;
    private ghostChild: THREE.Object3D | null = null;
    private ghostMesh: THREE.InstancedMesh | null = null;
    private ghost: LevelObject[] = [];
    private placeGhost: THREE.Group;
    private linkLines: THREE.LineSegments | null = null;
    private gridHelper: THREE.GridHelper | null = null;
    private gridKey = '';
    private rubber: HTMLDivElement;
    private readonly geo: THREE.BufferGeometry;
    private raf = 0;
    private keys = new Set<string>();
    private downAt: { x: number; y: number; alt: boolean; button: number } | null = null;
    private boxing = false;
    private flying = false;
    private flyLook = { x: 0, y: 0 };
    private rightDown: { x: number; y: number; t: number } | null = null;
    /** Called when pick mode starts or ends (a banner in the editor shell). */
    onPickChange: (() => void) | null = null;
    private dragBefore: Map<number, LevelObject> | null = null;
    private dragStart: {
        c: THREE.Vector3;
        q: THREE.Quaternion;
        pos: Map<number, [number, number, number]>;
        scale: Map<number, [number, number, number]>;
    } | null = null;
    private readonly mats = new Map<EntityKind, THREE.MeshLambertMaterial>();
    private lastTick = 0;
    private dirtyInstances = true;
    private dirtySel = true;
    private dirtyLinks = true;
    private hoverId: number | null = null;
    private hoverRaf = 0;
    private lastPointer: PointerEvent | null = null;
    private ground: GroundSource = {};
    private editorMode = false;
    private refPicker: ((id: number | null) => void) | null = null;

    doc = new LevelDocument();
    stack = new CommandStack(this.doc);
    /** Called with every document `setDocument` installs (the landscape binding attaches the level terrain). */
    readonly documentListeners = new Set<(doc: LevelDocument) => void>();
    selection = new Set<number>();
    hiddenKinds = new Set<EntityKind>();
    /** Display categories switched off in the Show menu (`ObjectCategory` ids) and the function that names them. */
    hiddenCategories = new Set<string>();
    categoryOf: ((o: LevelObject) => string) | null = null;
    hiddenIds = new Set<number>();
    lockedIds = new Set<number>();
    /** Restrictions for hosts that edit a fixed set of objects (the save editor): no add/remove, optional move-only. */
    policy: { structural: boolean; translateOnly: boolean; movable?: (id: number) => boolean } = {
        structural: true,
        translateOnly: false,
    };
    classFilter = '';
    search = '';
    /** Count of objects that pass the filters (for the status line). */
    visibleCount = 0;
    fps = 0;
    snap: SnapSettings = { ...DEFAULT_SNAP };
    space: ToolSpace = 'world';
    mode: ToolMode = 'translate';
    /** Click-placement helper for the editor: the ground point under the last pointer position inside the viewport. */
    /** Draw link lines: for the selection only, or for every object. */
    showLinks: 'selection' | 'all' | 'off' = 'selection';
    /** Fly speed multiplier (wheel while flying or the toolbar slider). */
    flySpeed = 1;
    bookmarks: CameraBookmark[] = [];
    /** Class being dragged from the asset browser (set by the browser on dragstart). */
    dragKey: string | null = null;
    /** Show the editor grid. */
    gridVisible = true;
    private frames = 0;
    private fpsT = performance.now();
    /** Size the canvas has now; the host changed size and the next frame applies it (scene/viewsize.ts). */
    private size: CanvasSize | null = null;
    private sizeDirty = true;
    private resizeBurst = false;
    private lastResizeAt = -Infinity;

    constructor(
        private readonly host: HTMLElement,
        private readonly ev: LevelViewEvents,
    ) {
        host.appendChild(this.renderer.domElement);
        this.scene.background = new THREE.Color(0x14161a);
        this.scene.add(this.hemi, this.sun, this.models.root, this.icons.group, this.iconVolumes.group);
        this.setViewOptions(this.viewOptions);

        // Marker: a pyramid whose tip points along local +x (yaw 0 faces +x, positive yaw turns toward -z, matching
        // three's rotation.y). The convention is unverified against the game; see the journal note.
        const g = new THREE.ConeGeometry(0.6, 2.4, 4);
        g.rotateZ(-Math.PI / 2);
        this.geo = g;

        this.hoverMesh = new THREE.Mesh(
            this.geo,
            new THREE.MeshBasicMaterial({ color: 0xfde047, wireframe: true, depthTest: false }),
        );
        this.hoverMesh.visible = false;
        this.hoverMesh.renderOrder = 11;
        this.scene.add(this.hoverMesh);

        this.placeGhost = new THREE.Group();
        const pm = new THREE.Mesh(
            this.geo,
            new THREE.MeshBasicMaterial({ color: 0x5aa2ff, transparent: true, opacity: 0.55, depthTest: false }),
        );
        pm.scale.setScalar(0.4);
        const pl = new THREE.Line(
            new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, -4, 0)]),
            new THREE.LineBasicMaterial({ color: 0x5aa2ff, depthTest: false }),
        );
        this.placeGhost.add(pm, pl);
        this.placeGhost.visible = false;
        this.placeGhost.renderOrder = 12;
        this.scene.add(this.placeGhost);

        this.camera.position.set(450, 600, 900);
        this.orbit = new OrbitControls(this.camera, this.renderer.domElement);
        this.orbit.enableDamping = false;
        this.orbit.target.set(450, 170, 350);
        this.orbit.mouseButtons = { LEFT: THREE.MOUSE.ROTATE, MIDDLE: THREE.MOUSE.DOLLY, RIGHT: THREE.MOUSE.PAN };

        this.gizmo = new TransformControls(this.camera, this.renderer.domElement);
        this.gizmo.size = 0.8;
        this.scene.add(this.pivot);
        this.scene.add(this.gizmo.getHelper());
        this.gizmo.addEventListener('dragging-changed', (e) => {
            const dragging = Boolean((e as unknown as { value: boolean }).value);
            this.orbit.enabled = !dragging;
            if (dragging) this.beginDrag();
            else this.endDrag();
        });
        this.gizmo.addEventListener('objectChange', () => this.liveDrag());

        this.rubber = document.createElement('div');
        this.rubber.className = 'rubberband';
        this.rubber.hidden = true;
        host.appendChild(this.rubber);

        const el = this.renderer.domElement;
        el.addEventListener('pointerdown', this.onPointerDownCapture, true);
        el.addEventListener('pointerdown', this.onPointerDown);
        el.addEventListener('pointermove', this.onPointerMove);
        el.addEventListener('pointerup', this.onPointerUp);
        el.addEventListener('pointerleave', () => this.setHover(null, 0, 0));
        el.addEventListener('contextmenu', (e) => e.preventDefault());
        el.addEventListener('wheel', this.onWheel, { passive: true });
        host.addEventListener('dragover', this.onDragOver);
        host.addEventListener('dragleave', () => (this.placeGhost.visible = false));
        host.addEventListener('drop', this.onDrop);
        window.addEventListener('keydown', this.onKeyDown);
        window.addEventListener('keyup', this.onKeyUp);
        window.addEventListener('blur', () => this.keys.clear());
        // A resize only marks the size; the frame loop applies it right before it draws (see scene/viewsize.ts).
        new ResizeObserver(() => this.noteResize()).observe(host);
        this.resize();
        this.rebuildScheduled();
    }

    dispose(): void {
        cancelAnimationFrame(this.raf);
        window.removeEventListener('keydown', this.onKeyDown);
        window.removeEventListener('keyup', this.onKeyUp);
    }

    setActive(on: boolean): void {
        cancelAnimationFrame(this.raf);
        if (on) {
            this.sizeDirty = true;
            this.resize();
            this.lastTick = performance.now();
            const loop = (): void => {
                this.raf = requestAnimationFrame(loop);
                this.frame();
            };
            loop();
        }
    }

    /**
     * Editor controls (the Level tab): left click selects and box-selects, alt+left orbits, middle pans, the right
     * button flies (WASD/QE), hover highlights and the editor's shortcut map replaces the built-in keys. Off by default,
     * which keeps the simple controls the Save tab uses (left rotates, right pans, 1/2/F/Del/Ctrl+Z keys).
     */
    setEditorControls(on: boolean): void {
        this.editorMode = on;
        const none = -1 as unknown as THREE.MOUSE;
        this.orbit.mouseButtons = on
            ? { LEFT: none, MIDDLE: THREE.MOUSE.PAN, RIGHT: none }
            : { LEFT: THREE.MOUSE.ROTATE, MIDDLE: THREE.MOUSE.DOLLY, RIGHT: THREE.MOUSE.PAN };
        this.orbit.zoomToCursor = on;
        this.setViewOptions(this.viewOptions);
    }

    /** Ground sources from the terrain renderer; until it lands placement falls back to object height. */
    setGround(g: GroundSource): void {
        this.ground = g;
    }

    setBackground(color: number): void {
        this.bgColor = color;
        (this.scene.background as THREE.Color).set(color);
        if (this.scene.fog) this.scene.fog.color.set(color);
    }

    /** Apply display options: lighting, fog and draw distance, terrain and model visibility, helper classes. */
    setViewOptions(o: ViewOptions): void {
        const prev = this.viewOptions;
        this.viewOptions = o;
        const l = LIGHTING[o.lighting];
        this.hemi.color.set(l.sky);
        this.hemi.groundColor.set(l.ground);
        this.hemi.intensity = l.hemi;
        this.sun.intensity = l.sun;
        this.sun.position.set(...l.sunDir);
        // The Save tab shares this class and has world-sized scenes: near/far and fog only apply in the editor.
        this.camera.near = this.editorMode ? 0.1 : 0.5;
        this.camera.far = this.editorMode ? o.drawDistance : 20000;
        this.camera.updateProjectionMatrix();
        this.scene.fog =
            o.fog && this.editorMode ? new THREE.Fog(this.bgColor, o.drawDistance * 0.35, o.drawDistance) : null;
        this.models.terrainRoot.visible = o.terrain;
        this.models.anim.setEnabled(o.animate && o.models);
        this.models.anim.range = animRange(o.drawDistance);
        if (o.wireframe !== prev.wireframe || o.terrain !== prev.terrain) this.models.setTerrainWireframe(o.wireframe);
        this.dirtyInstances = true;
    }

    /** Called after terrain or models finished loading: redraw instances and re-apply terrain options. */
    modelsChanged(): void {
        this.models.setTerrainWireframe(this.viewOptions.wireframe);
        this.models.terrainRoot.visible = this.viewOptions.terrain;
        this.dirtyInstances = true;
    }

    /** Draw calls and triangles of the last frame, and instance counts (status bar). */
    stats(): { calls: number; triangles: number; models: number; markers: number } {
        const r = this.renderer.info.render;
        return {
            calls: r.calls,
            triangles: r.triangles,
            models: this.models.instanceCount,
            markers: this.markerObjs.length,
        };
    }

    // ---- document ---------------------------------------------------------------------------------------------

    setDocument(doc: LevelDocument): void {
        this.doc = doc;
        this.stack = new CommandStack(doc);
        this.stack.onChange = () => this.afterCommand();
        this.selection.clear();
        this.hiddenIds.clear();
        this.lockedIds.clear();
        this.dirtyInstances = true;
        this.dirtyLinks = true;
        this.updateGizmo();
        this.ev.onSelection([]);
        this.frameAll();
        for (const l of this.documentListeners) l(doc);
        this.ev.onChange();
        this.ev.onVisibility?.();
    }

    /** Reference objects drawn as cyan outlines (a world dump compared with the document); not editable. */
    setGhost(objs: LevelObject[]): void {
        this.ghost = objs;
        this.dirtyInstances = true;
    }

    run(cmd: Parameters<CommandStack['run']>[0]): void {
        this.stack.run(cmd);
    }

    /** Objects were moved outside a command (a landscape stroke carrying them, live): redraw them and the gizmo. */
    objectsMoved(): void {
        this.dirtyInstances = true;
        this.dirtySel = true;
        this.dirtyLinks = true;
        this.updateGizmo();
    }

    private afterCommand(): void {
        for (const id of [...this.selection]) if (!this.doc.has(id)) this.selection.delete(id);
        for (const id of [...this.hiddenIds]) if (!this.doc.has(id)) this.hiddenIds.delete(id);
        for (const id of [...this.lockedIds]) if (!this.doc.has(id)) this.lockedIds.delete(id);
        this.dirtyInstances = true;
        this.dirtyLinks = true;
        this.updateGizmo();
        this.ev.onSelection([...this.selection]);
        this.ev.onChange();
    }

    undo(): void {
        this.stack.undo();
    }
    redo(): void {
        this.stack.redo();
    }
    deleteSelected(): void {
        const ids = this.editable();
        if (this.policy.structural && ids.length)
            this.stack.run(new DeleteCommand(ids, `Delete ${this.describe(ids)}`));
    }
    duplicateSelected(): void {
        const ids = this.editable();
        if (!this.policy.structural || !ids.length) return;
        const cmd = this.stack.duplicate(ids);
        this.select(cmd.ids);
    }
    /** `Orc #123` / `3 objects` for history labels. */
    private describe(ids: readonly number[]): string {
        return describeObjects(ids.map((i) => this.doc.get(i)).filter((o): o is LevelObject => !!o));
    }
    /** Selected objects that are not locked. */
    editable(): number[] {
        return [...this.selection].filter((id) => !this.lockedIds.has(id));
    }
    setTransformField(id: number, field: 'pos' | 'rot', axis: number, value: number): void {
        const o = this.doc.get(id);
        if (!o || !Number.isFinite(value)) return;
        const before = structuredClone(o.transform);
        const after = structuredClone(o.transform);
        after[field][axis] = value;
        this.stack.run(
            new TransformCommand(
                new Map([[id, before]]),
                new Map([[id, after]]),
                `${field === 'pos' ? 'Move' : 'Rotate'} ${this.describe([id])}`,
            ),
        );
    }
    setProp(id: number, key: string, value: string | number | boolean | undefined): void {
        this.stack.run(new SetPropCommand(id, key, value));
    }

    // ---- hidden / locked --------------------------------------------------------------------------------------

    setHidden(ids: Iterable<number>, hidden: boolean): void {
        for (const id of ids) {
            if (hidden) this.hiddenIds.add(id);
            else this.hiddenIds.delete(id);
        }
        for (const id of [...this.selection]) if (this.hiddenIds.has(id)) this.selection.delete(id);
        this.dirtyInstances = true;
        this.dirtyLinks = true;
        this.updateGizmo();
        this.ev.onSelection([...this.selection]);
        this.ev.onVisibility?.();
    }

    setLocked(ids: Iterable<number>, locked: boolean): void {
        for (const id of ids) {
            if (locked) this.lockedIds.add(id);
            else this.lockedIds.delete(id);
        }
        this.updateGizmo();
        this.ev.onVisibility?.();
    }

    // ---- filters / selection ----------------------------------------------------------------------------------

    passes(o: LevelObject): boolean {
        if (this.hiddenIds.has(o.id)) return false;
        if (this.hiddenKinds.has(o.kind)) return false;
        if (this.hiddenCategories.size && this.categoryOf && this.hiddenCategories.has(this.categoryOf(o)))
            return false;
        if (this.classFilter && !o.class.toLowerCase().includes(this.classFilter)) return false;
        if (this.search) {
            const q = this.search;
            const hay = `${o.id} ${o.class} ${o.resource} ${o.model} ${o.kind} ${o.props.handle ?? ''}`.toLowerCase();
            if (!hay.includes(q)) return false;
        }
        return true;
    }

    setFilters(f: {
        hidden?: Set<EntityKind>;
        categories?: Set<string>;
        categoryOf?: (o: LevelObject) => string;
        cls?: string;
        search?: string;
    }): void {
        if (f.hidden) this.hiddenKinds = f.hidden;
        if (f.categories) this.hiddenCategories = f.categories;
        if (f.categoryOf) this.categoryOf = f.categoryOf;
        if (f.cls !== undefined) this.classFilter = f.cls.trim().toLowerCase();
        if (f.search !== undefined) this.search = f.search.trim().toLowerCase();
        for (const id of [...this.selection]) {
            const o = this.doc.get(id);
            if (!o || !this.passes(o)) this.selection.delete(id);
        }
        this.dirtyInstances = true;
        this.dirtyLinks = true;
        this.updateGizmo();
        this.ev.onSelection([...this.selection]);
        this.ev.onVisibility?.();
    }

    select(ids: number[], additive = false): void {
        if (!additive) this.selection.clear();
        for (const id of ids) {
            if (additive && this.selection.has(id)) this.selection.delete(id);
            else if (this.doc.has(id)) this.selection.add(id);
        }
        this.dirtySel = true;
        this.dirtyLinks = true;
        this.updateGizmo();
        this.ev.onSelection([...this.selection]);
    }

    selectVisible(): void {
        const ids: number[] = [];
        for (const o of this.doc.objects()) if (this.passes(o) && !this.lockedIds.has(o.id)) ids.push(o.id);
        this.select(ids);
    }

    invertSelection(): void {
        const ids: number[] = [];
        for (const o of this.doc.objects()) {
            if (this.passes(o) && !this.lockedIds.has(o.id) && !this.selection.has(o.id)) ids.push(o.id);
        }
        this.select(ids);
    }

    /** Select every visible object of the same class as the selection. */
    selectSameClass(): void {
        const res = new Set([...this.selection].map((id) => this.doc.get(id)?.resource));
        const ids: number[] = [];
        for (const o of this.doc.objects()) {
            if (res.has(o.resource) && this.passes(o) && !this.lockedIds.has(o.id)) ids.push(o.id);
        }
        this.select(ids);
    }

    private ndc(e: { clientX: number; clientY: number }): THREE.Vector2 {
        const [x, y] = clientToNdc(this.renderer.domElement.getBoundingClientRect(), e.clientX, e.clientY);
        return new THREE.Vector2(x, y);
    }

    /** Client-pixel positions of objects (those that project in front of the camera). */
    private screenPoints(list: readonly LevelObject[], includeLocked: boolean): ScreenPoint[] {
        const r = this.renderer.domElement.getBoundingClientRect();
        const out: ScreenPoint[] = [];
        for (const o of list) {
            if (!includeLocked && this.lockedIds.has(o.id)) continue;
            tmpV.set(o.transform.pos[0] * S, o.transform.pos[1] * S, o.transform.pos[2] * S).project(this.camera);
            if (tmpV.z > 1 || tmpV.z < -1) continue;
            out.push({
                id: o.id,
                x: ((tmpV.x + 1) / 2) * r.width + r.left,
                y: ((1 - tmpV.y) / 2) * r.height + r.top,
            });
        }
        return out;
    }

    /** Object under the pointer: model instances and markers by ray (nearest wins), then the nearest marker within a few pixels. */
    pickAt(e: { clientX: number; clientY: number }, includeLocked = false): number | null {
        const ndc = this.ndc(e);
        this.raycaster.setFromCamera(ndc, this.camera);
        let best: { d: number; id: number } | null = null;
        // Editor icons are drawn on top of everything, so they win over the surface behind them.
        const ic = nearestScreenPoint(
            this.screenPoints(this.iconObjs, includeLocked),
            e.clientX,
            e.clientY,
            ICON_PICK_PX,
        );
        if (ic !== null) return ic;
        const m = this.models.pick(this.raycaster.ray, (id) => !includeLocked && this.lockedIds.has(id));
        if (m) best = { d: m.distance, id: m.id };
        for (const { mesh, ids } of this.meshes.values()) {
            const hit = this.raycaster.intersectObject(mesh, false)[0];
            if (hit && hit.instanceId !== undefined) {
                const id = ids[hit.instanceId]!;
                if (!includeLocked && this.lockedIds.has(id)) continue;
                if (!best || hit.distance < best.d) best = { d: hit.distance, id };
            }
        }
        if (best) return best.id;
        // Screen-space fallback for markers only: they are small when zoomed out. Models are picked by their surface.
        return nearestScreenPoint(this.screenPoints(this.markerObjs, includeLocked), e.clientX, e.clientY, PICK_PX);
    }

    /** Drawn objects whose position projects inside the client-space rectangle. */
    private boxSelect(x0: number, y0: number, x1: number, y1: number, additive: boolean): void {
        const ids = pointsInRect(this.screenPoints(this.drawn, false), x0, y0, x1, y1);
        if (additive) {
            const merged = new Set([...this.selection, ...ids]);
            this.select([...merged]);
        } else this.select(ids);
    }

    // ---- placement --------------------------------------------------------------------------------------------

    /** Where a ray through the pointer meets the level: terrain, an object marker, else a plane at the nearest object height. */
    groundAt(e: { clientX: number; clientY: number }): [number, number, number] | null {
        const ray = this.raycaster;
        ray.setFromCamera(this.ndc(e), this.camera);
        if (this.ground.meshes?.length) {
            const hit = ray.intersectObjects(this.ground.meshes, true)[0];
            if (hit) return [hit.point.x / S, hit.point.y / S, hit.point.z / S];
        }
        const origin = ray.ray.origin;
        const dir = ray.ray.direction;
        const planeHit = (y: number): THREE.Vector3 | null => {
            if (Math.abs(dir.y) < 1e-6) return null;
            const t = (y - origin.y) / dir.y;
            return t > 0 ? origin.clone().addScaledVector(dir, t) : null;
        };
        if (this.ground.height && !this.ground.meshes?.length) {
            // Height function but no mesh: march the ray against the height field.
            for (let t = 0.5; t < 6000; t += Math.max(0.5, t * 0.01)) {
                const p = origin.clone().addScaledVector(dir, t);
                const h = this.ground.height(p.x / S, p.z / S);
                if (h !== null && p.y <= h * S) return [p.x / S, h, p.z / S];
            }
        }
        let best: { d: number; p: THREE.Vector3 } | null = null;
        for (const { mesh } of this.meshes.values()) {
            const hit = ray.intersectObject(mesh, false)[0];
            if (hit && (!best || hit.distance < best.d)) best = { d: hit.distance, p: hit.point };
        }
        if (best) return [best.p.x / S, best.p.y / S, best.p.z / S];
        let p = planeHit(this.orbit.target.y);
        if (!p) return null;
        const y = nearestObjectPlaneY(this.doc.objects(), [p.x / S, 0, p.z / S]) * S;
        p = planeHit(y) ?? p;
        return [p.x / S, y / S, p.z / S];
    }

    pointerGround(): [number, number, number] | null {
        const p = this.lastPointer;
        if (!p) return null;
        const r = this.renderer.domElement.getBoundingClientRect();
        if (p.clientX < r.left || p.clientX > r.right || p.clientY < r.top || p.clientY > r.bottom) return null;
        return this.groundAt(p);
    }

    /** Terrain height under (x, z) in game units (the height function, else a ray straight down onto the ground meshes). */
    groundHeightAt(x: number, z: number): number | null {
        if (this.ground.height) return this.ground.height(x, z);
        if (this.ground.meshes?.length) {
            const ray = new THREE.Raycaster(new THREE.Vector3(x * S, 1e5, z * S), new THREE.Vector3(0, -1, 0));
            const hit = ray.intersectObjects(this.ground.meshes, true)[0];
            if (hit) return hit.point.y / S;
        }
        return null;
    }

    get hasGround(): boolean {
        return !!(this.ground.height || this.ground.meshes?.length);
    }

    get isFlying(): boolean {
        return this.flying;
    }

    /** Ground point under the viewport centre (used when a class is added without dragging). */
    groundAtCentre(): [number, number, number] | null {
        const r = this.renderer.domElement.getBoundingClientRect();
        return this.groundAt({ clientX: r.left + r.width / 2, clientY: r.top + r.height / 2 });
    }

    private onDragOver = (e: DragEvent): void => {
        if (!this.dragKey) return;
        e.preventDefault();
        if (e.dataTransfer) e.dataTransfer.dropEffect = 'copy';
        this.dragHover(e);
    };

    /** The pointer (a native drag or the Asset Browser's pointer drag) is over the viewport with `dragKey` set. */
    dragHover(e: { clientX: number; clientY: number }): void {
        if (!this.dragKey) return;
        const p = this.groundAt(e);
        this.updateDragModel();
        if (!p) {
            this.placeGhost.visible = false;
            return;
        }
        const [x, y, z] = this.placementPoint(p);
        this.placeGhost.position.set(x * S, y * S, z * S);
        this.placeGhost.visible = true;
    }

    placeGhostHide(): void {
        this.placeGhost.visible = false;
    }

    /** Grid-snapped x/z of a ground hit, with y re-read from the terrain at the snapped spot (within 2000 units of the hit). */
    private placementPoint(p: [number, number, number]): [number, number, number] {
        if (!this.snap.grid) return p;
        const x = snapValue(p[0], this.snap.gridSize);
        const z = snapValue(p[2], this.snap.gridSize);
        const h = this.groundHeightAt(x, z);
        return [x, h !== null && Number.isFinite(h) && Math.abs(h - p[1]) < 2000 ? h : p[1], z];
    }

    /** True when client coordinates fall inside the viewport canvas. */
    isOverViewport(x: number, y: number): boolean {
        const r = this.renderer.domElement.getBoundingClientRect();
        return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    }

    /** Hide the placement ghost and forget the dragged class (drag ended or left the viewport). */
    dragCancel(): void {
        this.placeGhost.visible = false;
        this.dragKey = null;
        this.updateDragModel();
    }

    /** Place the dragged class where the pointer meets the ground. */
    dragDrop(e: { clientX: number; clientY: number }): void {
        const key = this.dragKey;
        this.dragCancel();
        if (!key) return;
        const p = this.groundAt(e);
        if (!p) return;
        this.ev.onDropAsset?.(key, this.placementPoint(p));
    }

    /** Show the dragged class's real model in the placement ghost once it is loaded (marker pyramid until then). */
    private updateDragModel(): void {
        const key = this.dragKey;
        const m = key ? this.models.ghost(key) : null;
        if (m === this.ghostChild) return;
        if (this.ghostChild) this.placeGhost.remove(this.ghostChild);
        this.ghostChild = m;
        if (m) this.placeGhost.add(m);
        // The pyramid is the first child; hide it while a model stands in.
        this.placeGhost.children[0]!.visible = !m;
    }

    private onDrop = (e: DragEvent): void => {
        if (!this.dragKey) return;
        e.preventDefault();
        e.stopPropagation();
        this.dragDrop(e);
    };

    // ---- pointer ----------------------------------------------------------------------------------------------

    /** Choose the orbit mapping before OrbitControls sees the event: alt+left orbits. */
    private onPointerDownCapture = (e: PointerEvent): void => {
        // Recorded first (capture phase) so the gizmo's drag start can see the modifier keys (alt-drag duplicates).
        this.downAt = { x: e.clientX, y: e.clientY, alt: e.altKey, button: e.button };
        this.boxing = false;
        if (!this.editorMode) return;
        const rotate = THREE.MOUSE.ROTATE;
        const none = -1 as unknown as THREE.MOUSE;
        this.orbit.mouseButtons.LEFT = e.altKey ? rotate : none;
    };

    private onPointerDown = (e: PointerEvent): void => {
        if (e.button === 2 && this.editorMode) {
            this.flying = true;
            this.rightDown = { x: e.clientX, y: e.clientY, t: performance.now() };
            this.flyLook = { x: e.clientX, y: e.clientY };
            this.renderer.domElement.setPointerCapture(e.pointerId);
            this.renderer.domElement.style.cursor = 'none';
        }
    };

    private onPointerMove = (e: PointerEvent): void => {
        this.lastPointer = e;
        if (!this.editorMode) return;
        if (this.flying) {
            this.look(e.clientX - this.flyLook.x, e.clientY - this.flyLook.y);
            this.flyLook = { x: e.clientX, y: e.clientY };
            return;
        }
        const d = this.downAt;
        if (d && d.button === 0 && !d.alt && !this.gizmo.dragging && !this.gizmo.axis && !this.refPicker) {
            if (this.boxing || Math.hypot(e.clientX - d.x, e.clientY - d.y) > DRAG_PX) {
                this.boxing = true;
                const r = this.host.getBoundingClientRect();
                const l = Math.min(d.x, e.clientX) - r.left;
                const t = Math.min(d.y, e.clientY) - r.top;
                Object.assign(this.rubber.style, {
                    left: `${l}px`,
                    top: `${t}px`,
                    width: `${Math.abs(e.clientX - d.x)}px`,
                    height: `${Math.abs(e.clientY - d.y)}px`,
                });
                this.rubber.hidden = false;
            }
            return;
        }
        if (!d && !this.hoverRaf) {
            this.hoverRaf = requestAnimationFrame(() => {
                this.hoverRaf = 0;
                const p = this.lastPointer;
                if (!p || this.gizmo.dragging || this.gizmo.axis) return this.setHover(null, 0, 0);
                this.setHover(this.pickAt(p, true), p.clientX, p.clientY);
            });
        }
    };

    private setHover(id: number | null, x: number, y: number): void {
        if (id !== this.hoverId) {
            this.hoverId = id;
            const o = id === null ? undefined : this.doc.get(id);
            this.hoverMesh.visible = false;
            this.models.setHover([]);
            this.icons.setHover(o && this.iconIds.has(o.id) ? o.id : null);
            this.updateIconVolumes();
            if (o && this.iconIds.has(o.id)) {
                /* the icon itself lights up */
            } else if (o && this.isModelDrawn(o)) this.models.setHover([o]);
            else if (o) {
                this.objMatrix(o, 0.55, tmpM);
                tmpM.decompose(this.hoverMesh.position, this.hoverMesh.quaternion, this.hoverMesh.scale);
                this.hoverMesh.visible = true;
            }
        }
        this.ev.onHover?.(id, x, y);
    }

    private onPointerUp = (e: PointerEvent): void => {
        const d = this.downAt;
        this.downAt = null;
        if (e.button === 2 && this.flying) {
            this.flying = false;
            this.renderer.domElement.style.cursor = '';
            if (this.renderer.domElement.hasPointerCapture(e.pointerId)) {
                this.renderer.domElement.releasePointerCapture(e.pointerId);
            }
            const rd = this.rightDown;
            this.rightDown = null;
            if (rd && performance.now() - rd.t < 400 && Math.hypot(e.clientX - rd.x, e.clientY - rd.y) < DRAG_PX) {
                const id = this.pickAt(e, true);
                if (id !== null && !this.selection.has(id)) this.select([id]);
                this.ev.onContextMenu?.(id, e.clientX, e.clientY, this.groundAt(e));
            }
            return;
        }
        const wasBoxing = this.boxing;
        this.boxing = false;
        this.rubber.hidden = true;
        if (!d || e.button !== 0 || this.gizmo.dragging || d.alt) return;
        if (wasBoxing) {
            this.boxSelect(d.x, d.y, e.clientX, e.clientY, e.shiftKey || e.ctrlKey || e.metaKey);
            return;
        }
        if (Math.hypot(e.clientX - d.x, e.clientY - d.y) > DRAG_PX) return;
        if (this.gizmo.axis) return;
        const id = this.pickAt(e);
        if (this.refPicker) {
            this.completePick(id);
            return;
        }
        if (id === null) {
            if (!e.shiftKey && !e.ctrlKey) this.select([]);
        } else this.select([id], e.shiftKey || e.ctrlKey || e.metaKey);
    };

    /** The next click on an object calls `cb` with its id (null when it hits nothing) instead of selecting. Esc cancels. */
    pickObject(cb: (id: number | null) => void): void {
        this.refPicker = cb;
        this.renderer.domElement.style.cursor = 'crosshair';
        this.onPickChange?.();
    }
    /** Finish pick mode with `id` (the viewport click, or a click on an Outliner row). */
    completePick(id: number | null): void {
        const cb = this.refPicker;
        if (!cb) return;
        this.refPicker = null;
        this.renderer.domElement.style.cursor = '';
        this.onPickChange?.();
        cb(id);
    }
    cancelPick(): boolean {
        if (!this.refPicker) return false;
        this.refPicker = null;
        this.renderer.domElement.style.cursor = '';
        this.onPickChange?.();
        return true;
    }
    /** Client-pixel position of an object (null when behind the camera or unknown). */
    objectScreenPos(id: number): { x: number; y: number } | null {
        const o = this.doc.get(id);
        if (!o) return null;
        return this.screenPoints([o], true)[0] ?? null;
    }
    get picking(): boolean {
        return this.refPicker !== null;
    }

    private onWheel = (e: WheelEvent): void => {
        if (this.flying) {
            this.flySpeed = Math.min(20, Math.max(0.05, this.flySpeed * (e.deltaY < 0 ? 1.25 : 0.8)));
            this.ev.onCamera?.();
        }
    };

    // ---- gizmo ------------------------------------------------------------------------------------------------

    private centroid(ids: Iterable<number> = this.selection): THREE.Vector3 | null {
        const c = new THREE.Vector3();
        let n = 0;
        for (const id of ids) {
            const o = this.doc.get(id);
            if (!o) continue;
            const p = o.transform.pos;
            c.add(tmpV.set(p[0] * S, p[1] * S, p[2] * S));
            n++;
        }
        return n ? c.multiplyScalar(1 / n) : null;
    }

    private updateGizmo(): void {
        if (this.gizmo.dragging) return;
        if (this.mode === 'select') {
            this.gizmo.detach();
            return;
        }
        const ids = this.editable();
        const c = this.centroid(ids);
        const movable = this.policy.movable;
        if (!c || (movable && ids.some((id) => !movable(id)))) {
            this.gizmo.detach();
            return;
        }
        this.pivot.position.copy(c);
        this.pivot.scale.set(1, 1, 1);
        this.pivot.quaternion.identity();
        if (this.space === 'local' && ids.length === 1) {
            const o = this.doc.get(ids[0]!)!;
            this.pivot.quaternion.setFromEuler(tmpE.set(o.transform.rot[0], o.transform.rot[1], o.transform.rot[2]));
        }
        this.gizmo.setSpace(this.space);
        this.gizmo.attach(this.pivot);
    }

    setMode(m: ToolMode): void {
        if (m !== 'translate' && this.policy.translateOnly) return;
        this.mode = m;
        if (m !== 'select') this.gizmo.setMode(m);
        // Rotation is yaw only: the composition order of the three angles is undecoded (level-format.md section 3).
        this.gizmo.showX = this.gizmo.showZ = m !== 'rotate';
        this.gizmo.showY = true;
        this.updateGizmo();
    }

    setSpace(s: ToolSpace): void {
        this.space = s;
        this.updateGizmo();
    }

    setSnap(s: Partial<SnapSettings>): void {
        this.snap = { ...this.snap, ...s };
        this.gizmo.setRotationSnap(this.snap.angle ? (this.snap.angleStep * Math.PI) / 180 : null);
        this.gizmo.setScaleSnap(this.snap.grid ? 0.05 : null);
        this.gridKey = '';
    }

    private beginDrag(): void {
        const ids = this.editable();
        if (this.downAt?.alt && this.mode === 'translate' && this.policy.structural && ids.length) {
            // Alt-drag: leave the originals, drag copies.
            const cmd = this.stack.duplicate(ids, 0);
            this.selection = new Set(cmd.ids);
            this.updateGizmo();
            this.pivot.position.copy(this.centroid()!);
            this.dirtyInstances = true;
            this.ev.onSelection([...this.selection]);
        }
        this.dragBefore = new Map();
        const pos = new Map<number, [number, number, number]>();
        const scale = new Map<number, [number, number, number]>();
        for (const id of this.editable()) {
            const o = this.doc.get(id)!;
            this.dragBefore.set(id, structuredClone(o));
            pos.set(id, [...o.transform.pos]);
            if (o.scale) scale.set(id, [...o.scale]);
        }
        this.dragStart = { c: this.pivot.position.clone(), q: this.pivot.quaternion.clone(), pos, scale };
    }

    private liveDrag(): void {
        const s = this.dragStart;
        const before = this.dragBefore;
        if (!s || !before) return;
        if (this.gizmo.mode === 'translate' && this.mode !== 'select' && this.snap.grid) {
            // Snap the group's centre to the grid (x/z), keeping the offsets between the objects.
            this.pivot.position.x = snapValue(this.pivot.position.x / S, this.snap.gridSize) * S;
            this.pivot.position.z = snapValue(this.pivot.position.z / S, this.snap.gridSize) * S;
        }
        const dq = this.pivot.quaternion.clone().multiply(s.q.clone().invert());
        const dp = this.pivot.position.clone().sub(s.c);
        for (const id of before.keys()) {
            const o = this.doc.get(id)!;
            const b = before.get(id)!.transform;
            const p0 = s.pos.get(id)!;
            if (this.gizmo.mode === 'scale') {
                const base = s.scale.get(id);
                if (base) {
                    const k = this.pivot.scale;
                    o.scale = [base[0] * k.x, base[1] * k.y, base[2] * k.z];
                }
                continue;
            }
            tmpV.set(p0[0] * S, p0[1] * S, p0[2] * S)
                .sub(s.c)
                .applyQuaternion(dq)
                .add(s.c)
                .add(dp);
            o.transform.pos = [tmpV.x / S, tmpV.y / S, tmpV.z / S];
            if (this.gizmo.mode === 'rotate') {
                tmpQ.setFromEuler(tmpE.set(b.rot[0], b.rot[1], b.rot[2]));
                tmpQ.premultiply(dq);
                tmpE.setFromQuaternion(tmpQ);
                o.transform.rot = [tmpE.x, tmpE.y, tmpE.z];
            }
        }
        this.dirtyInstances = true;
        this.dirtySel = true;
        this.dirtyLinks = true;
    }

    private endDrag(): void {
        const before = this.dragBefore;
        this.dragBefore = null;
        this.dragStart = null;
        if (!before) return;
        const bt = new Map<number, Transform>();
        const at = new Map<number, Transform>();
        let moved = false;
        let scaled = false;
        const scaledIds: number[] = [];
        for (const [id, b] of before) {
            const o = this.doc.get(id);
            if (!o) continue;
            bt.set(id, structuredClone(b.transform));
            at.set(id, structuredClone(o.transform));
            if (
                b.transform.pos.some((v, i) => v !== o.transform.pos[i]) ||
                b.transform.rot.some((v, i) => v !== o.transform.rot[i])
            ) {
                moved = true;
            }
            if (b.scale && o.scale && b.scale.some((v, i) => v !== o.scale![i])) {
                scaled = true;
                scaledIds.push(id);
            }
        }
        if (scaled) {
            // Put the live values back, then run one undoable command with the final ones.
            const finals = new Map(
                scaledIds.map((id) => [id, [...this.doc.get(id)!.scale!] as [number, number, number]]),
            );
            for (const id of scaledIds) this.doc.get(id)!.scale = [...before.get(id)!.scale!];
            const cmd = editObjects(
                this.doc,
                scaledIds,
                `Scale ${this.describe(scaledIds)}`,
                (o) => (o.scale = [...finals.get(o.id)!]),
            );
            if (cmd) this.stack.run(cmd);
        }
        if (moved) {
            this.stack.run(
                new TransformCommand(
                    bt,
                    at,
                    `${this.mode === 'rotate' ? 'Rotate' : 'Move'} ${this.describe([...bt.keys()])}`,
                ),
            );
        }
        this.updateGizmo();
    }

    // ---- camera -----------------------------------------------------------------------------------------------

    private look(dx: number, dy: number): void {
        const e = new THREE.Euler().setFromQuaternion(this.camera.quaternion, 'YXZ');
        e.y -= dx * 0.0035;
        e.x = Math.max(-1.5, Math.min(1.5, e.x - dy * 0.0035));
        this.camera.quaternion.setFromEuler(e);
        const dist = Math.max(this.camera.position.distanceTo(this.orbit.target), 1);
        const f = new THREE.Vector3();
        this.camera.getWorldDirection(f);
        this.orbit.target.copy(this.camera.position).addScaledVector(f, dist);
        this.ev.onCamera?.();
    }

    frameIds(ids: readonly number[] | null): void {
        const box = ids ? new THREE.Box3() : (this.models.terrainBounds() ?? new THREE.Box3());
        for (const o of this.doc.objects()) {
            if (!ids && !box.isEmpty()) break;
            if (ids ? !ids.includes(o.id) : !this.passes(o)) continue;
            box.expandByPoint(tmpV.set(o.transform.pos[0] * S, o.transform.pos[1] * S, o.transform.pos[2] * S));
        }
        if (box.isEmpty()) return;
        const c = box.getCenter(new THREE.Vector3());
        const r = Math.max(box.getSize(new THREE.Vector3()).length() / 2, 8);
        const dir = this.camera.position.clone().sub(this.orbit.target).normalize();
        this.orbit.target.copy(c);
        this.camera.position.copy(c).addScaledVector(dir, r * 2.2);
        this.orbit.update();
        this.ev.onCamera?.();
    }

    frameSelection(): void {
        this.frameIds(this.selection.size ? [...this.selection] : null);
    }

    frameAll(): void {
        this.frameIds(null);
    }

    topView(): void {
        const dist = this.camera.position.distanceTo(this.orbit.target);
        this.camera.position.copy(this.orbit.target).add(new THREE.Vector3(0, dist, 0.001));
        this.orbit.update();
        this.ev.onCamera?.();
    }

    saveBookmark(name?: string): CameraBookmark {
        const b: CameraBookmark = {
            name: name ?? `View ${this.bookmarks.length + 1}`,
            pos: this.camera.position.toArray() as [number, number, number],
            target: this.orbit.target.toArray() as [number, number, number],
        };
        this.bookmarks.push(b);
        return b;
    }

    gotoBookmark(i: number): boolean {
        const b = this.bookmarks[i];
        if (!b) return false;
        this.camera.position.fromArray(b.pos);
        this.orbit.target.fromArray(b.target);
        this.orbit.update();
        this.ev.onCamera?.();
        return true;
    }

    /** Current camera in game units (for the status bar). */
    cameraGamePos(): [number, number, number] {
        const p = this.camera.position;
        return [p.x / S, p.y / S, p.z / S];
    }

    private readonly onKeyUp = (e: KeyboardEvent): void => {
        this.keys.delete(e.code);
    };

    private readonly onKeyDown = (e: KeyboardEvent): void => {
        const t = e.target as HTMLElement | null;
        if (t && /^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName)) return;
        if (!this.host.offsetParent) return;
        if (!this.editorMode) {
            this.legacyKey(e);
            return;
        }
        // Fly keys only while the right button is held; everything else is routed by the editor's shortcut map.
        if (this.flying && 'KeyW KeyA KeyS KeyD KeyQ KeyE ShiftLeft ShiftRight'.includes(e.code)) {
            this.keys.add(e.code);
            e.preventDefault();
        }
    };

    /** The Save tab's keys: Ctrl+Z/Y undo/redo, Ctrl+D/A, Del, F, 1/2 tools, Esc, WASD/QE fly. */
    private legacyKey(e: KeyboardEvent): void {
        const ctrl = e.ctrlKey || e.metaKey;
        if (ctrl && e.code === 'KeyZ') {
            e.preventDefault();
            if (e.shiftKey) this.redo();
            else this.undo();
        } else if (ctrl && e.code === 'KeyY') {
            e.preventDefault();
            this.redo();
        } else if (ctrl && e.code === 'KeyD') {
            e.preventDefault();
            this.duplicateSelected();
        } else if (ctrl && e.code === 'KeyA') {
            e.preventDefault();
            this.selectVisible();
        } else if (!ctrl && (e.code === 'Delete' || e.code === 'Backspace')) {
            this.deleteSelected();
        } else if (!ctrl && e.code === 'KeyF') {
            this.frameSelection();
        } else if (!ctrl && e.code === 'Digit1') {
            this.setMode('translate');
        } else if (!ctrl && e.code === 'Digit2') {
            this.setMode('rotate');
        } else if (!ctrl && e.code === 'Escape') {
            this.select([]);
        } else if (!ctrl && 'KeyW KeyA KeyS KeyD KeyQ KeyE'.includes(e.code)) {
            this.keys.add(e.code);
        }
    }

    private fly(dt: number): void {
        if (!this.keys.size) return;
        if (!this.editorMode) {
            // Legacy fly: speed follows the distance to the orbit target.
            const dist = this.camera.position.distanceTo(this.orbit.target);
            const speed = Math.max(dist, 20) * 0.9 * dt;
            const f = new THREE.Vector3();
            this.camera.getWorldDirection(f);
            const r = new THREE.Vector3().crossVectors(f, this.camera.up).normalize();
            const d = new THREE.Vector3();
            if (this.keys.has('KeyW')) d.add(f);
            if (this.keys.has('KeyS')) d.sub(f);
            if (this.keys.has('KeyD')) d.add(r);
            if (this.keys.has('KeyA')) d.sub(r);
            if (this.keys.has('KeyE')) d.y += 1;
            if (this.keys.has('KeyQ')) d.y -= 1;
            d.multiplyScalar(speed);
            this.camera.position.add(d);
            this.orbit.target.add(d);
            return;
        }
        if (!this.flying) return;
        const boost = this.keys.has('ShiftLeft') || this.keys.has('ShiftRight') ? 4 : 1;
        const speed = 60 * this.flySpeed * boost * dt;
        const f = new THREE.Vector3();
        this.camera.getWorldDirection(f);
        const r = new THREE.Vector3().crossVectors(f, this.camera.up).normalize();
        const d = new THREE.Vector3();
        if (this.keys.has('KeyW')) d.add(f);
        if (this.keys.has('KeyS')) d.sub(f);
        if (this.keys.has('KeyD')) d.add(r);
        if (this.keys.has('KeyA')) d.sub(r);
        if (this.keys.has('KeyE')) d.y += 1;
        if (this.keys.has('KeyQ')) d.y -= 1;
        d.multiplyScalar(speed);
        this.camera.position.add(d);
        this.orbit.target.add(d);
        this.clampAboveGround();
        this.ev.onCamera?.();
    }

    /** Keep the fly camera a little above the terrain so it does not sink below the surface. */
    private clampAboveGround(): void {
        if (!this.viewOptions.keepAboveGround) return;
        const p = this.camera.position;
        const h = this.groundHeightAt(p.x / S, p.z / S);
        if (h === null || !Number.isFinite(h)) return;
        const min = h * S + 0.35;
        if (p.y < min) {
            const dy = min - p.y;
            p.y += dy;
            this.orbit.target.y += dy;
        }
    }

    // ---- instancing -------------------------------------------------------------------------------------------

    private rebuildScheduled(): void {
        this.dirtyInstances = true;
    }

    private objMatrix(o: LevelObject, scale: number, out: THREE.Matrix4): void {
        tmpQ.setFromEuler(tmpE.set(o.transform.rot[0], o.transform.rot[1], o.transform.rot[2]));
        tmpV.set(o.transform.pos[0] * S, o.transform.pos[1] * S, o.transform.pos[2] * S);
        if (o.scale) tmpS.set(scale * o.scale[0], scale * o.scale[1], scale * o.scale[2]);
        else tmpS.set(scale, scale, scale);
        tmpS.clampScalar(scale * 0.2, scale * 6);
        out.compose(tmpV, tmpQ, tmpS);
    }

    /** True when the object is drawn with its model (not a marker). */
    private isModelDrawn(o: LevelObject): boolean {
        return this.viewOptions.models && this.models.has(o.resource) && this.modelIds.has(o.id);
    }
    private modelIds = new Set<number>();

    /**
     * Helper classes with a name the rules do not know (bird song emitters, wind and fog switchers, ...) still use the
     * placeholder cube of their kind: a model of at most 12 triangles with the one debug texture of the named sound or
     * trigger classes. Learned from the classes the name rules do catch, once their models are loaded.
     */
    private helperByModel = new Map<string, HelperGroup | null>();
    private helperModelCount = -1;
    private debugTextures = new Map<number, HelperGroup>();
    private modelHelper(resource: string): HelperGroup | null {
        if (this.helperModelCount !== this.models.entryCount) {
            this.helperModelCount = this.models.entryCount;
            this.helperByModel.clear();
            this.debugTextures.clear();
            const seen = new Set<string>();
            for (const o of this.doc.objects()) {
                if (seen.has(o.resource)) continue;
                seen.add(o.resource);
                const g = helperGroupOf(o.class);
                const t = g === 'sound' || g === 'trigger' ? this.cubeTexture(o.resource) : null;
                if (g && t !== null) this.debugTextures.set(t, g);
            }
        }
        let g = this.helperByModel.get(resource);
        if (g === undefined) {
            const t = this.cubeTexture(resource);
            g = t === null ? null : (this.debugTextures.get(t) ?? null);
            this.helperByModel.set(resource, g);
        }
        return g;
    }
    /** Objects per helper group, by class name and by learned placeholder model (the Show menu counts). */
    helperCounts(): Record<HelperGroup, number> {
        const out = Object.fromEntries(HELPER_GROUPS.map((g) => [g.id, 0])) as Record<HelperGroup, number>;
        const byRes = new Map<string, HelperGroup | null>();
        for (const o of this.doc.objects()) {
            const key = `${o.class}\u0000${o.resource}`;
            let g = byRes.get(key);
            if (g === undefined) byRes.set(key, (g = helperGroupOf(o.class) ?? this.modelHelper(o.resource)));
            if (g) out[g]++;
        }
        return out;
    }
    /** Changes when more models have loaded (a learned group may appear). */
    get helperRevision(): number {
        return this.models.entryCount;
    }
    private cubeTexture(resource: string): number | null {
        const e = this.models.entry(resource);
        if (!e || e.model.lod0.triCount > 12 || e.model.textures.length !== 1) return null;
        const t = e.model.textures[0]!;
        return t.id | (t.db << 16);
    }

    private rebuild(): void {
        for (const { mesh } of this.meshes.values()) {
            this.scene.remove(mesh);
            mesh.dispose();
        }
        this.meshes.clear();
        const visible: LevelObject[] = [];
        for (const o of this.doc.objects()) if (this.passes(o)) visible.push(o);
        this.visibleCount = visible.length;
        const opt = this.viewOptions;
        const part = partitionObjects(
            visible,
            (r) => opt.models && this.models.has(r),
            (g) => opt.helpers[g],
            opt.editorIcons && this.editorMode,
            (r) => this.modelHelper(r),
        );
        this.iconObjs = part.icons.map((i) => i.o);
        this.iconIds = new Set(this.iconObjs.map((o) => o.id));
        this.iconGroups = new Map(part.icons.map((i) => [i.o.id, i.group]));
        this.icons.set(
            part.icons.map(({ o, group }) => ({
                id: o.id,
                pos: [o.transform.pos[0]! * S, o.transform.pos[1]! * S, o.transform.pos[2]! * S] as const,
                group,
            })),
        );
        this.icons.setHover(null);
        this.icons.setFade(30, Math.min(opt.drawDistance * 0.4, 1500));
        this.models.sync(part.byResource);
        this.modelIds.clear();
        const modelObjs: LevelObject[] = [];
        for (const list of part.byResource.values()) {
            for (const o of list) {
                this.modelIds.add(o.id);
                modelObjs.push(o);
            }
        }
        this.models.anim.setObjects(modelObjs);
        // Markers: objects without a model, or every object when the markers option asks for it.
        const markerList =
            opt.markers === 'off' ? [] : opt.markers === 'all' ? [...part.markers, ...modelObjs] : part.markers;
        this.markerObjs = markerList;
        this.drawn = [...part.markers, ...modelObjs, ...this.iconObjs];
        const groups = new Map<EntityKind, LevelObject[]>();
        for (const o of markerList) {
            let g = groups.get(o.kind);
            if (!g) groups.set(o.kind, (g = []));
            g.push(o);
        }
        for (const { kind, color } of KINDS) {
            const list = groups.get(kind);
            if (!list?.length) continue;
            let mat = this.mats.get(kind);
            if (!mat) this.mats.set(kind, (mat = new THREE.MeshLambertMaterial({ color })));
            const mesh = new THREE.InstancedMesh(this.geo, mat, list.length);
            mesh.frustumCulled = false;
            const sz = kind === 'static' ? 0.2 : 0.35;
            list.forEach((o, i) => {
                this.objMatrix(o, sz, tmpM);
                mesh.setMatrixAt(i, tmpM);
            });
            mesh.instanceMatrix.needsUpdate = true;
            mesh.computeBoundingSphere();
            this.scene.add(mesh);
            this.meshes.set(kind, { mesh, ids: list.map((o) => o.id) });
        }
        if (this.ghostMesh) {
            this.scene.remove(this.ghostMesh);
            this.ghostMesh.dispose();
            this.ghostMesh = null;
        }
        if (this.ghost.length) {
            const gm = new THREE.InstancedMesh(
                this.geo,
                new THREE.MeshBasicMaterial({ color: 0x22d3ee, wireframe: true }),
                this.ghost.length,
            );
            gm.frustumCulled = false;
            this.ghost.forEach((o, i) => {
                this.objMatrix(o, 0.3, tmpM);
                gm.setMatrixAt(i, tmpM);
            });
            gm.instanceMatrix.needsUpdate = true;
            this.scene.add(gm);
            this.ghostMesh = gm;
        }
        this.dirtySel = true;
        this.hoverMesh.visible = false;
        this.models.setHover([]);
        this.hoverId = null;
    }

    private rebuildSel(): void {
        if (this.selMesh) {
            this.scene.remove(this.selMesh);
            this.selMesh.dispose();
            this.selMesh = null;
        }
        const all = [...this.selection].map((id) => this.doc.get(id)).filter((o): o is LevelObject => !!o);
        this.models.setSelection(all.filter((o) => this.isModelDrawn(o)));
        this.icons.setSelection(this.selection);
        this.updateIconVolumes();
        // Markers (and, when the markers option is on, models too) get the white wireframe.
        const objs = all.filter(
            (o) => !this.iconIds.has(o.id) && (!this.isModelDrawn(o) || this.viewOptions.markers === 'all'),
        );
        if (!objs.length) return;
        const m = new THREE.InstancedMesh(
            this.geo,
            new THREE.MeshBasicMaterial({ color: 0xffffff, wireframe: true, depthTest: false }),
            objs.length,
        );
        m.frustumCulled = false;
        m.renderOrder = 10;
        objs.forEach((o, i) => {
            this.objMatrix(o, 0.6, tmpM);
            m.setMatrixAt(i, tmpM);
        });
        m.instanceMatrix.needsUpdate = true;
        this.scene.add(m);
        this.selMesh = m;
    }

    /** Wire volume of the selected (or hovered) trigger and barrier icons: the box their placeholder model would fill. */
    private updateIconVolumes(): void {
        const list: VolumeBox[] = [];
        const ids = new Set(this.selection);
        const strong = new Set(this.selection);
        if (this.hoverId !== null) ids.add(this.hoverId);
        for (const id of ids) {
            if (!this.iconIds.has(id)) continue;
            const o = this.doc.get(id);
            if (!o) continue;
            const g = this.iconGroups.get(id);
            if (g !== 'trigger' && g !== 'barrier') continue;
            const e = this.models.entry(o.resource);
            if (!e) continue;
            list.push({
                matrix: docInstanceMatrix(o, new THREE.Matrix4()),
                min: e.model.bounds.min,
                max: e.model.bounds.max,
                group: g,
                strong: strong.has(id),
            });
        }
        this.iconVolumes.set(list);
    }

    /** Link lines: orange out of the selection (what it triggers), cyan into it. Dashed look comes from the arrow tick. */
    private rebuildLinks(): void {
        if (this.linkLines) {
            this.scene.remove(this.linkLines);
            this.linkLines.geometry.dispose();
            (this.linkLines.material as THREE.Material).dispose();
            this.linkLines = null;
        }
        if (this.showLinks === 'off') return;
        const pos: number[] = [];
        const col: number[] = [];
        const out = new THREE.Color(0xfb923c);
        const inn = new THREE.Color(0x22d3ee);
        const add = (a: LevelObject, b: LevelObject, c: THREE.Color): void => {
            const A = a.transform.pos;
            const B = b.transform.pos;
            pos.push(A[0] * S, A[1] * S, A[2] * S, B[0] * S, B[1] * S, B[2] * S);
            col.push(c.r, c.g, c.b, c.r, c.g, c.b);
            // Arrow tick at 85 % of the way: a short bar across the line.
            const t = 0.85;
            const mx = A[0] + (B[0] - A[0]) * t;
            const my = A[1] + (B[1] - A[1]) * t;
            const mz = A[2] + (B[2] - A[2]) * t;
            const dx = B[0] - A[0];
            const dz = B[2] - A[2];
            const n = Math.hypot(dx, dz) || 1;
            const w = 200;
            pos.push(
                (mx - (dz / n) * w) * S,
                my * S,
                (mz + (dx / n) * w) * S,
                (mx + (dz / n) * w) * S,
                my * S,
                (mz - (dx / n) * w) * S,
            );
            col.push(c.r, c.g, c.b, c.r, c.g, c.b);
        };
        const all = this.showLinks === 'all';
        for (const o of this.doc.objects()) {
            if (!o.links?.length || this.hiddenIds.has(o.id)) continue;
            const sel = this.selection.has(o.id);
            for (const to of o.links) {
                const t = this.doc.get(to);
                if (!t || this.hiddenIds.has(to)) continue;
                if (sel) add(o, t, out);
                else if (this.selection.has(to)) add(o, t, inn);
                else if (all) add(o, t, new THREE.Color(0x64748b));
            }
        }
        if (!pos.length) return;
        const geo = new THREE.BufferGeometry();
        geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
        geo.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
        const l = new THREE.LineSegments(geo, new THREE.LineBasicMaterial({ vertexColors: true, depthTest: false }));
        l.renderOrder = 9;
        l.frustumCulled = false;
        this.scene.add(l);
        this.linkLines = l;
    }

    setShowLinks(m: 'selection' | 'all' | 'off'): void {
        this.showLinks = m;
        this.dirtyLinks = true;
    }

    /** Editor grid: spacing is the snap size times a power of two so the lines stay readable at any distance. */
    private updateGrid(): void {
        if (!this.gridVisible) {
            if (this.gridHelper) this.gridHelper.visible = false;
            return;
        }
        const base = Math.max(this.snap.gridSize, 1) * S;
        const dist = Math.max(this.camera.position.distanceTo(this.orbit.target), 1);
        let k = 0;
        while (base * 2 ** k * 30 < dist && k < 16) k++;
        const spacing = base * 2 ** k;
        const cx = Math.round(this.orbit.target.x / spacing) * spacing;
        const cz = Math.round(this.orbit.target.z / spacing) * spacing;
        const key = `${spacing}`;
        if (key !== this.gridKey || !this.gridHelper) {
            this.gridKey = key;
            if (this.gridHelper) {
                this.scene.remove(this.gridHelper);
                this.gridHelper.geometry.dispose();
            }
            const lines = 120;
            this.gridHelper = new THREE.GridHelper(
                lines * spacing,
                lines,
                this.gridColors.major,
                this.gridColors.minor,
            );
            (this.gridHelper.material as THREE.Material).transparent = true;
            (this.gridHelper.material as THREE.Material).opacity = 0.55;
            this.scene.add(this.gridHelper);
        }
        this.gridHelper.visible = true;
        this.gridHelper.position.set(cx, this.orbit.target.y, cz);
    }

    gridColors = { major: 0x3a4150, minor: 0x24282f };
    setGridColors(major: number, minor: number): void {
        this.gridColors = { major, minor };
        this.gridKey = '';
    }

    // ---- loop -------------------------------------------------------------------------------------------------

    private noteResize(): void {
        const now = performance.now();
        this.resizeBurst = inBurst(this.lastResizeAt, now);
        this.lastResizeAt = now;
        this.sizeDirty = true;
    }

    /** Fit the canvas and the camera's aspect to the host. Nothing in the scene changes. */
    private resize(): void {
        const next = canvasSize(
            this.host.clientWidth,
            this.host.clientHeight,
            window.devicePixelRatio,
            this.sizeDirty && this.resizeBurst,
        );
        this.sizeDirty = false;
        if (sameSize(this.size, next)) return;
        // Each of these reallocates (and clears) the drawing buffer: only call them for a real change.
        if (!this.size || this.size.ratio !== next.ratio) this.renderer.setPixelRatio(next.ratio);
        this.renderer.setSize(next.w, next.h, false);
        this.size = next;
        this.camera.aspect = next.w / next.h;
        this.camera.updateProjectionMatrix();
    }

    private frame(): void {
        const now = performance.now();
        const dt = Math.min((now - this.lastTick) / 1000, 0.1);
        this.lastTick = now;
        // Resize here, just before drawing, so the cleared canvas is drawn again in the same frame.
        if (this.sizeDirty || settled(this.size, this.lastResizeAt, now, window.devicePixelRatio)) this.resize();
        this.fly(dt);
        if (this.dirtyInstances) {
            this.dirtyInstances = false;
            this.rebuild();
        }
        if (this.dirtySel) {
            this.dirtySel = false;
            this.rebuildSel();
        }
        if (this.dirtyLinks) {
            this.dirtyLinks = false;
            this.rebuildLinks();
        }
        this.updateGrid();
        this.orbit.update();
        this.models.anim.update(dt, this.camera);
        this.renderer.render(this.scene, this.camera);
        for (const f of this.afterFrame) f();
        this.frames++;
        if (now - this.fpsT >= 1000) {
            this.fps = Math.round((this.frames * 1000) / (now - this.fpsT));
            this.frames = 0;
            this.fpsT = now;
        }
    }
}

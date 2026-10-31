import * as THREE from 'three';
import { TransformControls } from 'three/examples/jsm/controls/TransformControls.js';
import {
    FILTER_REASONS,
    TEAM_COLORS,
    type ArenaDraft,
    type FilterPlan,
    type FilterReason,
    type Vec3,
} from '@drakan/editor-core';
import type { LevelView } from '../../level.ts';
import type { ArenaSel, ArenaStore } from './store.ts';

/** Game units to scene units (level.ts). */
const U = 0.001;
const BOUNDS_SAMPLES = 48;
const YELLOW = 0xfacc15;
const CYAN = 0x22d3ee;

const FILTER_COLOR = 0xef4444;
const KEEP_COLOR = 0x4ade80;
const MANUAL_COLOR = 0xfb923c;

type Pick = { kind: 'spawn' | 'pickup' | 'marker'; uid: number } | { kind: 'bounds'; corner: 0 | 1 };

const teamColor = (t: number): number => TEAM_COLORS[t] ?? TEAM_COLORS[0];

const labelCache = new Map<string, THREE.CanvasTexture>();
function labelTexture(text: string, color: number): THREE.CanvasTexture {
    const key = `${text}:${color}`;
    let t = labelCache.get(key);
    if (!t) {
        const c = document.createElement('canvas');
        c.width = c.height = 64;
        const g = c.getContext('2d')!;
        g.fillStyle = 'rgba(12,14,18,0.82)';
        g.beginPath();
        g.arc(32, 32, 30, 0, Math.PI * 2);
        g.fill();
        g.lineWidth = 4;
        g.strokeStyle = `#${color.toString(16).padStart(6, '0')}`;
        g.stroke();
        g.fillStyle = '#fff';
        g.font = 'bold 30px sans-serif';
        g.textAlign = 'center';
        g.textBaseline = 'middle';
        g.fillText(text, 32, 34);
        t = new THREE.CanvasTexture(c);
        t.colorSpace = THREE.SRGBColorSpace;
        labelCache.set(key, t);
    }
    return t;
}

function label(text: string, color: number, y: number): THREE.Sprite {
    const s = new THREE.Sprite(
        new THREE.SpriteMaterial({ map: labelTexture(text, color), depthTest: false, transparent: true }),
    );
    s.scale.setScalar(480 * U);
    s.position.y = y * U;
    s.renderOrder = 20;
    return s;
}

const basic = (color: number, opacity = 1): THREE.MeshBasicMaterial =>
    new THREE.MeshBasicMaterial({ color, transparent: opacity < 1, opacity, depthWrite: opacity >= 1 });

function disposeTree(root: THREE.Object3D): void {
    root.traverse((o) => {
        const m = o as THREE.Mesh;
        m.geometry?.dispose();
        const mat = m.material as THREE.Material | THREE.Material[] | undefined;
        if (Array.isArray(mat)) mat.forEach((x) => x.dispose());
        else mat?.dispose();
    });
}

/** An invisible, slightly fat hit target (raycast needs a real mesh). */
function hit(geo: THREE.BufferGeometry, pick: Pick): THREE.Mesh {
    const m = new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ transparent: true, opacity: 0, depthWrite: false }));
    m.userData.pick = pick;
    return m;
}

function spawnObject(s: ArenaDraft['spawns'][number], index: number, selected: boolean): THREE.Group {
    const g = new THREE.Group();
    const col = teamColor(s.team);
    const disc = new THREE.Mesh(new THREE.CylinderGeometry(220 * U, 220 * U, 40 * U, 24), basic(col, 0.85));
    disc.position.y = 20 * U;
    const pillar = new THREE.Mesh(new THREE.CylinderGeometry(14 * U, 14 * U, 800 * U, 8), basic(col, 0.7));
    pillar.position.y = 400 * U;
    const arrow = new THREE.Group();
    const shaft = new THREE.Mesh(new THREE.BoxGeometry(50 * U, 30 * U, 300 * U), basic(0xffffff));
    shaft.position.set(0, 60 * U, 230 * U);
    const head = new THREE.Mesh(new THREE.ConeGeometry(110 * U, 200 * U, 4), basic(0xffffff));
    head.rotation.x = Math.PI / 2;
    head.position.set(0, 60 * U, 480 * U);
    arrow.add(shaft, head);
    g.add(disc, pillar, arrow, label(String(index + 1), col, 980));
    if (selected) {
        const ring = new THREE.Mesh(
            new THREE.RingGeometry(260 * U, 300 * U, 32),
            new THREE.MeshBasicMaterial({ color: 0xffffff, side: THREE.DoubleSide }),
        );
        ring.rotation.x = -Math.PI / 2;
        ring.position.y = 50 * U;
        g.add(ring);
    }
    g.add(
        hit(new THREE.CylinderGeometry(330 * U, 330 * U, 900 * U, 8), { kind: 'spawn', uid: s.uid }).translateY(
            450 * U,
        ),
    );
    g.position.set(s.pos[0] * U, s.pos[1] * U, s.pos[2] * U);
    g.userData.scalable = true;
    g.rotation.y = (s.yaw * Math.PI) / 180;
    // the label must not spin with the yaw: sprites ignore rotation, fine.
    return g;
}

function pickupObject(p: ArenaDraft['pickups'][number], index: number, selected: boolean): THREE.Group {
    const g = new THREE.Group();
    const adopt = !p.res;
    const col = adopt ? CYAN : YELLOW;
    const gem = new THREE.Mesh(
        new THREE.OctahedronGeometry(150 * U),
        adopt ? new THREE.MeshBasicMaterial({ color: col, wireframe: true }) : basic(col),
    );
    gem.position.y = 330 * U;
    const ring = new THREE.Mesh(
        new THREE.RingGeometry(170 * U, 230 * U, 28),
        new THREE.MeshBasicMaterial({ color: selected ? 0xffffff : col, side: THREE.DoubleSide }),
    );
    ring.rotation.x = -Math.PI / 2;
    ring.position.y = 30 * U;
    const stem = new THREE.Mesh(new THREE.CylinderGeometry(10 * U, 10 * U, 300 * U, 6), basic(col, 0.6));
    stem.position.y = 150 * U;
    g.add(gem, ring, stem, label(`P${index + 1}`, col, 700));
    g.add(hit(new THREE.SphereGeometry(300 * U, 8, 6), { kind: 'pickup', uid: p.uid }).translateY(330 * U));
    g.position.set(p.pos[0] * U, p.pos[1] * U, p.pos[2] * U);
    g.userData.scalable = true;
    return g;
}

function markerObject(m: ArenaDraft['markers'][number], index: number, selected: boolean): THREE.Group {
    const g = new THREE.Group();
    const col = m.team ? teamColor(m.team) : 0xf472b6;
    const r = m.radius * U;
    const ring = new THREE.Mesh(
        new THREE.RingGeometry(r * 0.9, r, 48),
        new THREE.MeshBasicMaterial({ color: selected ? 0xffffff : col, side: THREE.DoubleSide }),
    );
    ring.rotation.x = -Math.PI / 2;
    ring.position.y = 20 * U;
    g.add(ring);
    if (m.kind === 'flag') {
        const pole = new THREE.Mesh(new THREE.CylinderGeometry(18 * U, 18 * U, 800 * U, 8), basic(0xe5e7eb));
        pole.position.y = 400 * U;
        const cloth = new THREE.Mesh(
            new THREE.PlaneGeometry(420 * U, 260 * U),
            new THREE.MeshBasicMaterial({ color: col, side: THREE.DoubleSide }),
        );
        cloth.position.set(220 * U, 660 * U, 0);
        g.add(pole, cloth);
    } else if (m.kind === 'base') {
        const pad = new THREE.Mesh(new THREE.CylinderGeometry(r * 0.88, r * 0.88, 30 * U, 40), basic(col, 0.28));
        pad.position.y = 15 * U;
        const post = new THREE.Mesh(new THREE.CylinderGeometry(25 * U, 25 * U, 500 * U, 8), basic(col, 0.8));
        post.position.y = 250 * U;
        g.add(pad, post);
    } else {
        const zone = new THREE.Mesh(
            new THREE.CylinderGeometry(r, r, 400 * U, 48, 1, true),
            new THREE.MeshBasicMaterial({
                color: col,
                transparent: true,
                opacity: 0.2,
                side: THREE.DoubleSide,
                depthWrite: false,
            }),
        );
        zone.position.y = 200 * U;
        g.add(zone);
    }
    const tag = label(m.kind === 'flag' ? 'F' : m.kind === 'base' ? 'B' : 'Z', col, 1000);
    tag.userData.labelSprite = true;
    g.add(tag);
    g.add(
        hit(new THREE.CylinderGeometry(Math.min(r, 450 * U), Math.min(r, 450 * U), 900 * U, 10), {
            kind: 'marker',
            uid: m.uid,
        }).translateY(450 * U),
    );
    g.position.set(m.pos[0] * U, m.pos[1] * U, m.pos[2] * U);
    void index;
    return g;
}

/**
 * Viewport side of the multiplayer map mode: draws spawns, pickups, markers, the bounds (draped over the terrain) and
 * the objects the arena filter removes; picks them, moves them with a gizmo (snapped like the level objects) and draws
 * new ones with the placement tools.
 */
export class ArenaOverlay {
    private readonly root = new THREE.Group();
    private readonly items = new THREE.Group();
    private readonly gizmo: TransformControls;
    private readonly pivot = new THREE.Object3D();
    private boundsLine: THREE.LineSegments | null = null;
    private boundsFill: THREE.Mesh | null = null;
    private filterPoints: THREE.Points | null = null;
    private attached: ArenaSel = null;
    private dragging = false;
    private downAt: { x: number; y: number } | null = null;
    private drawStart: Vec3 | null = null;
    private plan: FilterPlan | null = null;
    private planPos = new Map<number, Vec3>();
    private groundSeen = false;
    private raf = 0;
    private rescaleRaf = 0;
    private lastCam = '';
    private readonly ray = new THREE.Raycaster();
    private readonly canvas: HTMLElement;
    private gizmoMode: 'translate' | 'rotate' = 'translate';

    constructor(
        private readonly view: LevelView,
        private readonly st: ArenaStore,
    ) {
        this.canvas = view.renderer.domElement;
        this.root.visible = false;
        this.root.add(this.items, this.pivot);
        view.scene.add(this.root);
        this.gizmo = new TransformControls(view.camera, this.canvas);
        this.gizmo.size = 0.75;
        this.gizmo.enabled = false;
        const helper = this.gizmo.getHelper();
        helper.visible = false;
        view.scene.add(helper);
        this.gizmo.addEventListener('dragging-changed', (e) => {
            const on = Boolean((e as unknown as { value: boolean }).value);
            view.orbit.enabled = !on;
            this.dragging = on;
            if (on) st.beginLive();
            else st.endLive(this.dragLabel());
        });
        this.gizmo.addEventListener('objectChange', () => this.onGizmoMove());
        this.canvas.addEventListener('pointerdown', this.onDown);
        this.canvas.addEventListener('pointermove', this.onMove);
        this.canvas.addEventListener('pointerup', this.onUp);
        window.addEventListener('keydown', this.onKey);
        st.on((e) => {
            if (e === 'draft') this.sync();
            else if (e === 'selection') this.attach();
            else if (e === 'mode' || e === 'filter') this.setVisible();
            else if (e === 'tool') this.canvas.style.cursor = st.tool === 'select' ? '' : 'crosshair';
        });
        this.raf = window.setInterval(() => this.tick(), 800);
        const loop = (): void => {
            this.rescale();
            this.rescaleRaf = requestAnimationFrame(loop);
        };
        this.rescaleRaf = requestAnimationFrame(loop);
    }

    dispose(): void {
        window.clearInterval(this.raf);
        cancelAnimationFrame(this.rescaleRaf);
        window.removeEventListener('keydown', this.onKey);
        this.view.scene.remove(this.root, this.gizmo.getHelper());
        this.gizmo.dispose();
    }

    setGizmoMode(m: 'translate' | 'rotate'): void {
        this.gizmoMode = m;
        this.attach();
    }
    get mode(): 'translate' | 'rotate' {
        return this.gizmoMode;
    }

    /** Objects the filter removes (null clears them). */
    setPlan(plan: FilterPlan | null, positions: Map<number, Vec3>): void {
        this.plan = plan;
        this.planPos = positions;
        this.drawFilter();
    }

    private setVisible(): void {
        this.root.visible = this.st.active;
        if (this.filterPoints) this.filterPoints.visible = this.st.active && this.st.showFilter;
        if (!this.st.active) {
            this.gizmo.detach();
            this.gizmo.getHelper().visible = false;
            this.gizmo.enabled = false;
            this.canvas.style.cursor = '';
        } else {
            this.sync();
            this.attach();
        }
    }

    /**
     * Keep the markers readable at any zoom: spawn points, pickups and handles grow with the camera distance (never
     * below their real size), the letters of flags, bases and zones too. Runs every frame, does nothing while still.
     */
    private rescale(): void {
        if (!this.st.active) return;
        const cam = this.view.camera.position;
        const key = `${cam.x.toFixed(3)},${cam.y.toFixed(3)},${cam.z.toFixed(3)},${this.items.children.length}`;
        if (key === this.lastCam) return;
        this.lastCam = key;
        for (const o of this.items.children) {
            const d = cam.distanceTo(o.position);
            if (o.userData.scalable) o.scale.setScalar(Math.max(1, d * 0.075));
            else
                for (const c of o.children) {
                    if (c.userData.labelSprite) c.scale.setScalar(0.48 * Math.max(1, d * 0.06));
                }
        }
    }

    private tick(): void {
        if (!this.st.active) return;
        const has = this.view.hasGround;
        if (has !== this.groundSeen) {
            this.groundSeen = has;
            this.sync();
        }
    }

    private groundOr(x: number, z: number, y: number): number {
        return this.view.groundHeightAt(x, z) ?? y;
    }

    /** Rebuild everything the draft draws. Cheap: tens of objects. */
    sync(): void {
        if (!this.st.active) return;
        const d = this.st.draft;
        const sel = this.st.sel;
        disposeTree(this.items);
        this.items.clear();
        this.lastCam = '';
        d.spawns.forEach((s, i) => this.items.add(spawnObject(s, i, sel?.kind === 'spawn' && sel.uid === s.uid)));
        d.pickups.forEach((p, i) => this.items.add(pickupObject(p, i, sel?.kind === 'pickup' && sel.uid === p.uid)));
        d.markers.forEach((m, i) => this.items.add(markerObject(m, i, sel?.kind === 'marker' && sel.uid === m.uid)));
        this.drawBounds(d);
        this.attach(false);
    }

    private drawBounds(d: ArenaDraft): void {
        for (const o of [this.boundsLine, this.boundsFill]) {
            if (o) {
                this.root.remove(o);
                disposeTree(o);
            }
        }
        this.boundsLine = this.boundsFill = null;
        if (!d.bounds) return;
        const [ax, az, bx, bz] = d.bounds;
        const x0 = Math.min(ax, bx);
        const x1 = Math.max(ax, bx);
        const z0 = Math.min(az, bz);
        const z1 = Math.max(az, bz);
        const base = d.spawns[0]?.pos[1] ?? this.view.orbit.target.y / U;
        const pts: number[] = [];
        const edge = (xa: number, za: number, xb: number, zb: number): void => {
            let prev: Vec3 | null = null;
            for (let i = 0; i <= BOUNDS_SAMPLES; i++) {
                const t = i / BOUNDS_SAMPLES;
                const x = xa + (xb - xa) * t;
                const z = za + (zb - za) * t;
                const p: Vec3 = [x, this.groundOr(x, z, base) + 120, z];
                if (prev) pts.push(prev[0] * U, prev[1] * U, prev[2] * U, p[0] * U, p[1] * U, p[2] * U);
                prev = p;
            }
        };
        edge(x0, z0, x1, z0);
        edge(x1, z0, x1, z1);
        edge(x1, z1, x0, z1);
        edge(x0, z1, x0, z0);
        const geo = new THREE.BufferGeometry();
        geo.setAttribute('position', new THREE.Float32BufferAttribute(pts, 3));
        this.boundsLine = new THREE.LineSegments(
            geo,
            new THREE.LineBasicMaterial({ color: 0x60a5fa, depthTest: false, transparent: true }),
        );
        this.boundsLine.renderOrder = 10;
        this.root.add(this.boundsLine);
        // corner handles
        const corner = (x: number, z: number, c: 0 | 1): void => {
            const m = hit(new THREE.BoxGeometry(500 * U, 500 * U, 500 * U), { kind: 'bounds', corner: c });
            (m.material as THREE.MeshBasicMaterial).opacity = 0.9;
            (m.material as THREE.MeshBasicMaterial).color.set(
                this.st.sel?.kind === 'bounds' && this.st.sel.corner === c ? 0xffffff : 0x60a5fa,
            );
            (m.material as THREE.MeshBasicMaterial).depthTest = false;
            m.renderOrder = 11;
            m.position.set(x * U, (this.groundOr(x, z, base) + 250) * U, z * U);
            m.userData.scalable = true;
            this.items.add(m);
        };
        corner(x0, z0, 0);
        corner(x1, z1, 1);
        const fill = new THREE.Mesh(
            new THREE.PlaneGeometry((x1 - x0) * U, (z1 - z0) * U),
            new THREE.MeshBasicMaterial({
                color: 0x3b82f6,
                transparent: true,
                opacity: 0.05,
                depthWrite: false,
                side: THREE.DoubleSide,
            }),
        );
        fill.rotation.x = -Math.PI / 2;
        fill.position.set(((x0 + x1) / 2) * U, base * U, ((z0 + z1) / 2) * U);
        this.boundsFill = fill;
        this.root.add(fill);
    }

    private drawFilter(): void {
        if (this.filterPoints) {
            this.root.remove(this.filterPoints);
            disposeTree(this.filterPoints);
            this.filterPoints = null;
        }
        const plan = this.plan;
        if (!plan) return;
        const pos: number[] = [];
        const col: number[] = [];
        const c = new THREE.Color();
        for (const [id, e] of plan.entries) {
            const p = this.planPos.get(id);
            if (!p) continue;
            pos.push(p[0] * U, p[1] * U + 0.05, p[2] * U);
            c.set(e.kept ? KEEP_COLOR : e.reason === 'manual' ? MANUAL_COLOR : FILTER_COLOR);
            col.push(c.r, c.g, c.b);
        }
        const geo = new THREE.BufferGeometry();
        geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
        geo.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
        this.filterPoints = new THREE.Points(
            geo,
            new THREE.PointsMaterial({
                size: 7,
                sizeAttenuation: false,
                vertexColors: true,
                depthTest: false,
                transparent: true,
                opacity: 0.9,
            }),
        );
        this.filterPoints.renderOrder = 9;
        this.filterPoints.frustumCulled = false;
        this.filterPoints.visible = this.st.active && this.st.showFilter;
        this.root.add(this.filterPoints);
    }

    /** Where the gizmo sits and what it moves. */
    private attach(rebuild = true): void {
        void rebuild;
        const sel = this.st.sel;
        const pos = this.selectionPos();
        this.attached = sel;
        if (!sel || !pos || !this.st.active) {
            this.gizmo.detach();
            this.gizmo.getHelper().visible = false;
            this.gizmo.enabled = false;
            return;
        }
        if (!this.dragging) {
            this.pivot.position.set(pos[0] * U, pos[1] * U, pos[2] * U);
            const s = this.st.selected() as { yaw?: number } | null;
            this.pivot.rotation.set(0, ((s?.yaw ?? 0) * Math.PI) / 180, 0);
        }
        const rotate = this.gizmoMode === 'rotate' && sel.kind === 'spawn';
        this.gizmo.setMode(rotate ? 'rotate' : 'translate');
        this.gizmo.setSpace(rotate ? 'local' : 'world');
        this.gizmo.showX = !rotate;
        this.gizmo.showZ = !rotate;
        this.gizmo.showY = rotate || sel.kind !== 'bounds';
        const snap = this.view.snap;
        this.gizmo.setTranslationSnap(snap.grid ? snap.gridSize * U : null);
        this.gizmo.setRotationSnap(snap.angle ? (snap.angleStep * Math.PI) / 180 : null);
        this.gizmo.attach(this.pivot);
        this.gizmo.getHelper().visible = true;
        this.gizmo.enabled = true;
    }

    private selectionPos(): Vec3 | null {
        const sel = this.st.sel;
        if (!sel) return null;
        if (sel.kind === 'bounds') {
            const b = this.st.draft.bounds;
            if (!b) return null;
            const x = sel.corner === 0 ? Math.min(b[0], b[2]) : Math.max(b[0], b[2]);
            const z = sel.corner === 0 ? Math.min(b[1], b[3]) : Math.max(b[1], b[3]);
            return [x, this.groundOr(x, z, 0) + 250, z];
        }
        return this.st.selected()?.pos ?? null;
    }

    private dragLabel(): string {
        const s = this.st.sel;
        if (!s) return 'Edit map';
        if (s.kind === 'bounds') return 'Move bounds corner';
        const what = { spawn: 'spawn point', pickup: 'pickup', marker: 'marker' }[s.kind];
        return `${this.gizmoMode === 'rotate' ? 'Rotate' : 'Move'} ${what}`;
    }

    private onGizmoMove(): void {
        const sel = this.st.sel;
        if (!sel) return;
        const p: Vec3 = [this.pivot.position.x / U, this.pivot.position.y / U, this.pivot.position.z / U];
        this.st.liveEdit((d) => {
            if (sel.kind === 'bounds') {
                const b = d.bounds;
                if (!b) return;
                const lo = [Math.min(b[0], b[2]), Math.min(b[1], b[3])];
                const hi = [Math.max(b[0], b[2]), Math.max(b[1], b[3])];
                if (sel.corner === 0)
                    d.bounds = [Math.min(p[0], hi[0]! - 100), Math.min(p[2], hi[1]! - 100), hi[0]!, hi[1]!];
                else d.bounds = [lo[0]!, lo[1]!, Math.max(p[0], lo[0]! + 100), Math.max(p[2], lo[1]! + 100)];
                return;
            }
            const list = sel.kind === 'spawn' ? d.spawns : sel.kind === 'pickup' ? d.pickups : d.markers;
            const it = list.find((x) => x.uid === sel.uid);
            if (!it) return;
            if (this.gizmoMode === 'rotate' && sel.kind === 'spawn') {
                const yaw = ((((this.pivot.rotation.y * 180) / Math.PI) % 360) + 360) % 360;
                (it as ArenaDraft['spawns'][number]).yaw = Math.round(yaw * 10) / 10;
            } else it.pos = p;
        });
    }

    // ---- pointer ----

    private onKey = (e: KeyboardEvent): void => {
        if (!this.st.active) return;
        if (/^(INPUT|TEXTAREA|SELECT)$/.test((e.target as HTMLElement | null)?.tagName ?? '')) return;
        if (e.key === 'Escape' && this.st.tool !== 'select') {
            this.st.setTool('select');
            this.drawStart = null;
        } else if (e.key === 'Delete' && this.st.sel && this.st.sel.kind !== 'bounds' && !this.view.selection.size) {
            const sel = this.st.sel;
            this.st.commit(`Delete ${sel.kind}`, (d) => {
                const list = sel.kind === 'spawn' ? d.spawns : sel.kind === 'pickup' ? d.pickups : d.markers;
                const at = list.findIndex((x) => x.uid === sel.uid);
                if (at >= 0) list.splice(at, 1);
            });
            this.st.select(null);
        }
    };

    private onDown = (e: PointerEvent): void => {
        if (!this.st.active || e.button !== 0) return;
        this.downAt = { x: e.clientX, y: e.clientY };
        if (this.st.tool === 'bounds' && !e.altKey) {
            const g = this.view.groundAt(e);
            if (g) {
                this.drawStart = this.snapXZ(g);
                this.st.beginLive();
            }
        }
    };

    private onMove = (e: PointerEvent): void => {
        if (!this.st.active || !this.drawStart || !(e.buttons & 1)) return;
        const g = this.view.groundAt(e);
        if (!g) return;
        const s = this.snapXZ(g);
        const a = this.drawStart;
        this.st.liveEdit((d) => {
            d.bounds = [
                Math.min(a[0], s[0]),
                Math.min(a[2], s[2]),
                Math.max(a[0], s[0]) || a[0] + 100,
                Math.max(a[2], s[2]) || a[2] + 100,
            ];
        });
    };

    private onUp = (e: PointerEvent): void => {
        const d = this.downAt;
        this.downAt = null;
        if (!this.st.active || e.button !== 0 || !d) return;
        const click = Math.hypot(e.clientX - d.x, e.clientY - d.y) <= 4;
        if (this.drawStart) {
            this.drawStart = null;
            this.st.endLive('Draw bounds');
            if (!click) {
                this.view.select([]);
                return;
            }
        }
        if (this.dragging || this.gizmo.axis || !click || e.altKey) return;
        const tool = this.st.tool;
        if (tool !== 'select' && tool !== 'bounds') {
            const g = this.view.groundAt(e);
            if (g) this.place(tool, this.snapXZ(g));
            this.view.select([]);
            return;
        }
        const p = this.pickAt(e);
        if (p) {
            this.view.select([]);
            this.st.select(p);
        } else if (!this.view.selection.size && this.st.sel) this.st.select(null);
    };

    private snapXZ(p: Vec3): Vec3 {
        const snap = this.view.snap;
        const x = snap.grid ? Math.round(p[0] / snap.gridSize) * snap.gridSize : p[0];
        const z = snap.grid ? Math.round(p[2] / snap.gridSize) * snap.gridSize : p[2];
        return [x, this.groundOr(x, z, p[1]), z];
    }

    private place(tool: 'spawn' | 'pickup' | 'flag' | 'base' | 'zone', at: Vec3): void {
        const d = this.st.draft;
        const uid = d.nextUid;
        this.st.commit(`Place ${tool}`, (n) => {
            n.nextUid = uid + 1;
            if (tool === 'spawn') n.spawns.push({ uid, pos: at, yaw: 0, team: 0 });
            else if (tool === 'pickup') n.pickups.push({ uid, pos: at, respawn: 30, res: 0, label: '' });
            else
                n.markers.push({
                    uid,
                    kind: tool,
                    pos: at,
                    radius: tool === 'flag' ? 150 : 600,
                    team: 0,
                    label: '',
                });
        });
        this.st.select({ kind: tool === 'spawn' ? 'spawn' : tool === 'pickup' ? 'pickup' : 'marker', uid });
    }

    private pickAt(e: { clientX: number; clientY: number }): Pick | null {
        const r = this.canvas.getBoundingClientRect();
        this.ray.setFromCamera(
            new THREE.Vector2(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1),
            this.view.camera,
        );
        const targets: THREE.Object3D[] = [];
        this.items.traverse((o) => {
            if (o.userData.pick) targets.push(o);
        });
        const hits = this.ray.intersectObjects(targets, false);
        return (hits[0]?.object.userData.pick as Pick | undefined) ?? null;
    }

    rootObject(): THREE.Object3D {
        return this.root;
    }

    /** Move the camera so `p` (game units) is the orbit target, keeping the viewing direction. */
    frameAt(p: Vec3, dist = 5): void {
        const t = new THREE.Vector3(p[0] * U, p[1] * U, p[2] * U);
        const dir = this.view.camera.position.clone().sub(this.view.orbit.target);
        if (dir.lengthSq() < 1e-6) dir.set(0.4, 0.8, 0.6);
        dir.normalize();
        this.view.camera.position.copy(t).addScaledVector(dir, dist);
        this.view.orbit.target.copy(t);
        this.view.orbit.update();
    }

    /** Look straight down on the rectangle (minX, minZ, maxX, maxZ). */
    frameRect(b: readonly number[]): void {
        const cx = ((b[0]! + b[2]!) / 2) * U;
        const cz = ((b[1]! + b[3]!) / 2) * U;
        const size = Math.max(Math.abs(b[2]! - b[0]!), Math.abs(b[3]! - b[1]!)) * U;
        const y = (this.view.groundHeightAt(cx / U, cz / U) ?? this.view.orbit.target.y / U) * U;
        this.view.orbit.target.set(cx, y, cz);
        this.view.camera.position.set(cx, y + size * 0.95, cz + size * 0.12);
        this.view.orbit.update();
    }

    /** Selected (or any) item in world units for framing. */
    focus(sel: ArenaSel): Vec3 | null {
        const prev = this.st.sel;
        void prev;
        if (!sel) return null;
        if (sel.kind === 'bounds') return this.selectionPos();
        const d = this.st.draft;
        const list = sel.kind === 'spawn' ? d.spawns : sel.kind === 'pickup' ? d.pickups : d.markers;
        return list.find((x) => x.uid === sel.uid)?.pos ?? null;
    }

    reasonColor(reason: FilterReason, kept: boolean): number {
        void FILTER_REASONS;
        return kept ? KEEP_COLOR : reason === 'manual' ? MANUAL_COLOR : FILTER_COLOR;
    }
}

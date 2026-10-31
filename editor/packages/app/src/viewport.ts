import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import type { TextureResult } from './rpc.ts';
import { onThemeChange } from './editor/theme.ts';

export interface QuadInfo {
    index: number;
    label: string;
}

const MAX_QUADS = 400;
const GAP = 8;
/** Pointer travel (CSS px) above which a press is a camera drag rather than a pick. */
const DRAG_PX = 4;

/** three.js viewport showing decoded textures as textured quads on a grid, with orbit controls. */
export class TextureViewport {
    private readonly renderer: THREE.WebGLRenderer;
    private readonly scene = new THREE.Scene();
    private readonly camera = new THREE.PerspectiveCamera(45, 1, 0.1, 100000);
    private readonly controls: OrbitControls;
    private readonly group = new THREE.Group();
    private readonly raycaster = new THREE.Raycaster();
    private extent = new THREE.Box3();
    private hovered: THREE.Mesh | null = null;
    private raf = 0;
    private downAt: { x: number; y: number } | null = null;
    private readonly outline = new THREE.LineSegments(
        new THREE.EdgesGeometry(new THREE.PlaneGeometry(1, 1)),
        new THREE.LineBasicMaterial({ color: 0xffb020 }),
    );

    onHover: (info: QuadInfo | null) => void = () => {};
    onPick: (info: QuadInfo) => void = () => {};

    constructor(private readonly host: HTMLElement) {
        this.renderer = new THREE.WebGLRenderer({ antialias: true });
        host.appendChild(this.renderer.domElement);
        this.scene.add(this.group);
        this.outline.visible = false;
        this.scene.add(this.outline);
        this.controls = new OrbitControls(this.camera, this.renderer.domElement);
        this.controls.enableDamping = true;
        this.controls.screenSpacePanning = true;
        this.controls.mouseButtons = { LEFT: THREE.MOUSE.PAN, MIDDLE: THREE.MOUSE.DOLLY, RIGHT: THREE.MOUSE.ROTATE };
        this.applyTheme();
        onThemeChange(() => this.applyTheme());
        new ResizeObserver(() => this.resize()).observe(host);
        this.renderer.domElement.addEventListener('pointermove', (e) => this.pointer(e, false));
        this.renderer.domElement.addEventListener('pointerdown', (e) => {
            this.downAt = { x: e.clientX, y: e.clientY };
        });
        this.renderer.domElement.addEventListener('click', (e) => {
            const d = this.downAt;
            this.downAt = null;
            // A left-button pan ends with a click on the same element; only a press-and-release in place picks.
            if (d && Math.hypot(e.clientX - d.x, e.clientY - d.y) > DRAG_PX) return;
            this.pointer(e, true);
        });
        this.resize();
        this.reset();
        const tick = (): void => {
            this.raf = requestAnimationFrame(tick);
            this.controls.update();
            this.renderer.render(this.scene, this.camera);
        };
        tick();
    }

    private applyTheme(): void {
        const light = document.documentElement.dataset.theme === 'light';
        this.scene.background = new THREE.Color(light ? 0xd9dbe0 : 0x121214);
    }

    private resize(): void {
        const w = Math.max(this.host.clientWidth, 1);
        const h = Math.max(this.host.clientHeight, 1);
        this.renderer.setPixelRatio(window.devicePixelRatio);
        this.renderer.setSize(w, h, false);
        this.camera.aspect = w / h;
        this.camera.updateProjectionMatrix();
    }

    private pointer(e: PointerEvent | MouseEvent, click: boolean): void {
        const r = this.renderer.domElement.getBoundingClientRect();
        const ndc = new THREE.Vector2(
            ((e.clientX - r.left) / r.width) * 2 - 1,
            -((e.clientY - r.top) / r.height) * 2 + 1,
        );
        this.raycaster.setFromCamera(ndc, this.camera);
        const hit = this.raycaster.intersectObjects(this.group.children, false)[0]?.object as THREE.Mesh | undefined;
        const info = hit ? (hit.userData as QuadInfo) : null;
        if (click) {
            if (info) this.onPick(info);
        } else if (hit !== this.hovered) {
            this.hovered = hit ?? null;
            this.renderer.domElement.style.cursor = hit ? 'pointer' : 'default';
            this.onHover(info);
        }
    }

    clear(): void {
        for (const o of [...this.group.children] as THREE.Mesh[]) {
            (o.material as THREE.MeshBasicMaterial).map?.dispose();
            (o.material as THREE.Material).dispose();
            o.geometry.dispose();
            this.group.remove(o);
        }
        this.hovered = null;
        this.outline.visible = false;
    }

    /** Lay textures out in rows of roughly square overall shape. */
    setTextures(textures: TextureResult[]): number {
        this.clear();
        const ok = textures.filter((t) => t.rgba && t.width && t.height).slice(0, MAX_QUADS);
        const total = ok.reduce((s, t) => s + t.width! * t.height!, 0);
        const rowWidth = Math.max(Math.sqrt(total) * 1.4, ...ok.map((t) => t.width!));
        let x = 0;
        let y = 0;
        let rowH = 0;
        for (const t of ok) {
            const w = t.width!;
            const h = t.height!;
            if (x > 0 && x + w > rowWidth) {
                x = 0;
                y -= rowH + GAP;
                rowH = 0;
            }
            const tex = new THREE.DataTexture(t.rgba!, w, h, THREE.RGBAFormat);
            tex.colorSpace = THREE.SRGBColorSpace;
            tex.magFilter = THREE.NearestFilter;
            tex.minFilter = THREE.LinearFilter;
            tex.generateMipmaps = false;
            tex.needsUpdate = true;
            const mesh = new THREE.Mesh(
                new THREE.PlaneGeometry(w, h),
                new THREE.MeshBasicMaterial({ map: tex, transparent: true, side: THREE.DoubleSide }),
            );
            mesh.position.set(x + w / 2, y - h / 2, 0);
            mesh.userData = {
                index: t.index,
                label: `id ${t.id} sub ${t.sub}  ${w}x${h}  ${t.bpp} bpp`,
            } satisfies QuadInfo;
            this.group.add(mesh);
            x += w + GAP;
            rowH = Math.max(rowH, h);
        }
        this.extent = new THREE.Box3().setFromObject(this.group);
        this.reset();
        return ok.length;
    }

    /** Outline the quad for SRSC entry `index` and frame it; false when that texture is not on screen. */
    focus(index: number): boolean {
        const mesh = (this.group.children as THREE.Mesh[]).find((m) => (m.userData as QuadInfo).index === index);
        if (!mesh) return false;
        const p = (mesh.geometry as THREE.PlaneGeometry).parameters;
        this.outline.position.set(mesh.position.x, mesh.position.y, 0.5);
        this.outline.scale.set(p.width + 4, p.height + 4, 1);
        this.outline.visible = true;
        this.frame(new THREE.Box3().setFromObject(mesh).expandByScalar(Math.max(p.width, p.height) * 0.5));
        return true;
    }

    reset(): void {
        this.frame(
            this.extent.isEmpty()
                ? new THREE.Box3(new THREE.Vector3(-64, -64, 0), new THREE.Vector3(64, 64, 0))
                : this.extent,
        );
    }

    private frame(box: THREE.Box3): void {
        const c = box.getCenter(new THREE.Vector3());
        const s = box.getSize(new THREE.Vector3());
        const fov = THREE.MathUtils.degToRad(this.camera.fov);
        const dist = Math.max(s.y / 2 / Math.tan(fov / 2), s.x / 2 / Math.tan(fov / 2) / this.camera.aspect) * 1.1 + 1;
        this.camera.position.set(c.x, c.y, dist);
        this.controls.target.copy(c);
        this.controls.update();
    }

    dispose(): void {
        cancelAnimationFrame(this.raf);
        this.clear();
        this.controls.dispose();
        this.renderer.dispose();
    }
}

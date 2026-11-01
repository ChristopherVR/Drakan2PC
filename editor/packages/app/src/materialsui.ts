/**
 * Files tab "Materials" view: load a PBR material folder written by analysis/scripts/pbr_pack.py (M_*.json + T_*.png)
 * and judge each material: the three maps side by side and a lit sphere / plane (three.js MeshStandardMaterial with a
 * room environment). Everything stays in the browser; the pictures derive from the player's disc and are never stored.
 */
import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { RoomEnvironment } from 'three/examples/jsm/environments/RoomEnvironment.js';
import { baseName, parseMaterial, standardSettings, type MaterialDesc } from './materialpack.ts';

type Shape = 'sphere' | 'plane';

const $ = <T extends HTMLElement>(id: string): T => {
    const el = document.getElementById(id);
    if (!el) throw new Error(`missing #${id}`);
    return el as T;
};

export function initMaterialsUi(): void {
    const toggle = $<HTMLButtonElement>('fl-materials');
    const cols = document.querySelector<HTMLElement>('.fl-cols')!;
    const pane = $('mat-pane');
    const input = $<HTMLInputElement>('mat-input');
    const list = $('mat-list');
    const filter = $<HTMLInputElement>('mat-filter');
    const info = $('mat-info');
    const maps = $('mat-maps');
    const stage = $('mat-stage');
    const shapeSel = $<HTMLSelectElement>('mat-shape');
    const lightSlider = $<HTMLInputElement>('mat-light');
    const useNormal = $<HTMLInputElement>('mat-use-normal');
    const useOrm = $<HTMLInputElement>('mat-use-orm');
    const empty = $('mat-empty');

    const files = new Map<string, File>();
    const mats: MaterialDesc[] = [];
    const urls = new Map<string, string>();
    let current: MaterialDesc | null = null;
    let three: ReturnType<typeof makeStage> | null = null;
    let textures: THREE.Texture[] = [];

    toggle.onclick = () => {
        const on = Boolean(pane.hidden);
        pane.hidden = !on;
        cols.hidden = on;
        toggle.classList.toggle('active', on);
        toggle.setAttribute('aria-pressed', String(on));
        if (on) ensureStage().resize();
    };

    async function load(fl: FileList | File[]): Promise<void> {
        files.clear();
        mats.length = 0;
        for (const u of urls.values()) URL.revokeObjectURL(u);
        urls.clear();
        for (const f of Array.from(fl)) files.set(f.name, f);
        const jsons = [...files.values()]
            .filter((f) => /^M_.*\.json$/.test(f.name))
            .sort((a, b) => a.name.localeCompare(b.name));
        for (const f of jsons) {
            try {
                mats.push(parseMaterial(await f.text()));
            } catch {
                /* foreign files are skipped */
            }
        }
        info.textContent = `${mats.length} materials`;
        empty.hidden = mats.length > 0;
        renderList();
        if (mats[0]) select(mats[0]);
    }

    function renderList(): void {
        const q = filter.value.trim().toLowerCase();
        list.replaceChildren();
        for (const m of mats) {
            if (q && !m.name.toLowerCase().includes(q)) continue;
            const li = document.createElement('li');
            li.dataset.name = m.name;
            li.className = m === current ? 'sel' : '';
            li.textContent = m.name;
            const tag = document.createElement('small');
            tag.textContent = m.blendMode === 'Opaque' ? '' : m.blendMode;
            li.append(tag);
            li.onclick = () => select(m);
            list.append(li);
        }
    }

    function url(path: string | null): string | null {
        if (!path) return null;
        const f = files.get(baseName(path));
        if (!f) return null;
        let u = urls.get(f.name);
        if (!u) urls.set(f.name, (u = URL.createObjectURL(f)));
        return u;
    }

    function mapCard(title: string, path: string | null): HTMLElement {
        const card = document.createElement('figure');
        const u = url(path);
        if (u) {
            const img = document.createElement('img');
            img.src = u;
            img.alt = title;
            card.append(img);
        } else {
            const none = document.createElement('div');
            none.className = 'mat-none';
            none.textContent = 'missing';
            card.append(none);
        }
        const cap = document.createElement('figcaption');
        cap.textContent = title;
        card.append(cap);
        return card;
    }

    function select(m: MaterialDesc): void {
        current = m;
        for (const li of list.querySelectorAll('li')) li.classList.toggle('sel', li.dataset.name === m.name);
        maps.replaceChildren(
            mapCard('BaseColor (sRGB)', m.baseColor),
            mapCard('Normal (DirectX)', m.normal),
            mapCard('ORM (R AO, G Rough, B Metal)', m.orm),
        );
        const flags = [
            m.blendMode,
            m.twoSided ? 'two-sided' : 'one-sided',
            m.unlit ? 'unlit' : 'lit',
            m.hd ? 'HD' : 'source res',
        ];
        $('mat-title').textContent = m.name;
        $('mat-flags').textContent =
            `${m.size[0]}x${m.size[1]}  ${flags.join(', ')}  tiling ${m.uvTiling.join('x')}  roughness ${m.roughnessMean}`;
        $('mat-sources').textContent = m.sources.join('\n');
        void applyToStage();
    }

    function ensureStage(): ReturnType<typeof makeStage> {
        if (!three) three = makeStage(stage);
        return three;
    }

    function loadTex(u: string | null, srgb: boolean, m: MaterialDesc): Promise<THREE.Texture | null> {
        if (!u) return Promise.resolve(null);
        return new Promise((resolve) => {
            new THREE.TextureLoader().load(
                u,
                (t) => {
                    t.colorSpace = srgb ? THREE.SRGBColorSpace : THREE.NoColorSpace;
                    t.wrapS = t.wrapT = THREE.RepeatWrapping;
                    t.repeat.set(m.uvTiling[0], m.uvTiling[1]);
                    t.anisotropy = 8;
                    resolve(t);
                },
                undefined,
                () => resolve(null),
            );
        });
    }

    async function applyToStage(): Promise<void> {
        const m = current;
        if (!m) return;
        const st = ensureStage();
        const [bc, nrm, orm] = await Promise.all([
            loadTex(url(m.baseColor), true, m),
            useNormal.checked ? loadTex(url(m.normal), false, m) : Promise.resolve(null),
            useOrm.checked ? loadTex(url(m.orm), false, m) : Promise.resolve(null),
        ]);
        if (m !== current) return;
        for (const t of textures) t.dispose();
        textures = [bc, nrm, orm].filter((t): t is THREE.Texture => !!t);
        const s = standardSettings(m);
        const mat = new THREE.MeshStandardMaterial({
            map: bc,
            normalMap: nrm,
            aoMap: orm,
            roughnessMap: orm,
            metalnessMap: orm,
            roughness: 1,
            metalness: orm ? 1 : 0,
            transparent: s.transparent,
            alphaTest: s.alphaTest,
            side: s.side === 'double' ? THREE.DoubleSide : THREE.FrontSide,
            blending: s.blending === 'additive' ? THREE.AdditiveBlending : THREE.NormalBlending,
            depthWrite: s.depthWrite,
        });
        if (!orm) mat.roughness = m.roughnessMean;
        mat.normalScale.set(s.normalScale[0], s.normalScale[1]);
        if (s.emissiveIntensity > 0 && bc) {
            mat.emissiveMap = bc;
            mat.emissive.set(0xffffff);
            mat.emissiveIntensity = s.emissiveIntensity;
        }
        st.setMaterial(mat, shapeSel.value as Shape);
    }

    input.onchange = () => {
        if (input.files?.length) load(input.files).catch(() => (info.textContent = 'could not read the folder'));
    };
    $('mat-open').onclick = () => input.click();
    filter.oninput = renderList;
    shapeSel.onchange = () => void applyToStage();
    useNormal.onchange = () => void applyToStage();
    useOrm.onchange = () => void applyToStage();
    lightSlider.oninput = () => ensureStage().setLight(Number(lightSlider.value));
    pane.addEventListener('dragover', (e) => e.preventDefault());
    pane.addEventListener('drop', (e) => {
        e.preventDefault();
        if (e.dataTransfer?.files.length) load(e.dataTransfer.files).catch(() => undefined);
    });
    new ResizeObserver(() => three?.resize()).observe(stage);
}

function makeStage(host: HTMLElement) {
    const renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
    renderer.setPixelRatio(Math.min(2, window.devicePixelRatio));
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    host.append(renderer.domElement);
    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x202024);
    const pmrem = new THREE.PMREMGenerator(renderer);
    scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
    scene.environmentIntensity = 0.55;
    const camera = new THREE.PerspectiveCamera(40, 1, 0.1, 50);
    camera.position.set(0, 0.2, 3.2);
    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    const key = new THREE.DirectionalLight(0xffffff, 2.4);
    scene.add(key, new THREE.AmbientLight(0xffffff, 0.15));
    const setLight = (deg: number): void => {
        const r = (deg * Math.PI) / 180;
        key.position.set(Math.cos(r) * 3, 2.2, Math.sin(r) * 3);
    };
    setLight(40);
    let mesh: THREE.Mesh | null = null;
    const geo: Record<Shape, THREE.BufferGeometry> = {
        sphere: new THREE.SphereGeometry(1, 96, 64),
        plane: new THREE.PlaneGeometry(2, 2, 1, 1),
    };
    const setMaterial = (mat: THREE.MeshStandardMaterial, shape: Shape): void => {
        if (!mesh) {
            mesh = new THREE.Mesh(geo[shape], mat);
            scene.add(mesh);
        } else {
            (mesh.material as THREE.Material).dispose();
            mesh.geometry = geo[shape];
            mesh.material = mat;
        }
    };
    const resize = (): void => {
        const w = host.clientWidth;
        const h = host.clientHeight;
        if (w < 2 || h < 2) return;
        renderer.setSize(w, h);
        camera.aspect = w / h;
        camera.updateProjectionMatrix();
    };
    const loop = (): void => {
        controls.update();
        renderer.render(scene, camera);
        requestAnimationFrame(loop);
    };
    resize();
    loop();
    return { setMaterial, setLight, resize };
}

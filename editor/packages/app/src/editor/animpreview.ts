import * as THREE from 'three';
import { clipLoops, clipFitsRig, rootMotion, type Ps2Anim } from '@drakan/formats';
import { isArrayType, typeCode, type ClassSchema, type LevelObject, type SchemaProvider } from '@drakan/editor-core';
import type { RefService } from '../refs.ts';
import { ClipClock, SkinnedModel } from '../scene/skinned.ts';
import { fill, h, icon } from './dom.ts';
import './animpreview.css';

/**
 * Skinned animation preview: a small orbit viewport that plays one clip on one model (play / pause / scrub / speed),
 * shared by the animation picker, the Details panel ("Animation" section) and the file inspector. The model is deformed
 * on the CPU (`SkinnedModel`); the clip is decoded in the worker (`animdata.ts`). One WebGL renderer is shared by every
 * preview and copied into a 2D canvas, so opening pickers does not use up GL contexts.
 */

export interface ModelRef {
    stem: string;
    id: number;
}
export interface ClipRef {
    stem: string;
    id: number;
}

export interface AnimPreview {
    el: HTMLElement;
    /** Show model `m` (null: clear). Resolves when it is on screen. */
    setModel(m: ModelRef | null): Promise<void>;
    /** Play clip `c` on the current model (null: rest pose). */
    setClip(c: ClipRef | null): Promise<void>;
    /** Text under the viewport: what is playing or why it cannot. */
    status(): string;
    dispose(): void;
}

let shared: THREE.WebGLRenderer | null = null;
const sharedRenderer = (): THREE.WebGLRenderer =>
    (shared ??= new THREE.WebGLRenderer({ antialias: true, alpha: true }));

const SPEEDS = [0.25, 0.5, 1, 2];
const fmtT = (s: number): string => s.toFixed(2);

function pauseIcon(size: number): SVGElement {
    const svg = icon('play', size);
    svg.innerHTML = '<path d="M8 5v14M16 5v14"/>';
    return svg;
}

export function createAnimPreview(refs: RefService, opts: { height?: number; compact?: boolean } = {}): AnimPreview {
    const height = opts.height ?? 200;
    const canvas = h('canvas', { class: 'ap-canvas', width: 320, height, 'aria-label': 'Animation preview' });
    canvas.style.height = `${height}px`;
    const overlay = h('div', { class: 'ap-overlay dim' });
    const stage = h('div', { class: 'ap-stage' }, canvas, overlay);
    const playBtn = h('button', {
        class: 'tool sm',
        type: 'button',
        title: 'Play / pause (Space)',
        'aria-label': 'Play or pause',
    });
    const scrub = h('input', {
        type: 'range',
        class: 'ap-scrub',
        min: '0',
        max: '1000',
        value: '0',
        'aria-label': 'Animation time',
    });
    const timeLab = h('span', { class: 'ap-time mono dim' }, '0.00 / 0.00 s');
    const speed = h(
        'select',
        { class: 'sel ap-speed', 'aria-label': 'Playback speed' },
        ...SPEEDS.map((s) => h('option', { value: String(s) }, `${s}x`)),
    );
    speed.value = '1';
    const inPlace = h('input', { type: 'checkbox', checked: true, 'aria-label': 'Play in place' });
    const statusEl = h('div', { class: 'ap-status dim' });
    const rootEl = h(
        'div',
        { class: 'ap-root' },
        stage,
        h('div', { class: 'ap-bar' }, playBtn, scrub, timeLab),
        opts.compact
            ? null
            : h(
                  'div',
                  { class: 'ap-bar2' },
                  h(
                      'label',
                      { class: 'ap-check', title: 'Remove the root joint travel so the character stays put' },
                      inPlace,
                      'In place',
                  ),
                  h('span', { class: 'spacer' }),
                  speed,
              ),
        statusEl,
    );

    // ---- state
    let skinned: SkinnedModel | null = null;
    let clip: Ps2Anim | null = null;
    let fits = true;
    const clock = new ClipClock(0);
    let disposed = false;
    let modelKey = '';
    let clipKey = '';
    let msg = '';
    let seq = 0;
    const scene = new THREE.Scene();
    scene.add(new THREE.HemisphereLight(0xffffff, 0x445055, 1.7));
    const sun = new THREE.DirectionalLight(0xffffff, 1.5);
    sun.position.set(1, 2, 1.5);
    scene.add(sun);
    const grid = new THREE.GridHelper(1, 8, 0x555a63, 0x30343a);
    scene.add(grid);
    const cam = new THREE.PerspectiveCamera(35, 1, 0.001, 100);
    let yaw = -0.7;
    let pitch = 0.25;
    let dist = 1;
    const target = new THREE.Vector3();
    let radius = 1;

    const paintPlay = (): void => {
        playBtn.replaceChildren(clock.playing ? pauseIcon(12) : icon('play', 12));
        playBtn.classList.toggle('on', clock.playing);
    };
    paintPlay();
    const setStatus = (s: string): void => {
        msg = s;
        statusEl.textContent = s;
    };
    const loops = (): boolean => (clip ? clipLoops(clip) : true);
    const applyPose = (): void => {
        if (!skinned) return;
        skinned.setClip(fits ? clip : null, clock.time, { inPlace: inPlace.checked });
    };

    playBtn.addEventListener('click', () => {
        if (clock.finished(loops())) clock.time = 0;
        clock.playing = !clock.playing;
        paintPlay();
    });
    scrub.addEventListener('input', () => {
        clock.playing = false;
        paintPlay();
        clock.time = (Number(scrub.value) / 1000) * clock.duration;
        applyPose();
    });
    speed.addEventListener('change', () => (clock.speed = Number(speed.value)));
    inPlace.addEventListener('change', applyPose);
    rootEl.addEventListener('keydown', (e) => {
        if (e.key === ' ' && e.target === rootEl) {
            e.preventDefault();
            playBtn.click();
        }
    });

    // ---- orbit
    let drag: { x: number; y: number } | null = null;
    canvas.addEventListener('pointerdown', (e) => {
        drag = { x: e.clientX, y: e.clientY };
        canvas.setPointerCapture(e.pointerId);
    });
    canvas.addEventListener('pointermove', (e) => {
        if (!drag) return;
        yaw -= (e.clientX - drag.x) * 0.01;
        pitch = Math.max(-1.2, Math.min(1.4, pitch + (e.clientY - drag.y) * 0.01));
        drag = { x: e.clientX, y: e.clientY };
    });
    const endDrag = (): void => void (drag = null);
    canvas.addEventListener('pointerup', endDrag);
    canvas.addEventListener('pointercancel', endDrag);
    canvas.addEventListener(
        'wheel',
        (e) => {
            e.preventDefault();
            dist = Math.max(0.4, Math.min(4, dist * (e.deltaY > 0 ? 1.1 : 0.9)));
        },
        { passive: false },
    );

    const frameModel = (): void => {
        if (!skinned) return;
        const b = skinned.bounds();
        const sphere = b.getBoundingSphere(new THREE.Sphere());
        radius = Math.max(sphere.radius, 1e-4);
        target.copy(sphere.center);
        grid.scale.setScalar(radius * 2.4);
        grid.position.set(sphere.center.x, b.min.y, sphere.center.z);
        dist = 1;
    };

    // ---- loop
    let last = performance.now();
    let idleFrames = 0;
    let wasConnected = false;
    const frame = (now: number): void => {
        if (disposed) return;
        requestAnimationFrame(frame);
        const dt = Math.min(0.1, (now - last) / 1000);
        last = now;
        if (rootEl.isConnected) wasConnected = true;
        else if (wasConnected || ++idleFrames > 120) return dispose();
        if (!rootEl.isConnected || !canvas.offsetParent) return;
        if (skinned && clip && fits) {
            const was = clock.time;
            clock.tick(dt, loops());
            if (clock.playing || was !== clock.time) {
                applyPose();
                if (clock.finished(loops()) && clock.playing) {
                    clock.playing = false;
                    paintPlay();
                }
            }
        }
        scrub.value = clock.duration > 0 ? String(Math.round((clock.time / clock.duration) * 1000)) : '0';
        timeLab.textContent = `${fmtT(clock.time)} / ${fmtT(clock.duration)} s`;
        render();
    };
    const render = (): void => {
        const w = Math.max(64, Math.round((canvas.clientWidth || 320) * Math.min(2, window.devicePixelRatio || 1)));
        const hgt = Math.max(64, Math.round(height * Math.min(2, window.devicePixelRatio || 1)));
        if (canvas.width !== w) canvas.width = w;
        if (canvas.height !== hgt) canvas.height = hgt;
        const g = canvas.getContext('2d');
        if (!g) return;
        const r = sharedRenderer();
        r.setSize(w, hgt, false);
        r.setClearColor(0x000000, 0);
        cam.aspect = w / hgt;
        const d = (radius / Math.sin((cam.fov * Math.PI) / 360)) * dist * 1.05;
        cam.position.set(
            target.x + d * Math.cos(pitch) * Math.sin(yaw),
            target.y + d * Math.sin(pitch),
            target.z + d * Math.cos(pitch) * Math.cos(yaw),
        );
        cam.near = d * 0.02;
        cam.far = d * 20;
        cam.updateProjectionMatrix();
        cam.lookAt(target);
        r.render(scene, cam);
        g.clearRect(0, 0, w, hgt);
        g.drawImage(r.domElement, 0, 0, w, hgt);
    };
    requestAnimationFrame(frame);

    function dispose(): void {
        if (disposed) return;
        disposed = true;
        skinned?.dispose();
        skinned = null;
        rootEl.remove();
    }

    async function setModel(m: ModelRef | null): Promise<void> {
        const key = m ? `${m.stem}:${m.id}` : '';
        if (key === modelKey) return;
        modelKey = key;
        const mine = ++seq;
        if (skinned) {
            scene.remove(skinned.mesh);
            skinned.dispose();
            skinned = null;
        }
        overlay.textContent = m ? 'Loading model...' : 'No model';
        if (!m) return;
        const r = await refs.rig(m.stem, m.id);
        if (mine !== seq || disposed) return;
        if (!r?.data.model) return void (overlay.textContent = 'Model could not be loaded');
        if (!r.rig) return void (overlay.textContent = 'This model has no skeleton');
        skinned = new SkinnedModel(r.data.model, r.rig, r.lookup);
        scene.add(skinned.mesh);
        overlay.textContent = '';
        frameModel();
        evaluateFit();
        applyPose();
    }

    function evaluateFit(): void {
        if (!skinned) return;
        if (!clip) return setStatus(clipKey ? 'Loading clip...' : 'Rest pose');
        fits = clipFitsRig(skinned.rig, clip);
        if (!fits) {
            setStatus(
                `"${clip.name}" has ${clip.tracks.length} tracks; this skeleton has ${skinned.rig.jointCount} joints.`,
            );
            return;
        }
        const rm = rootMotion(clip, skinned.rig);
        const travel = Math.hypot(...rm.delta) * 2048;
        setStatus(
            `${clip.name}  ${clip.duration.toFixed(2)} s${clipLoops(clip) ? ', loops' : ', plays once'}${
                travel > 20 ? `, root travels ${Math.round(travel)} units` : ''
            }`,
        );
    }

    async function setClip(c: ClipRef | null): Promise<void> {
        const key = c ? `${c.stem}:${c.id}` : '';
        if (key === clipKey && (clip || !c)) return;
        clipKey = key;
        const mine = ++seq;
        clip = null;
        fits = true;
        clock.time = 0;
        clock.duration = 0;
        if (!c) {
            setStatus('Rest pose');
            applyPose();
            return;
        }
        setStatus('Loading clip...');
        const a = await refs.clip(c.stem, c.id);
        if (mine !== seq || disposed) return;
        if (!a) {
            setStatus(`Clip #${c.id} not found in ${c.stem}.ADB`);
            applyPose();
            return;
        }
        clip = a;
        clock.duration = a.duration;
        clock.time = 0;
        clock.playing = true;
        paintPlay();
        evaluateFit();
        applyPose();
    }

    return { el: rootEl, setModel, setClip, status: () => msg, dispose };
}

// ---------------------------------------------------------------- the class of an object

export interface ClassClip {
    stem: string;
    id: number;
    /** Field name(s) of the class that use it (`Walk`, `Base Idle`), or the clip's own name for the model's list. */
    label: string;
}

/** Clips the class names in its animation fields (object overrides first), with the field names. */
export function classClips(provider: SchemaProvider, schema: ClassSchema, o: LevelObject | null): ClassClip[] {
    const out = new Map<string, ClassClip>();
    for (const f of schema.fields) {
        if (typeCode(f.type) !== 9) continue;
        const over = o?.fields?.find((x) => x.fieldIndex === f.index);
        const v = over ? over.value : f.default;
        const vals = Array.isArray(v) ? v : typeof v === 'number' ? [v] : [];
        vals.forEach((n, i) => {
            if (!n || n === 0xffffffff) return;
            const t = provider.resolveRef(schema, f.type, n >>> 0);
            if (!t.stem) return;
            const key = `${t.stem}:${t.id}`;
            const label = isArrayType(f.type) && vals.length > 1 ? `${f.name} ${i + 1}` : f.name;
            const had = out.get(key);
            if (had) {
                if (!had.label.split(', ').includes(label)) had.label += `, ${label}`;
            } else out.set(key, { stem: t.stem, id: t.id, label });
        });
    }
    return [...out.values()];
}

/** The model of a class together with the ADB its listed clips live in. */
export async function classModelOf(refs: RefService, schema: ClassSchema | null): Promise<ModelRef | null> {
    if (!schema) return null;
    return refs.classModel(schema.db, schema.classId);
}

/** Idle first: a class field called "Base Idle" / "Idle", then any clip named idle, else the first. */
export function pickIdle<T extends { label?: string; name?: string }>(clips: readonly T[]): T | null {
    const text = (c: T): string => `${c.label ?? ''} ${c.name ?? ''}`.toLowerCase();
    return (
        clips.find((c) => /base idle/.test(text(c))) ??
        clips.find((c) => /\bidle\b/.test(text(c))) ??
        clips.find((c) => /idle/.test(text(c))) ??
        clips[0] ??
        null
    );
}

/**
 * Body of the Details "Animation" section for one object: the class model deformed by a clip picked from the class's
 * own clip fields and the model's clip list (idle by default). Null when the class has no skinned model.
 */
export async function objectAnimationBody(
    refs: RefService,
    provider: SchemaProvider,
    o: LevelObject,
): Promise<HTMLElement | null> {
    const schema = provider.schemaFor(o.resource) ?? null;
    if (!schema) return null;
    const model = await classModelOf(refs, schema);
    if (!model) return null;
    const rig = await refs.rig(model.stem, model.id);
    if (!rig?.rig) return null;
    const fromClass = classClips(provider, schema, o);
    const fromModel = rig.clips.map((c) => ({ stem: model.stem, id: c.id, name: c.name, label: '' }));
    const preview = createAnimPreview(refs, { height: 190 });
    const sel = h('select', { class: 'sel ap-clip', 'aria-label': 'Animation clip' });
    const key = (c: { stem: string; id: number }): string => `${c.stem}:${c.id}`;
    const nameOf = new Map<string, string>(fromModel.map((c) => [key(c), c.name]));
    const opt = (c: { stem: string; id: number }, text: string): HTMLElement => h('option', { value: key(c) }, text);
    const groups: HTMLElement[] = [];
    if (fromClass.length) {
        const g = h('optgroup', { label: 'Class animations' });
        for (const c of fromClass) {
            const n = nameOf.get(key(c));
            g.append(opt(c, `${c.label}${n ? ` - ${n}` : ''}`));
        }
        groups.push(g);
    }
    if (fromModel.length) {
        const g = h('optgroup', { label: `All clips of the model (${fromModel.length})` });
        for (const c of fromModel) g.append(opt(c, c.name || `#${c.id}`));
        groups.push(g);
    }
    fill(sel, ...groups);
    const idle = pickIdle([...fromClass.map((c) => ({ ...c, name: nameOf.get(key(c)) })), ...fromModel]);
    const choose = (k: string): void => {
        const [stem, id] = [k.slice(0, k.lastIndexOf(':')), Number(k.slice(k.lastIndexOf(':') + 1))];
        void preview.setClip({ stem, id });
    };
    if (idle) {
        sel.value = key(idle);
        if (sel.value !== key(idle)) sel.selectedIndex = 0;
    }
    sel.addEventListener('change', () => choose(sel.value));
    await preview.setModel(model);
    if (sel.value) choose(sel.value);
    const search = h('input', {
        type: 'search',
        class: 'fsearch ap-find',
        placeholder: 'Find a clip (walk, attack...)',
        'aria-label': 'Find a clip',
    });
    search.addEventListener('input', () => {
        const q = search.value.trim().toLowerCase();
        if (!q) return;
        const hit = [...sel.options].find((x) => x.textContent!.toLowerCase().includes(q));
        if (hit) {
            sel.value = hit.value;
            sel.dispatchEvent(new Event('change'));
        }
    });
    return h('div', { class: 'ap-obj' }, h('div', { class: 'ap-pick' }, sel, search), preview.el);
}

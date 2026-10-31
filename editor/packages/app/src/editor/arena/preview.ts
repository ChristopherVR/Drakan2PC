import * as THREE from 'three';
import { TEAM_COLORS, type ArenaDraft, type Vec3 } from '@drakan/editor-core';
import type { LevelView } from '../../level.ts';

const U = 0.001;

export interface PreviewOptions {
    width?: number;
    height?: number;
    /** Draw spawn points and pickups on top of the terrain. */
    marks?: boolean;
}

/** The rectangle a preview shows, fitted to the aspect ratio with a margin; null without bounds or spawns. */
export function previewRect(
    draft: ArenaDraft,
    aspect: number,
): { cx: number; cz: number; w: number; h: number } | null {
    let x0: number, x1: number, z0: number, z1: number;
    if (draft.bounds) {
        x0 = Math.min(draft.bounds[0], draft.bounds[2]);
        x1 = Math.max(draft.bounds[0], draft.bounds[2]);
        z0 = Math.min(draft.bounds[1], draft.bounds[3]);
        z1 = Math.max(draft.bounds[1], draft.bounds[3]);
    } else if (draft.spawns.length) {
        x0 = Math.min(...draft.spawns.map((s) => s.pos[0])) - 3000;
        x1 = Math.max(...draft.spawns.map((s) => s.pos[0])) + 3000;
        z0 = Math.min(...draft.spawns.map((s) => s.pos[2])) - 3000;
        z1 = Math.max(...draft.spawns.map((s) => s.pos[2])) + 3000;
    } else return null;
    let w = (x1 - x0) * 1.06;
    let h = (z1 - z0) * 1.06;
    if (w / h < aspect) w = h * aspect;
    else h = w / aspect;
    return { cx: (x0 + x1) / 2, cz: (z0 + z1) / 2, w, h };
}

/**
 * A top-down picture of the map: the loaded terrain and level objects rendered with an orthographic camera over the
 * bounds, north (-Z) up, the arena overlay and the grid hidden; spawn points and pickups are drawn on top. Returns a
 * PNG, or null when there is nothing to frame. The multiplayer menu shows `previews/<map name>.png` next to the exe.
 */
export async function capturePreview(
    view: LevelView,
    draft: ArenaDraft,
    hide0: THREE.Object3D[],
    opts: PreviewOptions = {},
): Promise<Uint8Array | null> {
    let hide = hide0;
    const width = opts.width ?? 640;
    const height = opts.height ?? 480;
    const rect = previewRect(draft, width / height);
    if (!rect) return null;
    const renderer = view.renderer;
    const scene = view.scene;
    const cam = new THREE.OrthographicCamera(
        (-rect.w / 2) * U,
        (rect.w / 2) * U,
        (rect.h / 2) * U,
        (-rect.h / 2) * U,
        1,
        4000,
    );
    cam.up.set(0, 0, -1);
    cam.position.set(rect.cx * U, 1500, rect.cz * U);
    cam.lookAt(rect.cx * U, 0, rect.cz * U);
    cam.updateMatrixWorld(true);

    const target = new THREE.WebGLRenderTarget(width, height, { samples: 4 });
    target.texture.colorSpace = THREE.SRGBColorSpace;
    const savedFog = scene.fog;
    const savedBg = scene.background;
    for (const o of scene.children) if ((o as unknown as { isGridHelper?: boolean }).isGridHelper) hide = [...hide, o];
    hide = [...hide, view.gizmo.getHelper()];
    const wasVisible = hide.map((o) => o.visible);
    const pixels = new Uint8Array(width * height * 4);
    try {
        scene.fog = null;
        for (const o of hide) o.visible = false;
        const prev = renderer.getRenderTarget();
        renderer.setRenderTarget(target);
        renderer.render(scene, cam);
        renderer.readRenderTargetPixels(target, 0, 0, width, height, pixels);
        renderer.setRenderTarget(prev);
    } finally {
        scene.fog = savedFog;
        scene.background = savedBg;
        hide.forEach((o, i) => (o.visible = wasVisible[i]!));
        target.dispose();
    }

    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    const g = canvas.getContext('2d')!;
    const img = g.createImageData(width, height);
    for (let y = 0; y < height; y++) {
        img.data.set(pixels.subarray((height - 1 - y) * width * 4, (height - y) * width * 4), y * width * 4);
    }
    g.putImageData(img, 0, 0);
    const px = (p: Vec3): [number, number] => [
        ((p[0] - (rect.cx - rect.w / 2)) / rect.w) * width,
        ((p[2] - (rect.cz - rect.h / 2)) / rect.h) * height,
    ];
    if (opts.marks !== false) {
        g.lineWidth = 2;
        for (const p of draft.pickups) {
            const [x, y] = px(p.pos);
            g.fillStyle = '#facc15';
            g.strokeStyle = '#1f2937';
            g.beginPath();
            g.moveTo(x, y - 7);
            g.lineTo(x + 6, y);
            g.lineTo(x, y + 7);
            g.lineTo(x - 6, y);
            g.closePath();
            g.fill();
            g.stroke();
        }
        for (const s of draft.spawns) {
            const [x, y] = px(s.pos);
            g.fillStyle = `#${TEAM_COLORS[s.team]!.toString(16).padStart(6, '0')}`;
            g.strokeStyle = '#111827';
            g.beginPath();
            g.arc(x, y, 7, 0, Math.PI * 2);
            g.fill();
            g.stroke();
            const a = (s.yaw * Math.PI) / 180;
            g.beginPath();
            g.moveTo(x, y);
            g.lineTo(x + Math.sin(a) * 15, y + Math.cos(a) * 15);
            g.stroke();
        }
    }
    const blob: Blob | null = await new Promise((res) => canvas.toBlob(res, 'image/png'));
    return blob ? new Uint8Array(await blob.arrayBuffer()) : null;
}

// Cut scene camera path in the level viewport: a polyline through the camera's keyframes with a marker at every keyframe,
// green at the first and red at the last. Game units are scaled like the logic overlay (`logicview.ts`, 0.001).

import * as THREE from 'three';
import type { Ctx } from '../editor/ctx.ts';
import type { LevelView } from '../level.ts';

const S = 0.001;
const LINE = 0xff5fd2;
const START = 0x7ee08c;
const END = 0xff6b6b;

export interface PathLayer {
    /** Draw the path (game units), or remove it with null. */
    show(points: [number, number, number][] | null): void;
    /** Number of points currently drawn. */
    shown(): number;
}

const dot = new THREE.SphereGeometry(1, 10, 8);

export function createPathLayer(ctx: Pick<Ctx, 'view'>): PathLayer {
    let group: THREE.Group | null = null;
    let n = 0;
    const clear = (): void => {
        if (!group) return;
        group.parent?.remove(group);
        group.traverse((c) => {
            const m = c as THREE.Mesh;
            if (m.geometry && m.geometry !== dot) m.geometry.dispose();
            (m.material as THREE.Material | undefined)?.dispose?.();
        });
        group = null;
    };
    return {
        show(points) {
            clear();
            n = 0;
            if (!points || !points.length) return;
            let v: LevelView;
            try {
                v = ctx.view();
            } catch {
                return;
            }
            const g = new THREE.Group();
            g.renderOrder = 22;
            const pts = points.map((p) => new THREE.Vector3(p[0] * S, p[1] * S, p[2] * S));
            if (pts.length > 1) {
                const line = new THREE.Line(
                    new THREE.BufferGeometry().setFromPoints(pts),
                    new THREE.LineBasicMaterial({ color: LINE, depthTest: false, transparent: true }),
                );
                line.renderOrder = 22;
                g.add(line);
            }
            pts.forEach((p, i) => {
                const last = i === pts.length - 1;
                const m = new THREE.Mesh(
                    dot,
                    new THREE.MeshBasicMaterial({
                        color: i === 0 ? START : last ? END : LINE,
                        depthTest: false,
                    }),
                );
                m.position.copy(p);
                m.scale.setScalar(i === 0 || last ? 0.2 : 0.1);
                m.renderOrder = 23;
                g.add(m);
            });
            v.scene.add(g);
            group = g;
            n = pts.length;
        },
        shown: () => n,
    };
}

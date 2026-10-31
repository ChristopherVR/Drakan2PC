import * as THREE from 'three';

const SVG = 'http://www.w3.org/2000/svg';
const AXES = [
    { name: 'X', dir: new THREE.Vector3(1, 0, 0), color: 'var(--axis-x)' },
    { name: 'Y', dir: new THREE.Vector3(0, 1, 0), color: 'var(--axis-y)' },
    { name: 'Z', dir: new THREE.Vector3(0, 0, 1), color: 'var(--axis-z)' },
];
const SIZE = 76;
const R = 26;

/**
 * Orientation gizmo for the viewport corner: three axes that follow the camera, positive ends labelled. Clicking the
 * Y axis jumps to the top view. `update` is cheap (a few attribute writes) and is called on every camera change.
 */
export function createNavGizmo(host: HTMLElement, onTop: () => void): { update(q: THREE.Quaternion): void } {
    const svg = document.createElementNS(SVG, 'svg');
    svg.setAttribute('viewBox', `${-SIZE / 2} ${-SIZE / 2} ${SIZE} ${SIZE}`);
    svg.setAttribute('width', String(SIZE));
    svg.setAttribute('height', String(SIZE));
    const bg = document.createElementNS(SVG, 'circle');
    bg.setAttribute('r', String(SIZE / 2 - 2));
    bg.setAttribute('class', 'ng-bg');
    svg.append(bg);
    const parts = AXES.flatMap((a) => {
        const g = document.createElementNS(SVG, 'g');
        g.setAttribute('class', 'ng-axis');
        const line = document.createElementNS(SVG, 'line');
        line.setAttribute('stroke', a.color);
        line.setAttribute('stroke-width', '2');
        line.setAttribute('stroke-linecap', 'round');
        const neg = document.createElementNS(SVG, 'circle');
        neg.setAttribute('r', '4');
        neg.setAttribute('fill', a.color);
        neg.setAttribute('opacity', '0.35');
        const pos = document.createElementNS(SVG, 'circle');
        pos.setAttribute('r', '7.5');
        pos.setAttribute('fill', a.color);
        const label = document.createElementNS(SVG, 'text');
        label.textContent = a.name;
        label.setAttribute('class', 'ng-label');
        label.setAttribute('text-anchor', 'middle');
        label.setAttribute('dominant-baseline', 'central');
        g.append(neg, line, pos, label);
        if (a.name === 'Y') {
            g.classList.add('clickable');
            g.addEventListener('click', onTop);
        }
        svg.append(g);
        return [{ a, g, line, neg, pos, label }];
    });
    host.append(svg);
    const inv = new THREE.Quaternion();
    const v = new THREE.Vector3();
    return {
        update(q) {
            inv.copy(q).invert();
            const order = parts.map((p) => {
                v.copy(p.a.dir).applyQuaternion(inv);
                return { p, x: v.x * R, y: -v.y * R, z: v.z };
            });
            order.sort((a, b) => a.z - b.z);
            for (const { p, x, y } of order) {
                p.line.setAttribute('x2', x.toFixed(1));
                p.line.setAttribute('y2', y.toFixed(1));
                p.pos.setAttribute('cx', x.toFixed(1));
                p.pos.setAttribute('cy', y.toFixed(1));
                p.label.setAttribute('x', x.toFixed(1));
                p.label.setAttribute('y', y.toFixed(1));
                p.neg.setAttribute('cx', (-x).toFixed(1));
                p.neg.setAttribute('cy', (-y).toFixed(1));
                svg.append(p.g);
            }
        },
    };
}

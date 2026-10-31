import type { Screenshot } from '@drakan/formats';

export type Kid = Node | string | null | false | undefined;
export type Attrs = Record<string, string | number | boolean | ((e: Event) => void) | undefined>;

/** Tiny element builder. Keys starting with `on` set handlers, `class` the class name, the rest are attributes. */
export function h<K extends keyof HTMLElementTagNameMap>(
    tag: K,
    attrs?: Attrs | null,
    ...kids: Kid[]
): HTMLElementTagNameMap[K] {
    const el = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs ?? {})) {
        if (v === undefined || v === false) continue;
        if (typeof v === 'function') (el as unknown as Record<string, unknown>)[k] = v;
        else if (k === 'class') el.className = String(v);
        else if (k === 'value') (el as unknown as { value: string }).value = String(v);
        else el.setAttribute(k, v === true ? '' : String(v));
    }
    for (const kid of kids) if (kid !== null && kid !== false && kid !== undefined) el.append(kid);
    return el;
}

export const hex = (n: number, w: number): string => n.toString(16).padStart(w, '0');
export const fmtDate = (ms: number): string => (ms ? new Date(ms).toLocaleString() : '');

export function badge(text: string, kind: string, title?: string): HTMLElement {
    return h('span', { class: `badge ${kind}`, title }, text);
}

export function shotCanvas(shot: Screenshot, cls: string): HTMLCanvasElement {
    const c = h('canvas', { class: cls, width: shot.width, height: shot.height });
    const img = new ImageData(shot.width, shot.height);
    img.data.set(shot.rgba);
    c.getContext('2d')?.putImageData(img, 0, 0);
    return c;
}

export function hexDump(data: Uint8Array, from: number, to: number): string {
    const lines: string[] = [];
    for (let i = from; i < to && i < data.length; i += 16) {
        const row = data.subarray(i, Math.min(i + 16, data.length, to));
        const hx = Array.from(row, (b) => hex(b, 2)).join(' ');
        const asc = Array.from(row, (b) => (b >= 32 && b < 127 ? String.fromCharCode(b) : '.')).join('');
        lines.push(`${hex(i, 6)}  ${hx.padEnd(47)}  ${asc}`);
    }
    return lines.join('\n');
}

/** A stat tile: label, big value, optional bar (value / max) and a caption. */
export function statTile(
    label: string,
    value: string,
    opts: { bar?: [number, number]; note?: string } = {},
): HTMLElement {
    const bar = opts.bar
        ? h(
              'div',
              { class: 'bar', title: `${opts.bar[0]} of ${opts.bar[1]}` },
              h('i', { style: `width:${Math.max(0, Math.min(100, (opts.bar[0] / Math.max(opts.bar[1], 1)) * 100))}%` }),
          )
        : null;
    return h(
        'div',
        { class: 'tile' },
        h('small', null, label),
        h('b', null, value),
        bar,
        opts.note ? h('small', { class: 'dim' }, opts.note) : null,
    );
}

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
    return h('span', { class: `sv-badge ${kind}`, title }, text);
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
              { class: 'sv-meter', title: `${opts.bar[0]} of ${opts.bar[1]}` },
              h('i', { style: `width:${Math.max(0, Math.min(100, (opts.bar[0] / Math.max(opts.bar[1], 1)) * 100))}%` }),
          )
        : null;
    return h(
        'div',
        { class: 'tile' },
        h('small', null, label),
        h('b', null, value),
        bar,
        opts.note ? h('small', { class: 'sv-dim' }, opts.note) : null,
    );
}

const ICON_PATHS: Record<string, string> = {
    folder: 'M3 6.5A1.5 1.5 0 0 1 4.5 5h4.2l1.8 2H19.5A1.5 1.5 0 0 1 21 8.5v9a1.5 1.5 0 0 1-1.5 1.5h-15A1.5 1.5 0 0 1 3 17.5z',
    file: 'M6 3h8l4 4v14H6zM14 3v4h4',
    save: 'M5 3h11l3 3v15H5zM8 3v5h7V3M8 21v-7h8v7',
    download: 'M12 4v11m0 0-4-4m4 4 4-4M5 20h14',
    undo: 'M9 7 4 12l5 5M4 12h10a6 6 0 0 1 0 12',
    redo: 'M15 7l5 5-5 5M20 12H10a6 6 0 0 0 0 12',
    reload: 'M20 5v5h-5M4 19v-5h5M19 10a7 7 0 0 0-12-3M5 14a7 7 0 0 0 12 3',
    revert: 'M4 12a8 8 0 1 0 3-6.2M4 4v5h5',
    cube: 'M12 3l8 4.5v9L12 21l-8-4.5v-9zM12 12l8-4.5M12 12v9M12 12 4 7.5',
    search: 'M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14zM16.5 16.5 21 21',
    check: 'M5 12.5l4.5 4.5L19 7.5',
    warn: 'M12 4 2.5 20h19zM12 10v5M12 17.5v.5',
    user: 'M12 12a4 4 0 1 0 0-8 4 4 0 0 0 0 8zM4 21a8 8 0 0 1 16 0',
    bag: 'M6 8h12l1 12H5zM9 8a3 3 0 0 1 6 0',
    scroll: 'M7 4h11v13a3 3 0 0 1-3 3H6a3 3 0 0 0 3-3V4M7 4a2 2 0 0 0-2 2v2h4M12 9h4M12 13h4',
    map: 'M3 6l6-2 6 2 6-2v14l-6 2-6-2-6 2zM9 4v14M15 6v14',
    swap: 'M4 8h14l-4-4M20 16H6l4 4',
    compare: 'M9 4v16M15 4v16M4 8h5M15 14h5',
    wrench: 'M14.5 6.5a4 4 0 0 0-5 5L3 18l3 3 6.5-6.5a4 4 0 0 0 5-5l-2.5 2.5-2-.5-.5-2z',
    chevron: 'M9 6l6 6-6 6',
    plus: 'M12 5v14M5 12h14',
    drag: 'M8 5h.01M8 12h.01M8 19h.01M16 5h.01M16 12h.01M16 19h.01',
};

/** A 24-grid stroke icon (inline SVG, inherits `currentColor`). */
export function icon(name: keyof typeof ICON_PATHS | string, size = 16): SVGElement {
    const ns = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('viewBox', '0 0 24 24');
    svg.setAttribute('width', String(size));
    svg.setAttribute('height', String(size));
    svg.setAttribute('fill', 'none');
    svg.setAttribute('stroke', 'currentColor');
    svg.setAttribute('stroke-width', '1.8');
    svg.setAttribute('stroke-linecap', 'round');
    svg.setAttribute('stroke-linejoin', 'round');
    svg.setAttribute('aria-hidden', 'true');
    svg.setAttribute('class', 'sv-ico');
    const p = document.createElementNS(ns, 'path');
    p.setAttribute('d', ICON_PATHS[name] ?? ICON_PATHS.file!);
    svg.append(p);
    return svg;
}

/** A button with an optional leading icon. `kind`: primary | ghost | danger (default is the neutral raised button). */
export function button(
    label: string,
    opts: {
        icon?: string;
        kind?: string;
        title?: string;
        onclick?: (e: Event) => void;
        disabled?: boolean;
        id?: string;
    },
): HTMLButtonElement {
    return h(
        'button',
        {
            id: opts.id,
            class: `sv-btn${opts.kind ? ` ${opts.kind}` : ''}`,
            title: opts.title,
            onclick: opts.onclick,
            disabled: opts.disabled,
            type: 'button',
        },
        opts.icon ? icon(opts.icon) : null,
        label ? h('span', null, label) : null,
    );
}

/** A titled panel (AbioticEditor "card"): accent title row, optional right-hand tools and a hint line. */
export function card(
    title: string,
    opts: { hint?: string; tools?: Kid; class?: string } = {},
    ...kids: Kid[]
): HTMLElement {
    return h(
        'section',
        { class: `sv-card${opts.class ? ` ${opts.class}` : ''}` },
        h(
            'header',
            { class: 'sv-card-head' },
            h('h3', null, title),
            opts.hint ? h('p', null, opts.hint) : null,
            opts.tools ? h('div', { class: 'sv-card-tools' }, opts.tools) : null,
        ),
        h('div', { class: 'sv-card-body' }, ...kids),
    );
}

/** An empty or blocked state: icon, one-line title, an explanation of what to do next, optional actions. */
export function emptyState(iconName: string, title: string, body: string, ...actions: Kid[]): HTMLElement {
    return h(
        'div',
        { class: 'sv-empty' },
        h('div', { class: 'sv-empty-ico' }, icon(iconName, 28)),
        h('h3', null, title),
        h('p', null, body),
        actions.length ? h('div', { class: 'sv-empty-actions' }, ...actions) : null,
    );
}

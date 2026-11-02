/** Tiny DOM helpers and the icon set shared by the editor panels. No dependencies. */

type Child = Node | string | number | null | undefined | false;
type Attrs = Record<string, string | number | boolean | EventListener | undefined | null>;

/** Create an element: `h('button', { class: 'btn', onclick: fn }, 'Text')`. */
export function h<K extends keyof HTMLElementTagNameMap>(
    tag: K,
    attrs: Attrs = {},
    ...children: Child[]
): HTMLElementTagNameMap[K] {
    const el = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
        if (v === undefined || v === null || v === false) continue;
        if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v as EventListener);
        else if (k === 'class') el.className = String(v);
        else if (k === 'value') (el as unknown as HTMLInputElement).value = String(v);
        else if (v === true) el.setAttribute(k, '');
        else el.setAttribute(k, String(v));
    }
    append(el, children);
    return el;
}

export function append(el: Element, children: Child[]): void {
    for (const c of children) {
        if (c === null || c === undefined || c === false) continue;
        el.append(typeof c === 'object' ? c : String(c));
    }
}

/** Rebuild a container's children. */
export function fill(el: Element, ...children: Child[]): void {
    el.replaceChildren();
    append(el, children);
}

const PATHS: Record<string, string> = {
    select: '<path d="M5 3l14 7-6 2-2 6z"/>',
    external: '<path d="M14 4h6v6M20 4l-9 9M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h5"/>',
    'dock-bottom': '<rect x="3" y="4" width="18" height="16" rx="1.5"/><path d="M3 14h18"/>',
    move: '<path d="M12 3v18M3 12h18M12 3l-3 3M12 3l3 3M12 21l-3-3M12 21l3-3M3 12l3-3M3 12l3 3M21 12l-3-3M21 12l-3 3"/>',
    rotate: '<path d="M20 12a8 8 0 1 1-2.5-5.8"/><path d="M20 4v5h-5"/>',
    scale: '<rect x="4" y="10" width="10" height="10" rx="1"/><path d="M14 10l6-6M15 4h5v5"/>',
    eye: '<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7-10-7-10-7z"/><circle cx="12" cy="12" r="3"/>',
    'eye-off':
        '<path d="M3 3l18 18"/><path d="M10.6 5.1A9.5 9.5 0 0 1 12 5c6 0 10 7 10 7a17 17 0 0 1-3.2 3.9M6.5 6.6A16.5 16.5 0 0 0 2 12s4 7 10 7a9.6 9.6 0 0 0 4.2-1"/><path d="M9.9 9.9a3 3 0 0 0 4.2 4.2"/>',
    lock: '<rect x="5" y="11" width="14" height="9" rx="2"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>',
    unlock: '<rect x="5" y="11" width="14" height="9" rx="2"/><path d="M8 11V8a4 4 0 0 1 7.5-2"/>',
    undo: '<path d="M9 14L4 9l5-5"/><path d="M4 9h10a6 6 0 0 1 0 12h-4"/>',
    redo: '<path d="M15 14l5-5-5-5"/><path d="M20 9H10a6 6 0 0 0 0 12h4"/>',
    copy: '<rect x="9" y="9" width="11" height="11" rx="2"/><path d="M5 15V6a2 2 0 0 1 2-2h8"/>',
    paste: '<rect x="5" y="5" width="14" height="16" rx="2"/><path d="M9 5V3h6v2M9 12h6M9 16h4"/>',
    trash: '<path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/>',
    list: '<path d="M9 6h11M9 12h11M9 18h11M4.5 6h.01M4.5 12h.01M4.5 18h.01"/>',
    grid: '<path d="M4 4h16v16H4zM4 9.3h16M4 14.7h16M9.3 4v16M14.7 4v16"/>',
    magnet: '<path d="M6 3v8a6 6 0 0 0 12 0V3h-4v8a2 2 0 0 1-4 0V3zM6 7h4M14 7h4"/>',
    focus: '<circle cx="12" cy="12" r="3"/><path d="M12 3v4M12 17v4M3 12h4M17 12h4"/>',
    search: '<circle cx="11" cy="11" r="6"/><path d="M20 20l-4.5-4.5"/>',
    star: '<path d="M12 3l2.8 5.8 6.2.9-4.5 4.4 1 6.2L12 17.3 6.5 20.3l1-6.2L3 9.7l6.2-.9z"/>',
    sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1.5 1.5M17.5 17.5L19 19M5 19l1.5-1.5M17.5 6.5L19 5"/>',
    moon: '<path d="M20 14.5A8 8 0 0 1 9.5 4 8 8 0 1 0 20 14.5z"/>',
    help: '<circle cx="12" cy="12" r="9"/><path d="M9.5 9.5a2.5 2.5 0 1 1 3.5 2.3c-.7.4-1 .9-1 1.7M12 17h.01"/>',
    plus: '<path d="M12 5v14M5 12h14"/>',
    download: '<path d="M12 4v11M7 11l5 5 5-5M5 20h14"/>',
    play: '<path d="M7 4l13 8-13 8z"/>',
    chevron: '<path d="M9 6l6 6-6 6"/>',
    link: '<path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1"/>',
    x: '<path d="M6 6l12 12M18 6L6 18"/>',
    ground: '<path d="M12 3v12M7 11l5 5 5-5M4 20h16"/>',
    layers: '<path d="M12 3l9 5-9 5-9-5zM3 13l9 5 9-5"/>',
    world: '<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3a14 14 0 0 1 0 18M12 3a14 14 0 0 0 0 18"/>',
    local: '<path d="M5 19V5M5 19h14M5 19l8-8"/>',
    history: '<path d="M4 12a8 8 0 1 0 2.5-5.8"/><path d="M4 4v5h5M12 8v4l3 2"/>',
    check: '<path d="M5 12l5 5 9-10"/>',
    warn: '<path d="M12 4l9 16H3zM12 10v4M12 17h.01"/>',
    box: '<path d="M12 3l8 4.5v9L12 21l-8-4.5v-9zM12 12l8-4.5M12 12L4 7.5M12 12v9"/>',
    save: '<path d="M5 4h11l3 3v13H5zM8 4v5h7V4M8 20v-6h8v6"/>',
    open: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
    camera: '<path d="M4 8h3l2-3h6l2 3h3v11H4z"/><circle cx="12" cy="13" r="3.5"/>',
    top: '<rect x="5" y="5" width="14" height="14" rx="1"/><path d="M12 9v6M9 12h6"/>',
    align: '<path d="M4 4v16M8 7h12M8 12h8M8 17h10"/>',
    distribute: '<path d="M4 4v16M20 4v16M9 8h6v8H9z"/>',
    character: '<circle cx="12" cy="7" r="3.5"/><path d="M5 21a7 7 0 0 1 14 0"/>',
    item: '<path d="M12 3l8 4v10l-8 4-8-4V7z"/><path d="M12 12l8-4M12 12L4 8M12 12v9"/>',
    tree: '<path d="M12 3l6 9h-3l4 6H5l4-6H6zM12 18v3"/>',
    rock: '<path d="M3 18l4-9 4 3 3-6 7 12z"/>',
    building: '<path d="M4 20V9l8-5 8 5v11zM10 20v-6h4v6"/>',
    door: '<rect x="6" y="3" width="12" height="18" rx="1"/><path d="M14 12h.01"/>',
    light: '<path d="M9 18h6M10 21h4M12 3a6 6 0 0 0-3.5 10.9c.6.5 1 1.2 1 2.1h5c0-.9.4-1.6 1-2.1A6 6 0 0 0 12 3z"/>',
    effect: '<path d="M12 3l1.8 5.2L19 10l-5.2 1.8L12 17l-1.8-5.2L5 10l5.2-1.8zM19 16l.8 2.2L22 19l-2.2.8L19 22l-.8-2.2L16 19l2.2-.8z"/>',
    trigger: '<path d="M13 3L5 14h6l-1 7 8-11h-6z"/>',
    sound: '<path d="M4 9v6h4l5 4V5L8 9zM16 9a4 4 0 0 1 0 6M18.5 6.5a8 8 0 0 1 0 11"/>',
    folder: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
    'folder-open':
        '<path d="M3 18V6a2 2 0 0 1 2-2h4l2 2h7a2 2 0 0 1 2 2v2M3 18l2.5-7.5a2 2 0 0 1 1.9-1.4H21l-2.4 7.9a2 2 0 0 1-1.9 1.5H5a2 2 0 0 1-2-1.5"/>',
    'chevron-down': '<path d="M6 9l6 6 6-6"/>',
    outliner: '<path d="M4 6h3M4 12h3M4 18h3M10 6h10M10 12h10M10 18h10"/>',
    details:
        '<path d="M4 6h10M18 6h2M4 12h4M12 12h8M4 18h12M20 18h0"/><circle cx="16" cy="6" r="2"/><circle cx="10" cy="12" r="2"/><circle cx="18" cy="18" r="2"/>',
    place: '<path d="M12 3l8 4.5v9L12 21l-8-4.5v-9z"/><path d="M12 8v8M8 12h8"/>',
    terminal: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M7 10l3 2-3 2M13 15h4"/>',
    filter: '<path d="M4 5h16l-6 8v6l-4-2v-4z"/>',
    sliders: '<path d="M4 7h9M17 7h3M4 17h3M11 17h9"/><circle cx="15" cy="7" r="2"/><circle cx="9" cy="17" r="2"/>',
    info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v5M12 8h.01"/>',
    more: '<path d="M5 12h.01M12 12h.01M19 12h.01"/>',
    rocket: '<path d="M14 4c3 0 6 1 6 1s1 3 1 6c-3 3-6 5-8 6l-4-4c1-2 3-5 5-9zM9 15l-4 1 1-4M9 15l-1 4 4-1"/><circle cx="15" cy="9" r="1.2"/>',
    gauge: '<path d="M4 17a8 8 0 1 1 16 0M12 17l4-6"/>',
    perspective: '<path d="M4 18l4-12h8l4 12zM8 6l-2 12M16 6l2 12"/>',
    cube: '<path d="M12 3l8 4.5v9L12 21l-8-4.5v-9zM12 12l8-4.5M12 12L4 7.5M12 12v9"/>',
    clear: '<circle cx="12" cy="12" r="9"/><path d="M9 9l6 6M15 9l-6 6"/>',
    'check-circle': '<circle cx="12" cy="12" r="9"/><path d="M8 12.5l3 3 5-6"/>',
    'alert-circle': '<circle cx="12" cy="12" r="9"/><path d="M12 8v5M12 16h.01"/>',
    chat: '<path d="M20 12a7.5 7.5 0 0 1-11 6.6L4 20l1.4-4.6A7.5 7.5 0 1 1 20 12z"/>',
    shop: '<path d="M4 9l1.5-5h13L20 9M4 9h16M4 9v1a2.7 2.7 0 0 0 5.3 0 2.7 2.7 0 0 0 5.4 0 2.7 2.7 0 0 0 5.3 0V9M5 12v8h14v-8M10 20v-5h4v5"/>',
    sidebar: '<rect x="3.5" y="4.5" width="17" height="15" rx="2"/><path d="M9.5 4.5v15"/>',
    generic: '<rect x="5" y="5" width="14" height="14" rx="3"/>',
};

/** Inline SVG icon (16 px by default, currentColor stroke). */
export function icon(name: string, size = 16): SVGElement {
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('viewBox', '0 0 24 24');
    svg.setAttribute('width', String(size));
    svg.setAttribute('height', String(size));
    svg.setAttribute('fill', 'none');
    svg.setAttribute('stroke', 'currentColor');
    svg.setAttribute('stroke-width', '1.8');
    svg.setAttribute('stroke-linecap', 'round');
    svg.setAttribute('stroke-linejoin', 'round');
    svg.setAttribute('aria-hidden', 'true');
    svg.classList.add('ico');
    svg.innerHTML = PATHS[name] ?? PATHS.generic!;
    return svg;
}

/** Icon-only button with a tooltip (and an optional shortcut hint appended to it). */
export function iconButton(
    name: string,
    title: string,
    onclick: (e: MouseEvent) => void,
    opts: { id?: string; active?: boolean; hint?: string } = {},
): HTMLButtonElement {
    const b = h('button', {
        class: `tool${opts.active ? ' active' : ''}`,
        type: 'button',
        title: opts.hint ? `${title} (${opts.hint})` : title,
        'aria-label': title,
        id: opts.id,
        onclick: onclick as EventListener,
    });
    b.append(icon(name));
    return b;
}

/** Fire `fn` at most once per animation frame. */
export function raf(fn: () => void): () => void {
    let pending = 0;
    return () => {
        if (pending) return;
        pending = requestAnimationFrame(() => {
            pending = 0;
            fn();
        });
    };
}

export const clamp = (v: number, lo: number, hi: number): number => Math.min(hi, Math.max(lo, v));

/** localStorage with try/catch (private windows, blocked storage, thumbnails). */
export const store = {
    get(key: string): string | null {
        try {
            return localStorage.getItem(`drakan-editor:${key}`);
        } catch {
            return null;
        }
    },
    set(key: string, value: string): void {
        try {
            localStorage.setItem(`drakan-editor:${key}`, value);
        } catch {
            // storage unavailable: the editor works without it
        }
    },
    json<T>(key: string, fallback: T): T {
        const s = this.get(key);
        if (!s) return fallback;
        try {
            return JSON.parse(s) as T;
        } catch {
            return fallback;
        }
    },
};

export const hex = (n: number, w = 0): string => n.toString(16).padStart(w, '0');

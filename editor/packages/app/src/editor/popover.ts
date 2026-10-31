import { h } from './dom.ts';

/** One floating panel at a time, anchored to the element that opened it; closes on Esc and on an outside click. */
let popover: { el: HTMLElement; close: () => void } | null = null;

export function closePopover(): void {
    popover?.close();
}

export interface PopoverOptions {
    /** Extra class on the panel (`ed-pop` is always set). */
    className?: string;
    /** Called once when the panel closes, whichever way. */
    onClose?: () => void;
}

export function openPopover(
    anchor: HTMLElement,
    build: (close: () => void) => HTMLElement,
    opts: PopoverOptions = {},
): HTMLElement {
    closePopover();
    const el = h('div', { class: `ed-pop${opts.className ? ` ${opts.className}` : ''}`, role: 'dialog' });
    const onDown = (e: PointerEvent): void => {
        if (!el.contains(e.target as Node) && !anchor.contains(e.target as Node)) close();
    };
    const onKey = (e: KeyboardEvent): void => {
        if (e.key === 'Escape') {
            e.stopPropagation();
            close();
        }
    };
    const close = (): void => {
        el.remove();
        document.removeEventListener('pointerdown', onDown, true);
        document.removeEventListener('keydown', onKey, true);
        if (popover?.el === el) popover = null;
        opts.onClose?.();
    };
    el.append(build(close));
    (anchor.closest('#level-main') ?? document.body).append(el);
    const r = anchor.getBoundingClientRect();
    const w = el.offsetWidth;
    const hgt = el.offsetHeight;
    el.style.left = `${Math.max(8, Math.min(window.innerWidth - w - 8, r.right - w))}px`;
    const below = r.bottom + 4;
    el.style.top = `${below + hgt > window.innerHeight - 8 ? Math.max(8, r.top - hgt - 4) : below}px`;
    document.addEventListener('pointerdown', onDown, true);
    document.addEventListener('keydown', onKey, true);
    popover = { el, close };
    return el;
}

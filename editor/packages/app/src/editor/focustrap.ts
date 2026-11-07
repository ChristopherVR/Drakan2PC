const FOCUSABLE =
    'a[href], button:not([disabled]), input:not([disabled]):not([type="hidden"]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';

function focusables(root: HTMLElement): HTMLElement[] {
    return [...root.querySelectorAll<HTMLElement>(FOCUSABLE)].filter((e) => e.offsetParent !== null || e === document.activeElement);
}

/**
 * Keep Tab and Shift+Tab inside `root` while it is open: past the last control the focus returns to the first one,
 * and a focus that is outside (the opener, the page) is pulled in. `isOpen` says whether the trap applies right now.
 */
export function trapFocus(root: HTMLElement, isOpen: () => boolean = () => !root.hidden): () => void {
    const onKey = (e: KeyboardEvent): void => {
        if (e.key !== 'Tab' || !isOpen()) return;
        const items = focusables(root);
        const a = document.activeElement as HTMLElement | null;
        if (!items.length) {
            e.preventDefault();
            if (!root.hasAttribute('tabindex')) root.tabIndex = -1;
            root.focus();
            return;
        }
        const first = items[0]!;
        const last = items[items.length - 1]!;
        if (!a || !root.contains(a)) {
            e.preventDefault();
            (e.shiftKey ? last : first).focus();
        } else if (e.shiftKey && (a === first || a === root)) {
            e.preventDefault();
            last.focus();
        } else if (!e.shiftKey && a === last) {
            e.preventDefault();
            first.focus();
        }
    };
    window.addEventListener('keydown', onKey, true);
    return () => window.removeEventListener('keydown', onKey, true);
}

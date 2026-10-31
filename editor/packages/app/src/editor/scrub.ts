/**
 * Drag-to-scrub for a number input: pressing and dragging the label changes the value by `step` per pixel (Shift x10,
 * Alt x0.1). The input shows the live value while dragging and gets one `change` event on release, so the host commits
 * a single undo step per drag.
 */
export function attachScrub(
    lab: HTMLElement,
    input: HTMLInputElement,
    opts: { step: number; digits: number; integer?: boolean },
): void {
    const fmt = (v: number): string => String(Number(v.toFixed(opts.digits)));
    let start: { x: number; v: number } | null = null;
    lab.addEventListener('pointerdown', (e) => {
        lab.setPointerCapture(e.pointerId);
        start = { x: e.clientX, v: Number(input.value) || 0 };
        document.body.classList.add('ed-scrub');
    });
    lab.addEventListener('pointermove', (e) => {
        if (!start) return;
        const k = (e.shiftKey ? 10 : e.altKey ? 0.1 : 1) * opts.step;
        const v = start.v + (e.clientX - start.x) * k;
        input.value = fmt(opts.integer ? Math.round(v) : v);
    });
    const end = (e: PointerEvent): void => {
        if (!start) return;
        const moved = Math.abs(e.clientX - start.x) > 2;
        start = null;
        lab.releasePointerCapture(e.pointerId);
        document.body.classList.remove('ed-scrub');
        if (moved) input.dispatchEvent(new Event('change'));
    };
    lab.addEventListener('pointerup', end);
    lab.addEventListener('pointercancel', end);
}

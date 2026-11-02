/**
 * Entry points of the guided tours: `installEditorTours()` in the editor window (main.ts), `installSaveWindowTours()`
 * in the Save Editor window (saveapp.ts). Tour ids are listed in docs/journal/2026-11-02/guided-tours.md.
 */
import { addTours, installTourSystem, runTour } from './install.ts';
import { EDITOR_TOURS, TOUR_ALIASES, saveWindowTour, takePendingWindowTour } from './tours.ts';

export { tourBridge } from './events.ts';

export function installEditorTours(): void {
    addTours(EDITOR_TOURS, TOUR_ALIASES);
    installTourSystem();
}

/** The Save Editor window: its own tour, started when the editor's "Edit a save" tour handed over to it. */
export function installSaveWindowTours(): void {
    addTours([saveWindowTour]);
    installTourSystem();
    const next = takePendingWindowTour();
    if (next) setTimeout(() => runTour(next, true), 600);
}

// Bridge between the Logic panel (exit objects) and the story session that owns string edits: the picker for an exit's
// destination level writes the destination string of the level's XDB, which then leaves with the Story pack.

export interface ExitEditHost {
    /** Load the string tables of the disc (once). */
    ensure(): Promise<void>;
    /** Current text (as edited) of a destination string, undefined when the table is not loaded. */
    text(file: string, id: number): string | undefined;
    /** Rewrite the destination; false when the string is unknown. */
    set(file: string, id: number, text: string): boolean;
    /** Whether the string differs from the disc. */
    changed(file: string, id: number): boolean;
    /** A new string of the same table holding `text` as the destination (copy of string `fromId`); its id, null when unknown. */
    addDestination(file: string, fromId: number, text: string): number | null;
}

let host: ExitEditHost | null = null;
const listeners = new Set<() => void>();

export const notifyExitEdit = (): void => listeners.forEach((l) => l());
export const setExitEditHost = (h: ExitEditHost | null): void => {
    host = h;
    notifyExitEdit();
};
export const exitEditHost = (): ExitEditHost | null => host;
/** Called when the host appears or its strings change. */
export const onExitEditChange = (fn: () => void): void => void listeners.add(fn);

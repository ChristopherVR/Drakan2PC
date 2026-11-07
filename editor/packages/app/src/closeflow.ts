// What the page does when the desktop shell asks to close the window: write the unsaved work while telling the shell it is
// alive (a heartbeat, so a long write is waited for), answer when done, and when a write fails let the user choose instead
// of closing silently. Kept free of Tauri so it can be tested; `platform.ts` wires it to the commands.

export type CloseChoice = 'retry' | 'close' | 'cancel';

export interface CloseDeps {
    /** Tell the shell the page is still working (`ask` = waiting for the user, which has no time limit). */
    beat(ask: boolean): void;
    /** Everything is written (or the user chose to close anyway): the window may go. */
    ack(): void;
    /** The user chose to keep the window open. */
    cancel(): void;
    /** Show the failure and let the user choose. */
    askFailure(message: string): Promise<CloseChoice>;
    /** Heartbeat period in ms (the shell gives up on a page that is silent for 3 s). */
    beatMs?: number;
}

export const CLOSE_BEAT_MS = 500;

/** One close request from start to finish. Resolves when the shell has been told the outcome. */
export async function runCloseFlow(flush: () => Promise<void>, deps: CloseDeps): Promise<'closed' | 'cancelled'> {
    const every = deps.beatMs ?? CLOSE_BEAT_MS;
    for (;;) {
        deps.beat(false);
        const timer = setInterval(() => deps.beat(false), every);
        let failure: string | null = null;
        try {
            await flush();
        } catch (e) {
            failure = e instanceof Error && e.message ? e.message : String(e);
        } finally {
            clearInterval(timer);
        }
        if (failure === null) {
            deps.ack();
            return 'closed';
        }
        // The user may take as long as they like: keep the shell informed that the page is alive.
        deps.beat(true);
        const asking = setInterval(() => deps.beat(true), every);
        let choice: CloseChoice;
        try {
            choice = await deps.askFailure(failure);
        } catch {
            choice = 'cancel';
        } finally {
            clearInterval(asking);
        }
        if (choice === 'retry') continue;
        if (choice === 'close') {
            deps.ack();
            return 'closed';
        }
        deps.cancel();
        return 'cancelled';
    }
}

import type { QuestClass, XdbString, XdbStrings } from '@drakan/formats';
import type { Api, Method } from '../rpc.ts';

type Call = <M extends Method>(method: M, ...args: Parameters<Api[M]>) => Promise<ReturnType<Api[M]>>;

/** Quest classes and journal text of the open disc, ready for `describeQuestRows`. */
export interface StoryData {
    classes: ReadonlyMap<number, QuestClass>;
    strings: XdbStrings;
    /** Language slot of the disc's language folder. */
    slot: number;
}

export interface StoryArt {
    /** The catalog once it has loaded (and starts loading on the first call); null before that or without a disc. */
    get(): StoryData | null;
    /** Forget everything (a different disc or language was opened). */
    reset(): void;
    onChange(fn: () => void): void;
}

/** Loads the System object database and string table through the worker, once per disc. */
export function createStoryArt(call: Call): StoryArt {
    let data: StoryData | null = null;
    let loading = false;
    let failed = false;
    let epoch = 0;
    const listeners: (() => void)[] = [];

    return {
        get() {
            if (data) return data;
            if (!loading && !failed) {
                loading = true;
                const ep = epoch;
                call('loadStoryCatalog')
                    .then((c) => {
                        if (ep !== epoch) return;
                        data = {
                            classes: new Map(c.classes.map((k) => [k.classId, k])),
                            strings: new Map(c.strings.map((s: XdbString) => [s.id, s])),
                            slot: c.slot,
                        };
                        listeners.forEach((f) => f());
                    })
                    .catch(() => {
                        if (ep === epoch) failed = true; // no disc yet: retried after reset()
                    })
                    .finally(() => {
                        if (ep === epoch) loading = false;
                    });
            }
            return null;
        },
        reset() {
            epoch++;
            data = null;
            loading = false;
            failed = false;
            listeners.forEach((f) => f());
        },
        onChange: (fn) => void listeners.push(fn),
    };
}

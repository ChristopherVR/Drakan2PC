import {
    priceChanges,
    priceProblem,
    type ItemRecordSource,
    type PriceEdits,
    type StoryEntryChange,
} from '@drakan/editor-core';
import { ODB_CLASS_TYPE, SrscReader, parseOdbClasses } from '@drakan/formats';

/** Pending item price edits of the Vendors tab and their export as class-record overrides (no DOM; node scripts use it too). */

/** Disc folder of the item databases a price can be edited in (stem as the editor names them). */
export const ITEM_DBS: Record<string, string> = {
    RESOURES: 'COMMON\\RESOURES\\RESOURES.ODB',
    MAGIC: 'COMMON\\MAGIC\\MAGIC.ODB',
};

export interface VendorHost {
    /** Read a file of the open disc's language tree (null when absent). */
    readFile(path: string): Promise<Uint8Array | null>;
    /** Display names of Resources classes, by class id (empty when unavailable). */
    itemNames(): Promise<ReadonlyMap<number, string>>;
}

/** Pending item price edits, shared by the panel, the Details section and the export. */
export class VendorPrices {
    private readonly edits = new Map<string, number>();
    private readonly listeners = new Set<() => void>();
    private records = new Map<
        string,
        Promise<{ reader: SrscReader; classes: Awaited<ReturnType<typeof parseOdbClasses>> } | null>
    >();

    constructor(private host: VendorHost | null = null) {}

    setHost(h: VendorHost | null): void {
        this.host = h;
        this.records.clear();
    }

    get map(): PriceEdits {
        return this.edits;
    }

    count(): number {
        return this.edits.size;
    }

    subscribe(fn: () => void): () => void {
        this.listeners.add(fn);
        return () => this.listeners.delete(fn);
    }

    /** Set a price; `null` (or the disc value) removes the edit. */
    set(stem: string, id: number, price: number | null, disc?: number): void {
        const key = `${stem}:${id}`;
        if (price === null || price === disc) this.edits.delete(key);
        else this.edits.set(key, price);
        this.listeners.forEach((l) => l());
    }

    clear(): void {
        this.edits.clear();
        this.records.clear();
        this.listeners.forEach((l) => l());
    }

    exportState(): { stem: string; id: number; price: number }[] {
        return [...this.edits].map(([k, price]) => {
            const at = k.lastIndexOf(':');
            return { stem: k.slice(0, at), id: Number(k.slice(at + 1)), price };
        });
    }

    importState(list: readonly { stem: string; id: number; price: number }[] | undefined): void {
        this.edits.clear();
        for (const p of list ?? []) if (!priceProblem(p.price)) this.edits.set(`${p.stem}:${p.id}`, p.price);
        this.listeners.forEach((l) => l());
    }

    private db(stem: string) {
        let p = this.records.get(stem);
        if (!p) {
            const path = ITEM_DBS[stem];
            p = (async () => {
                if (!path || !this.host) return null;
                const bytes = await this.host.readFile(path);
                if (!bytes) return null;
                const reader = await SrscReader.fromBytes(bytes);
                return { reader, classes: await parseOdbClasses(reader) };
            })();
            this.records.set(stem, p);
        }
        return p;
    }

    /** Class-record overrides for the pending prices (rejects with a `ModError` for a price that cannot be carried). */
    async changes(): Promise<StoryEntryChange[]> {
        const sources = new Map<string, ItemRecordSource>();
        for (const key of this.edits.keys()) {
            const at = key.lastIndexOf(':');
            const stem = key.slice(0, at);
            const id = Number(key.slice(at + 1));
            const db = await this.db(stem);
            const e = db?.reader.entries.find((x) => x.type === ODB_CLASS_TYPE && x.id === id);
            const defs = db?.classes.get(id)?.fields;
            if (!db || !e || !defs) continue;
            sources.set(key, {
                file: `${stem}.ODB`,
                group: e.sub,
                record: (await db.reader.read(e)).data,
                defs,
            });
        }
        return priceChanges(this.edits, (stem, id) => sources.get(`${stem}:${id}`) ?? null);
    }
}

export const vendorPrices = new VendorPrices();

/** The shape `StorySession.addChangeSource` takes, so prices ride in the story pack. */
export const vendorChangeSource = {
    changes: () => vendorPrices.changes(),
    subscribe: (fn: () => void) => vendorPrices.subscribe(fn),
    count: () => vendorPrices.count(),
};

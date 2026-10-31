// Main-thread side of resource references: container listings with names, thumbnails (3D model renders, texture images),
// sound playback and the recently used lists the pickers show. Everything is cached for the session; the worker does the
// disc reading and decoding (refdata.ts).

import type { Api, Method } from './rpc.ts';
import type { RefEntry, RefKind, RefSound } from './refdata.ts';
import type { ModelData } from './scene/decode.ts';
import { TextureCache, classThumbnail } from './scene/index.ts';
import { store } from './editor/dom.ts';

export type { RefEntry, RefKind } from './refdata.ts';

type Call = <M extends Method>(method: M, ...args: Parameters<Api[M]>) => Promise<ReturnType<Api[M]>>;

/** Field type code (low byte of the type word) to the kind of resource it references. */
const KIND_OF_TYPE: Record<number, RefKind> = {
    4: 'model',
    5: 'sound',
    9: 'animation',
    0x0b: 'sequence',
    0x0e: 'texture',
};
export const refKindOf = (type: number): RefKind | null => KIND_OF_TYPE[type & 0xff] ?? null;

export const KIND_LABEL: Record<RefKind, string> = {
    model: 'Model',
    sound: 'Sound',
    texture: 'Texture',
    animation: 'Animation',
    sequence: 'Sequence',
};

/** Container file extension per kind, for labels. */
export const KIND_FILE: Record<RefKind, string> = {
    model: '.MOD',
    sound: '.SDB',
    texture: '.TXD',
    animation: '.ADB',
    sequence: '.SSD',
};

export interface RecentRef {
    stem: string;
    id: number;
}

export interface ThumbOptions {
    /** Pixel size of the square image. */
    size?: number;
    /** Asked again just before the work starts; when false the request is dropped (a row scrolled out of view). */
    visible?: () => boolean;
}

export interface RefService {
    /** Entries of a container (cached); null when the disc has no such container. */
    list(kind: RefKind, stem: string): Promise<RefEntry[] | null>;
    /** The entry if its container listing is already loaded (null: loaded, no such id; undefined: not loaded yet). */
    peek(kind: RefKind, stem: string, id: number): RefEntry | null | undefined;
    /** Loads the listing and returns the entry. */
    describe(kind: RefKind, stem: string, id: number): Promise<RefEntry | null>;
    /** Which stems have a container for the kind. */
    available(kind: RefKind, stems: string[]): Promise<boolean[]>;
    /** Image of the resource as a data URL (models: 3D render, textures: the picture); '' when none can be made. */
    thumb(kind: RefKind, stem: string, id: number, opts?: ThumbOptions): Promise<string>;
    /** Cached thumbnail or null. */
    thumbNow(kind: RefKind, stem: string, id: number, size?: number): string | null;
    /** Decoded sound (cached). */
    sound(stem: string, id: number): Promise<RefSound | null>;
    /** Peak amplitudes (0..1) of a sound in `bins` buckets. */
    waveform(stem: string, id: number, bins: number): Promise<Float32Array | null>;
    /** Play (or restart) a sound; resolves false when it cannot be played. */
    play(stem: string, id: number): Promise<boolean>;
    stop(): void;
    /** `stem:id` of the sound playing now. */
    playing(): string | null;
    /** Called when playback starts or ends; returns the unsubscribe function. */
    onPlayState(fn: () => void): () => void;
    recent(kind: RefKind): RecentRef[];
    pushRecent(kind: RefKind, stem: string, id: number): void;
    /** Forget everything (a different disc was opened). */
    reset(): void;
}

const THUMB_DEFAULT = 64;
const MAX_RECENT = 12;
const keyOf = (stem: string, id: number): string => `${stem}:${id}`;

function textureDataUrl(rgba: Uint8Array, w: number, h: number, size: number): string {
    const full = document.createElement('canvas');
    full.width = w;
    full.height = h;
    const g = full.getContext('2d');
    if (!g) return '';
    g.putImageData(
        new ImageData(new Uint8ClampedArray(rgba.buffer as ArrayBuffer, rgba.byteOffset, rgba.byteLength), w, h),
        0,
        0,
    );
    if (w <= size && h <= size) return full.toDataURL('image/png');
    const k = Math.min(size / w, size / h);
    const out = document.createElement('canvas');
    out.width = Math.max(1, Math.round(w * k));
    out.height = Math.max(1, Math.round(h * k));
    const o = out.getContext('2d');
    if (!o) return '';
    o.imageSmoothingEnabled = true;
    o.imageSmoothingQuality = 'high';
    o.drawImage(full, 0, 0, out.width, out.height);
    return out.toDataURL('image/png');
}

export function createRefs(call: Call): RefService {
    let lists = new Map<string, Promise<RefEntry[] | null>>();
    let index = new Map<string, Map<number, RefEntry>>();
    let thumbs = new Map<string, string>();
    let pending = new Map<string, Promise<string>>();
    let models = new Map<string, Promise<ModelData | null>>();
    let sounds = new Map<string, Promise<RefSound | null>>();
    let textureCache = new TextureCache();
    let epoch = 0;
    const jobs: { run: () => Promise<void>; drop: () => void; visible: () => boolean }[] = [];
    let pumping = false;
    const listeners: (() => void)[] = [];

    // ---- one at a time, newest request first (what the user is looking at now), dropped when it scrolled away
    function pump(): void {
        if (pumping) return;
        pumping = true;
        void (async () => {
            // Let the caller finish building its rows before the first visibility check.
            await Promise.resolve();
            while (jobs.length) {
                const job = jobs.pop()!;
                if (!job.visible()) {
                    job.drop();
                    continue;
                }
                try {
                    await job.run();
                } catch {
                    /* the placeholder stays */
                }
                await new Promise<void>((r) => requestAnimationFrame(() => r()));
            }
            pumping = false;
        })();
    }

    function list(kind: RefKind, stem: string): Promise<RefEntry[] | null> {
        const key = `${kind}:${stem}`;
        let p = lists.get(key);
        if (!p) {
            const ep = epoch;
            p = call('listRefs', stem, kind)
                .then((r) => {
                    if (r && ep === epoch) index.set(key, new Map(r.map((e) => [e.id, e])));
                    return r;
                })
                .catch(() => null);
            lists.set(key, p);
        }
        return p;
    }

    function modelData(stem: string, id: number): Promise<ModelData | null> {
        const key = keyOf(stem, id);
        let p = models.get(key);
        if (!p) {
            p = call('loadRefModel', stem, id).catch(() => null);
            models.set(key, p);
            if (models.size > 24) models.delete(models.keys().next().value!);
        }
        return p;
    }

    function sound(stem: string, id: number): Promise<RefSound | null> {
        const key = keyOf(stem, id);
        let p = sounds.get(key);
        if (!p) {
            p = call('loadRefSound', stem, id).catch(() => null);
            sounds.set(key, p);
            if (sounds.size > 24) sounds.delete(sounds.keys().next().value!);
        }
        return p;
    }

    function thumb(kind: RefKind, stem: string, id: number, opts: ThumbOptions = {}): Promise<string> {
        const size = opts.size ?? THUMB_DEFAULT;
        const key = `${kind}:${stem}:${id}@${size}`;
        const have = thumbs.get(key);
        if (have !== undefined) return Promise.resolve(have);
        if (kind !== 'model' && kind !== 'texture') return Promise.resolve('');
        const was = pending.get(key);
        if (was) return was;
        const ep = epoch;
        const p = new Promise<string>((resolve) => {
            jobs.push({
                visible: opts.visible ?? (() => true),
                drop: () => {
                    pending.delete(key);
                    resolve('');
                },
                run: async () => {
                    let url = '';
                    if (kind === 'model') {
                        const data = await modelData(stem, id);
                        if (data?.model && data.model.triTexture.length) {
                            const lookup = textureCache.addShared(data.textures);
                            url = classThumbnail(`ref:${stem}:${id}`, data.model, lookup, size, `ref:${stem}:${id}`);
                        }
                    } else {
                        const t = await call('loadRefTexture', stem, id);
                        if (t) url = textureDataUrl(t.rgba, t.width, t.height, size);
                    }
                    if (ep === epoch) thumbs.set(key, url);
                    pending.delete(key);
                    resolve(url);
                },
            });
            pump();
        });
        pending.set(key, p);
        return p;
    }

    // ---- sound playback (Web Audio, one voice)
    let audio: AudioContext | null = null;
    let source: AudioBufferSourceNode | null = null;
    let nowPlaying: string | null = null;
    const notifyPlay = (): void => listeners.forEach((f) => f());

    function stop(): void {
        const s = source;
        source = null;
        if (s) {
            s.onended = null;
            try {
                s.stop();
            } catch {
                /* already stopped */
            }
        }
        if (nowPlaying !== null) {
            nowPlaying = null;
            notifyPlay();
        }
    }

    async function play(stem: string, id: number): Promise<boolean> {
        stop();
        try {
            audio ??= new AudioContext();
            if (audio.state === 'suspended') await audio.resume();
            const s = await sound(stem, id);
            if (!s || !s.pcm.length) return false;
            const buf = audio.createBuffer(1, s.pcm.length, s.rate);
            const ch = buf.getChannelData(0);
            for (let i = 0; i < s.pcm.length; i++) ch[i] = s.pcm[i]! / 32768;
            const node = audio.createBufferSource();
            node.buffer = buf;
            node.connect(audio.destination);
            node.onended = () => {
                if (source === node) {
                    source = null;
                    nowPlaying = null;
                    notifyPlay();
                }
            };
            source = node;
            nowPlaying = keyOf(stem, id);
            node.start();
            notifyPlay();
            return true;
        } catch {
            return false;
        }
    }

    function recent(kind: RefKind): RecentRef[] {
        const v = store.json<unknown>(`recent-ref-${kind}`, []);
        return Array.isArray(v)
            ? v.filter(
                  (x): x is RecentRef =>
                      !!x && typeof (x as RecentRef).stem === 'string' && Number.isInteger((x as RecentRef).id),
              )
            : [];
    }

    return {
        list,
        peek(kind, stem, id) {
            const m = index.get(`${kind}:${stem}`);
            return m ? (m.get(id) ?? null) : undefined;
        },
        async describe(kind, stem, id) {
            await list(kind, stem);
            return index.get(`${kind}:${stem}`)?.get(id) ?? null;
        },
        available(kind, stems) {
            return call('refContainers', kind, stems).catch(() => stems.map(() => false));
        },
        thumb,
        thumbNow(kind, stem, id, size = THUMB_DEFAULT) {
            const t = thumbs.get(`${kind}:${stem}:${id}@${size}`);
            return t ? t : null;
        },
        sound,
        async waveform(stem, id, bins) {
            const s = await sound(stem, id);
            if (!s || !s.pcm.length) return null;
            const out = new Float32Array(bins);
            const per = s.pcm.length / bins;
            for (let b = 0; b < bins; b++) {
                let peak = 0;
                const from = Math.floor(b * per);
                const to = Math.min(s.pcm.length, Math.max(from + 1, Math.floor((b + 1) * per)));
                for (let i = from; i < to; i++) peak = Math.max(peak, Math.abs(s.pcm[i]!));
                out[b] = peak / 32768;
            }
            return out;
        },
        play,
        stop,
        playing: () => nowPlaying,
        onPlayState(fn) {
            listeners.push(fn);
            return () => {
                const i = listeners.indexOf(fn);
                if (i >= 0) listeners.splice(i, 1);
            };
        },
        recent,
        pushRecent(kind, stem, id) {
            const next = [{ stem, id }, ...recent(kind).filter((r) => !(r.stem === stem && r.id === id))].slice(
                0,
                MAX_RECENT,
            );
            store.set(`recent-ref-${kind}`, JSON.stringify(next));
        },
        reset() {
            epoch++;
            stop();
            lists = new Map();
            index = new Map();
            thumbs = new Map();
            pending = new Map();
            models = new Map();
            sounds = new Map();
            textureCache = new TextureCache();
            jobs.length = 0;
        },
    };
}

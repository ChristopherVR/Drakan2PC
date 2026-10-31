import type { ClassSchema, SchemaProvider } from '@drakan/editor-core';
import { KIND_FILE, KIND_LABEL, type RecentRef, type RefEntry, type RefKind, type RefService } from '../refs.ts';
import { fill, h, icon } from './dom.ts';
import { classModelOf, createAnimPreview } from './animpreview.ts';
import { openPopover } from './popover.ts';
import { virtualList, type VList } from './vlist.ts';

/**
 * Pickers and field buttons for resource references (model, sound, texture, animation, sequence). A reference is
 * `(db << 16) | id`; `db` picks one of the owning class's containers, so the picker offers entries of every container the
 * class can reach, with the names stored in the container and a picture or sound for each.
 */

export interface RefTarget {
    stem: string;
    id: number;
}

export interface RefField {
    kind: RefKind;
    /** Field name, for titles. */
    name: string;
    type: number;
    schema: ClassSchema | null;
    provider: SchemaProvider | null;
    /** The value now; null when the selection disagrees. */
    get(): number | null;
    /** Store the chosen entry (null clears the field to 0). */
    pick(target: RefTarget | null): void;
    /** Store a raw 32-bit value. */
    setRaw(value: number): void;
    /** Back to the class default (omitted for list elements). */
    reset?: () => void;
    /** The class default, shown in the footer. */
    defaultText?: string;
}

const hexRef = (v: number): string => `0x${(v >>> 0).toString(16)}`;
const KIND_ICON: Record<RefKind, string> = {
    model: 'box',
    sound: 'sound',
    texture: 'layers',
    animation: 'history',
    sequence: 'camera',
};
const ROW_H = 46;
const THUMB = 72;

interface Item {
    stem: string;
    id: number;
    name: string;
    detail: string;
    w?: number;
    h?: number;
    seconds?: number;
    /** Extra right-hand note (usage counts on the Suggested tab). */
    note?: string;
}

export const itemKey = (i: { stem: string; id: number }): string => `${i.stem}:${i.id}`;

export function targetOf(f: RefField, value: number | null): { target: RefTarget | null; db: number } | null {
    if (value === null || value === 0 || value === 0xffffffff || !f.provider || !f.schema) return null;
    const t = f.provider.resolveRef(f.schema, f.type, value);
    return { target: t.stem ? { stem: t.stem, id: t.id } : null, db: t.db };
}

/** Container stems the owning class can address: its own (db 0) first, then its .DB dependencies. */
export function reachableStems(schema: ClassSchema | null): string[] {
    if (!schema) return [];
    const out = [schema.db];
    for (const s of schema.deps.values()) if (!out.includes(s)) out.push(s);
    return out;
}

function displayName(kind: RefKind, e: { id: number; name: string } | null, id: number): string {
    return e?.name ? e.name : `${KIND_LABEL[kind]} #${e?.id ?? id}`;
}

const itemOf = (stem: string, e: RefEntry): Item => ({
    stem,
    id: e.id,
    name: e.name,
    detail: e.detail,
    ...(e.w ? { w: e.w } : {}),
    ...(e.h ? { h: e.h } : {}),
    ...(e.seconds !== undefined ? { seconds: e.seconds } : {}),
});

/** Picture box for an entry; fills in the thumbnail when it is made. */
export function thumbBox(
    refs: RefService,
    kind: RefKind,
    stem: string,
    id: number,
    size: number,
    visible: () => boolean = () => true,
): HTMLElement {
    const box = h('span', { class: `rthumb k-${kind}`, style: `width:${size}px;height:${size}px` });
    const set = (url: string): boolean => {
        if (!url) return false;
        box.replaceChildren(h('img', { src: url, alt: '', draggable: false }));
        return true;
    };
    box.append(icon(KIND_ICON[kind], Math.round(size * 0.5)));
    if (kind === 'model' || kind === 'texture') {
        const have = refs.thumbNow(kind, stem, id, THUMB);
        if (have) set(have);
        else
            void refs.thumb(kind, stem, id, { size: THUMB, visible }).then((u) => {
                if (u) set(u);
            });
    }
    return box;
}

function playButton(refs: RefService, stem: string, id: number, small = false): HTMLButtonElement {
    const key = `${stem}:${id}`;
    const btn = h('button', {
        class: `tool sm rp-play${small ? ' mini' : ''}`,
        type: 'button',
        'aria-label': 'Play sound',
        title: 'Play (Ctrl+Enter in the picker)',
    });
    const paint = (): void => {
        const on = refs.playing() === key;
        btn.classList.toggle('on', on);
        btn.replaceChildren(icon(on ? 'x' : 'play', 12));
        btn.title = on ? 'Stop' : 'Play';
        btn.setAttribute('aria-label', on ? 'Stop sound' : 'Play sound');
    };
    paint();
    const off = refs.onPlayState(() => {
        if (!btn.isConnected) return off();
        paint();
    });
    btn.addEventListener('click', (e) => {
        e.stopPropagation();
        if (refs.playing() === key) refs.stop();
        else void refs.play(stem, id);
    });
    // Rows are rebuilt on scroll: do not let a click on the button also pick the row.
    btn.addEventListener('pointerdown', (e) => e.stopPropagation());
    return btn;
}

// ---------------------------------------------------------------- field button

/** The button a field row shows: picture, name and where it lives. Clicking opens the picker. */
export function refButton(refs: RefService, f: RefField, open: (anchor: HTMLElement) => void): HTMLElement {
    const btn = h('button', { class: 'refbtn two', type: 'button', 'aria-haspopup': 'dialog' });
    const play = h('span', { class: 'rb-play' });
    let last = '';
    let seq = 0;
    const paint = (): void => {
        const v = f.get();
        const sig = String(v);
        if (sig === last) return;
        last = sig;
        const mine = ++seq;
        if (v === null) {
            fill(
                btn,
                h('span', { class: 'rthumb', style: 'width:28px;height:28px' }),
                h('span', { class: 'rb-text' }, h('span', { class: 'rl dim' }, 'Mixed values')),
            );
            btn.title = 'The selected objects have different values; pick one to set them all';
            play.replaceChildren();
            return;
        }
        if (v === 0 || v === 0xffffffff) {
            fill(
                btn,
                h('span', { class: 'rthumb', style: 'width:28px;height:28px' }, icon(KIND_ICON[f.kind], 14)),
                h('span', { class: 'rb-text' }, h('span', { class: 'rl dim' }, 'None')),
            );
            btn.title = `No ${KIND_LABEL[f.kind].toLowerCase()}\nvalue ${hexRef(v)}\nClick to choose one`;
            play.replaceChildren();
            return;
        }
        const r = targetOf(f, v);
        const t = r?.target ?? null;
        const render = (e: RefEntry | null | undefined): void => {
            if (mine !== seq) return;
            const bad = !t || e === null;
            const sub = !t
                ? `db ${r?.db ?? '?'} is not in this class's dependency list`
                : e === null
                  ? `not found in ${t.stem}${KIND_FILE[f.kind]}`
                  : [`${t.stem} #${t.id}`, e?.detail].filter(Boolean).join('  ');
            fill(
                btn,
                t
                    ? thumbBox(refs, f.kind, t.stem, t.id, 28)
                    : h('span', { class: 'rthumb', style: 'width:28px;height:28px' }),
                h(
                    'span',
                    { class: 'rb-text' },
                    h(
                        'span',
                        { class: `rl${bad ? ' bad' : ''}` },
                        t ? displayName(f.kind, e ?? null, t.id) : hexRef(v),
                    ),
                    h('span', { class: 'rs dim' }, sub),
                ),
            );
            btn.title = `${t ? displayName(f.kind, e ?? null, t.id) : hexRef(v)}\n${sub}\nvalue ${hexRef(v)}\nClick to choose another`;
            play.replaceChildren(f.kind === 'sound' && t && e ? playButton(refs, t.stem, t.id, true) : '');
        };
        if (!t) return render(null);
        const hit = refs.peek(f.kind, t.stem, t.id);
        render(hit);
        if (hit === undefined) {
            void refs.describe(f.kind, t.stem, t.id).then((e) => {
                if (mine === seq) render(e);
            });
        }
    };
    btn.addEventListener('click', () => open(btn));
    paint();
    const wrap = h('span', { class: 'rb-wrap' }, btn, play);
    // Keep the button current when the value changes elsewhere (undo, multi-edit).
    (wrap as HTMLElement & { update?: () => void }).update = paint;
    return wrap;
}

// ---------------------------------------------------------------- picker

type Tab = 'suggested' | 'recent' | 'browse';

export function openRefPicker(
    refs: RefService,
    anchor: HTMLElement,
    f: RefField,
    /** Entries other classes use for this field, most used first (`SchemaProvider.refValues`). */
    suggested: readonly { stem: string; id: number; count: number; examples: string[] }[],
    /** True when `stem` can be written by the first owner (otherwise the entry is shown dimmed and cannot be picked). */
    canPick: (stem: string) => boolean,
): void {
    const kind = f.kind;
    const reach = reachableStems(f.schema);
    const cur = targetOf(f, f.get())?.target ?? null;
    const recents = refs.recent(kind).filter((r) => reach.includes(r.stem));
    const sugg = suggested.filter((s) => reach.includes(s.stem));
    let tab: Tab = cur ? 'browse' : sugg.length ? 'suggested' : recents.length ? 'recent' : 'browse';
    let scope = cur && reach.includes(cur.stem) ? cur.stem : '*';
    let available: string[] = reach;
    let query = '';
    let disposed = false;
    const unsub: (() => void)[] = [];

    openPopover(
        anchor,
        (close) => {
            const finish = (): void => {
                refs.stop();
                close();
            };
            const search = h('input', {
                type: 'search',
                class: 'rp-search',
                placeholder: `Search ${KIND_LABEL[kind].toLowerCase()}s by name or id`,
                'aria-label': `Search ${KIND_LABEL[kind].toLowerCase()}s`,
                spellcheck: 'false',
                autocomplete: 'off',
            });
            const tabsEl = h('div', { class: 'seg', role: 'tablist' });
            const scopeSel = h('select', { class: 'sel rp-scope', 'aria-label': 'Container' });
            const status = h('div', { class: 'rp-status dim' });
            const preview = h('div', { class: 'rp-preview' });

            const choose = (it: Item): void => {
                if (!canPick(it.stem)) return;
                refs.pushRecent(kind, it.stem, it.id);
                f.pick({ stem: it.stem, id: it.id });
                finish();
            };

            const list: VList<Item> = virtualList<Item>({
                rowHeight: kind === 'texture' ? 92 : ROW_H,
                ...(kind === 'texture' ? { cellWidth: 92 } : {}),
                key: itemKey,
                label: `${KIND_LABEL[kind]} list`,
                onPick: choose,
                onActive: (it) => showPreview(it),
                onVisible: () => {
                    /* thumbnails are asked for by the row builders, `isVisible` filters stale requests */
                },
                empty: () => h('div', { class: 'note rp-empty' }, status.textContent || 'Nothing to show.'),
                row: (it): HTMLElement => {
                    const isCur = !!cur && cur.stem === it.stem && cur.id === it.id;
                    const ok = canPick(it.stem);
                    const nameText = displayName(kind, it, it.id);
                    const k = itemKey(it);
                    if (kind === 'texture') {
                        return h(
                            'div',
                            {
                                class: `rp-cell${isCur ? ' current' : ''}${ok ? '' : ' disabled'}`,
                                title: `${nameText}
${it.detail ?? ''}
${it.note ?? `${it.stem} #${it.id}`}`,
                            },
                            thumbBox(refs, kind, it.stem, it.id, 64, () => list.isVisible(k)),
                            h('span', { class: 'rp-cname' }, nameText),
                        );
                    }
                    return h(
                        'div',
                        {
                            class: `rp-row${isCur ? ' current' : ''}${ok ? '' : ' disabled'}`,
                            title: ok ? '' : `Not reachable from ${f.schema?.name ?? 'this class'}`,
                        },
                        thumbBox(refs, kind, it.stem, it.id, 36, () => list.isVisible(k)),
                        h(
                            'span',
                            { class: 'rp-main' },
                            h(
                                'span',
                                { class: 'rp-name' },
                                nameText,
                                isCur ? h('span', { class: 'rp-cur' }, 'current') : null,
                            ),
                            h('span', { class: 'rp-sub dim' }, it.detail || KIND_FILE[kind]),
                        ),
                        h('span', { class: 'rp-where dim' }, it.note ?? `${it.stem} #${it.id}`),
                        kind === 'sound' ? playButton(refs, it.stem, it.id) : null,
                    );
                },
            });

            // ---- items for the current tab
            let gen = 0;
            const gather = async (): Promise<Item[]> => {
                const mine = ++gen;
                const stems =
                    tab === 'browse'
                        ? scope === '*'
                            ? available
                            : [scope]
                        : [...new Set((tab === 'suggested' ? sugg : recents).map((x) => x.stem))];
                status.textContent = stems.length > 1 ? `Loading ${stems.length} containers...` : 'Loading...';
                const loaded = new Map<string, Map<number, RefEntry>>();
                const rows: { stem: string; entries: RefEntry[] }[] = [];
                for (const s of stems) {
                    const entries = await refs.list(kind, s);
                    if (mine !== gen || disposed) return [];
                    if (entries) {
                        loaded.set(s, new Map(entries.map((e) => [e.id, e])));
                        rows.push({ stem: s, entries });
                    }
                }
                const named = (x: { stem: string; id: number }): Item => {
                    const e = loaded.get(x.stem)?.get(x.id);
                    return e
                        ? itemOf(x.stem, e)
                        : { stem: x.stem, id: x.id, name: '', detail: `not in ${x.stem}${KIND_FILE[kind]}` };
                };
                if (tab === 'suggested')
                    return sugg.map((s) => ({
                        ...named(s),
                        note: `${s.count}x ${s.stem} #${s.id}`,
                    }));
                if (tab === 'recent') return recents.map((r: RecentRef) => named(r));
                const all = rows.flatMap((r) => r.entries.map((e) => itemOf(r.stem, e)));
                const byName = kind !== 'texture';
                all.sort((a, b) => {
                    if (byName) {
                        if (!!a.name !== !!b.name) return a.name ? -1 : 1;
                        const c = a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: 'base' });
                        if (c) return c;
                    }
                    return a.stem.localeCompare(b.stem) || a.id - b.id;
                });
                return all;
            };

            const matches = (it: Item, tokens: string[]): boolean => {
                if (!tokens.length) return true;
                const hay = `${it.name} ${it.detail} ${it.stem} #${it.id} ${it.id} ${hexRef(it.id)}`.toLowerCase();
                return tokens.every((t) => hay.includes(t));
            };

            let current: Item[] = [];
            const apply = (keepKey?: string): void => {
                const tokens = query.toLowerCase().split(/\s+/).filter(Boolean);
                const shown = current.filter((it) => matches(it, tokens));
                const total = current.length;
                status.textContent = shown.length
                    ? `${shown.length}${shown.length === total ? '' : ` of ${total}`} ${KIND_LABEL[kind].toLowerCase()}${shown.length === 1 ? '' : 's'}`
                    : tab === 'recent' && !total
                      ? 'Nothing picked yet. Entries you choose show up here.'
                      : tab === 'suggested' && !total
                        ? 'No other class sets this field.'
                        : total
                          ? 'No match.'
                          : `No ${KIND_LABEL[kind].toLowerCase()} container on this disc for ${scope === '*' ? 'these classes' : scope}.`;
                list.setItems(shown, keepKey);
                statusLine.textContent = shown.length ? status.textContent : '';
            };
            const statusLine = h('span', { class: 'dim' });

            const reload = async (): Promise<void> => {
                const key = list.active() ? itemKey(list.active()!) : undefined;
                current = await gather();
                if (disposed) return;
                const target = cur && tab === 'browse' && !query ? itemKey(cur) : key;
                apply(target);
            };

            const paintTabs = (): void => {
                const mk = (id: Tab, label: string, n: number | null): HTMLElement =>
                    h(
                        'button',
                        {
                            class: `seg-btn${tab === id ? ' active' : ''}`,
                            type: 'button',
                            role: 'tab',
                            'aria-selected': String(tab === id),
                            onclick: () => {
                                tab = id;
                                paintTabs();
                                void reload();
                                search.focus();
                            },
                        },
                        label,
                        n === null ? null : h('span', { class: 'dim' }, String(n)),
                    );
                fill(
                    tabsEl,
                    mk('suggested', 'Suggested', sugg.length),
                    mk('recent', 'Recent', recents.length),
                    mk('browse', 'Browse', null),
                );
                scopeSel.hidden = tab !== 'browse';
            };

            const paintScope = (): void => {
                fill(
                    scopeSel,
                    h('option', { value: '*' }, `All containers (${available.length})`),
                    ...available.map((s) => h('option', { value: s }, `${s}${KIND_FILE[kind]}`)),
                );
                scopeSel.value = available.includes(scope) || scope === '*' ? scope : '*';
            };
            scopeSel.addEventListener('change', () => {
                scope = scopeSel.value;
                void reload();
            });

            // ---- preview
            let previewKey = '';
            let previewTimer = 0;
            function showPreview(it: Item | null): void {
                window.clearTimeout(previewTimer);
                previewTimer = window.setTimeout(() => void paintPreview(it), it ? 60 : 0);
            }
            async function paintPreview(it: Item | null): Promise<void> {
                if (!it) {
                    previewKey = '';
                    return fill(preview, h('div', { class: 'note' }, 'Hover or arrow to an entry to preview it.'));
                }
                const key = `${kind}:${itemKey(it)}`;
                if (key === previewKey) return;
                previewKey = key;
                const facts = h(
                    'dl',
                    { class: 'rp-facts' },
                    h('dt', {}, 'Container'),
                    h('dd', {}, `${it.stem}${KIND_FILE[kind]}`),
                    h('dt', {}, 'Id'),
                    h('dd', { class: 'mono' }, `#${it.id}`),
                );
                const stage = h('div', { class: `rp-stage k-${kind}` });
                if (kind === 'model' || kind === 'texture') {
                    stage.append(icon(KIND_ICON[kind], 40));
                    const big = 192;
                    void refs
                        .thumb(kind, it.stem, it.id, { size: big, visible: () => previewKey === key })
                        .then((url) => {
                            if (url && previewKey === key) stage.replaceChildren(h('img', { src: url, alt: '' }));
                        });
                } else if (kind === 'sound') {
                    const canvas = h('canvas', { class: 'rp-wave', width: 240, height: 80 });
                    stage.append(canvas);
                    void refs.waveform(it.stem, it.id, 120).then((w) => {
                        if (!w || previewKey !== key) return;
                        const g = canvas.getContext('2d');
                        if (!g) return;
                        const style = getComputedStyle(canvas);
                        g.clearRect(0, 0, canvas.width, canvas.height);
                        g.fillStyle = style.getPropertyValue('--accent') || '#5aa2ff';
                        const mid = canvas.height / 2;
                        const bw = canvas.width / w.length;
                        for (let i = 0; i < w.length; i++) {
                            const hh = Math.max(1, w[i]! * (canvas.height - 4));
                            g.fillRect(i * bw + 0.5, mid - hh / 2, Math.max(1, bw - 1), hh);
                        }
                    });
                } else stage.append(icon(KIND_ICON[kind], 40));
                const head = h('div', { class: 'rp-ptitle' }, displayName(kind, it, it.id));
                const body: Node[] = [stage, head, facts];
                if (it.w) {
                    facts.append(h('dt', {}, 'Size'), h('dd', {}, `${it.w} x ${it.h}`));
                }
                if (it.detail) facts.append(h('dt', {}, 'Info'), h('dd', {}, it.detail));
                if (kind === 'sound')
                    body.push(
                        h(
                            'div',
                            { class: 'rp-actions' },
                            playButton(refs, it.stem, it.id),
                            h('span', { class: 'dim' }, 'Ctrl+Enter'),
                        ),
                    );
                if (kind === 'animation') {
                    const ap = createAnimPreview(refs, { height: 170, compact: true });
                    body.splice(0, 1, ap.el);
                    void (async () => {
                        // The owning class's model; a class without one borrows a model of the clip's container that fits.
                        let m = await classModelOf(refs, f.schema);
                        if (previewKey !== key) return ap.dispose();
                        await ap.setClip({ stem: it.stem, id: it.id });
                        if (previewKey !== key) return ap.dispose();
                        if (!m) {
                            const clip = await refs.clip(it.stem, it.id);
                            m = clip ? await refs.modelForTracks(it.stem, clip.tracks.length) : null;
                        }
                        if (previewKey !== key) return ap.dispose();
                        await ap.setModel(m);
                    })();
                }
                if (kind === 'sequence')
                    body.push(
                        h(
                            'div',
                            { class: 'note' },
                            'Cutscene sequence from the level sound script (SSD 0x311, inferred).',
                        ),
                    );
                fill(preview, ...body);
            }

            // ---- footer: clear, reset, raw value
            const raw = h('input', {
                type: 'text',
                class: 'txt mono rp-raw',
                'aria-label': `${f.name} raw value`,
                spellcheck: 'false',
                value: f.get() === null ? '' : hexRef(f.get()!),
                placeholder: 'mixed',
            });
            raw.addEventListener('change', () => {
                const n = Number(raw.value.trim());
                if (Number.isInteger(n) && n >= 0 && n <= 0xffffffff) {
                    f.setRaw(n);
                    finish();
                } else raw.value = f.get() === null ? '' : hexRef(f.get()!);
            });
            const footer = h(
                'div',
                { class: 'rp-footer' },
                h(
                    'button',
                    {
                        class: 'btn sm',
                        type: 'button',
                        onclick: () => {
                            f.pick(null);
                            finish();
                        },
                    },
                    icon('x', 12),
                    'None',
                ),
                f.reset
                    ? h(
                          'button',
                          {
                              class: 'btn sm',
                              type: 'button',
                              title: f.defaultText ? `Class default: ${f.defaultText}` : 'Class default',
                              onclick: () => {
                                  f.reset?.();
                                  finish();
                              },
                          },
                          icon('undo', 12),
                          'Default',
                      )
                    : null,
                h('span', { class: 'spacer' }),
                h('label', { class: 'dim rp-rawl' }, 'Raw', raw),
            );
            const hint = h(
                'div',
                { class: 'rp-hint dim' },
                statusLine,
                h('span', { class: 'spacer' }),
                h('span', {}, 'Up/Down move, Enter pick, '),
                kind === 'sound' ? h('span', {}, 'Ctrl+Enter play, ') : null,
                h('span', {}, 'Esc close'),
            );

            // ---- keyboard (the search box keeps focus)
            const el = h(
                'div',
                { class: 'rp' },
                h(
                    'div',
                    { class: 'rp-head' },
                    h('div', { class: 'pop-title' }, `${f.name}`),
                    h('span', { class: 'dim' }, `${KIND_LABEL[kind]} reference`),
                ),
                h('div', { class: 'rp-bar' }, search),
                h('div', { class: 'rp-bar' }, tabsEl, scopeSel),
                h('div', { class: 'rp-cols' }, h('div', { class: 'rp-listcol' }, list.el, hint), preview),
                footer,
            );
            el.addEventListener('keydown', (e) => {
                const k = e.key;
                if (k === 'ArrowDown') list.move(1);
                else if (k === 'ArrowUp') list.move(-1);
                else if (k === 'ArrowRight' && kind === 'texture' && !search.value) list.step(1);
                else if (k === 'ArrowLeft' && kind === 'texture' && !search.value) list.step(-1);
                else if (k === 'PageDown') list.move(7);
                else if (k === 'PageUp') list.move(-7);
                else if (k === 'Home' && e.ctrlKey) list.moveTo(0);
                else if (k === 'End' && e.ctrlKey) list.moveTo(1e9);
                else if (k === 'Enter') {
                    const it = list.active();
                    if (e.ctrlKey || e.metaKey) {
                        if (kind === 'sound' && it) {
                            if (refs.playing() === itemKey(it)) refs.stop();
                            else void refs.play(it.stem, it.id);
                        }
                    } else if (it) choose(it);
                } else return;
                e.preventDefault();
                e.stopPropagation();
            });
            search.addEventListener('input', () => {
                query = search.value;
                apply();
            });

            paintTabs();
            paintScope();
            showPreview(null);
            // Which reachable containers exist for this kind, then the first listing.
            void refs.available(kind, reach).then((ok) => {
                if (disposed) return;
                available = reach.filter((_, i) => ok[i]);
                if (scope !== '*' && !available.includes(scope)) scope = '*';
                paintScope();
                void reload();
            });
            queueMicrotask(() => search.focus());
            return el;
        },
        {
            className: 'ed-pop-ref',
            onClose: () => {
                disposed = true;
                for (const u of unsub) u();
            },
        },
    );
}

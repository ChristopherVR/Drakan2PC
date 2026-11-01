import {
    FIELD_CATEGORIES,
    argbToParts,
    effectiveValue,
    fieldCategory,
    partsToArgb,
    resetObjectField,
    sameFieldValue,
    searchCatalog,
    setObjectField,
    suggestValues,
    type Suggestion,
    typeCode,
    typeName,
    widgetFor,
    type AssetClass,
    type ClassSchema,
    type DocField,
    type FieldCategory,
    type LevelObject,
    type SchemaField,
} from '@drakan/editor-core';
import { decodeElement, encodeElement, enumNames, refParts } from '@drakan/formats';
import type { Ctx } from './ctx.ts';
import { fill, h, icon } from './dom.ts';
import { attachScrub } from './scrub.ts';
import { closePopover, openPopover } from './popover.ts';
import { openRefPicker, refButton, type RefField } from './refpicker.ts';
import { refKindOf } from '../refs.ts';

export { closePopover };

/**
 * Inspector "Fields": every field of the selected objects' classes (not only the overrides), grouped, typed and editable.
 * Each field shows the effective value (the object's override, else the class default from the ODB), marks overridden
 * values and can reset to the default. Edits go through `ctx.edit`, i.e. one undo step per edit, over the whole selection.
 */

type Value = DocField['value'];

interface Owner {
    id: number;
    schema: ClassSchema | null;
    field: SchemaField;
}

interface Row {
    key: string;
    name: string;
    type: number;
    category: FieldCategory;
    owners: Owner[];
}

/** UI state that survives re-renders (selection changes). */
const state = {
    query: '',
    changed: false,
    catOpen: new Map<string, boolean>(),
    arrOpen: new Set<string>(),
};

/** The field filter is shared with the search box at the top of the Details panel. */
export const fieldQuery = {
    get: (): string => state.query,
    set: (q: string): void => {
        state.query = q;
    },
};

const fmtNum = (v: number, float: boolean): string => (float ? String(Number(v.toPrecision(7))) : String(v));
const hexRef = (v: number): string => `0x${(v >>> 0).toString(16)}`;

function stepFor(v: number): number {
    const a = Math.abs(v);
    return a >= 1000 ? 10 : a >= 100 ? 1 : a >= 10 ? 0.1 : a >= 1 ? 0.05 : 0.01;
}

function buildRows(ctx: Ctx, objs: readonly LevelObject[]): Row[] {
    const provider = ctx.schemas();
    const rows = new Map<string, Row>();
    for (const o of objs) {
        const schema = provider?.schemaFor(o.resource) ?? null;
        const seen = new Map<string, number>();
        const add = (sf: SchemaField): void => {
            const base = `${sf.name}\u0000${sf.type}`;
            const n = seen.get(base) ?? 0;
            seen.set(base, n + 1);
            const key = `${base}\u0000${n}`;
            let r = rows.get(key);
            if (!r) rows.set(key, (r = { key, name: sf.name, type: sf.type, category: sf.category, owners: [] }));
            r.owners.push({ id: o.id, schema, field: sf });
        };
        if (schema) schema.fields.forEach(add);
        else {
            for (const f of o.fields ?? []) {
                add({
                    index: f.fieldIndex,
                    type: f.type,
                    name: f.name,
                    default: Array.isArray(f.value) ? [...f.value] : f.value,
                    category: fieldCategory(f.name, f.type),
                    unknownDefault: true,
                });
            }
        }
    }
    return [...rows.values()];
}

const catalogIndex = new WeakMap<AssetClass[], Map<string, AssetClass>>();
function classByResource(ctx: Ctx, resource: string): AssetClass | undefined {
    const cat = ctx.catalog();
    let m = catalogIndex.get(cat);
    if (!m) catalogIndex.set(cat, (m = new Map(cat.map((c) => [c.resource, c]))));
    return m.get(resource);
}

function thumbBox(ctx: Ctx, cls: AssetClass | undefined, size: number): HTMLElement {
    const box = h('span', { class: 'rthumb', style: `width:${size}px;height:${size}px` });
    const set = (): boolean => {
        const url = cls ? ctx.classThumb(cls) : null;
        if (url) {
            box.replaceChildren(h('img', { src: url, alt: '', draggable: false }));
            return true;
        }
        if (!box.firstChild) box.append(icon(cls?.icon ?? 'generic', Math.round(size * 0.6)));
        return false;
    };
    if (!set() && cls) {
        let tries = 0;
        const t = setInterval(() => {
            if (!box.isConnected || set() || ++tries > 40) clearInterval(t);
        }, 500);
    }
    return box;
}

/** Searchable class list with thumbnails. `pick(null)` clears (value 0). */
function classPicker(
    ctx: Ctx,
    anchor: HTMLElement,
    opts: {
        title: string;
        canPick: (c: AssetClass) => boolean;
        pick: (c: AssetClass | null) => void;
        allowNone: boolean;
    },
): void {
    openPopover(anchor, (close) => {
        const search = h('input', { type: 'search', placeholder: 'Search classes', 'aria-label': 'Search classes' });
        const list = h('div', { class: 'pop-list' });
        const all = [...ctx.catalog()].sort((a, b) => b.instances - a.instances || a.name.localeCompare(b.name));
        const render = (): void => {
            const hits = searchCatalog(all, search.value).slice(0, 120);
            const items: Node[] = [];
            if (opts.allowNone && !search.value.trim()) {
                items.push(
                    h(
                        'button',
                        {
                            class: 'pop-item',
                            type: 'button',
                            onclick: () => {
                                opts.pick(null);
                                close();
                            },
                        },
                        h('span', { class: 'rthumb', style: 'width:30px;height:30px' }, icon('x', 14)),
                        h('span', { class: 'pi-name' }, 'None'),
                        h('span', { class: 'dim' }, 'clear (0)'),
                    ),
                );
            }
            for (const c of hits) {
                const ok = opts.canPick(c);
                items.push(
                    h(
                        'button',
                        {
                            class: `pop-item${ok ? '' : ' disabled'}`,
                            type: 'button',
                            disabled: !ok,
                            title: ok
                                ? `${c.name} (${c.db} #${c.classId})`
                                : 'This class cannot be referenced from here',
                            onclick: () => {
                                opts.pick(c);
                                close();
                            },
                        },
                        thumbBox(ctx, c, 30),
                        h('span', { class: 'pi-name' }, c.name),
                        h(
                            'span',
                            { class: 'dim' },
                            c.instances ? `${c.category} - ${c.instances} in level` : c.category,
                        ),
                    ),
                );
            }
            if (!hits.length) items.push(h('div', { class: 'note' }, 'No class matches.'));
            else if (hits.length === 120)
                items.push(h('div', { class: 'note' }, 'Showing the first 120; refine the search.'));
            fill(list, ...items);
        };
        search.addEventListener('input', render);
        render();
        queueMicrotask(() => search.focus());
        return h('div', { class: 'pop-body' }, h('div', { class: 'pop-title' }, opts.title), search, list);
    });
}

/** List of resources other classes use for a field (sounds, models, textures, ...). */
function resourcePicker(
    ctx: Ctx,
    anchor: HTMLElement,
    row: Row,
    owner: Owner,
    pick: (stem: string, id: number) => void,
): void {
    const provider = ctx.schemas();
    if (!provider) return;
    openPopover(anchor, (close) => {
        const search = h('input', {
            type: 'search',
            placeholder: 'Search classes',
            'aria-label': 'Search',
        });
        const list = h('div', { class: 'pop-list' });
        const all = provider.refValues(row.name, row.type);
        const render = (): void => {
            const q = search.value.trim().toLowerCase();
            const hits = all
                .filter(
                    (e) =>
                        !q ||
                        e.stem.toLowerCase().includes(q) ||
                        String(e.id) === q ||
                        hexRef(e.id).includes(q.replace(/^0x/, '0x')) ||
                        e.examples.some((x) => x.toLowerCase().includes(q)),
                )
                .slice(0, 150);
            fill(
                list,
                ...hits.map((e) => {
                    const ok = owner.schema ? provider.refValueFor(owner.schema, e.stem, e.id) !== null : false;
                    return h(
                        'button',
                        {
                            class: `pop-item${ok ? '' : ' disabled'}`,
                            type: 'button',
                            disabled: !ok,
                            title: ok ? '' : `${owner.schema?.name ?? 'This class'} cannot reference ${e.stem}`,
                            onclick: () => {
                                pick(e.stem, e.id);
                                close();
                            },
                        },
                        h('span', { class: 'pi-name mono' }, `${e.stem} #${e.id}`),
                        h('span', { class: 'dim' }, `${e.count}x: ${e.examples.join(', ')}`),
                    );
                }),
                hits.length ? null : h('div', { class: 'note' }, 'No other class uses a value for this field.'),
            );
        };
        search.addEventListener('input', render);
        render();
        queueMicrotask(() => search.focus());
        return h(
            'div',
            { class: 'pop-body' },
            h('div', { class: 'pop-title' }, `${row.name}: values used by shipped classes`),
            search,
            list,
        );
    });
}

// ---------------------------------------------------------------- one field row

interface Built {
    el: HTMLElement;
    update: () => void;
    changed: () => boolean;
}

function buildRow(ctx: Ctx, row: Row, objCount: number): Built {
    const view = ctx.view();
    const provider = ctx.schemas();
    const base = typeCode(row.type);
    const isFloat = base === 2;
    const w = widgetFor(row.type);
    const updates: (() => void)[] = [];

    const live = (ow: Owner): { value: Value; overridden: boolean } | undefined => {
        const o = ctx.doc().get(ow.id);
        return o ? effectiveValue(o, ow.field) : undefined;
    };
    const vals = (): { value: Value; overridden: boolean }[] =>
        row.owners.map(live).filter((x): x is { value: Value; overridden: boolean } => !!x);
    const mixed = (): boolean => {
        const v = vals();
        return v.some((x) => !sameFieldValue(row.type, x.value, v[0]!.value));
    };
    const cur = (): Value | null => (mixed() ? null : (vals()[0]?.value ?? null));
    const curNum = (): number | null => {
        const v = cur();
        return typeof v === 'number' ? v : null;
    };
    const anyOverridden = (): boolean => vals().some((x) => x.overridden);
    const editIds = (): number[] => row.owners.map((o) => o.id).filter((id) => !view.lockedIds.has(id));
    /** Set the field on every editable owner; `value` may depend on the owner (references are relative to the class). */
    const commit = (value: Value | ((ow: Owner) => Value | null)): void =>
        ctx.edit(`Set ${row.name}`, editIds(), (o) => {
            const ow = row.owners.find((x) => x.id === o.id);
            if (!ow) return;
            const v = typeof value === 'function' ? value(ow) : value;
            if (v !== null) setObjectField(o, ow.field, v);
        });

    // ---- name (scrub handle for numbers), marker, reset
    const nameEl = h('span', { class: 'fname' }, row.name);
    const first = row.owners[0]!;
    const defText = (): string => {
        const d = first.field.default;
        if (first.field.unknownDefault) return 'unknown';
        if (Array.isArray(d)) return `[${d.length}]`;
        if (typeof d === 'number')
            return w === 'class' || w === 'ref' || w === 'colour' ? hexRef(d) : fmtNum(d, isFloat);
        return d === '' ? '(empty)' : String(d);
    };
    nameEl.title =
        `${row.name}\n${typeName(row.type)}\nClass default: ${defText()}` +
        (row.owners.length < objCount ? `\nOn ${row.owners.length} of ${objCount} selected` : '');
    if (row.owners.length < objCount) nameEl.append(h('span', { class: 'dim' }, ` (${row.owners.length}/${objCount})`));

    const resetField = (): void =>
        ctx.edit(`Reset ${row.name}`, editIds(), (o) => {
            const ow = row.owners.find((x) => x.id === o.id);
            if (ow) resetObjectField(o, ow.field);
        });
    const reset = h(
        'button',
        {
            class: 'tool sm reset',
            type: 'button',
            'aria-label': `Reset ${row.name}`,
            title: 'Reset to the class default (removes the override)',
            onclick: resetField,
        },
        icon('undo', 13),
    );
    const wrap = h('div', { class: 'fwrap' });
    const line = h('div', { class: 'ifield' });
    wrap.append(line);
    const markState = (): void => {
        const ov = anyOverridden();
        line.classList.toggle('overridden', ov);
        reset.toggleAttribute('disabled', !ov || editIds().length === 0);
    };
    updates.push(markState);

    // ---- controls
    let control: HTMLElement;

    const numberInput = (
        get: () => number | null,
        set: (n: number) => void,
        o: { integer: boolean; step?: number; scrubBy?: HTMLElement; unsigned?: boolean },
    ): HTMLInputElement => {
        const input = h('input', { type: 'number', class: 'num', 'aria-label': row.name });
        const show = (): void => {
            const v = get();
            input.value = v === null ? '' : fmtNum(v, !o.integer);
            input.placeholder = v === null ? 'mixed' : '';
            const s = o.step ?? (o.integer ? 1 : stepFor(v ?? 0));
            input.step = String(s);
        };
        updates.push(() => {
            if (document.activeElement !== input) show();
        });
        show();
        input.addEventListener('change', () => {
            const n = Number(input.value);
            if (input.value.trim() === '' || !Number.isFinite(n)) return show();
            const v = o.integer ? Math.trunc(n) : n;
            set(o.unsigned ? Math.max(0, v) : v);
        });
        if (o.scrubBy) {
            o.scrubBy.classList.add('scrubby');
            o.scrubBy.title += '\nDrag the name to scrub (Shift x10, Alt x0.1)';
            attachScrub(o.scrubBy, input, {
                step: o.step ?? (o.integer ? 1 : stepFor(get() ?? 0)),
                digits: o.integer ? 0 : 4,
                integer: o.integer,
            });
        }
        return input;
    };

    const refLabelFor = (ow: Owner, type: number, value: number): { text: string; bad: boolean; cls?: AssetClass } => {
        if (value === 0 || value === 0xffffffff) return { text: 'None', bad: false };
        if (!provider || !ow.schema) return { text: hexRef(value), bad: false };
        const t = provider.resolveRef(ow.schema, type, value);
        const cls = t.resource ? classByResource(ctx, t.resource) : undefined;
        if (typeCode(type) === 3) {
            return t.name
                ? { text: t.name, bad: false, ...(cls ? { cls } : {}) }
                : { text: `${t.stem ?? `db ${t.db}`} #${t.id} (missing)`, bad: true };
        }
        return { text: `${t.stem ?? `db ${t.db}`} #${t.id}`, bad: t.stem === null };
    };

    /** A class-reference button for one value (field or array element). */
    const classButton = (
        ow: Owner,
        type: number,
        get: () => number | null,
        set: (v: number) => void,
        extraLabel?: string,
    ): HTMLElement => {
        const btn = h('button', { class: 'refbtn', type: 'button' });
        const paint = (): void => {
            const v = get();
            if (v === null) {
                fill(
                    btn,
                    h('span', { class: 'rthumb', style: 'width:22px;height:22px' }),
                    h('span', { class: 'rl dim' }, 'mixed'),
                );
                btn.title = 'Values differ across the selection; pick a class to set them all';
                return;
            }
            const l = refLabelFor(ow, type, v);
            fill(
                btn,
                thumbBox(ctx, l.cls, 22),
                h('span', { class: `rl${l.bad ? ' bad' : ''}` }, l.text),
                extraLabel ? h('span', { class: 'dim' }, extraLabel) : null,
            );
            btn.title = `${l.text}\nvalue ${hexRef(v)}\nClick to pick another class`;
        };
        let last: string | undefined;
        updates.push(() => {
            const sig = String(get());
            if (sig !== last) {
                last = sig;
                paint();
            }
        });
        paint();
        btn.addEventListener('click', () =>
            classPicker(ctx, btn, {
                title: `${row.name}: pick a class`,
                allowNone: true,
                canPick: (c) => !!ow.schema && !!provider && provider.refValueFor(ow.schema, c.db, c.classId) !== null,
                pick: (c) => {
                    if (!c) return set(0);
                    const v = provider && ow.schema ? provider.refValueFor(ow.schema, c.db, c.classId) : null;
                    if (v !== null) set(v);
                },
            }),
        );
        return btn;
    };

    if (
        w === 'bool' ||
        (w === 'enum' &&
            provider?.isToggle(
                row.name,
                row.type,
                vals().map((x) => Number(x.value)),
            ))
    ) {
        const cb = h('input', { type: 'checkbox', class: 'cb', 'aria-label': row.name });
        const paint = (): void => {
            const v = curNum();
            cb.indeterminate = v === null;
            cb.checked = v !== null && v !== 0;
        };
        paint();
        updates.push(paint);
        cb.addEventListener('change', () => commit(cb.checked ? 1 : 0));
        control = h('span', { class: 'inputrow' }, cb, h('span', { class: 'dim tog' }, ''));
        updates.push(() => {
            const v = curNum();
            (control.lastElementChild as HTMLElement).textContent = v === null ? 'mixed' : v ? 'Yes' : 'No';
        });
        (control.lastElementChild as HTMLElement).textContent = curNum() === null ? 'mixed' : curNum() ? 'Yes' : 'No';
    } else if (w === 'int' || w === 'float') {
        control = h(
            'span',
            { class: 'inputrow' },
            numberInput(curNum, (n) => commit(n), { integer: !isFloat, scrubBy: nameEl }),
        );
    } else if (w === 'enum') {
        const names = enumNames(first.schema ? provider?.rflOf(first.schema.resource) : null, row.name, row.type);
        const opts = (): number[] => {
            if (names) {
                const all = new Set<number>(names.map((_, i) => i));
                for (const x of vals()) all.add(Number(x.value));
                return [...all].sort((a, b) => a - b);
            }
            const set = new Set<number>(provider?.enumValues(row.name, row.type) ?? []);
            for (const x of vals()) set.add(Number(x.value));
            if (typeof first.field.default === 'number') set.add(first.field.default);
            return [...set].sort((a, b) => a - b);
        };
        const sel = h('select', { class: 'sel', 'aria-label': row.name });
        const other = numberInput(curNum, (n) => commit(n), { integer: true, unsigned: true });
        other.classList.add('other');
        const build = (): void => {
            const list = opts();
            const v = curNum();
            const def = typeof first.field.default === 'number' ? first.field.default : null;
            sel.replaceChildren(
                ...(v === null ? [h('option', { value: 'mixed', disabled: true }, 'mixed')] : []),
                ...list.map((x) =>
                    h(
                        'option',
                        { value: String(x) },
                        `${names?.[x] ? `${x}  ${names[x]}` : x}${x === def ? ' (default)' : ''}`,
                    ),
                ),
                h('option', { value: 'other' }, 'Other value...'),
            );
            sel.value = v === null ? 'mixed' : String(v);
            other.hidden = !(v !== null && !list.includes(v)) && sel.value !== 'other';
        };
        build();
        let lastSig = '';
        updates.push(() => {
            const sig = `${curNum()}|${opts().join()}`;
            if (sig !== lastSig && document.activeElement !== sel) {
                lastSig = sig;
                build();
            }
        });
        sel.addEventListener('change', () => {
            if (sel.value === 'other') {
                other.hidden = false;
                other.focus();
            } else commit(Number(sel.value));
        });
        control = h('span', { class: 'inputrow' }, sel, other);
    } else if (w === 'colour') {
        const colour = h('input', { type: 'color', class: 'colour', 'aria-label': `${row.name} colour` });
        const alpha = h('input', {
            type: 'number',
            min: 0,
            max: 255,
            class: 'num alpha',
            title: 'Alpha',
            'aria-label': 'Alpha',
        });
        const paint = (): void => {
            const v = curNum();
            if (v === null) return;
            const p = argbToParts(v);
            colour.value = p.hex;
            alpha.value = String(p.alpha);
        };
        paint();
        updates.push(() => {
            if (document.activeElement !== alpha) paint();
        });
        const push = (): void => commit(partsToArgb(colour.value, Number(alpha.value)));
        colour.addEventListener('change', push);
        alpha.addEventListener('change', push);
        control = h('span', { class: 'colourrow' }, colour, alpha);
    } else if (w === 'string') {
        const input = h('input', { type: 'text', class: 'txt', 'aria-label': row.name, spellcheck: 'false' });
        const paint = (): void => {
            const v = cur();
            input.value = typeof v === 'string' ? v : '';
            input.placeholder = v === null ? 'mixed' : '';
        };
        paint();
        updates.push(() => {
            if (document.activeElement !== input) paint();
        });
        input.addEventListener('change', () => commit(input.value));
        control = h('span', { class: 'inputrow' }, input);
    } else if (w === 'class') {
        control = h(
            'span',
            { class: 'inputrow' },
            classButton(first, row.type, curNum, (v) => commit((ow) => refFor(ow, v, first))),
        );
    } else if (w === 'ref') {
        const kind = refKindOf(row.type);
        control = h('span', { class: 'inputrow' }, kind && provider ? resourceButton(kind) : resourceControl());
    } else if (w === 'array') {
        control = arrayControl();
    } else {
        control = h(
            'span',
            { class: 'inputrow' },
            numberInput(curNum, (n) => commit(n), { integer: true, unsigned: true }),
        );
    }

    /** The reference value to store on `ow` when `v` was computed for `from` (db indexes differ between classes). */
    function refFor(ow: Owner, v: number, from: Owner): number | null {
        if (ow === from || !provider || !from.schema || !ow.schema) return v;
        const { id, db } = refParts(v);
        if (v === 0) return 0;
        const stem = db === 0 ? from.schema.db : from.schema.deps.get(db);
        return stem ? provider.refValueFor(ow.schema, stem, id) : null;
    }

    /** Entry button for a model, sound, texture, animation or sequence field: name and picture, picker on click. */
    function resourceButton(kind: NonNullable<ReturnType<typeof refKindOf>>): HTMLElement {
        const refs = ctx.refs();
        const f: RefField = {
            kind,
            name: row.name,
            type: row.type,
            schema: first.schema,
            provider: provider ?? null,
            get: curNum,
            pick: (t) =>
                t
                    ? commit((ow) => (ow.schema && provider ? provider.refValueFor(ow.schema, t.stem, t.id) : null))
                    : commit(0),
            setRaw: (v) => commit((ow) => refFor(ow, v, first)),
            reset: resetField,
            defaultText: defText(),
        };
        const el = refButton(refs, f, (anchor) =>
            openRefPicker(
                refs,
                anchor,
                f,
                provider?.refValues(row.name, row.type) ?? [],
                (stem) => !!first.schema && !!provider && provider.refValueFor(first.schema, stem, 0) !== null,
            ),
        );
        updates.push(() => (el as HTMLElement & { update?: () => void }).update?.());
        return el;
    }

    function resourceControl(): HTMLElement {
        const input = h('input', {
            type: 'text',
            class: 'txt mono',
            'aria-label': `${row.name} value`,
            spellcheck: 'false',
        });
        const lab = h('span', { class: 'ref-label' });
        const paint = (): void => {
            const v = curNum();
            input.value = v === null ? '' : hexRef(v);
            input.placeholder = v === null ? 'mixed' : '';
            if (v === null) lab.textContent = '';
            else {
                const l = refLabelFor(first, row.type, v);
                lab.textContent = l.text;
                lab.classList.toggle('bad', l.bad);
            }
        };
        paint();
        updates.push(() => {
            if (document.activeElement !== input) paint();
        });
        input.addEventListener('change', () => {
            const n = Number(input.value.trim());
            if (Number.isInteger(n) && n >= 0 && n <= 0xffffffff) commit((ow) => refFor(ow, n, first));
            else paint();
        });
        const browse = h(
            'button',
            {
                class: 'tool sm',
                type: 'button',
                title: 'Browse values other classes use for this field',
                'aria-label': 'Browse values',
                onclick: () =>
                    resourcePicker(ctx, browse, row, first, (stem, id) =>
                        commit((ow) => (ow.schema && provider ? provider.refValueFor(ow.schema, stem, id) : null)),
                    ),
            },
            icon('search', 14),
        );
        return h('span', { class: 'refcell' }, h('span', { class: 'inputrow' }, input, browse), lab);
    }

    function arrayControl(): HTMLElement {
        const elType = row.type & 0xff;
        const isClass = elType === 3;
        const elKind = provider ? refKindOf(elType) : null;
        const isFloatEl = elType === 2;
        const summary = h('button', { class: 'btn sm arrbtn', type: 'button', 'aria-expanded': 'false' });
        const extra = h('div', { class: 'arr' });
        let lastSig = '';
        const words = (): number[] | null => {
            const v = cur();
            return Array.isArray(v) ? v : null;
        };
        const editable = (): boolean => words() !== null;
        const canPickStem = (stem: string): boolean =>
            !!first.schema && !!provider && provider.refValueFor(first.schema, stem, 0) !== null;
        const elementField = (
            kind: NonNullable<ReturnType<typeof refKindOf>>,
            get: () => number,
            set: (v: number) => void,
        ): RefField => ({
            kind,
            name: row.name,
            type: elType,
            schema: first.schema,
            provider: provider ?? null,
            get,
            pick: (t) =>
                set(t && first.schema && provider ? (provider.refValueFor(first.schema, t.stem, t.id) ?? 0) : 0),
            setRaw: set,
        });
        const setWords = (next: number[]): void => commit(next);
        let known: Suggestion[] | null = null;
        const suggestions = (): Suggestion[] =>
            (known ??= suggestValues({
                name: row.name,
                type: row.type,
                doc: ctx.doc(),
                catalog: ctx.catalog(),
                provider: provider ?? null,
                owner: first.schema,
            }));
        /**
         * An element with known values: a type-to-filter dropdown of them plus "Other value..." which reveals the raw
         * number box (also shown when the stored value is not one of the known ones).
         */
        const suggestControl = (
            list: Suggestion[],
            value: number,
            input: HTMLInputElement,
            set: (n: number) => void,
        ): HTMLElement => {
            const sel = h('select', { class: 'sel', 'aria-label': `${row.name} value`, 'data-search': 'on' });
            const groups = new Map<string, HTMLElement>();
            for (const g of new Set(list.map((x) => x.group))) {
                const og = h('optgroup', { label: g });
                groups.set(g, og);
                sel.append(og);
            }
            for (const x of list) groups.get(x.group)!.append(h('option', { value: String(x.value) }, x.label));
            const isKnown = list.some((x) => x.value === value);
            if (!isKnown) sel.prepend(h('option', { value: String(value) }, String(value)));
            sel.append(h('option', { value: 'other' }, 'Other value...'));
            sel.value = String(value);
            input.hidden = true;
            input.value = String(value);
            sel.addEventListener('change', () => {
                if (sel.value === 'other') {
                    input.hidden = false;
                    input.focus();
                } else set(Number(sel.value));
            });
            return h('span', { class: 'inputrow sugg' }, sel, input);
        };
        const paintExtra = (): void => {
            const ws = words();
            const open = state.arrOpen.has(row.key);
            summary.setAttribute('aria-expanded', String(open));
            summary.replaceChildren(
                icon('chevron', 11),
                ws === null ? 'mixed lists' : `${ws.length} item${ws.length === 1 ? '' : 's'}`,
            );
            extra.hidden = !open;
            if (!open) return extra.replaceChildren();
            if (ws === null)
                return fill(
                    extra,
                    h(
                        'div',
                        { class: 'note' },
                        'The selected objects have different lists. Select one object to edit.',
                    ),
                );
            const rowsEl: Node[] = ws.map((word, i) => {
                const rm = h(
                    'button',
                    {
                        class: 'tool sm',
                        type: 'button',
                        'aria-label': 'Remove item',
                        title: 'Remove this item',
                        onclick: () => setWords(ws.filter((_, j) => j !== i)),
                    },
                    icon('x', 12),
                );
                let ctl: HTMLElement;
                if (isClass) {
                    ctl = classButton(
                        first,
                        3,
                        () => words()?.[i] ?? 0,
                        (v) => {
                            const cw = words();
                            if (cw) setWords(cw.map((x, j) => (j === i ? v : x)));
                        },
                    );
                } else if (elKind) {
                    const refs = ctx.refs();
                    const f = elementField(
                        elKind,
                        () => words()?.[i] ?? 0,
                        (v) => {
                            const cw = words();
                            if (cw) setWords(cw.map((x, j) => (j === i ? v : x)));
                        },
                    );
                    const b = refButton(refs, f, (anchor) => openRefPicker(refs, anchor, f, [], canPickStem));
                    ctl = b;
                } else {
                    const input = h('input', { type: 'number', class: 'num', 'aria-label': `${row.name} ${i + 1}` });
                    const el = decodeElement(row.type, word);
                    input.value = fmtNum(el, isFloatEl);
                    input.step = isFloatEl ? '0.1' : '1';
                    const setAt = (n: number): void => {
                        const cw = words();
                        if (!cw || !Number.isFinite(n)) return;
                        setWords(
                            cw.map((x, j) => (j === i ? encodeElement(row.type, isFloatEl ? n : Math.trunc(n)) : x)),
                        );
                    };
                    input.addEventListener('change', () => setAt(Number(input.value)));
                    const known = isFloatEl ? [] : suggestions();
                    ctl = known.length ? suggestControl(known, el, input, setAt) : input;
                }
                return h('div', { class: 'arr-row' }, h('span', { class: 'dim idx' }, String(i + 1)), ctl, rm);
            });
            const add = h(
                'button',
                {
                    class: 'btn sm',
                    type: 'button',
                    onclick: (e: Event) => {
                        const cw = words() ?? [];
                        if (elKind) {
                            const refs = ctx.refs();
                            const nf = elementField(
                                elKind,
                                () => 0,
                                (v) => setWords([...cw, v]),
                            );
                            return openRefPicker(refs, e.currentTarget as HTMLElement, nf, [], canPickStem);
                        }
                        if (!isClass) return setWords([...cw, 0]);
                        classPicker(ctx, e.currentTarget as HTMLElement, {
                            title: `${row.name}: add a class`,
                            allowNone: false,
                            canPick: (c) =>
                                !!first.schema &&
                                !!provider &&
                                provider.refValueFor(first.schema, c.db, c.classId) !== null,
                            pick: (c) => {
                                const v =
                                    c && provider && first.schema
                                        ? provider.refValueFor(first.schema, c.db, c.classId)
                                        : null;
                                if (v !== null) setWords([...cw, v]);
                            },
                        });
                    },
                },
                icon('plus', 12),
                isClass ? 'Add class...' : elKind ? 'Add entry...' : 'Add item',
            );
            fill(extra, ...rowsEl, h('div', { class: 'btnrow' }, add));
        };
        summary.addEventListener('click', () => {
            if (state.arrOpen.has(row.key)) state.arrOpen.delete(row.key);
            else state.arrOpen.add(row.key);
            lastSig = '';
            paintExtra();
        });
        updates.push(() => {
            const sig = `${JSON.stringify(cur())}|${state.arrOpen.has(row.key)}`;
            if (sig !== lastSig && !extra.contains(document.activeElement)) {
                lastSig = sig;
                paintExtra();
            }
        });
        lastSig = `${JSON.stringify(cur())}|${state.arrOpen.has(row.key)}`;
        paintExtra();
        wrap.append(extra);
        void editable;
        return h('span', { class: 'inputrow' }, summary);
    }

    line.append(nameEl, control, reset);
    markState();
    return {
        el: wrap,
        update: () => {
            for (const u of updates) u();
        },
        changed: anyOverridden,
    };
}

// ---------------------------------------------------------------- the section body

export interface FieldsPanel {
    /** Elements for the section body. */
    nodes: Node[];
    /** Number of fields shown in the section header. */
    count: number;
    /** Refresh values and markers without rebuilding (after an edit, undo or redo). */
    update: () => void;
}

/** Rows for the selection, grouped by category, with a search box and a "changed only" filter. */
export function buildFieldsPanel(
    ctx: Ctx,
    objs: readonly LevelObject[],
    opts: { ownSearch?: boolean } = {},
): FieldsPanel {
    const rows = buildRows(ctx, objs);
    const provider = ctx.schemas();
    const blocks = h('div', { class: 'fblocks' });
    let built: Built[] = [];

    const search = h('input', {
        type: 'search',
        class: 'fsearch',
        placeholder: 'Filter fields',
        'aria-label': 'Filter fields',
        value: state.query,
    });
    const changedBtn = h(
        'button',
        {
            class: `chip${state.changed ? ' active' : ''}`,
            type: 'button',
            title: 'Show only fields that differ from the class default',
        },
        'Changed',
    );
    const summary = h('span', { class: 'dim fsum' });
    const resetAll = h(
        'button',
        {
            class: 'btn sm',
            type: 'button',
            title: 'Reset every field of the selection to the class defaults',
            onclick: () => {
                const ids = objs.map((o) => o.id).filter((id) => !ctx.view().lockedIds.has(id));
                ctx.edit('Reset all fields', ids, (o) => {
                    if (o.fields?.length) o.fields = [];
                });
            },
        },
        icon('undo', 12),
        'Reset all',
    );

    const changedNow = (o: LevelObject): number => o.fields?.length ?? 0;
    const totalChanged = (): number => objs.reduce((n, o) => n + changedNow(ctx.doc().get(o.id) ?? o), 0);

    const renderBlocks = (): void => {
        const q = state.query.trim().toLowerCase();
        built = [];
        const cats = new Map<FieldCategory, Row[]>();
        for (const r of rows) {
            if (q && !r.name.toLowerCase().includes(q)) continue;
            let l = cats.get(r.category);
            if (!l) cats.set(r.category, (l = []));
            l.push(r);
        }
        const nodes: Node[] = [];
        let shown = 0;
        const changedSet = new Set<string>();
        for (const r of rows) {
            if (
                r.owners.some((ow) =>
                    ctx
                        .doc()
                        .get(ow.id)
                        ?.fields?.some((f) => f.fieldIndex === ow.field.index),
                )
            )
                changedSet.add(r.key);
        }
        for (const cat of FIELD_CATEGORIES) {
            let list = cats.get(cat);
            if (!list) continue;
            if (state.changed) list = list.filter((r) => changedSet.has(r.key));
            if (!list.length) continue;
            const nChanged = list.filter((r) => changedSet.has(r.key)).length;
            const open = state.catOpen.get(cat) ?? (q !== '' || nChanged > 0 || rows.length <= 14);
            const head = h(
                'button',
                { class: `fg-head${open ? ' open' : ''}`, type: 'button', 'aria-expanded': String(open) },
                icon('chevron', 11),
                h('span', {}, cat),
                h('span', { class: 'dim' }, String(list.length)),
                nChanged ? h('span', { class: 'badge-changed' }, `${nChanged} changed`) : null,
            );
            head.addEventListener('click', () => {
                state.catOpen.set(cat, !open);
                renderBlocks();
            });
            const group = h('div', { class: 'fgroup' }, head);
            if (open) {
                for (const r of list) {
                    const b = buildRow(ctx, r, objs.length);
                    built.push(b);
                    group.append(b.el);
                    shown++;
                }
            }
            nodes.push(group);
        }
        if (!nodes.length) {
            nodes.push(
                h(
                    'div',
                    { class: 'note' },
                    rows.length
                        ? state.changed
                            ? 'No field differs from the class defaults.'
                            : 'No field matches the filter.'
                        : provider
                          ? 'The class of this object has no fields.'
                          : 'No class field list is available (no disc level loaded); only per-object overrides can be shown.',
                ),
            );
        }
        fill(blocks, ...nodes);
        void shown;
        paintSummary();
    };

    const paintSummary = (): void => {
        const n = rows.filter(
            (r) =>
                built.length >= 0 &&
                r.owners.some((ow) =>
                    ctx
                        .doc()
                        .get(ow.id)
                        ?.fields?.some((f) => f.fieldIndex === ow.field.index),
                ),
        ).length;
        summary.textContent = `${rows.length} field${rows.length === 1 ? '' : 's'}, ${n} changed`;
        resetAll.toggleAttribute('disabled', totalChanged() === 0);
    };

    search.addEventListener('input', () => {
        state.query = search.value;
        renderBlocks();
    });
    changedBtn.addEventListener('click', () => {
        state.changed = !state.changed;
        changedBtn.classList.toggle('active', state.changed);
        renderBlocks();
    });
    renderBlocks();

    const toolbar = h('div', { class: 'ftoolbar' }, ...(opts.ownSearch === false ? [] : [search]), changedBtn);
    const meta = h('div', { class: 'fmeta' }, summary, resetAll);
    let lastChanged = -1;
    return {
        nodes: [toolbar, meta, blocks],
        count: rows.length,
        update: () => {
            const nc = totalChanged();
            // The set of changed fields moved (override added or dropped): the "changed only" view and badges need a rebuild.
            if (lastChanged !== -1 && nc !== lastChanged && (state.changed || state.query === '')) {
                const active = document.activeElement;
                if (!blocks.contains(active)) renderBlocks();
            }
            lastChanged = nc;
            for (const b of built) b.update();
            paintSummary();
        },
    };
}

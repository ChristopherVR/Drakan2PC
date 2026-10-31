import {
    argbToParts,
    groupFields,
    isEditableType,
    observedValues,
    partsToArgb,
    snapToGround,
    typeName,
    widgetFor,
    type DocField,
    type FieldGroup,
    type LevelObject,
} from '@drakan/editor-core';
import { KINDS } from '../level.ts';
import type { Ctx, Panel } from './ctx.ts';
import { fill, h, icon } from './dom.ts';

const R2D = 180 / Math.PI;
const D2R = Math.PI / 180;

const kindColor = (o: LevelObject): string =>
    `#${(KINDS.find((k) => k.kind === o.kind)?.color ?? 0x888888).toString(16).padStart(6, '0')}`;

const fmt = (v: number, digits: number): string => String(Number(v.toFixed(digits)));

/** A value shared by every object, or null when they differ. */
function common<T>(objs: readonly LevelObject[], get: (o: LevelObject) => T): T | null {
    const first = get(objs[0]!);
    return objs.every((o) => get(o) === first) ? first : null;
}

/**
 * Inspector: transform, schema-driven fields, links and info for the selection, with multi-edit (editing a field sets
 * it on every selected object that has it, as one undo step).
 */
export function createInspector(ctx: Ctx): Panel {
    const body = h('div', { class: 'insp-body' });
    const root = h(
        'div',
        { class: 'panel inspector' },
        h('div', { class: 'panel-head' }, h('span', { class: 'panel-title' }, icon('box', 14), 'Inspector')),
        body,
    );
    let updaters: (() => void)[] = [];
    let sig = '';
    const open = new Set<string>(['Transform', 'Fields', 'Links']);

    const selected = (): LevelObject[] => {
        const v = ctx.view();
        return [...v.selection].map((id) => v.doc.get(id)).filter((o): o is LevelObject => !!o);
    };
    const editableIds = (objs: readonly LevelObject[]): number[] => {
        const v = ctx.view();
        return objs.filter((o) => !v.lockedIds.has(o.id)).map((o) => o.id);
    };

    // ---- widgets ------------------------------------------------------------------------------------------------

    /** Labelled number input; drag the label to scrub. `get` returns null for mixed values. */
    function numField(
        label: string,
        get: () => number | null,
        commit: (v: number) => void,
        opts: { step: number; digits: number; title?: string; cls?: string },
    ): HTMLElement {
        const input = h('input', { type: 'number', step: opts.step, class: 'num', 'aria-label': label });
        const lab = h(
            'span',
            { class: `scrub ${opts.cls ?? ''}`, title: opts.title ?? `${label} (drag to scrub)` },
            label,
        );
        const set = (): void => {
            const v = get();
            input.value = v === null ? '' : fmt(v, opts.digits);
            input.placeholder = v === null ? 'mixed' : '';
        };
        updaters.push(() => {
            if (document.activeElement !== input) set();
        });
        set();
        input.addEventListener('change', () => {
            const n = Number(input.value);
            if (input.value.trim() !== '' && Number.isFinite(n)) commit(n);
            else set();
        });
        let start: { x: number; v: number } | null = null;
        lab.addEventListener('pointerdown', (e) => {
            lab.setPointerCapture(e.pointerId);
            start = { x: e.clientX, v: Number(input.value) || 0 };
            document.body.classList.add('ed-scrub');
        });
        lab.addEventListener('pointermove', (e) => {
            if (!start) return;
            const k = (e.shiftKey ? 10 : e.altKey ? 0.1 : 1) * opts.step;
            input.value = fmt(start.v + (e.clientX - start.x) * k, opts.digits);
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
        return h('label', { class: 'nf' }, lab, input);
    }

    const section = (title: string, count: number | null, ...kids: (Node | null)[]): HTMLElement => {
        const isOpen = open.has(title);
        const head = h(
            'button',
            { class: `sec-head${isOpen ? ' open' : ''}`, type: 'button', 'aria-expanded': String(isOpen) },
            icon('chevron', 12),
            h('span', {}, title),
            count === null ? null : h('span', { class: 'dim' }, String(count)),
        );
        head.addEventListener('click', () => {
            if (open.has(title)) open.delete(title);
            else open.add(title);
            render(true);
        });
        const sec = h('section', { class: 'sec' }, head);
        if (isOpen) sec.append(h('div', { class: 'sec-body' }, ...kids));
        return sec;
    };

    // ---- transform ----------------------------------------------------------------------------------------------

    function transformSection(objs: LevelObject[]): HTMLElement {
        const ids = editableIds(objs);
        const axes = ['X', 'Y', 'Z'];
        const pos = h('div', { class: 'vec3' });
        axes.forEach((a, i) =>
            pos.append(
                numField(
                    a,
                    () => common(objs, (o) => o.transform.pos[i]!),
                    (v) => ctx.edit('Set position', ids, (o) => (o.transform.pos[i] = v)),
                    { step: 10, digits: 1, cls: `ax${a}`, title: `Position ${a} (game units)` },
                ),
            ),
        );
        const rot = h('div', { class: 'vec3' });
        // Order: pitch (x), yaw (y), roll (z); yaw is the one the game reads for most objects.
        (['Pitch', 'Yaw', 'Roll'] as const).forEach((a, i) =>
            rot.append(
                numField(
                    a[0]!,
                    () => {
                        const c = common(objs, (o) => Math.round(o.transform.rot[i]! * R2D * 100) / 100);
                        return c;
                    },
                    (v) => ctx.edit(`Set ${a.toLowerCase()}`, ids, (o) => (o.transform.rot[i] = v * D2R)),
                    { step: 1, digits: 1, cls: `ax${'XYZ'[i]}`, title: `${a} (degrees)` },
                ),
            ),
        );
        const withScale = objs.filter((o) => o.scale);
        const kids: (Node | null)[] = [
            h('div', { class: 'lbl' }, 'Position'),
            pos,
            h('div', { class: 'lbl' }, 'Rotation'),
            rot,
        ];
        if (withScale.length) {
            const sc = h('div', { class: 'vec3' });
            axes.forEach((a, i) =>
                sc.append(
                    numField(
                        a,
                        () => common(withScale, (o) => o.scale![i]!),
                        (v) =>
                            ctx.edit(
                                'Set scale',
                                withScale.map((o) => o.id).filter((id) => ids.includes(id)),
                                (o) => {
                                    if (o.scale) o.scale[i] = v;
                                },
                            ),
                        { step: 0.05, digits: 3, cls: `ax${a}`, title: `Scale ${a}` },
                    ),
                ),
            );
            kids.push(h('div', { class: 'lbl' }, 'Scale'), sc);
            if (withScale.length < objs.length)
                kids.push(
                    h(
                        'div',
                        { class: 'note' },
                        `${objs.length - withScale.length} selected object(s) cannot be scaled.`,
                    ),
                );
        } else {
            kids.push(
                h(
                    'div',
                    { class: 'note' },
                    'Scale is only stored for objects with the scale flag (0x100); it cannot be added.',
                ),
            );
        }
        const bar = h(
            'div',
            { class: 'btnrow' },
            h(
                'button',
                {
                    class: 'btn',
                    type: 'button',
                    title: 'Drop the selection onto the ground (End)',
                    onclick: () => snapGround(),
                },
                icon('ground', 14),
                'To ground',
            ),
            h(
                'button',
                {
                    class: 'btn',
                    type: 'button',
                    title: 'Select every object of the same class (Ctrl+Shift+A)',
                    onclick: () => ctx.view().selectSameClass(),
                },
                'Same class',
            ),
        );
        kids.push(bar);
        if (ids.length < objs.length)
            kids.push(h('div', { class: 'note warn' }, `${objs.length - ids.length} locked object(s) are not edited.`));
        return section('Transform', null, ...kids);
    }

    function snapGround(): void {
        const v = ctx.view();
        const ids = v.editable();
        const m = snapToGround(v.doc, ids, (x, z) => v.groundHeightAt(x, z));
        if (!m.size) {
            ctx.toast('No ground under the selection (terrain is not loaded yet).');
            return;
        }
        ctx.edit('Snap to ground', [...m.keys()], (o) => (o.transform.pos = [...m.get(o.id)!.pos]));
    }

    // ---- fields -------------------------------------------------------------------------------------------------

    function fieldRow(g: FieldGroup, objs: LevelObject[]): HTMLElement {
        const ids = g.owners.map((x) => x.id).filter((id) => !ctx.view().lockedIds.has(id));
        const w = widgetFor(g.type);
        const editable = isEditableType(g.type);
        const first = (): DocField =>
            ctx
                .doc()
                .get(g.owners[0]!.id)
                ?.fields?.find((f) => f.name === g.name && f.type === g.type) ?? g.owners[0]!.field;
        const valueOf = (id: number): DocField['value'] | undefined =>
            ctx
                .doc()
                .get(id)
                ?.fields?.find((f) => f.name === g.name && f.type === g.type)?.value;
        const mixedNow = (): boolean => {
            const vals = g.owners.map((x) => valueOf(x.id));
            return vals.some((v) => JSON.stringify(v) !== JSON.stringify(vals[0]));
        };
        const setAll = (value: number): void =>
            ctx.edit(`Set ${g.name}`, ids, (o) => {
                const f = o.fields?.find((x) => x.name === g.name && x.type === g.type);
                if (f) f.value = value;
            });
        const cur = (): number | null => {
            if (mixedNow()) return null;
            const v = first().value;
            return typeof v === 'number' ? v : null;
        };

        let control: HTMLElement;
        const isFloat = (g.type & 0xff) === 2;
        if (!editable || w === 'string' || w === 'array') {
            const f = first();
            const txt = mixedNow()
                ? 'mixed'
                : Array.isArray(f.value)
                  ? `[${f.value.length}] ${f.value.slice(0, 6).join(', ')}${f.value.length > 6 ? ', ...' : ''}`
                  : String(f.value);
            control = h('span', { class: 'ro', title: 'Strings and arrays are shown but not editable yet' }, txt);
        } else if (w === 'bool') {
            const cb = h('input', { type: 'checkbox', class: 'cb' });
            const set = (): void => {
                const v = cur();
                cb.indeterminate = v === null;
                cb.checked = v !== null && v !== 0;
            };
            set();
            updaters.push(set);
            cb.addEventListener('change', () => setAll(cb.checked ? 1 : 0));
            control = cb;
        } else if (w === 'colour') {
            const colour = h('input', { type: 'color', class: 'colour' });
            const alpha = h('input', { type: 'number', min: 0, max: 255, class: 'num alpha', title: 'Alpha' });
            const set = (): void => {
                const v = cur();
                if (v === null) return;
                const p = argbToParts(v);
                colour.value = p.hex;
                alpha.value = String(p.alpha);
            };
            set();
            updaters.push(() => {
                if (document.activeElement !== alpha) set();
            });
            const push = (): void => setAll(partsToArgb(colour.value, Number(alpha.value)));
            colour.addEventListener('change', push);
            alpha.addEventListener('change', push);
            control = h('span', { class: 'colourrow' }, colour, alpha);
        } else {
            const input = h('input', { type: 'number', step: isFloat ? 0.1 : 1, class: 'num' });
            const set = (): void => {
                const v = cur();
                input.value = v === null ? '' : isFloat ? fmt(v, 4) : String(v);
                input.placeholder = v === null ? 'mixed' : '';
            };
            set();
            updaters.push(() => {
                if (document.activeElement !== input) set();
            });
            input.addEventListener('change', () => {
                const n = Number(input.value);
                if (input.value.trim() !== '' && Number.isFinite(n)) setAll(isFloat ? n : Math.trunc(n));
                else set();
            });
            const extras: Node[] = [input];
            if (w === 'enum' || w === 'id' || w === 'class') {
                const obs = observedValues(ctx.doc(), g.name, g.type);
                if (obs.length > 1) {
                    const listId = `dl-${g.name.replace(/\W/g, '')}-${g.type}`;
                    const dl = h('datalist', { id: listId });
                    for (const v of obs) dl.append(h('option', { value: String(v) }));
                    input.setAttribute('list', listId);
                    extras.push(dl);
                }
            }
            if (w === 'object') {
                const label = h('span', { class: 'ref-label' });
                const upd = (): void => {
                    const v = cur();
                    const t = v === null ? undefined : ctx.doc().get(v);
                    label.textContent =
                        v === null ? '' : t ? `${t.class} #${t.id}` : v === 0 ? 'none' : 'missing object';
                    label.classList.toggle('bad', v !== null && v !== 0 && !t);
                };
                upd();
                updaters.push(upd);
                extras.push(
                    h(
                        'button',
                        {
                            class: 'tool sm',
                            type: 'button',
                            title: 'Pick an object in the viewport',
                            onclick: () => pickInto((id) => id !== null && setAll(id)),
                        },
                        icon('focus', 14),
                    ),
                    h(
                        'button',
                        {
                            class: 'tool sm',
                            type: 'button',
                            title: 'Select the referenced object',
                            onclick: () => {
                                const v = cur();
                                if (v && ctx.doc().has(v)) ctx.locate([v]);
                            },
                        },
                        icon('select', 14),
                    ),
                    label,
                );
            }
            if (w === 'class') {
                const label = h('span', { class: 'ref-label' });
                const upd = (): void => {
                    const v = cur();
                    label.textContent = v === null ? '' : (ctx.catalog().find((c) => c.classId === v)?.name ?? '');
                };
                upd();
                updaters.push(upd);
                extras.push(label);
            }
            control = h('span', { class: 'inputrow' }, ...extras);
        }

        const canReset = objs.every((o) => (o.fields ?? []).every((f) => isEditableType(f.type)));
        const reset = h(
            'button',
            {
                class: 'tool sm reset',
                type: 'button',
                title: canReset
                    ? 'Reset to the class default (removes the override)'
                    : 'Reset needs an object without string or array parameters',
                'aria-label': `Reset ${g.name}`,
                disabled: !canReset || ids.length === 0,
                onclick: () =>
                    ctx.edit(`Reset ${g.name}`, ids, (o) => {
                        o.fields = o.fields?.filter((f) => !(f.name === g.name && f.type === g.type));
                    }),
            },
            icon('undo', 13),
        );
        return h(
            'div',
            { class: 'field' },
            h(
                'span',
                {
                    class: 'fname',
                    title: `${g.name}\n${typeName(g.type)}${g.complete ? '' : `\nOn ${g.owners.length} of ${objs.length} selected`}`,
                },
                g.name,
                g.complete ? null : h('span', { class: 'dim' }, ` (${g.owners.length}/${objs.length})`),
            ),
            control,
            reset,
        );
    }

    function fieldsSection(objs: LevelObject[]): HTMLElement {
        const groups = groupFields(objs);
        const kids: Node[] = [];
        if (!groups.length)
            kids.push(h('div', { class: 'note' }, 'No per-instance fields: this object uses every class default.'));
        for (const g of groups) kids.push(fieldRow(g, objs));
        kids.push(
            h(
                'div',
                { class: 'note' },
                'Only fields that differ from the class default are listed; the class field list and defaults are not decoded yet.',
            ),
        );
        return section('Fields', groups.length, ...kids);
    }

    // ---- links --------------------------------------------------------------------------------------------------

    function pickInto(cb: (id: number | null) => void): void {
        const v = ctx.view();
        ctx.toast('Click an object in the viewport (Esc cancels).');
        v.pickObject((id) => {
            cb(id);
            render(true);
        });
    }

    function linksSection(objs: LevelObject[]): HTMLElement {
        const ids = editableIds(objs);
        const kids: Node[] = [];
        const one = objs.length === 1 ? objs[0]! : null;
        const doc = ctx.doc();
        const chip = (to: number, onRemove: (() => void) | null): HTMLElement => {
            const t = doc.get(to);
            return h(
                'div',
                { class: `linkrow${t ? '' : ' bad'}` },
                h(
                    'button',
                    {
                        class: 'linkname',
                        type: 'button',
                        title: 'Select the target',
                        onclick: () => t && ctx.locate([to]),
                    },
                    icon('link', 13),
                    t ? `${t.class} #${to}` : `missing #${to}`,
                ),
                onRemove
                    ? h(
                          'button',
                          {
                              class: 'tool sm',
                              type: 'button',
                              title: 'Remove this link',
                              'aria-label': 'Remove link',
                              onclick: onRemove,
                          },
                          icon('x', 13),
                      )
                    : null,
            );
        };
        if (one) {
            const links = one.links ?? [];
            if (!links.length) kids.push(h('div', { class: 'note' }, 'This object triggers nothing.'));
            for (const to of links) {
                kids.push(
                    chip(to, () =>
                        ctx.edit('Remove link', ids, (o) => {
                            o.links = (o.links ?? []).filter((l) => l !== to);
                        }),
                    ),
                );
            }
            const incoming = [...doc.objects()].filter((o) => o.links?.includes(one.id));
            if (incoming.length) {
                kids.push(h('div', { class: 'lbl' }, `Triggered by (${incoming.length})`));
                for (const o of incoming.slice(0, 20)) kids.push(chip(o.id, null));
            }
        } else {
            kids.push(
                h(
                    'div',
                    { class: 'note' },
                    'Link every selected object to a target, or select one object to see its links.',
                ),
            );
        }
        kids.push(
            h(
                'div',
                { class: 'btnrow' },
                h(
                    'button',
                    {
                        class: 'btn',
                        type: 'button',
                        disabled: !ids.length,
                        onclick: () =>
                            pickInto((to) => {
                                if (to !== null)
                                    ctx.edit(
                                        'Add link',
                                        ids.filter((i) => i !== to),
                                        (o) => {
                                            o.links = [...new Set([...(o.links ?? []), to])];
                                        },
                                    );
                            }),
                    },
                    icon('plus', 14),
                    'Add link (pick)',
                ),
                h(
                    'button',
                    {
                        class: 'btn',
                        type: 'button',
                        disabled: !ids.length || objs.every((o) => !o.links?.length),
                        onclick: () =>
                            ctx.edit('Clear links', ids, (o) => {
                                o.links = [];
                            }),
                    },
                    'Clear',
                ),
            ),
        );
        return section('Links', one ? (one.links?.length ?? 0) : null, ...kids);
    }

    // ---- info ---------------------------------------------------------------------------------------------------

    function infoSection(objs: LevelObject[]): HTMLElement {
        const o = objs[0]!;
        const row = (k: string, v: string): HTMLElement =>
            h('div', { class: 'kv' }, h('span', { class: 'dim' }, k), h('span', { class: 'mono' }, v));
        const kids: Node[] = [];
        if (objs.length === 1) {
            kids.push(
                row('Object id', String(o.id)),
                row('Class', o.class),
                row('Resource', o.resource),
                row('Kind', `${o.kind}${o.type >= 0 ? ` (type ${o.type})` : ''}`),
            );
            if (o.props.db) kids.push(row('ODB', String(o.props.db)));
            kids.push(
                row(
                    'Source',
                    o.origin !== undefined
                        ? `LVL object ${o.origin}`
                        : o.template !== undefined
                          ? `new, templated from ${o.template}`
                          : 'new',
                ),
            );
            for (const [k, v] of Object.entries(o.props)) if (k !== 'db') kids.push(row(k, String(v)));
            if (o.model) kids.push(row('Model', o.model));
        } else {
            kids.push(row('Selected', String(objs.length)));
            const classes = new Set(objs.map((x) => x.class));
            kids.push(row('Classes', classes.size === 1 ? o.class : `${classes.size} different`));
        }
        return section('Info', null, ...kids);
    }

    // ---- render -------------------------------------------------------------------------------------------------

    function render(force = false): void {
        const objs = selected();
        const doc = ctx.doc();
        const fieldSig = objs.map((o) => (o.fields ?? []).map((f) => `${f.name}:${f.type}`).join(',')).join('|');
        const linkSig =
            objs.length === 1
                ? `${(objs[0]!.links ?? []).join(',')}@${[...doc.objects()].filter((o) => o.links?.includes(objs[0]!.id)).length}`
                : '';
        const next = `${objs.map((o) => o.id).join(',')}#${fieldSig}#${linkSig}#${ctx.view().lockedIds.size}#${doc.size}`;
        if (!force && next === sig) {
            for (const u of updaters) u();
            return;
        }
        sig = next;
        updaters = [];
        if (!objs.length) {
            fill(
                body,
                h(
                    'div',
                    { class: 'empty-state' },
                    icon('select', 28),
                    h('strong', {}, 'Nothing selected'),
                    h(
                        'span',
                        {},
                        'Click an object in the viewport or the Outliner. Drag on empty space to box select.',
                    ),
                    doc.size
                        ? h('span', { class: 'dim' }, `${doc.size} objects in ${doc.level || 'this level'}`)
                        : null,
                ),
            );
            return;
        }
        const o = objs[0]!;
        const head = h(
            'div',
            { class: 'insp-head' },
            h('i', { class: 'dot big', style: `background:${kindColor(o)}` }),
            h(
                'div',
                { class: 'insp-title' },
                h('strong', {}, objs.length === 1 ? o.class || 'Object' : `${objs.length} objects`),
                h(
                    'span',
                    { class: 'dim' },
                    objs.length === 1 ? `#${o.id} - ${o.resource}` : (common(objs, (x) => x.class) ?? 'mixed classes'),
                ),
            ),
        );
        fill(body, head, transformSection(objs), fieldsSection(objs), linksSection(objs), infoSection(objs));
    }

    return {
        el: root,
        onSelection: () => render(),
        onChange: () => render(),
        onVisibility: () => render(),
        onLoad: () => render(true),
    };
}

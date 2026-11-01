import {
    logicOf,
    outgoingMessages,
    setMessageField,
    snapToGround,
    type EdgeMessage,
    type LevelObject,
} from '@drakan/editor-core';
import { MESSAGE_NAMES } from '@drakan/formats';
import { KINDS } from '../level.ts';
import type { Ctx, Panel } from './ctx.ts';
import { fill, h, icon } from './dom.ts';
import { buildFieldsPanel, closePopover, fieldQuery } from './fieldsui.ts';
import { nameOf, objectLabel, startLinkPick } from './linking.ts';
import { attachScrub } from './scrub.ts';
import { objectAnimationBody } from './animpreview.ts';
import { vendorDetails } from './vendor.ts';
import { changeOf, revertBlocked, revertToOriginal } from './revert.ts';

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
    const searchBox = h('input', {
        type: 'search',
        class: 'fsearch insp-q',
        placeholder: 'Search details',
        'aria-label': 'Search details',
        value: fieldQuery.get(),
    });
    const root = h(
        'div',
        { class: 'panel inspector' },
        h('div', { class: 'panel-head' }, h('span', { class: 'panel-title' }, icon('details', 14), 'Details')),
        h('div', { class: 'panel-tools insp-search' }, searchBox),
        body,
    );
    searchBox.addEventListener('input', () => {
        fieldQuery.set(searchBox.value);
        render(true);
    });
    searchBox.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && searchBox.value) {
            e.stopPropagation();
            searchBox.value = '';
            fieldQuery.set('');
            render(true);
        }
    });
    let updaters: (() => void)[] = [];
    let sig = '';
    const open = new Set<string>(['Transform', 'Fields', 'Links', 'Viewport animation', 'Vendor']);

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
        attachScrub(lab, input, { step: opts.step, digits: opts.digits });
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

    const whoLabel = (ids: readonly number[]): string =>
        ids.length === 1 ? nameOf(ctx, ids[0]!) : `${ids.length} objects`;

    function transformSection(objs: LevelObject[]): HTMLElement {
        const ids = editableIds(objs);
        const axes = ['X', 'Y', 'Z'];
        const pos = h('div', { class: 'vec3' });
        axes.forEach((a, i) =>
            pos.append(
                numField(
                    a,
                    () => common(objs, (o) => o.transform.pos[i]!),
                    (v) => ctx.edit(`Set position of ${whoLabel(ids)}`, ids, (o) => (o.transform.pos[i] = v)),
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
                    (v) =>
                        ctx.edit(
                            `Set ${a.toLowerCase()} of ${whoLabel(ids)}`,
                            ids,
                            (o) => (o.transform.rot[i] = v * D2R),
                        ),
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
                                `Set scale of ${whoLabel(ids)}`,
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
            kids.push(h('div', { class: 'note' }, 'This object cannot be scaled.'));
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

    function fieldsSection(objs: LevelObject[]): HTMLElement {
        const panel = buildFieldsPanel(ctx, objs, { ownSearch: false });
        updaters.push(panel.update);
        return section('Fields', panel.count, ...panel.nodes);
    }

    // ---- viewport animation: the clip the selected instance plays in the 3D view ------------------------------------

    function viewportAnimSection(o: LevelObject): HTMLElement | null {
        const v = ctx.view();
        const anim = v.models.anim;
        if (!anim.canAnimate(o.resource)) return null;
        const host = h('div', { class: 'ap-host' });
        if (open.has('Viewport animation')) {
            host.append(h('div', { class: 'note' }, 'Loading animations...'));
            void anim.want(o.resource).then((r) => {
                if (!host.isConnected && !r) return;
                if (!r) {
                    host.replaceChildren(h('div', { class: 'note' }, 'This class has no skinned model to animate.'));
                    return;
                }
                const key = (c: { stem: string; id: number }): string => `${c.stem}:${c.id}`;
                const sel = h('select', { class: 'sel ap-clip', 'aria-label': 'Viewport animation clip' });
                const idle = r.idle ? r.clips.find((c) => key(c) === key(r.idle!)) : null;
                sel.append(h('option', { value: '' }, `Default${idle ? ` (${idle.name || 'idle'})` : ' (rest pose)'}`));
                for (const c of r.clips) {
                    sel.append(
                        h('option', { value: key(c) }, c.label ? `${c.label} - ${c.name}` : c.name || `#${c.id}`),
                    );
                }
                const cur = anim.clipOf(o.id);
                sel.value = cur ? key(cur) : '';
                sel.addEventListener('change', () => {
                    const c = r.clips.find((x) => key(x) === sel.value);
                    anim.setClip(o.id, c ? { stem: c.stem, id: c.id } : null);
                });
                host.replaceChildren(
                    h('div', { class: 'ap-pick' }, sel),
                    h(
                        'div',
                        { class: 'note' },
                        v.viewOptions.animate
                            ? 'Plays on the selected instance while it is on screen (Display > Play animations).'
                            : 'Animations are off (Display > Play animations).',
                    ),
                );
            });
        }
        return section('Viewport animation', null, host);
    }

    // ---- animation preview (skinned classes only) ---------------------------------------------------------------

    /** Idle (or any class clip) played on the class model; built only while the section is open, hidden when the class has no skinned model. */
    function animationSection(o: LevelObject): HTMLElement | null {
        const provider = ctx.schemas();
        const schema = provider?.schemaFor(o.resource);
        // Classes with an animation field (type 9 or an array of it) are the ones that can animate.
        if (!provider || !schema?.fields.some((f) => (f.type & 0xff) === 9)) return null;
        const host = h('div', { class: 'ap-host' });
        if (open.has('Animation')) {
            host.append(h('div', { class: 'note' }, 'Loading animations...'));
            void objectAnimationBody(ctx.refs(), provider, o).then((body) => {
                if (!host.isConnected && !body) return;
                host.replaceChildren(
                    body ?? h('div', { class: 'note' }, 'This class has no skinned model to animate.'),
                );
            });
        }
        return section('Animation', null, host);
    }

    // ---- links --------------------------------------------------------------------------------------------------

    function messagesOf(o: LevelObject): EdgeMessage[] {
        const env = ctx.schemas();
        return env ? outgoingMessages(o, env, logicOf(o, env)) : [];
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
        const explain = (parts: (Node | string)[]): HTMLElement => h('div', { class: 'note link-explain' }, ...parts);
        if (one) {
            const links = one.links ?? [];
            const msgs = messagesOf(one);
            const env = ctx.schemas();
            if (msgs.length && env) {
                // "When this triggers, it sends [message] to these objects."
                const line: (Node | string)[] = ['When this triggers, it sends '];
                msgs.forEach((m, k) => {
                    const sel = h('select', {
                        class: 'link-msg',
                        'aria-label': `${m.label} message`,
                        title: `${m.label}: the message this object sends along its links`,
                        disabled: !ids.length,
                    }) as HTMLSelectElement;
                    MESSAGE_NAMES.forEach((n, i) => sel.append(h('option', { value: String(i) }, n)));
                    if (!MESSAGE_NAMES[m.value])
                        sel.append(h('option', { value: String(m.value) }, `message ${m.value}`));
                    sel.value = String(m.value);
                    sel.addEventListener('change', () => {
                        const name = MESSAGE_NAMES[Number(sel.value)] ?? sel.value;
                        ctx.edit(`Set ${m.label} of ${objectLabel(one)} to ${name}`, ids, (o) => {
                            setMessageField(o, env, m.field, Number(sel.value));
                        });
                    });
                    if (k > 0) line.push(k === msgs.length - 1 ? ' and ' : ', ');
                    line.push(msgs.length > 1 ? `${m.label}: ` : '', sel);
                });
                line.push(links.length ? ' to these objects:' : ' to the objects you link below.');
                kids.push(explain(line));
            } else {
                kids.push(
                    explain([
                        links.length
                            ? 'When this object triggers, it sends its message to these objects:'
                            : 'Links make this object trigger others: when it fires (a trigger is entered, a lever is pulled, a timer runs out), it sends its message to every linked object.',
                    ]),
                );
            }
            for (const to of links) {
                kids.push(
                    chip(to, () =>
                        ctx.edit(`Remove link ${objectLabel(one)} to ${nameOf(ctx, to)}`, ids, (o) => {
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
                explain([
                    'Link every selected object to one target: each of them will send its message to it. Select a single object to see and edit its links.',
                ]),
            );
        }
        kids.push(
            h(
                'div',
                { class: 'btnrow' },
                h(
                    'button',
                    {
                        class: 'btn primary',
                        type: 'button',
                        disabled: !ids.length,
                        title: 'Then click the target in the viewport or the Outliner (Esc cancels)',
                        onclick: () => startLinkPick(ctx, ids),
                    },
                    icon('plus', 14),
                    'Add link',
                ),
                h(
                    'button',
                    {
                        class: 'btn',
                        type: 'button',
                        disabled: !ids.length || objs.every((o) => !o.links?.length),
                        onclick: () =>
                            ctx.edit(
                                one ? `Clear links of ${objectLabel(one)}` : `Clear links of ${ids.length} objects`,
                                ids,
                                (o) => {
                                    o.links = [];
                                },
                            ),
                    },
                    'Clear',
                ),
            ),
            h(
                'div',
                { class: 'note dim' },
                'Also: right-click an object and choose Link to..., or drag the link handle next to the selected object onto its target.',
            ),
        );
        return section('Links', one ? (one.links?.length ?? 0) : null, ...kids);
    }

    function vendorSection(o: LevelObject): HTMLElement | null {
        const body = vendorDetails(ctx, o, ctx.view().lockedIds.has(o.id));
        return body ? section('Vendor', null, body) : null;
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
            if (o.props.db) kids.push(row('Container', String(o.props.db)));
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

    // ---- changed from the disc -----------------------------------------------------------------------------------

    /** "Changed" note with a Revert to original button; kept up to date in place as edits come in. */
    function revertBar(objs: LevelObject[]): HTMLElement {
        const bar = h('div', { class: 'insp-revert', hidden: true });
        const ids = objs.map((o) => o.id);
        let last = '';
        const update = (): void => {
            const kinds = ids.map((id) => changeOf(ctx, id));
            const changed = ids.filter((_, i) => kinds[i] !== null);
            const key = kinds.join(',');
            if (key === last) return;
            last = key;
            bar.hidden = changed.length === 0;
            if (!changed.length) return;
            const added = kinds.every((k) => k === null || k === 'added');
            const text =
                ids.length === 1
                    ? added
                        ? 'Added in this edit (not in the original level)'
                        : 'Changed from the original level'
                    : `${changed.length} of ${ids.length} changed from the original level`;
            const why = revertBlocked(ctx, changed);
            fill(
                bar,
                h('span', { class: 'insp-revert-t' }, icon('info', 12), text),
                h(
                    'button',
                    {
                        class: 'btn sm',
                        type: 'button',
                        disabled: !!why,
                        title:
                            why ??
                            (added ? 'Remove what was added' : 'Put back the position, links and fields from the disc'),
                        onclick: () => void revertToOriginal(ctx, changed),
                    },
                    icon('undo', 12),
                    added ? 'Remove' : 'Revert to original',
                ),
            );
        };
        updaters.push(update);
        update();
        return bar;
    }

    // ---- render -------------------------------------------------------------------------------------------------

    function render(force = false): void {
        const objs = selected();
        const doc = ctx.doc();
        // Field values refresh in place (no rebuild), so the signature only carries what changes the layout.
        const fieldSig = `${ctx.schemas() ? 'schema' : 'plain'}:${objs.map((o) => o.resource).join(',')}`;
        const linkSig =
            objs.length === 1
                ? `${(objs[0]!.links ?? []).join(',')}@${[...doc.objects()].filter((o) => o.links?.includes(objs[0]!.id)).length}@${messagesOf(
                      objs[0]!,
                  )
                      .map((m) => m.value)
                      .join(',')}`
                : '';
        const next = `${objs.map((o) => o.id).join(',')}#${fieldSig}#${linkSig}#${ctx.view().lockedIds.size}#${doc.size}`;
        if (!force && next === sig) {
            for (const u of updaters) u();
            return;
        }
        sig = next;
        const keepScroll = body.scrollTop;
        closePopover();
        updaters = [];
        if (!objs.length) {
            fill(
                body,
                h(
                    'div',
                    { class: 'empty-state' },
                    icon('select', 28),
                    h('strong', {}, 'Nothing selected'),
                    h('span', {}, 'Click an object in the viewport or the Outliner.'),
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
        // A search term narrows the whole panel: fields by name, the other sections by what they hold.
        const q = fieldQuery.get().trim().toLowerCase();
        const has = (words: string): boolean =>
            !q || words.includes(q) || q.split(/\s+/).every((w) => words.includes(w));
        fill(
            body,
            head,
            ctx.src() ? revertBar(objs) : null,
            has('transform position rotation scale pos rot ground snap x y z') ? transformSection(objs) : null,
            fieldsSection(objs),
            objs.length === 1 && has('animation clip play idle walk attack pose skeleton viewport')
                ? viewportAnimSection(objs[0]!)
                : null,
            objs.length === 1 && has('animation clip play idle walk attack pose skeleton')
                ? animationSection(objs[0]!)
                : null,
            objs.length === 1 && has('vendor shop merchant store stock price gold sell buy')
                ? vendorSection(objs[0]!)
                : null,
            has('links link trigger triggers message sends target') ? linksSection(objs) : null,
            has('info id class resource kind address') ? infoSection(objs) : null,
        );
        body.scrollTop = keepScroll;
    }

    return {
        el: root,
        onSelection: () => render(),
        onChange: () => render(),
        onVisibility: () => render(),
        onLoad: () => render(true),
    };
}

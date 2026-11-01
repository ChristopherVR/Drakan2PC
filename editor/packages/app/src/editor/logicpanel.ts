import {
    ApplyTemplateCommand,
    TEMPLATES,
    destinationLevelName,
    isError,
    logicOf,
    resourceParts,
    validateLogic,
    type ExitIndex,
    type ExitIndexEntry,
    type LevelObject,
    type LogicEnv,
    type LogicTemplate,
    type ParamValues,
    type TemplateParam,
    type ValidationIssue,
} from '@drakan/editor-core';
import { exitDestinationText } from '@drakan/formats';
import type { ExitIndexData } from '../logicdata.ts';
import type { Ctx, Panel } from './ctx.ts';
import { fill, h, icon, raf } from './dom.ts';
import { contextMenu } from './ui.ts';
import { addValidationProvider } from './panels.ts';
import { registerCommand, registerPanel, runCommand } from './registry.ts';

import { exitEditHost, onExitEditChange } from '../story/exitbridge.ts';
import { createLogicGraph } from './logicgraph.ts';
import { createVolumeLayer, type VolumeMode } from './logicview.ts';
import { projects } from './project/store.ts';

/** Set by the open Logic panel: opens a template form with some parameters preset. */
let openTemplateHook: ((id: string, preset: ParamValues) => void) | null = null;

/** Open the Logic panel's template form (for example the Quests panel's "Place quest object"). False without a panel. */
export function openTemplate(id: string, preset: ParamValues = {}): boolean {
    if (!openTemplateHook) return false;
    runCommand('panel.logic');
    openTemplateHook(id, preset);
    return true;
}

const norm = (s: string): string => s.toLowerCase().replace(/[^a-z0-9]/g, '');

/** Install the logic graph panel, the volume overlay and the logic validation checks. */
export function installLogic(ctx: Ctx, loadExits: (only?: string) => Promise<ExitIndexData>): Panel {
    const env = (): LogicEnv | null => ctx.schemas();
    let exitData: ExitIndexData | null = null;
    let exitLoading = false;
    /** The open level's exits alone (fast); the whole disc (`exitData`) follows and feeds the arrival check. */
    let quickExits: ExitIndexData | null = null;
    let quickFor = '';
    let quickLoading = false;
    const volumes = createVolumeLayer(ctx, env);

    const listeners = new Set<() => void>();

    // ---- DOM --------------------------------------------------------------------------------------------------------
    const volSel = h(
        'select',
        { class: 'sel', 'aria-label': 'Show zones', title: 'Trigger zones and distances, drawn in the level view' },
        h('option', { value: 'selected' }, 'Zones: selected'),
        h('option', { value: 'all' }, 'Zones: all'),
        h('option', { value: 'off' }, 'Zones: off'),
    );
    const tplSel = h(
        'select',
        { class: 'sel', 'aria-label': 'Ready-made setup' },
        h('option', { value: '' }, 'Ready-made setup'),
        ...TEMPLATES.map((t) => h('option', { value: t.id }, t.title)),
    );
    // The select is only the data model (setups can also be opened by id from elsewhere); the toolbar shows a menu button.
    const tplBtn = h(
        'button',
        {
            class: 'btn',
            type: 'button',
            'aria-haspopup': 'menu',
            title: 'Make a whole puzzle in one go: plates and a door, a quest, a cut scene trigger...',
        },
        icon('plus', 13),
        'Ready-made setup',
        icon('chevron-down', 11),
    );
    tplBtn.addEventListener('click', () => {
        const r = tplBtn.getBoundingClientRect();
        contextMenu(
            r.left,
            r.bottom + 2,
            TEMPLATES.map((t) => ({
                label: t.title,
                hint: t.summary.length > 60 ? `${t.summary.slice(0, 58)}...` : t.summary,
                run: () => {
                    tplSel.value = t.id;
                    tplSel.dispatchEvent(new Event('change'));
                },
            })),
        );
    });
    const form = h('div', { class: 'lg-form', hidden: true });
    const view = createLogicGraph({
        ctx,
        env,
        openSetup: (id) => {
            tplSel.value = id;
            tplSel.dispatchEvent(new Event('change'));
        },
        extra: (o) => exitPicker(o),
    });
    const root = h(
        'div',
        { class: 'panel logic-panel' },
        h(
            'div',
            { class: 'panel-tools' },
            ...view.tools.slice(0, 3),
            tplBtn,
            ...view.tools.slice(3),
            volSel,
            h('span', { class: 'spacer' }),
            view.info,
        ),
        form,
        view.el,
    );
    const style = document.createElement('style');
    style.textContent = `
.logic-panel{display:flex;flex-direction:column;height:100%;min-height:0}
.logic-panel .lg-body{display:flex;flex:1;min-height:0}
.logic-panel .lg-wrap{flex:1;min-width:0;position:relative;background:var(--surface-0);overflow:hidden}
.logic-panel .lg-wrap.drop-target{outline:2px dashed var(--accent);outline-offset:-3px}
.logic-panel .lg-canvas{display:block;user-select:none;outline:none;cursor:grab}
.logic-panel .lg-side{width:320px;overflow:auto;border-left:1px solid var(--border);padding:10px;font-size:12px}
.logic-panel .lg-form{padding:8px;border-bottom:1px solid var(--border);display:flex;flex-wrap:wrap;gap:8px;align-items:end}
.logic-panel .lg-form label{display:flex;flex-direction:column;gap:2px;font-size:11px}
.logic-panel .lg-form .lg-note{flex-basis:100%;font-size:11px;opacity:.75}
.logic-panel .panel-tools .dim.warn{color:var(--warn)}
.logic-panel .lg-node rect.box{fill:var(--surface-2);stroke-width:1.5}
.logic-panel .lg-node.sel rect.box{fill:var(--surface-3);stroke-width:3}
.logic-panel .lg-node.drop rect.box{stroke-width:4;fill:var(--surface-3)}
.logic-panel .lg-node text{pointer-events:none}
.logic-panel .lg-node text.role{fill:#101114;font-size:9px;font-weight:700;letter-spacing:.06em}
.logic-panel .lg-node text.name{fill:var(--fg);font-size:12px;font-weight:600}
.logic-panel .lg-node text.sub{fill:var(--fg-muted);font-size:10px}
.logic-panel .lg-port{fill:var(--surface-0);stroke-width:2;cursor:crosshair}
.logic-panel .lg-port.in{cursor:default}
.logic-panel .lg-port.idle{opacity:.35}
.logic-panel .lg-port.out:hover{fill:var(--fg)}
.logic-panel .lg-badge circle{fill:var(--warn)}
.logic-panel .lg-badge text{fill:#101114;font-size:11px;font-weight:800}
.logic-panel .lg-edge{fill:none;stroke:var(--fg-faint);stroke-width:1.8}
.logic-panel .lg-edge.sel{stroke:var(--warn);stroke-width:3}
.logic-panel .lg-edge.bad{stroke:var(--danger,#f06a62);stroke-dasharray:6 4}
.logic-panel .lg-hit{fill:none;stroke:transparent;stroke-width:14;cursor:pointer}
.logic-panel .lg-pill{cursor:pointer}
.logic-panel .lg-pill rect{fill:var(--surface-3);stroke:var(--border-strong)}
.logic-panel .lg-pill:hover rect{stroke:var(--accent)}
.logic-panel .lg-pill text{fill:var(--fg);font-size:10.5px}
.logic-panel .lg-pill.bad rect{stroke:var(--danger,#f06a62)}
.logic-panel .lg-empty{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:8px;text-align:center;padding:24px;color:var(--fg-muted);pointer-events:none}
.logic-panel .lg-empty[hidden]{display:none}
.logic-panel .lg-empty strong{color:var(--fg);font-size:14px}
.logic-panel .lg-empty span{max-width:420px}
.logic-panel .lg-empty-row{display:flex;gap:8px;pointer-events:auto;margin-top:6px}
.logic-panel .lg-side h4{margin:0 0 6px;font-size:13px;display:flex;align-items:center;gap:8px}
.logic-panel .lg-chip{font-size:9px;font-weight:700;color:#101114;border-radius:9px;padding:1px 7px;letter-spacing:.04em;text-transform:uppercase}
.logic-panel .lg-side .row{display:flex;gap:6px;align-items:center;margin:6px 0;flex-wrap:wrap}
.logic-panel .lg-sect{margin-top:12px}
.logic-panel .lg-sect-h{font-size:10px;font-weight:700;letter-spacing:.06em;text-transform:uppercase;color:var(--fg-muted);margin-bottom:4px}
.logic-panel .lg-linkrow{display:flex;align-items:center;gap:6px;margin:3px 0}
.logic-panel .lg-link-name{display:flex;align-items:center;gap:6px;flex:1;min-width:0;background:none;border:0;color:var(--fg);text-align:left;cursor:pointer;padding:2px 0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.logic-panel .lg-link-name:hover{text-decoration:underline}
.logic-panel .dot,.lg-add .dot{width:8px;height:8px;border-radius:50%;flex:none;display:inline-block}
.logic-panel .lg-problem{display:flex;gap:6px;align-items:flex-start;background:color-mix(in srgb,var(--warn) 14%,transparent);border-radius:6px;padding:6px 8px;margin:6px 0;color:var(--fg)}
.logic-panel .lg-problem .ico{flex:none;color:var(--warn);margin-top:1px}
.logic-panel .lg-sentence{margin:4px 0 8px;font-size:12.5px}
.logic-panel .lg-howto{margin:0 0 10px;padding-left:18px;line-height:1.6}
.logic-panel .lg-details{margin-top:14px}
.logic-panel .lg-details summary{cursor:pointer;color:var(--fg-muted);margin-bottom:6px}
.lg-add{position:fixed;z-index:1200;width:360px;max-height:min(460px,70vh);display:flex;flex-direction:column;background:var(--surface-2);border:1px solid var(--border-strong);border-radius:10px;box-shadow:0 12px 36px rgba(0,0,0,.45);overflow:hidden}
.lg-add-head{padding:8px 12px 0;font-size:11px;font-weight:700;letter-spacing:.05em;text-transform:uppercase;color:var(--fg-muted)}
.lg-add-q{margin:8px 10px;padding:7px 10px;border-radius:6px;border:1px solid var(--border-strong);background:var(--field);color:var(--fg);font-size:13px}
.lg-add-list{overflow:auto;padding:0 6px 8px;display:flex;flex-direction:column}
.lg-add-group{padding:8px 8px 3px;font-size:10px;font-weight:700;letter-spacing:.06em;text-transform:uppercase;color:var(--fg-muted)}
.lg-add-row{height:auto;min-height:40px;line-height:1.25;flex:none;display:flex;align-items:center;gap:10px;width:100%;text-align:left;background:none;border:0;border-radius:6px;padding:6px 8px;color:var(--fg);cursor:pointer}
.lg-add-row.active,.lg-add-row:hover{background:var(--surface-3)}
.lg-add-row:disabled{opacity:.45;cursor:default}
.lg-add-row .txt{display:flex;flex-direction:column;min-width:0}
.lg-add-row .nm{font-size:12.5px;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.lg-add-row .sb{font-size:10.5px;color:var(--fg-muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.lg-add-none{padding:14px;color:var(--fg-muted);font-size:12px}
`;
    root.append(style);

    // ---- exit destination ---------------------------------------------------------------------------------------------
    /** Destination level picker of an exit: rewrites the class's destination string of the level XDB (exported with the Story pack). */
    let exitOnlyThis = true;
    function exitPicker(o: LevelObject): HTMLElement | null {
        const host = exitEditHost();
        if (!host) return null;
        loadExitsOnce();
        const data = exitData ?? quickExits;
        if (!data) {
            loadQuickOnce();
            return h('div', { class: 'dim' }, 'Reading where the exits lead...');
        }
        const doc = ctx.doc();
        const mine = data.levels.find(
            (l) => norm(l.name) === norm(doc.level) || norm(destinationLevelName(l.name)) === norm(doc.level),
        );
        const { dbIndex, classId } = resourceParts(o.resource);
        const c = mine?.classes[`${dbIndex}:${classId}`];
        if (!c?.destRef) {
            return h(
                'div',
                { class: 'dim' },
                'Destination: ' + (c?.dest ?? 'unknown') + ' (no editable level string behind this exit).',
            );
        }
        const { file, id: classId0 } = c.destRef;
        // An exit with its own string (a per-exit override) edits that one; the others share the class's string.
        const own = o.fields?.find((f) => f.fieldIndex === 0 && typeof f.value === 'number');
        const id = own ? (own.value as number) : classId0;
        const text = host.text(file, id);
        if (text === undefined) {
            void host.ensure().catch(() => undefined);
            return h('div', { class: 'dim' }, 'Loading the level strings...');
        }
        const sel = h('select', { class: 'sel', 'aria-label': 'Destination level' });
        const names = data.levelNames ? [...data.levelNames] : data.levels.map((l) => l.name);
        // The project's level copies are destinations too (the game opens the copy's own LVL by the path in the text).
        for (const l of projects.get()?.levels ?? [])
            if (l.copyOf && !names.some((n) => norm(n) === norm(l.title))) names.push(l.title);
        const now = destinationLevelName(text);
        if (!names.some((n) => norm(n) === norm(now))) names.unshift(now);
        names.forEach((n) => sel.append(h('option', { value: n }, n)));
        sel.value = names.find((n) => norm(n) === norm(now)) ?? now;
        const only = h('input', { type: 'checkbox', id: 'lg-exit-only', class: 'cb' });
        only.checked = exitOnlyThis || !!own;
        only.disabled = !!own;
        only.addEventListener('change', () => (exitOnlyThis = only.checked));
        sel.addEventListener('change', () => {
            if (only.checked && !own) {
                // A new string for this exit alone; the object's own parameter points at it.
                const fresh = host.addDestination(file, classId0, exitDestinationText(text, sel.value));
                const schema = ctx.schemas()?.schemaFor(o.resource);
                const f0 = schema?.fields.find((f) => f.index === 0);
                if (fresh === null || !f0) {
                    ctx.toast('This exit has no destination field to override.', 'error');
                    return;
                }
                ctx.edit('Retarget one exit', [o.id], (obj) => {
                    const rest = (obj.fields ?? []).filter((f) => f.fieldIndex !== 0);
                    obj.fields = [
                        ...rest,
                        { slot: rest.length, fieldIndex: 0, type: f0.type, name: f0.name, value: fresh },
                    ];
                });
                ctx.toast(
                    `This exit alone now leads to ${sel.value} (new string ${fresh}); export it with the pack.`,
                    'ok',
                );
                return;
            }
            host.set(file, id, exitDestinationText(text, sel.value));
            ctx.toast(`Exit now leads to ${sel.value}; export it with the Story pack.`, 'ok');
        });
        return h(
            'div',
            { class: 'lg-exit' },
            h(
                'div',
                { class: 'dim' },
                `Destination (${file} string ${id}${own ? ', this exit only' : ''}${host.changed(file, id) ? ', edited' : ''})`,
            ),
            sel,
            h(
                'label',
                { class: 'dim', title: 'Exits of a class share one destination string; untick to change all of them' },
                only,
                ' Only this exit (its own string)',
            ),
            h(
                'div',
                { class: 'dim' },
                own
                    ? 'This exit has its own destination string; the other exits of the class are not affected.'
                    : 'Unticked: edits the shared level string, so every exit of this class changes. Arrival: an exit of the destination that leads back here (check Validation).',
            ),
        );
    }

    // ---- templates --------------------------------------------------------------------------------------------------
    let presets: ParamValues = {};
    function paramInput(t: LogicTemplate, p: TemplateParam, vals: ParamValues): HTMLElement {
        const e = env()!;
        const doc = ctx.doc();
        let control: HTMLElement;
        if (p.kind === 'number') {
            const i = h('input', {
                type: 'number',
                class: 'num',
                step: p.step ?? 1,
                min: p.min,
                max: p.max,
                value: String(presets[p.id] ?? p.default ?? 0),
            });
            vals[p.id] = Number(presets[p.id] ?? p.default ?? 0);
            i.addEventListener('input', () => (vals[p.id] = Number(i.value)));
            control = i;
        } else if (p.kind === 'bool') {
            const i = h('input', { type: 'checkbox', class: 'cb' });
            i.checked = p.default === true;
            vals[p.id] = i.checked;
            i.addEventListener('change', () => (vals[p.id] = i.checked));
            control = i;
        } else if (p.kind === 'choice') {
            const s = h(
                'select',
                { class: 'sel' },
                ...(p.options ?? []).map((o) => h('option', { value: o.value }, o.label)),
            );
            s.value = String(presets[p.id] ?? p.default ?? p.options?.[0]?.value ?? '');
            vals[p.id] = s.value;
            s.addEventListener('change', () => (vals[p.id] = s.value));
            control = s;
        } else if (p.kind === 'class') {
            const list = ctx
                .catalog()
                .filter((c) => c.rflClass === p.rfl)
                .sort((a, b) => b.instances - a.instances || a.name.localeCompare(b.name));
            const s = h(
                'select',
                { class: 'sel' },
                ...list.map((c) =>
                    h('option', { value: c.resource }, `${c.name}${c.instances ? ` (${c.instances} in level)` : ''}`),
                ),
            );
            vals[p.id] = list[0]?.resource;
            s.addEventListener('change', () => (vals[p.id] = s.value));
            control = list.length
                ? s
                : h('span', { class: 'dim' }, 'This level has none to copy. Try a level that has one.');
        } else {
            // object / objects
            const sel = new Set(ctx.doc().size ? ctx.view().selection : []);
            const cands = [...doc.objects()].filter((o) => {
                const s = logicOf(o, e);
                return s && (!p.roles || p.roles.includes(s.role));
            });
            // selected first, then by id
            cands.sort((a, b) => Number(sel.has(b.id)) - Number(sel.has(a.id)) || a.id - b.id);
            if (p.kind === 'object') {
                const s = h(
                    'select',
                    { class: 'sel' },
                    ...(p.optional ? [h('option', { value: '0' }, 'new')] : []),
                    ...cands
                        .slice(0, 400)
                        .map((o) =>
                            h(
                                'option',
                                { value: String(o.id) },
                                `${sel.has(o.id) ? '* ' : ''}${o.class} (object ${o.id})`,
                            ),
                        ),
                );
                vals[p.id] = Number(s.value);
                s.addEventListener('change', () => (vals[p.id] = Number(s.value)));
                control = s;
            } else {
                control = h('span', { class: 'dim' }, `${[...sel].filter((id) => doc.has(id)).length} selected`);
                vals[p.id] = undefined;
            }
        }
        void t;
        return h('label', { title: p.hint ?? '' }, p.label, control);
    }

    openTemplateHook = (id, preset) => {
        presets = preset;
        tplSel.value = id;
        tplSel.dispatchEvent(new Event('change'));
        presets = {};
    };
    tplSel.addEventListener('change', () => {
        const t = TEMPLATES.find((x) => x.id === tplSel.value);
        tplSel.value = '';
        if (!t) return void (form.hidden = true);
        if (!env() || !ctx.doc().size) return ctx.toast('Open a level from the disc first.', 'warn');
        const vals: ParamValues = {};
        const inputs = t.params.map((p) => paramInput(t, p, vals));
        const go = h('button', { class: 'btn primary', type: 'button' }, `Create`);
        go.addEventListener('click', () => {
            const v = ctx.view();
            const sel = [...v.selection];
            const at =
                (sel.length ? ctx.doc().get(sel[0]!)?.transform.pos : null) ?? v.groundAtCentre() ?? v.cameraGamePos();
            const r = t.build(
                { doc: ctx.doc(), env: env()!, catalog: ctx.catalog(), at: [at[0], at[1], at[2]], selection: sel },
                vals,
            );
            if (isError(r)) return ctx.toast(r.error, 'error');
            const cmd = new ApplyTemplateCommand(r, t.title);
            v.run(cmd);
            v.select(cmd.ids);
            for (const w of r.warnings) ctx.toast(w, 'warn');
            ctx.toast(r.notes.join('. '), 'ok');
            form.hidden = true;
            view.docChanged();
        });
        fill(
            form,
            h('strong', {}, t.title),
            ...inputs,
            go,
            h('button', { class: 'btn', type: 'button', onclick: () => (form.hidden = true) }, 'Cancel'),
            h(
                'div',
                { class: 'lg-note' },
                `${t.summary} Placed at the first selected object, else the viewport centre.`,
            ),
        );
        form.hidden = false;
    });

    volSel.addEventListener('change', () => volumes.setMode(volSel.value as VolumeMode));

    // ---- validation provider ----------------------------------------------------------------------------------------
    /** The level XDB the exit class's destination string lives in. */
    function ownFile(o: LevelObject): string {
        const doc = ctx.doc();
        const mine = (exitData ?? quickExits)?.levels.find(
            (l) => norm(l.name) === norm(doc.level) || norm(destinationLevelName(l.name)) === norm(doc.level),
        );
        const { dbIndex, classId } = resourceParts(o.resource);
        return mine?.classes[`${dbIndex}:${classId}`]?.destRef?.file ?? '';
    }
    function exitIndex(): ExitIndex | undefined {
        if (!exitData) return undefined;
        return { levels: new Map(exitData.levels.map((l) => [l.name, l.exits as readonly ExitIndexEntry[]])) };
    }
    function loadQuickOnce(): void {
        const name = ctx.doc().level;
        if (exitData || quickLoading || (quickExits && quickFor === name)) return;
        quickLoading = true;
        quickFor = name;
        loadExits(name)
            .then((d) => {
                quickExits = d;
                listeners.forEach((f) => f());
            })
            .catch(() => undefined)
            .finally(() => (quickLoading = false));
    }
    function loadExitsOnce(): void {
        if (exitData || exitLoading) return;
        exitLoading = true;
        loadExits()
            .then((d) => {
                exitData = d;
                listeners.forEach((f) => f());
            })
            .catch(() => undefined)
            .finally(() => (exitLoading = false));
    }
    addValidationProvider({
        issues(): ValidationIssue[] {
            const e = env();
            const doc = ctx.doc();
            if (!e || doc.size === 0) return [];
            loadExitsOnce();
            const mine = exitData?.levels.find(
                (l) => norm(l.name) === norm(doc.level) || norm(destinationLevelName(l.name)) === norm(doc.level),
            );
            return validateLogic(doc, e, {
                level: mine?.name ?? doc.level,
                ...(exitData ? { exits: exitIndex()! } : {}),
                exitDestination: (o: LevelObject) => {
                    if (!exitData) return { dest: '(loading)', portal: false };
                    // A per-exit string made in this session beats what the disc says.
                    const own = o.fields?.find((f) => f.fieldIndex === 0 && typeof f.value === 'number');
                    const ownText = own ? exitEditHost()?.text(ownFile(o), own.value as number) : undefined;
                    const placed = mine?.exits.find((x) => x.id === o.id && o.origin !== undefined);
                    if (ownText) return { dest: destinationLevelName(ownText), portal: placed?.portal ?? false };
                    if (placed) return { dest: placed.dest, portal: placed.portal };
                    const { dbIndex, classId } = resourceParts(o.resource);
                    const c = mine?.classes[`${dbIndex}:${classId}`];
                    return c ? { dest: c.dest, portal: c.portal } : null;
                },
            });
        },
        onChange(refresh: () => void) {
            listeners.add(refresh);
        },
    });

    // ---- panel registration -----------------------------------------------------------------------------------------
    const soon = raf(() => {
        view.docChanged();
        volumes.refresh();
    });
    const soonSel = raf(() => {
        view.selectionChanged();
        volumes.refresh();
    });
    const panel: Panel = {
        el: root,
        onSelection: soonSel,
        onChange: soon,
        onVisibility: () => volumes.refresh(),
        onLoad: () => {
            view.levelLoaded();
            volumes.refresh();
        },
    };
    onExitEditChange(soon);
    registerPanel({
        id: 'logic',
        title: 'Logic',
        icon: 'link',
        panel,
        command: {
            keys: 'Ctrl+Shift+L',
            keywords: 'trigger graph link message cut scene door puzzle',
            hint: 'Wire triggers, timers, doors and cut scenes in a graph',
        },
    });
    registerCommand({
        id: 'logic.volumes',
        title: 'Toggle trigger volumes in the viewport',
        category: 'View',
        keywords: 'trigger box radius',
        run: () => {
            const m = volumes.mode() === 'all' ? 'selected' : 'all';
            volSel.value = m;
            volumes.setMode(m);
        },
    });
    registerCommand({
        id: 'logic.level',
        title: 'Show the whole level logic graph',
        category: 'View',
        run: () => view.showLevel(),
    });
    return panel;
}

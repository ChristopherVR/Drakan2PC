// "Drakan 2 characters and weapons" section of the Order of the Flame import dialog: Wartoks, orcs and plain weapons use the
// disc's own models (with their textures, skeletons and animations) instead of the converted Order of the Flame ones. On by
// default, with a tick per group and per pair, a choice of animations for the enemies and pairs of your own. Remembered in
// this browser.

import {
    DEFAULT_PS2_MODEL_SUBSTITUTIONS,
    modelKey,
    type EnemyAnimations,
    type Ps2ModelCategory,
    type Ps2ModelSubstitution,
    type Ps2ModelSwapOptions,
} from '@drakan/formats';
import { h, store } from './dom.ts';

export interface Ps2ModelChoice {
    enabled: boolean;
    categoriesOff: Ps2ModelCategory[];
    /** Class names of pairs turned off / of off-by-default pairs turned on. */
    off: string[];
    on: string[];
    extra: Ps2ModelSubstitution[];
    enemyAnimations: EnemyAnimations;
}

export const MODEL_STORE_KEY = 'ootf-ps2-models';

export const defaultModelChoice = (): Ps2ModelChoice => ({
    enabled: true,
    categoriesOff: [],
    off: [],
    on: [],
    extra: [],
    enemyAnimations: 'drakan2',
});

export function modelChoiceToOptions(c: Ps2ModelChoice): Ps2ModelSwapOptions {
    return {
        enabled: c.enabled,
        categoriesOff: [...c.categoriesOff],
        off: [...c.off],
        on: [...c.on],
        extra: [...c.extra],
        enemyAnimations: c.enemyAnimations,
    };
}

const CATEGORY_NAMES: Record<Ps2ModelCategory, string> = {
    enemies: 'Enemies',
    weapons: 'Weapons',
    props: 'Props',
    flames: 'Flames',
    effects: 'Effects',
};
const strings = (v: unknown): string[] => (Array.isArray(v) ? v.filter((x): x is string => typeof x === 'string') : []);

/** A stored choice, tolerating anything malformed (falls back to the default for that part). */
export function readModelChoice(raw: unknown): Ps2ModelChoice {
    const c = defaultModelChoice();
    if (!raw || typeof raw !== 'object') return c;
    const o = raw as Record<string, unknown>;
    if (typeof o['enabled'] === 'boolean') c.enabled = o['enabled'];
    c.categoriesOff = strings(o['categoriesOff']).filter((x): x is Ps2ModelCategory => x in CATEGORY_NAMES);
    c.off = strings(o['off']);
    c.on = strings(o['on']);
    if (o['enemyAnimations'] === 'ootf') c.enemyAnimations = 'ootf';
    if (Array.isArray(o['extra']))
        for (const r of o['extra']) {
            const x = r as Partial<Ps2ModelSubstitution> | null;
            const row = customModelPair(x?.category ?? 'weapons', x?.ootf ?? '', x?.ps2 ?? '', x?.label);
            if (typeof row !== 'string') c.extra.push(row);
        }
    return c;
}

/** A pair typed by the player (class names as the two games' editors show them), or a sentence saying what is wrong. */
export function customModelPair(
    category: string,
    ootf: string,
    ps2: string,
    label?: string,
): Ps2ModelSubstitution | string {
    if (category !== 'enemies' && category !== 'weapons') return 'Choose Enemies or Weapons.';
    if (!modelKey(ootf)) return 'Order of the Flame class: write the class name, for example Long Sword.';
    if (!modelKey(ps2)) return 'Drakan 2 class: write the class name, for example Steel Longsword.';
    return {
        category: category as Ps2ModelCategory,
        ootf: ootf.trim(),
        ps2: ps2.trim(),
        label: (label ?? '').trim() || ootf.trim(),
        why: 'Added by you',
    };
}

/** Is the pair in use under this choice (for the ticks). */
export function pairOn(c: Ps2ModelChoice, r: Ps2ModelSubstitution): boolean {
    const k = modelKey(r.ootf);
    if (r.defaultOff) return c.on.some((x) => modelKey(x) === k);
    return !c.off.some((x) => modelKey(x) === k);
}

/** The result line after a conversion. */
export function modelResultText(r: { used: { label: string }[] } | undefined, enabled: boolean): string {
    if (!enabled) return 'Drakan 2 characters and weapons: off, the Order of the Flame models were converted.';
    const n = r?.used.length ?? 0;
    if (!n) return 'Drakan 2 characters and weapons: none of the selected levels uses one from the list.';
    return `Drakan 2 characters and weapons used for ${n === 1 ? '1 class' : `${n} classes`}.`;
}

export function ps2ModelPanel(): { el: HTMLElement; options(): Ps2ModelSwapOptions; enabled(): boolean } {
    const choice = readModelChoice(store.json<unknown>(MODEL_STORE_KEY, null));
    const save = (): void => store.set(MODEL_STORE_KEY, JSON.stringify(choice));
    const master = h('input', { type: 'checkbox' });
    master.checked = choice.enabled;
    const groups = h('div', {});
    const err = h('div', { class: 'od-bad' });
    const anim = h(
        'select',
        {},
        h('option', { value: 'drakan2' }, 'Drakan 2 animations'),
        h('option', { value: 'ootf' }, 'Order of the Flame animations on the Drakan 2 model'),
    );
    anim.value = choice.enemyAnimations;
    anim.addEventListener('change', () => {
        choice.enemyAnimations = anim.value === 'ootf' ? 'ootf' : 'drakan2';
        save();
    });

    const toggle = (list: 'off' | 'on', name: string, add: boolean): void => {
        choice[list] = choice[list].filter((x) => modelKey(x) !== modelKey(name));
        if (add) choice[list].push(name);
    };
    const pairRow = (r: Ps2ModelSubstitution, custom: boolean): HTMLElement => {
        const box = h('input', { type: 'checkbox' });
        box.checked = custom || pairOn(choice, r);
        box.disabled = !choice.enabled || choice.categoriesOff.includes(r.category);
        box.addEventListener('change', () => {
            if (custom) {
                if (!box.checked) choice.extra = choice.extra.filter((x) => modelKey(x.ootf) !== modelKey(r.ootf));
            } else if (r.defaultOff) toggle('on', r.ootf, box.checked);
            else toggle('off', r.ootf, !box.checked);
            save();
            if (custom && !box.checked) render();
        });
        return h(
            'label',
            { title: `${r.ootf}  ->  ${r.ps2}` },
            box,
            h('span', {}, r.label),
            h('span', { class: 'od-dim' }, r.why),
        );
    };
    const render = (): void => {
        groups.replaceChildren(
            ...(Object.keys(CATEGORY_NAMES) as Ps2ModelCategory[]).map((cat) => {
                const box = h('input', { type: 'checkbox' });
                box.checked = !choice.categoriesOff.includes(cat);
                box.disabled = !choice.enabled;
                box.addEventListener('change', () => {
                    choice.categoriesOff = choice.categoriesOff.filter((x) => x !== cat);
                    if (!box.checked) choice.categoriesOff.push(cat);
                    save();
                    render();
                });
                const rows = [
                    ...DEFAULT_PS2_MODEL_SUBSTITUTIONS.filter((r) => r.category === cat).map((r) => pairRow(r, false)),
                    ...choice.extra.filter((r) => r.category === cat).map((r) => pairRow(r, true)),
                ];
                return h(
                    'div',
                    {},
                    h('label', { class: 'od-row' }, box, h('strong', {}, CATEGORY_NAMES[cat])),
                    cat === 'enemies' ? h('div', { class: 'od-row' }, h('span', {}, 'Animations'), anim) : null,
                    h('div', { class: 'od-list' }, ...rows),
                );
            }),
        );
    };
    master.addEventListener('change', () => {
        choice.enabled = master.checked;
        save();
        render();
    });

    const catSel = h(
        'select',
        {},
        h('option', { value: 'weapons' }, 'Weapon'),
        h('option', { value: 'enemies' }, 'Enemy'),
    );
    const ootfIn = h('input', {
        type: 'text',
        spellcheck: 'false',
        placeholder: 'Order of the Flame class, e.g. Long Sword',
    });
    const ps2In = h('input', {
        type: 'text',
        spellcheck: 'false',
        placeholder: 'Drakan 2 class, e.g. Steel Longsword',
    });
    const add = h(
        'button',
        {
            class: 'btn',
            type: 'button',
            onclick: () => {
                const r = customModelPair(catSel.value, ootfIn.value, ps2In.value);
                if (typeof r === 'string') {
                    err.textContent = r;
                    return;
                }
                err.textContent = '';
                choice.extra = [...choice.extra.filter((x) => modelKey(x.ootf) !== modelKey(r.ootf)), r];
                save();
                ootfIn.value = ps2In.value = '';
                render();
            },
        },
        'Add pair',
    );
    render();
    const el = h(
        'details',
        { class: 'od-ps2model' },
        h(
            'summary',
            {},
            h(
                'label',
                { onclick: (ev: Event) => ev.stopPropagation() },
                master,
                ' Use Drakan 2 characters and weapons where the disc has the same one',
            ),
        ),
        h(
            'div',
            { class: 'od-dim' },
            "Wartoks, orcs, the spider and plain weapons use the disc's own models, textures and animations. Weapons keep their Order of the Flame damage and effects; the Wartoks, orcs and the spider already play as their Drakan 2 creatures. Rynn and Arokh are always the Drakan 2 characters; the Order of the Flame villagers have no Drakan 2 counterpart.",
        ),
        groups,
        h(
            'details',
            {},
            h('summary', { class: 'od-dim' }, 'Add your own pair'),
            h('div', { class: 'od-row' }, catSel, ootfIn, ps2In, add),
            err,
        ),
    );
    return {
        el,
        options: () => {
            save();
            return modelChoiceToOptions(choice);
        },
        enabled: () => choice.enabled,
    };
}

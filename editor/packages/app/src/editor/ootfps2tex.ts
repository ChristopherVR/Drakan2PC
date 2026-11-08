// "Drakan 2 textures" section of the Order of the Flame import dialog: use the disc's own texture where Drakan 2 has a matching
// one (a pine trunk, the tropical plant fronds) instead of the converted Order of the Flame picture. On by default; each pair can
// be turned off, and pairs of your own can be added. The choice is remembered in this browser.

import {
    DEFAULT_PS2_TEXTURE_SUBSTITUTIONS,
    ootfTextureKey,
    parseTextureRef,
    type Ps2TextureSubstitution,
    type Ps2TextureSwapOptions,
} from '@drakan/formats';
import { h, store } from './dom.ts';

/** What the player chose; stored as JSON under `STORE_KEY`. */
export interface Ps2TextureChoice {
    enabled: boolean;
    /** OotF refs of default pairs turned off. */
    off: string[];
    /** Pairs the player added. */
    extra: Ps2TextureSubstitution[];
}

export const STORE_KEY = 'ootf-ps2-textures';

export const defaultChoice = (): Ps2TextureChoice => ({ enabled: true, off: [], extra: [] });

/** The pipeline options for a choice (`OotfPipelineOptions.ps2Textures`). */
export function choiceToOptions(c: Ps2TextureChoice): Ps2TextureSwapOptions {
    return { enabled: c.enabled, off: [...c.off], extra: [...c.extra] };
}

/** A stored choice, tolerating anything malformed (falls back to the default for that part). */
export function readChoice(raw: unknown): Ps2TextureChoice {
    const c = defaultChoice();
    if (!raw || typeof raw !== 'object') return c;
    const o = raw as Partial<Record<keyof Ps2TextureChoice, unknown>>;
    if (typeof o.enabled === 'boolean') c.enabled = o.enabled;
    if (Array.isArray(o.off)) c.off = o.off.filter((x): x is string => typeof x === 'string');
    if (Array.isArray(o.extra))
        for (const r of o.extra) {
            const row = customPair(
                (r as Partial<Ps2TextureSubstitution>)?.ootf ?? '',
                (r as Partial<Ps2TextureSubstitution>)?.ps2 ?? '',
                (r as Partial<Ps2TextureSubstitution>)?.label,
            );
            if (typeof row !== 'string') c.extra.push(row);
        }
    return c;
}

/**
 * A pair typed by the player: the Order of the Flame texture (`<folder path>/<file name without extension>#<number>`) and the
 * Drakan 2 one (`<disc path>.TXD#<number>`). Returns the pair, or a sentence saying what is wrong.
 */
export function customPair(ootf: string, ps2: string, label?: string): Ps2TextureSubstitution | string {
    try {
        parseTextureRef(ootf);
    } catch {
        return 'Order of the Flame texture: write it as the texture file path without extension, then # and its number.';
    }
    let disc;
    try {
        disc = parseTextureRef(ps2);
    } catch {
        return 'Drakan 2 texture: write it as the disc path of the texture file, then # and its number.';
    }
    if (!/\.TXD$/i.test(disc.path)) return 'Drakan 2 texture: the disc path must end in .TXD.';
    const name = (label ?? '').trim() || `Texture ${parseTextureRef(ootf).id}`;
    return { ootf: ootf.trim(), ps2: `${disc.path.toUpperCase()}#${disc.id}`, label: name, why: 'Added by you' };
}

/** The result line after a conversion. */
export function resultText(r: { used: { label: string }[] } | undefined, enabled: boolean): string {
    if (!enabled) return 'Drakan 2 textures: off, every Order of the Flame texture was converted.';
    const n = r?.used.length ?? 0;
    if (!n) return 'Drakan 2 textures: none of the selected levels uses a texture from the list.';
    return `Drakan 2 textures used for ${n === 1 ? '1 texture' : `${n} textures`}: ${r!.used.map((u) => u.label).join(', ')}.`;
}

/** The dialog section. `options()` gives the current choice for the conversion (and remembers it). */
export function ps2TexturePanel(): { el: HTMLElement; options(): Ps2TextureSwapOptions; enabled(): boolean } {
    const choice = readChoice(store.json<unknown>(STORE_KEY, null));
    const save = (): void => store.set(STORE_KEY, JSON.stringify(choice));
    const master = h('input', { type: 'checkbox' });
    master.checked = choice.enabled;
    const list = h('div', { class: 'od-list' });
    const err = h('div', { class: 'od-bad' });

    const pairRow = (r: Ps2TextureSubstitution, custom: boolean): HTMLElement => {
        const box = h('input', { type: 'checkbox' });
        const key = ootfTextureKey(r.ootf);
        box.checked = custom || !choice.off.some((o) => ootfTextureKey(o) === key);
        box.disabled = !choice.enabled;
        box.addEventListener('change', () => {
            if (custom) {
                if (!box.checked) choice.extra = choice.extra.filter((x) => ootfTextureKey(x.ootf) !== key);
            } else {
                choice.off = choice.off.filter((o) => ootfTextureKey(o) !== key);
                if (!box.checked) choice.off.push(r.ootf);
            }
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
        list.replaceChildren(
            ...DEFAULT_PS2_TEXTURE_SUBSTITUTIONS.map((r) => pairRow(r, false)),
            ...choice.extra.map((r) => pairRow(r, true)),
        );
    };
    master.addEventListener('change', () => {
        choice.enabled = master.checked;
        save();
        render();
    });

    const ootfIn = h('input', {
        type: 'text',
        spellcheck: 'false',
        placeholder: 'Mountain World/Mountain World Common/Mountain World Common#4619',
        style: 'flex:1;min-width:16em',
    });
    const ps2In = h('input', {
        type: 'text',
        spellcheck: 'false',
        placeholder: 'WORLDCON/WORLDCON/WORLDCON.TXD#412',
        style: 'flex:1;min-width:12em',
    });
    const nameIn = h('input', { type: 'text', placeholder: 'Name (optional)', style: 'width:10em' });
    const add = h(
        'button',
        {
            class: 'btn',
            type: 'button',
            onclick: () => {
                const r = customPair(ootfIn.value, ps2In.value, nameIn.value);
                if (typeof r === 'string') {
                    err.textContent = r;
                    return;
                }
                err.textContent = '';
                choice.extra = [...choice.extra.filter((x) => ootfTextureKey(x.ootf) !== ootfTextureKey(r.ootf)), r];
                save();
                ootfIn.value = ps2In.value = nameIn.value = '';
                render();
            },
        },
        'Add pair',
    );
    render();
    const el = h(
        'details',
        { class: 'od-ps2tex' },
        h(
            'summary',
            {},
            h(
                'label',
                { onclick: (ev: Event) => ev.stopPropagation() },
                master,
                ' Use Drakan 2 textures where the disc has a matching one',
            ),
        ),
        h(
            'div',
            { class: 'od-dim' },
            "Some Order of the Flame objects have a Drakan 2 counterpart, such as the pine trunks. The disc's own texture is used for them (with its HD version when you have the HD textures). Untick a pair to keep the Order of the Flame texture.",
        ),
        list,
        h(
            'details',
            {},
            h('summary', { class: 'od-dim' }, 'Add your own pair'),
            h('div', { class: 'od-row' }, ootfIn, ps2In, nameIn, add),
            err,
        ),
    );
    return {
        el,
        options: () => {
            save();
            return choiceToOptions(choice);
        },
        enabled: () => choice.enabled,
    };
}

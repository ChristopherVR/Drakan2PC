/**
 * Shared UI building blocks of both applications (the level/mod editor and the save game editor). They first grew in
 * the save editor; here they carry structure and behaviour only. Each takes a class prefix: the save editor passes its
 * own (`sv-edit`, `sv-card`), so its Drakan skin (save.css / drakan.css) keeps styling them unchanged; the level editor
 * uses the default `c-` names, styled with its UE5 tokens in components.css.
 *
 *   changeRow   one pending change: what, before -> after, a tag, and Revert / Locate actions (the save editor's
 *               pending-changes tray; the level editor's Package and Play change list)
 *   changeList  a list of change rows with an empty state
 *   card        a titled section with a hint line and right-hand tools
 *   statTile    a label, a big value, an optional meter and a caption
 */
import './components.css';

export type Kid = Node | string | null | false | undefined;

function el<K extends keyof HTMLElementTagNameMap>(
    tag: K,
    cls: string | null,
    ...kids: Kid[]
): HTMLElementTagNameMap[K] {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    for (const k of kids) if (k !== null && k !== false && k !== undefined) e.append(k);
    return e;
}

export interface ChangeRowOptions {
    label: string;
    /** Old and new value as text; leave out for changes that have no single value (an added object). */
    before?: string;
    after?: string;
    /** A short tag at the end (confidence, "Added", "Moved"); `tone` colours it. */
    tag?: string;
    tone?: string;
    /** Revert this one change (shown as a button). */
    onRevert?: () => void;
    revertTitle?: string;
    /** Show what the change is about (select and frame an object). */
    onLocate?: () => void;
    locateTitle?: string;
    /** Element builders of the host (icons, badges, buttons) so a row looks native in either skin. */
    arrow?: () => Node;
    badge?: (text: string, tone: string) => Node;
    button?: (title: string, run: () => void, kind: 'revert' | 'locate') => Node;
    /** Class prefix: `c-change` (default) or the save editor's `sv-edit`. */
    prefix?: string;
}

export function changeRow(o: ChangeRowOptions): HTMLElement {
    const p = o.prefix ?? 'c-change';
    const hasValue = o.before !== undefined || o.after !== undefined;
    const plainButton = (title: string, run: () => void, kind: string): Node => {
        const b = el('button', `${p}-act ${kind}`, kind === 'revert' ? 'Revert' : 'Show');
        b.type = 'button';
        b.title = title;
        b.onclick = run;
        return b;
    };
    const btn = o.button ?? plainButton;
    const row = el(
        'div',
        `${p}${o.tone ? ` ${o.tone}` : ''}`,
        el(
            'div',
            `${p}-main`,
            el('b', null, o.label),
            hasValue
                ? el(
                      'span',
                      `${p}-chg`,
                      el('s', null, o.before ?? ''),
                      o.arrow ? o.arrow() : el('span', `${p}-arrow`, '->'),
                      el('em', null, o.after ?? ''),
                  )
                : null,
        ),
        o.tag ? (o.badge ? o.badge(o.tag, o.tone ?? '') : el('span', `${p}-tag ${o.tone ?? ''}`, o.tag)) : null,
        o.onLocate ? btn(o.locateTitle ?? `Show ${o.label}`, o.onLocate, 'locate') : null,
        o.onRevert ? btn(o.revertTitle ?? `Revert ${o.label}`, o.onRevert, 'revert') : null,
    );
    if (o.onLocate) {
        row.classList.add('locatable');
        row.addEventListener('dblclick', () => o.onLocate?.());
    }
    return row;
}

/** Change rows, or `empty` when there are none. */
export function changeList(rows: ChangeRowOptions[], empty: Node, max = 300, prefix = 'c-change'): HTMLElement {
    const box = el('div', `${prefix}-list`);
    if (!rows.length) box.append(empty);
    for (const r of rows.slice(0, max)) box.append(changeRow({ prefix, ...r }));
    if (rows.length > max) box.append(el('div', `${prefix}-more`, `${rows.length - max} more`));
    return box;
}

/** A titled section: title row, optional hint and right-hand tools, then the body. Prefix `c-card` or `sv-card`. */
export function card(
    title: string,
    opts: { hint?: string; tools?: Kid; class?: string; prefix?: string } = {},
    ...kids: Kid[]
): HTMLElement {
    const p = opts.prefix ?? 'c-card';
    const head = el(
        'header',
        `${p}-head`,
        el('h3', null, title),
        opts.hint ? el('p', null, opts.hint) : null,
        opts.tools ? el('div', `${p}-tools`, opts.tools) : null,
    );
    return el('section', `${p}${opts.class ? ` ${opts.class}` : ''}`, head, el('div', `${p}-body`, ...kids));
}

/** A stat tile: label, big value, optional meter (value / max) and a caption. Prefix `c-stat` or the save editor's. */
export function statTile(
    label: string,
    value: string,
    opts: { bar?: [number, number]; note?: string; cls?: string; meterCls?: string; noteCls?: string } = {},
): HTMLElement {
    const meter = opts.bar
        ? (() => {
              const m = el('div', opts.meterCls ?? 'c-meter');
              m.title = `${opts.bar[0]} of ${opts.bar[1]}`;
              const fillEl = el('i', null);
              fillEl.style.width = `${Math.max(0, Math.min(100, (opts.bar[0] / Math.max(opts.bar[1], 1)) * 100))}%`;
              m.append(fillEl);
              return m;
          })()
        : null;
    return el(
        'div',
        opts.cls ?? 'c-stat',
        el('small', null, label),
        el('b', null, value),
        meter,
        opts.note ? el('small', opts.noteCls ?? 'c-dim', opts.note) : null,
    );
}

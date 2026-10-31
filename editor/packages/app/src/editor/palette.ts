import { h, icon, store } from './dom.ts';
import { allCommands, allSources, fuzzyScore, onRegistryChange, type Command, type PaletteItem } from './registry.ts';
import './shellux.css';

/**
 * Command palette: one search box for every command, level, class and object. Plain text searches everything;
 * a prefix narrows it: ">" commands, "#" objects, "@" classes, ":" levels (prefixes come from the sources).
 */
interface Row {
    group: string;
    title: string;
    detail?: string;
    keys?: string;
    icon?: string;
    hint?: string;
    disabled?: boolean;
    run: () => void;
    commandId?: string;
}

const MAX_PER_GROUP = 8;
const MAX_RECENT = 6;

export interface Palette {
    open(initial?: string): void;
    close(): void;
    isOpen(): boolean;
}

export function createPalette(): Palette {
    const input = h('input', {
        class: 'pal-input',
        type: 'text',
        placeholder: 'Search commands, levels, classes and objects...',
        'aria-label': 'Command palette',
        autocomplete: 'off',
        spellcheck: 'false',
    });
    const list = h('div', { class: 'pal-list', role: 'listbox' });
    const foot = h(
        'div',
        { class: 'pal-foot' },
        h('span', {}, h('kbd', {}, 'Up'), h('kbd', {}, 'Down'), ' move'),
        h('span', {}, h('kbd', {}, 'Enter'), ' run'),
        h('span', {}, h('kbd', {}, 'Esc'), ' close'),
        h('span', { class: 'pal-spacer' }),
        h(
            'span',
            {},
            'Narrow with ',
            h('code', {}, '>'),
            ' commands ',
            h('code', {}, '#'),
            ' objects ',
            h('code', {}, '@'),
            ' classes ',
            h('code', {}, ':'),
            ' levels',
        ),
    );
    const card = h(
        'div',
        { class: 'pal-card', role: 'dialog', 'aria-label': 'Command palette' },
        h('div', { class: 'pal-head' }, icon('search', 15), input),
        list,
        foot,
    );
    const el = h('div', { class: 'pal-overlay', hidden: true }, card);
    document.body.append(el);

    let rows: Row[] = [];
    let sel = 0;
    const recent = (): string[] => store.json<string[]>('palette-recent', []);
    const pushRecent = (id: string): void =>
        store.set('palette-recent', JSON.stringify([id, ...recent().filter((r) => r !== id)].slice(0, MAX_RECENT)));

    const off = (c: Command): boolean => (c.enabled ? !c.enabled() : false);
    const cmdRow = (c: Command, group: string): Row => ({
        group,
        title: c.title,
        ...(c.category ? { detail: c.category } : {}),
        ...(c.keys ? { keys: c.keys } : {}),
        ...(c.icon ? { icon: c.icon } : {}),
        ...(c.hint ? { hint: c.hint } : {}),
        disabled: off(c),
        run: c.run,
        commandId: c.id,
    });
    const itemRow = (i: PaletteItem, group: string): Row => ({
        group,
        title: i.title,
        ...(i.detail ? { detail: i.detail } : {}),
        ...(i.icon ? { icon: i.icon } : {}),
        run: i.run,
    });

    function compute(raw: string): Row[] {
        const sources = allSources();
        const pfx = sources.find((s) => s.prefix && raw.startsWith(s.prefix));
        const onlyCommands = raw.startsWith('>');
        const q = (pfx || onlyCommands ? raw.slice(1) : raw).trim();
        const out: Row[] = [];
        const cmds = allCommands();
        if (!pfx) {
            if (!q) {
                const byId = new Map(cmds.map((c) => [c.id, c]));
                const rec = recent()
                    .map((id) => byId.get(id))
                    .filter((c): c is Command => !!c && !off(c));
                for (const c of rec) out.push(cmdRow(c, 'Recent'));
                const rest = cmds
                    .filter((c) => !rec.includes(c) && !off(c))
                    .sort(
                        (a, b) => (a.category ?? '').localeCompare(b.category ?? '') || a.title.localeCompare(b.title),
                    );
                for (const c of rest) out.push(cmdRow(c, c.category ?? 'Commands'));
            } else {
                const scored = cmds
                    .map((c) => ({
                        c,
                        s: Math.max(
                            fuzzyScore(c.title, q),
                            fuzzyScore(`${c.category ?? ''} ${c.title}`, q) * 0.9,
                            fuzzyScore(c.keywords ?? '', q) * 0.6,
                            fuzzyScore(c.hint ?? '', q) * 0.4,
                        ),
                    }))
                    .filter((x) => x.s > 0)
                    .sort((a, b) => Number(off(a.c)) - Number(off(b.c)) || b.s - a.s);
                for (const x of scored.slice(0, onlyCommands ? 40 : MAX_PER_GROUP)) out.push(cmdRow(x.c, 'Commands'));
            }
        }
        if (!onlyCommands) {
            for (const s of sources) {
                if (pfx && pfx !== s) continue;
                if (!pfx && !q && !s.showEmpty) continue;
                for (const i of s.search(q, pfx ? 60 : MAX_PER_GROUP)) out.push(itemRow(i, s.label));
            }
        }
        return out;
    }

    function render(): void {
        rows = compute(input.value);
        sel = Math.min(sel, Math.max(0, rows.length - 1));
        list.replaceChildren();
        if (!rows.length) {
            list.append(
                h(
                    'div',
                    { class: 'pal-none' },
                    h('strong', {}, 'No matches'),
                    h('span', {}, 'Try a class name ("dragon"), an object id ("#412"), or a command ("export").'),
                ),
            );
            return;
        }
        let last = '';
        rows.forEach((r, i) => {
            if (r.group !== last) {
                list.append(h('div', { class: 'pal-group' }, r.group));
                last = r.group;
            }
            const keys = h('span', { class: 'pal-keys' });
            if (r.keys) for (const k of r.keys.split('+')) keys.append(h('kbd', {}, k));
            const row = h(
                'div',
                {
                    class: `pal-row${i === sel ? ' sel' : ''}${r.disabled ? ' off' : ''}`,
                    role: 'option',
                    'aria-selected': String(i === sel),
                    'data-i': i,
                    ...(r.hint ? { title: r.hint } : {}),
                },
                icon(r.icon ?? 'generic', 15),
                h('span', { class: 'pal-title' }, r.title),
                r.detail ? h('span', { class: 'pal-detail' }, r.detail) : null,
                keys,
            );
            row.addEventListener('pointermove', () => {
                if (sel !== i) setSel(i, false);
            });
            row.addEventListener('click', () => run(i));
            list.append(row);
        });
    }

    function setSel(i: number, scroll = true): void {
        list.querySelector('.pal-row.sel')?.classList.remove('sel');
        sel = i;
        const row = list.querySelector<HTMLElement>(`.pal-row[data-i="${i}"]`);
        row?.classList.add('sel');
        if (scroll) row?.scrollIntoView({ block: 'nearest' });
    }

    function run(i: number): void {
        const r = rows[i];
        if (!r || r.disabled) return;
        close();
        if (r.commandId) pushRecent(r.commandId);
        // Let focus return to the editor before the command runs (some commands focus things).
        window.setTimeout(r.run, 0);
    }

    function close(): void {
        el.hidden = true;
        input.blur();
    }
    function open(initial = ''): void {
        input.value = initial;
        sel = 0;
        el.hidden = false;
        render();
        input.focus();
        input.setSelectionRange(initial.length, initial.length);
    }

    input.addEventListener('input', () => {
        sel = 0;
        render();
    });
    input.addEventListener('keydown', (e) => {
        if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
            e.preventDefault();
            if (rows.length) setSel((sel + (e.key === 'ArrowDown' ? 1 : -1) + rows.length) % rows.length);
        } else if (e.key === 'Enter') {
            e.preventDefault();
            run(sel);
        } else if (e.key === 'Escape') {
            e.preventDefault();
            e.stopPropagation();
            close();
        }
    });
    el.addEventListener('pointerdown', (e) => {
        if (e.target === el) close();
    });
    onRegistryChange(() => {
        if (!el.hidden) render();
    });
    return { open, close, isOpen: () => !el.hidden };
}

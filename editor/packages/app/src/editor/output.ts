import type { Panel } from './ctx.ts';
import { fill, h, icon } from './dom.ts';
import { emptyState } from './ui.ts';

export type LogKind = 'info' | 'ok' | 'warn' | 'error';

export interface OutputLog extends Panel {
    /** Append a line (toasts and status messages end up here). */
    log(msg: string, kind?: LogKind): void;
    /** Number of warnings and errors since the last clear. */
    counts(): { warn: number; error: number };
}

const ICON: Record<LogKind, string> = { info: 'info', ok: 'check-circle', warn: 'warn', error: 'alert-circle' };
const MAX = 400;

/** Output Log: a timestamped history of everything the editor told the user, filterable by severity. */
export function createOutputLog(onCounts: (warn: number, error: number) => void): OutputLog {
    const entries: { t: Date; kind: LogKind; msg: string }[] = [];
    let min: 'all' | 'warn' | 'error' = 'all';
    const list = h('div', { class: 'out-list', role: 'log', 'aria-live': 'off' });
    const chips = (['all', 'warn', 'error'] as const).map((f) => {
        const b = h(
            'button',
            { class: `chip${f === min ? ' active' : ''}`, type: 'button', 'data-f': f },
            f === 'all' ? 'All' : f === 'warn' ? 'Warnings' : 'Errors',
        );
        b.addEventListener('click', () => {
            min = f;
            for (const c of chips) c.classList.toggle('active', c.dataset.f === f);
            render();
        });
        return b;
    });
    const clear = h(
        'button',
        {
            class: 'ui-btn sm ghost',
            type: 'button',
            onclick: () => {
                entries.length = 0;
                render();
                onCounts(0, 0);
            },
        },
        icon('clear', 13),
        'Clear',
    );
    const root = h(
        'div',
        { class: 'panel output' },
        h(
            'div',
            { class: 'panel-tools' },
            h('div', { class: 'chips' }, ...chips),
            h('span', { class: 'spacer' }),
            clear,
        ),
        list,
    );

    const counts = (): { warn: number; error: number } => ({
        warn: entries.filter((e) => e.kind === 'warn').length,
        error: entries.filter((e) => e.kind === 'error').length,
    });

    function render(): void {
        const shown = entries.filter(
            (e) => min === 'all' || (min === 'warn' ? e.kind !== 'info' && e.kind !== 'ok' : e.kind === 'error'),
        );
        if (!shown.length) {
            fill(
                list,
                emptyState(
                    'terminal',
                    'Nothing logged',
                    'Messages about loading, saving and placing objects appear here.',
                ),
            );
            return;
        }
        fill(
            list,
            ...shown.map((e) =>
                h(
                    'div',
                    { class: `out-row ${e.kind}` },
                    h('span', { class: 'out-time' }, e.t.toLocaleTimeString([], { hour12: false })),
                    icon(ICON[e.kind], 13),
                    h('span', { class: 'out-msg' }, e.msg),
                ),
            ),
        );
        list.scrollTop = list.scrollHeight;
    }
    render();

    return {
        el: root,
        log(msg, kind = 'info') {
            entries.push({ t: new Date(), kind, msg });
            if (entries.length > MAX) entries.splice(0, entries.length - MAX);
            render();
            const c = counts();
            onCounts(c.warn, c.error);
        },
        counts,
    };
}

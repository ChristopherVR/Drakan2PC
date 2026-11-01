import {
    allPlainMessages,
    linkWithMessage,
    logicOf,
    messagesFor,
    nodeName,
    outgoingMessageField,
    outgoingMessages,
    plainMessage,
    type LevelObject,
    type LogicEnv,
} from '@drakan/editor-core';
import type { Ctx } from './ctx.ts';
import { contextMenu, type MenuItem } from './ui.ts';

/**
 * "What should it do?": the short plain-language list shown when two objects get linked (dragging a link in the Logic
 * graph, the link handle in the viewport, "Link to..." in a menu). Picking a row links the objects AND sets the message
 * the source sends, as one undo step.
 */

const cap = (s: string): string => s.charAt(0).toUpperCase() + s.slice(1);

/** Where the pointer last was, for menus opened from a click that has no position of its own (pick mode). */
export const lastPointer = { x: 200, y: 200 };
if (typeof window !== 'undefined')
    window.addEventListener(
        'pointerdown',
        (e) => {
            lastPointer.x = e.clientX;
            lastPointer.y = e.clientY;
        },
        true,
    );

/** True when linking source to target is a link that carries a message the modder can choose. */
export function canChooseMessage(env: LogicEnv | null, source: LevelObject | undefined): boolean {
    return !!env && !!source && outgoingMessageField(source, env) !== null;
}

function describeCurrent(env: LogicEnv, source: LevelObject, target: LevelObject): number | null {
    const spec = logicOf(source, env);
    const m = spec ? outgoingMessages(source, env, spec)[0] : undefined;
    return m ? m.value : null;
}

/**
 * Open the message list at (x, y) for `sources` -> `to` and apply the pick. Returns false when the sources send no
 * message (nothing to choose), so the caller links them directly.
 */
export function askLinkMessage(
    ctx: Ctx,
    sources: readonly number[],
    to: number,
    at: { x: number; y: number },
    done?: (applied: boolean) => void,
): boolean {
    const env = ctx.schemas();
    const doc = ctx.doc();
    const target = doc.get(to);
    const srcObjs = sources.map((i) => doc.get(i)).filter((o): o is LevelObject => !!o && o.id !== to);
    if (!env || !target || !srcObjs.length || !canChooseMessage(env, srcObjs[0])) return false;
    const tspec = logicOf(target, env);
    const sspec = logicOf(srcObjs[0]!, env);
    const tName = nodeName(target, tspec);
    const sName = srcObjs.length === 1 ? nodeName(srcObjs[0]!, sspec) : `${srcObjs.length} objects`;
    const current = srcObjs.length === 1 ? describeCurrent(env, srcObjs[0]!, target) : null;
    const apply = (value: number): void => {
        ctx.edit(
            `${sName} ${plainMessage(value, tspec?.role)} ${tName}`,
            srcObjs.map((o) => o.id),
            (o) => void linkWithMessage(o, env, to, value),
        );
        done?.(true);
    };
    const rowFor = (m: { value: number; verb: string; label: string }): MenuItem => ({
        label: `${cap(m.verb)}`,
        ...(current === m.value ? { icon: 'check' } : {}),
        ...(m.label === cap(m.verb) ? {} : { hint: m.label }),
        run: () => apply(m.value),
    });
    const items: MenuItem[] = [
        { heading: `When ${sName} fires, ${tName}...` },
        ...messagesFor(tspec?.role).map(rowFor),
        { sep: true },
        {
            label: 'Something else...',
            run: () =>
                contextMenu(at.x, at.y, [
                    { heading: `Other things ${sName} can tell ${tName}` },
                    ...allPlainMessages().map(rowFor),
                ]),
        },
        {
            label: 'Keep its current message',
            ...(current === null ? {} : { hint: cap(plainMessage(current, tspec?.role)) }),
            run: () => {
                ctx.edit(
                    `Link ${sName} to ${tName}`,
                    srcObjs.map((o) => o.id),
                    (o) => void linkWithMessage(o, env, to, null),
                );
                done?.(true);
            },
        },
    ];
    if (srcObjs.length === 1 && (srcObjs[0]!.links?.length ?? 0) > 0 && !srcObjs[0]!.links!.includes(to))
        items.splice(1, 0, { heading: `It tells everything it is linked to the same thing` });
    items.push({ sep: true }, { label: 'Cancel', run: () => done?.(false) });
    contextMenu(at.x, at.y, items);
    return true;
}

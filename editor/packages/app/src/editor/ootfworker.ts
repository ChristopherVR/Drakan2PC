// Worker for the Order of the Flame importer (see ootfconvert.ts). Messages in: one `convert` request, and `disc` answers to
// the file reads it asked for. Messages out: `progress`, `read` (asks the page for a disc file), `done` or `error`.

import { runConversion, type ConvertRequest, type ConvertResult } from './ootfconvert.ts';

export type ToWorker =
    { type: 'convert'; req: ConvertRequest } | { type: 'disc'; id: number; bytes: Uint8Array | null };
export type FromWorker =
    | { type: 'progress'; message: string }
    | { type: 'read'; id: number; path: string }
    | { type: 'done'; result: ConvertResult }
    | { type: 'error'; message: string };

const scope = self as unknown as {
    postMessage(m: FromWorker, transfer?: Transferable[]): void;
    onmessage: ((ev: MessageEvent<ToWorker>) => void) | null;
};
const waiting = new Map<number, (b: Uint8Array | null) => void>();
let nextId = 1;

const readDisc = (path: string): Promise<Uint8Array | null> =>
    new Promise((resolve) => {
        const id = nextId++;
        waiting.set(id, resolve);
        scope.postMessage({ type: 'read', id, path });
    });

scope.onmessage = (ev) => {
    const m = ev.data;
    if (m.type === 'disc') {
        waiting.get(m.id)?.(m.bytes);
        waiting.delete(m.id);
        return;
    }
    runConversion(m.req, readDisc, (message) => scope.postMessage({ type: 'progress', message }))
        .then((result) => {
            const buffers = Object.values(result.pack.files).map((b) => b.buffer as ArrayBuffer);
            scope.postMessage({ type: 'done', result }, [...new Set(buffers)]);
        })
        .catch((e: unknown) =>
            scope.postMessage({ type: 'error', message: e instanceof Error ? e.message : String(e) }),
        );
};

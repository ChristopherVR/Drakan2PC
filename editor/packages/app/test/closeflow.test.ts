import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { runCloseFlow, type CloseChoice, type CloseDeps } from '../src/closeflow.ts';

function deps(over: Partial<CloseDeps> = {}) {
    const log: string[] = [];
    const d: CloseDeps = {
        beat: (ask) => log.push(ask ? 'ask' : 'busy'),
        ack: () => log.push('ack'),
        cancel: () => log.push('cancel'),
        askFailure: async () => 'cancel',
        beatMs: 100,
        ...over,
    };
    return { d, log };
}

describe('closing the desktop window', () => {
    beforeEach(() => vi.useFakeTimers());
    afterEach(() => vi.useRealTimers());

    it('acks after a clean flush', async () => {
        const { d, log } = deps();
        expect(await runCloseFlow(async () => undefined, d)).toBe('closed');
        expect(log.at(-1)).toBe('ack');
    });

    it('keeps the shell informed during a slow flush and acks only at the end', async () => {
        const { d, log } = deps();
        const p = runCloseFlow(() => new Promise<void>((r) => setTimeout(r, 1000)), d);
        await vi.advanceTimersByTimeAsync(950);
        expect(log.filter((x) => x === 'busy').length).toBeGreaterThanOrEqual(9);
        expect(log).not.toContain('ack');
        await vi.advanceTimersByTimeAsync(100);
        expect(await p).toBe('closed');
        expect(log.at(-1)).toBe('ack');
        const n = log.length;
        await vi.advanceTimersByTimeAsync(500);
        expect(log.length).toBe(n); // the heartbeat stopped
    });

    it('never acks a failed flush by itself and offers the choice', async () => {
        const asked: string[] = [];
        const { d, log } = deps({
            askFailure: async (m) => {
                asked.push(m);
                return 'cancel';
            },
        });
        const r = await runCloseFlow(() => Promise.reject(new Error('disk full')), d);
        expect(r).toBe('cancelled');
        expect(asked).toEqual(['disk full']);
        expect(log).toContain('cancel');
        expect(log).not.toContain('ack');
    });

    it('beats as "ask" while the user decides, however long that takes', async () => {
        let answer: (c: CloseChoice) => void = () => undefined;
        const { d, log } = deps({ askFailure: () => new Promise<CloseChoice>((r) => (answer = r)) });
        const p = runCloseFlow(() => Promise.reject(new Error('x')), d);
        await vi.advanceTimersByTimeAsync(5000);
        expect(log.filter((x) => x === 'ask').length).toBeGreaterThan(40);
        expect(log).not.toContain('ack');
        answer('close');
        expect(await p).toBe('closed');
        expect(log.at(-1)).toBe('ack');
    });

    it('retry runs the flush again and acks when it works', async () => {
        let n = 0;
        const { d, log } = deps({ askFailure: async () => 'retry' });
        const r = await runCloseFlow(async () => {
            if (++n === 1) throw new Error('first');
        }, d);
        expect(r).toBe('closed');
        expect(n).toBe(2);
        expect(log.at(-1)).toBe('ack');
    });

    it('treats a failing dialog as keep-open', async () => {
        const { d, log } = deps({ askFailure: () => Promise.reject(new Error('no ui')) });
        expect(await runCloseFlow(() => Promise.reject(new Error('x')), d)).toBe('cancelled');
        expect(log).not.toContain('ack');
    });
});

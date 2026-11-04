import { expect, it, vi } from 'vitest';
import { createItemArt } from '../src/save/itemart.ts';
import { classThumbnail } from '../src/scene/index.ts';

vi.mock('../src/scene/index.ts', () => ({
    TextureCache: class {
        addShared() {
            return {};
        }
        dispose() {}
    },
    classThumbnail: vi.fn(() => 'data:image/png;base64,test'),
}));

it('loads catalog metadata without decoding picker models, then requests visible art once', async () => {
    const call = vi.fn(async (method: string) => {
        if (method === 'loadItemCatalog') return { classes: [{ classId: 0x3752, name: 'Health Elixir' }] };
        return [];
    });
    const art = createItemArt(call as never);
    art.info('0x33752', false);
    await vi.waitFor(() => expect(art.classes()).toHaveLength(1));
    expect(art.info('0x33752', false)?.name).toBe('Health Elixir');
    expect(call.mock.calls.map(([method]) => method)).toEqual(['loadItemCatalog']);
    art.info('0x33752', true);
    art.info('0x33752', true);
    await vi.waitFor(() => expect(call).toHaveBeenCalledTimes(2));
    expect(call.mock.calls[1]?.[0]).toBe('loadItemModels');
});

it('uses fresh image and geometry cache keys after the disc changes', async () => {
    const call = vi.fn(async (method: string) =>
        method === 'loadItemCatalog'
            ? { classes: [{ classId: 0x3752, name: 'Health Elixir' }] }
            : [{ classId: 0x3752, model: { triTexture: [0] }, textures: [] }],
    );
    const art = createItemArt(call as never);
    const load = async () => {
        art.info('0x33752', false);
        await vi.waitFor(() => expect(art.classes()).toHaveLength(1));
        art.info('0x33752', true);
        await vi.waitFor(() => expect(art.info('0x33752', false)?.thumb).toBeTruthy());
    };
    vi.mocked(classThumbnail).mockClear();
    await load();
    art.reset();
    await load();
    const calls = vi.mocked(classThumbnail).mock.calls;
    expect(calls).toHaveLength(2);
    expect(calls[0]?.[0]).not.toBe(calls[1]?.[0]);
    expect(calls[0]?.[4]).not.toBe(calls[1]?.[4]);
});

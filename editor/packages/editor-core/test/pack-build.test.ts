// Synthetic only: no game bytes. The manifest's `build` key (save compatibility, modding.md "Saves and pack builds").
import { strFromU8, strToU8, unzipSync } from 'fflate';
import { describe, expect, it } from 'vitest';
import { packBuildId } from '@drakan/formats';
import {
    buildManifestToml,
    buildModFiles,
    buildModZip,
    stampPackBuild,
    validateModMeta,
    zipModFiles,
} from '../src/index.ts';

const meta = { id: 'test.pack', version: '1.0.0', target: { profile: 'pal' } };
const entry = { file: 'LEVEL1NA.LVL', entry: { type: 0x20, id: 3, sub: 4 }, data: Uint8Array.of(1, 2, 3) };

describe('pack build in the manifest', () => {
    it('writes a pinned build and validates it', () => {
        expect(buildManifestToml({ ...meta, build: 'release-2' })).toContain('build = "release-2"');
        expect(buildManifestToml(meta)).not.toContain('build');
        expect(() => validateModMeta({ ...meta, build: 'has space' })).toThrow(/build/);
        expect(() => validateModMeta({ ...meta, build: '' })).toThrow(/build/);
    });

    it('stamps the content hash of the zipped files as a top-level key', () => {
        const files = buildModFiles(meta, [entry]);
        const toml = strFromU8(unzipSync(buildModZip(meta, [entry]))['mod.toml']!);
        expect(toml).toContain(`build = "${packBuildId(files)}"`);
        // Above the [target] table, so the runtime reads it as the top-level `build`.
        expect(toml.indexOf('build =')).toBeLessThan(toml.indexOf('[target]'));
        expect(toml.indexOf('build =')).toBeGreaterThan(toml.indexOf('id ='));
    });

    it('keeps a pinned build', () => {
        const pinned = buildModFiles({ ...meta, build: 'keep-me' }, [entry]);
        expect(stampPackBuild(pinned)).toBe(pinned);
        expect(strFromU8(unzipSync(zipModFiles(pinned))['mod.toml']!)).toContain('build = "keep-me"');
    });

    it('follows content changes, not the manifest or pictures', () => {
        const a = buildModFiles(meta, [entry]);
        const b = buildModFiles({ ...meta, description: 'another text' }, [entry]);
        b['previews/campaign-x.png'] = Uint8Array.of(9);
        const c = buildModFiles(meta, [{ ...entry, data: Uint8Array.of(1, 2, 4) }]);
        const build = (f: Record<string, Uint8Array>): string =>
            /build = "([0-9a-f]{16})"/.exec(strFromU8(stampPackBuild(f)['mod.toml']!))![1]!;
        expect(build(b)).toBe(build(a));
        expect(build(c)).not.toBe(build(a));
    });

    it('stamps mod.json and leaves packs without a manifest alone', () => {
        const files = {
            'mod.json': strToU8('{"id": "j", "target": {"profile": "pal"}}'),
            'files/A.LVL': Uint8Array.of(7),
        };
        const doc = JSON.parse(strFromU8(stampPackBuild(files)['mod.json']!)) as { build: string; id: string };
        expect(doc.id).toBe('j');
        expect(doc.build).toBe(packBuildId(files));
        const bare = { 'Content/a.txt': Uint8Array.of(1) };
        expect(stampPackBuild(bare)).toBe(bare);
    });
});

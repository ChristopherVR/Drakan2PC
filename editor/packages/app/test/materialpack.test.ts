import { describe, expect, it } from 'vitest';
import { baseName, parseMaterial, standardSettings } from '../src/materialpack.ts';

const base = {
    name: 'M_Test',
    domain: 'Surface',
    blendMode: 'Masked',
    twoSided: true,
    opacityMaskClip: 0.4,
    uvTiling: [2, 3],
    textures: { baseColor: 'Textures/T_Test_BC.png', normal: 'Textures/T_Test_N.png', orm: 'Textures/T_Test_ORM.png' },
    textureSettings: { normal: { convention: 'DirectX' } },
    emissive: { source: 'baseColor', intensity: 2 },
};

describe('material pack', () => {
    it('parses a material description', () => {
        const m = parseMaterial(JSON.stringify(base));
        expect(m.name).toBe('M_Test');
        expect(m.uvTiling).toEqual([2, 3]);
        expect(m.emissiveIntensity).toBe(2);
        expect(baseName(m.normal!)).toBe('T_Test_N.png');
    });
    it('maps masked two sided DirectX to three.js settings', () => {
        const s = standardSettings(parseMaterial(JSON.stringify(base)));
        expect(s.alphaTest).toBeCloseTo(0.4);
        expect(s.side).toBe('double');
        expect(s.normalScale).toEqual([1, -1]);
        expect(s.transparent).toBe(false);
        expect(s.depthWrite).toBe(true);
    });
    it('treats additive as transparent without depth writes', () => {
        const s = standardSettings(parseMaterial(JSON.stringify({ ...base, blendMode: 'Additive', twoSided: false })));
        expect(s.transparent).toBe(true);
        expect(s.blending).toBe('additive');
        expect(s.depthWrite).toBe(false);
        expect(s.side).toBe('front');
    });
    it('rejects files that are not materials', () => {
        expect(() => parseMaterial('{"schema":1}')).toThrow();
    });
});

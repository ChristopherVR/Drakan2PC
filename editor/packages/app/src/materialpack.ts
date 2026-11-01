/**
 * Material pack (tools: analysis/scripts/pbr_pack.py, docs/reference/hd-textures.md "PBR materials"): the pure part of
 * the Files tab Materials view. Parses `M_<Name>.json` and turns it into the settings a three.js MeshStandardMaterial
 * needs; no DOM and no three.js here so it is unit tested.
 */

export type BlendMode = 'Opaque' | 'Masked' | 'Translucent' | 'Additive';

export interface MaterialDesc {
    name: string;
    domain: 'Surface' | 'UserInterface';
    blendMode: BlendMode;
    twoSided: boolean;
    opacityMaskClip: number;
    uvTiling: [number, number];
    size: [number, number];
    hd: boolean;
    baseColor: string;
    normal: string | null;
    orm: string | null;
    emissiveIntensity: number;
    unlit: boolean;
    normalConvention: 'DirectX' | 'OpenGL';
    roughnessMean: number;
    sources: string[];
}

function str(v: unknown): string | null {
    return typeof v === 'string' && v.length ? v : null;
}

/** Final path component of a pack-relative texture path ("Textures/T_A_BC.png" -> "T_A_BC.png"). */
export function baseName(p: string): string {
    return p.split(/[\/]/).pop() ?? p;
}

/** Parse one material JSON; throws on a missing name or BaseColor. */
export function parseMaterial(json: string): MaterialDesc {
    const j = JSON.parse(json) as Record<string, unknown>;
    const name = str(j.name);
    const tex = (j.textures ?? {}) as Record<string, unknown>;
    const baseColor = str(tex.baseColor);
    if (!name || !baseColor) throw new Error('not a material description (name / textures.baseColor missing)');
    const settings = (j.textureSettings ?? {}) as Record<string, Record<string, unknown> | undefined>;
    const tile = Array.isArray(j.uvTiling) ? j.uvTiling : [1, 1];
    const size = Array.isArray(j.size) ? j.size : [0, 0];
    const emissive = j.emissive as { intensity?: unknown } | null | undefined;
    const scalars = (j.scalars ?? {}) as Record<string, unknown>;
    const blend = j.blendMode;
    return {
        name,
        domain: j.domain === 'UserInterface' ? 'UserInterface' : 'Surface',
        blendMode: blend === 'Masked' || blend === 'Translucent' || blend === 'Additive' ? blend : 'Opaque',
        twoSided: j.twoSided === true,
        opacityMaskClip: typeof j.opacityMaskClip === 'number' ? j.opacityMaskClip : 0.5,
        uvTiling: [Number(tile[0]) || 1, Number(tile[1]) || 1],
        size: [Number(size[0]) || 0, Number(size[1]) || 0],
        hd: j.hd === true,
        baseColor,
        normal: str(tex.normal),
        orm: str(tex.orm),
        emissiveIntensity: emissive && typeof emissive.intensity === 'number' ? emissive.intensity : 0,
        unlit: j.shadingModel === 'Unlit',
        normalConvention: settings.normal?.convention === 'OpenGL' ? 'OpenGL' : 'DirectX',
        roughnessMean: typeof scalars.roughnessMean === 'number' ? scalars.roughnessMean : 0.8,
        sources: Array.isArray(j.sources) ? j.sources.filter((s): s is string => typeof s === 'string') : [],
    };
}

/** three.js material parameters implied by the description (no textures). */
export interface StandardSettings {
    transparent: boolean;
    alphaTest: number;
    /** 'double' | 'front' (three.js Side). */
    side: 'double' | 'front';
    blending: 'normal' | 'additive';
    depthWrite: boolean;
    /** three.js normalScale: DirectX green-down maps need y = -1, OpenGL maps y = 1. */
    normalScale: [number, number];
    emissiveIntensity: number;
}

export function standardSettings(m: MaterialDesc): StandardSettings {
    return {
        transparent: m.blendMode === 'Translucent' || m.blendMode === 'Additive',
        alphaTest: m.blendMode === 'Masked' ? m.opacityMaskClip : 0,
        side: m.twoSided ? 'double' : 'front',
        blending: m.blendMode === 'Additive' ? 'additive' : 'normal',
        depthWrite: m.blendMode === 'Opaque' || m.blendMode === 'Masked',
        normalScale: m.normalConvention === 'DirectX' ? [1, -1] : [1, 1],
        emissiveIntensity: m.emissiveIntensity,
    };
}

// Message contract between the UI thread and the parsing worker.

import type { DiscProfile, Lvl, OdbIndex } from '@drakan/formats';
import type { ModelData, TerrainData } from './scene/decode.ts';
import type { ItemCatalog } from './itemdata.ts';

export interface LevelInfo {
    name: string;
    /** Mangled directory on disc, e.g. `LEVELS\LEVEL1NA`. */
    dirPath: string;
    /** False when LEVELLST names a directory this disc does not have. */
    present: boolean;
}

export interface OpenResult {
    volumeId: string;
    languages: string[];
    language: string;
    levels: LevelInfo[];
    /** From SYSTEM.CNF; null when the disc has none. */
    profile: DiscProfile | null;
}

export interface FileInfo {
    name: string;
    path: string;
    size: number;
    isDir: boolean;
}

export interface EntryInfo {
    index: number;
    type: number;
    id: number;
    sub: number;
    offset: number;
    size: number;
    zlib: boolean;
}

export type FileOpenResult =
    | { kind: 'srsc'; entries: EntryInfo[]; warnings: string[]; size: number }
    | { kind: 'text'; text: string; deps?: { index: number; path: string; resolved: string }[] }
    | { kind: 'binary'; size: number };

export interface EntryBytes {
    bytes: Uint8Array;
    /** Length of the full (possibly inflated) payload. */
    total: number;
    inflated: boolean;
}

export interface TextureResult {
    index: number;
    id: number;
    sub: number;
    width?: number;
    height?: number;
    bpp?: number;
    /** RGBA8 in stored row order (row 0 = bottom), ready for GL uploads. */
    rgba?: Uint8Array;
    error?: string;
}

export interface RoundTripResult {
    file: string;
    /** Entries rewritten with their own bytes (raw, as stored). */
    entries: { type: number; id: number; sub: number; data: Uint8Array }[];
    /** Writer self-test: unchanged rewrite equals the source byte for byte. */
    roundTrips: boolean;
}

export interface LevelLoadResult {
    /** Language-relative path of the LVL, e.g. `ENGLISH\LEVELS\LEVEL1NA\LEVEL1NA.LVL`. */
    path: string;
    /** File name used in `overrides/<FILE>/`, e.g. `LEVEL1NA.LVL`. */
    file: string;
    /** SRSC entry holding the object list (0x0020) with its stored size. */
    objects: { type: number; id: number; sub: number; size: number };
    lvl: Lvl;
    /** Class tables of the ODBs the level depends on, keyed by stem. */
    odbs: OdbIndex;
    /** Dependencies whose ODB could not be found on the disc. */
    missingOdbs: string[];
}

export interface Api {
    open(file: File): OpenResult;
    setLanguage(language: string): LevelInfo[];
    listDir(path: string): FileInfo[];
    openFile(path: string): FileOpenResult;
    readEntry(path: string, index: number, offset: number, length: number, inflate: boolean): EntryBytes;
    exportRoundTrip(path: string, max: number): RoundTripResult;
    loadLevel(dirPath: string): LevelLoadResult;
    decodeTextures(path: string, indices: number[] | null): TextureResult[];
    /** Terrain layers and their textures for the level in `dirPath` (scene/decode.ts). */
    loadTerrain(dirPath: string): TerrainData;
    /** Models (LOD 0) and textures for object classes `(classId, dbIndex)` of the level in `dirPath`. */
    loadModels(dirPath: string, classes: { classId: number; dbIndex: number }[]): ModelData[];
    /** Class names of the shared Resources ODB, where every inventory item is defined (no level needed). */
    loadItemCatalog(): ItemCatalog;
    /** Models and textures of Resources classes by class id. */
    loadItemModels(classIds: number[]): ModelData[];
}

export type Method = keyof Api;
export type Request = { id: number; method: Method; args: unknown[] };
export type Response = { id: number; ok: true; result: unknown } | { id: number; ok: false; error: string };

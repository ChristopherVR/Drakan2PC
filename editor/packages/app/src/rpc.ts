// Message contract between the UI thread and the parsing worker.

import type { DiscProfile, Lvl, OdbIndex } from '@drakan/formats';
import type { ModelData, TerrainData } from './scene/decode.ts';
import type { ItemCatalog } from './itemdata.ts';
import type { StoryCatalog } from './storydata.ts';
import type { QuestUsageLevel, StoryEditData } from './story/storyedit.ts';
import type { RefEntry, RefKind, RefSound, RefTexture } from './refdata.ts';

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
    /** Per ODB stem: its `.DB` dependency list (index -> container stem), which class reference fields are relative to. */
    odbDeps: Record<string, Record<number, string>>;
    /** Dependencies whose ODB could not be found on the disc. */
    missingOdbs: string[];
}

/** A disc the worker reads by range-fetching a URL instead of a `File` (desktop app: the shell's asset protocol). */
export interface RemoteBlob {
    name: string;
    size: number;
    url: string;
    /** Where it lives on disk, for display only. */
    path?: string;
}

export interface Api {
    open(source: File | RemoteBlob): OpenResult;
    setLanguage(language: string): LevelInfo[];
    listDir(path: string): FileInfo[];
    openFile(path: string): FileOpenResult;
    readEntry(path: string, index: number, offset: number, length: number, inflate: boolean): EntryBytes;
    exportRoundTrip(path: string, max: number): RoundTripResult;
    loadLevel(dirPath: string): LevelLoadResult;
    /** The raw `.LVL` container of the level in `dirPath` (template for converted Order of the Flame levels). */
    readLevelFile(dirPath: string): Uint8Array;
    /** Where the exits of every level on the disc lead (loads every level's object list; seconds). */
    exitIndex(): import('./logicdata.ts').ExitIndexData;
    decodeTextures(path: string, indices: number[] | null): TextureResult[];
    /** Terrain layers and their textures for the level in `dirPath` (scene/decode.ts). */
    loadTerrain(dirPath: string): TerrainData;
    /** Models (LOD 0) and textures for object classes `(classId, dbIndex)` of the level in `dirPath`. */
    loadModels(dirPath: string, classes: { classId: number; dbIndex: number }[]): ModelData[];
    /** Class names of the shared Resources ODB, where every inventory item is defined (no level needed). */
    loadItemCatalog(): ItemCatalog;
    /** Models and textures of Resources classes by class id. */
    loadItemModels(classIds: number[]): ModelData[];
    loadStoryCatalog(): StoryCatalog;
    /** Every XDB string table and the System.ODB quest classes, for the Strings and Quests editors. */
    loadStoryEdit(): StoryEditData;
    /** Where each quest class is placed and triggered, per level (loads every level's object list; seconds). */
    scanQuestUsage(): QuestUsageLevel[];
    /** A file of the language tree for the dialogue / cinematics editors (path like `LEVELS\LEVEL1ON\LEVEL1ON.SSD`); null when absent. */
    readStoryFile(path: string): Uint8Array | null;
    /** Level folders that have an `.SSD` or `.SDB` of their own. */
    listStoryStems(): string[];
    /** Placed objects whose string parameters hold string ids (loads every level's object list; seconds). */
    scanStringUsage(): import('./story/storyedit.ts').StringUsageLevel[];
    /** Which container stems have a file for a reference kind (model .MOD, sound .SDB, texture .TXD, animation .ADB, sequence .SSD). */
    refContainers(kind: RefKind, stems: string[]): boolean[];
    /** Names and facts of every entry of a container; null when the disc has no such container. */
    listRefs(stem: string, kind: RefKind): RefEntry[] | null;
    /** A model of a MOD container with its textures (picker thumbnails). */
    loadRefModel(stem: string, id: number): ModelData | null;
    /** A texture of a TXD container, top row first. */
    loadRefTexture(stem: string, id: number): RefTexture | null;
    /** A sound of an SDB container decoded to PCM. */
    loadRefSound(stem: string, id: number): RefSound | null;
    /** A skinned model with its rig and the clips it lists (animation previews). */
    loadRefRig(stem: string, id: number): import('./animdata.ts').RefRig | null;
    /** One animation clip of an ADB container. */
    loadRefClip(stem: string, id: number): import('@drakan/formats').Ps2Anim | null;
    /** The model (container stem, id) a class uses; null when it has none. */
    classModelRef(dbStem: string, classId: number): { stem: string; id: number } | null;
    /** A skinned model of a MOD container with that many joints (stand-in for a class without a model). */
    rigForTracks(stem: string, tracks: number): { stem: string; id: number } | null;
}

export type Method = keyof Api;
export type Request = { id: number; method: Method; args: unknown[] };
export type Response = { id: number; ok: true; result: unknown } | { id: number; ok: false; error: string };

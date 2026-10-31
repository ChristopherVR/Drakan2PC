// Message contract between the UI thread and the parsing worker.

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

export interface Api {
    open(file: File): OpenResult;
    setLanguage(language: string): LevelInfo[];
    listDir(path: string): FileInfo[];
    openFile(path: string): FileOpenResult;
    readEntry(path: string, index: number, offset: number, length: number, inflate: boolean): EntryBytes;
    decodeTextures(path: string, indices: number[] | null): TextureResult[];
}

export type Method = keyof Api;
export type Request = { id: number; method: Method; args: unknown[] };
export type Response = { id: number; ok: true; result: unknown } | { id: number; ok: false; error: string };

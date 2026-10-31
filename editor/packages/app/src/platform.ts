// The one place that knows whether the editor runs in a plain browser or inside the Tauri desktop shell
// (`editor/packages/desktop`). Everything else asks for a capability here; in the browser the same call sites keep using
// File System Access / downloads, and `isDesktop` is false so none of the desktop paths below ever run.
//
// The Tauri API is imported lazily, so the browser build never loads it.

import type { RemoteBlob } from './rpc.ts';

export const isDesktop: boolean = typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window;

// ---- shapes returned by the shell's commands (see src-tauri/src/commands.rs) --------------------------------------------

export interface DesktopSettings {
    recentIsos: string[];
    gameDir: string | null;
    modsDir: string | null;
    saveDir: string | null;
    isoDir: string | null;
}
export interface IsoInfo {
    path: string;
    name: string;
    size: number;
}
export interface FolderInfo {
    path: string;
    name: string;
    hasGame: boolean;
}
export interface SaveFileInfo {
    path: string;
    name: string;
    folder: string;
    size: number;
    modified: number;
}
export interface SaveFolderInfo {
    root: string;
    files: SaveFileInfo[];
}
export interface WriteResult {
    backup: string;
    size: number;
    modified: number;
}
export interface LaunchInfo {
    pid: number;
    exe: string;
    mods: string;
}

// ---- plumbing -----------------------------------------------------------------------------------------------------------

type Core = typeof import('@tauri-apps/api/core');
let coreP: Promise<Core> | null = null;
const core = (): Promise<Core> => (coreP ??= import('@tauri-apps/api/core'));

async function invoke<T>(cmd: string, args?: Record<string, unknown>): Promise<T> {
    if (!isDesktop) throw new Error('Desktop commands are only available in the desktop app.');
    return (await core()).invoke<T>(cmd, args);
}

const b64 = (bytes: Uint8Array): string => {
    let s = '';
    for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
    return btoa(s);
};
const b64Text = (text: string): string => b64(new TextEncoder().encode(text));

/** Raw-body call: the bytes travel as the request body, text parameters as base64 headers. */
async function invokeRaw<T>(cmd: string, body: Uint8Array, headers: Record<string, string>): Promise<T> {
    if (!isDesktop) throw new Error('Desktop commands are only available in the desktop app.');
    return (await core()).invoke<T>(cmd, body, { headers });
}

// ---- remembered choices -------------------------------------------------------------------------------------------------

export const getSettings = (): Promise<DesktopSettings> => invoke('settings_get');
export const forgetRecent = (): Promise<DesktopSettings> => invoke('settings_forget');

// ---- disc image ---------------------------------------------------------------------------------------------------------

/** What the worker needs to read the disc: a browser `File`, or a URL it can range-fetch (desktop). */
export type IsoSource = File | RemoteBlob;

async function toRemote(info: IsoInfo): Promise<RemoteBlob> {
    const { convertFileSrc } = await core();
    return { name: info.name, size: info.size, url: convertFileSrc(info.path), path: info.path };
}

/** Native open dialog; null when cancelled. */
export async function pickIso(): Promise<RemoteBlob | null> {
    const info = await invoke<IsoInfo | null>('iso_pick');
    return info ? toRemote(info) : null;
}

/** Re-open a disc from the recent list. */
export async function openRecentIso(path: string): Promise<RemoteBlob> {
    return toRemote(await invoke<IsoInfo>('iso_open', { path }));
}

// ---- folders ------------------------------------------------------------------------------------------------------------

export const pickFolder = (which: 'game' | 'mods'): Promise<FolderInfo | null> => invoke('folder_pick', { which });

// ---- saves --------------------------------------------------------------------------------------------------------------

export const pickSaveFile = (): Promise<SaveFileInfo | null> => invoke('save_pick_file');
export const pickSaveFolder = (): Promise<SaveFolderInfo | null> => invoke('save_pick_folder');
export const statSave = (path: string): Promise<SaveFileInfo> => invoke('save_stat', { path });

export async function readSave(path: string): Promise<Uint8Array> {
    return new Uint8Array(await invoke<ArrayBuffer>('save_read', { path }));
}

/** Overwrite a save, keeping the old file as `NAME.bak` (`.bak.1`, ...) beside it. */
export const writeSave = (path: string, bytes: Uint8Array): Promise<WriteResult> =>
    invokeRaw('save_write', bytes, { 'x-path': b64Text(path) });

// ---- export / download --------------------------------------------------------------------------------------------------

/**
 * Hand a file to the user: a download in the browser, a native "save as" dialog in the desktop app. Resolves to
 * where it went (desktop) or null (browser, or cancelled).
 */
export async function downloadFile(
    name: string,
    data: BlobPart,
    type = 'application/octet-stream',
): Promise<string | null> {
    if (isDesktop) {
        const bytes =
            typeof data === 'string'
                ? new TextEncoder().encode(data)
                : new Uint8Array(await new Blob([data]).arrayBuffer());
        const ext = /\.([A-Za-z0-9]+)$/.exec(name)?.[1] ?? '';
        return invokeRaw<string | null>('file_save_as', bytes, { 'x-name': b64Text(name), 'x-ext': b64Text(ext) });
    }
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([data], { type }));
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 10_000);
    return null;
}

// ---- mods and the game --------------------------------------------------------------------------------------------------

export const readModsIni = (): Promise<string> => invoke('mods_read_ini');

/** Write `files` (pack-relative path -> bytes) as `<mods>/<id>/` and replace `mods.ini` with `ini`. Returns the folder. */
export function writeModPack(id: string, files: Record<string, Uint8Array>, ini: string): Promise<string> {
    return invoke('mods_write_pack', {
        id,
        ini,
        files: Object.entries(files).map(([path, data]) => ({ path, data: b64(data) })),
    });
}

export const launchGame = (env: Record<string, string>): Promise<LaunchInfo> => invoke('game_launch', { env });
export const gameRunning = (): Promise<boolean> => invoke('game_running');

// ---- native menu --------------------------------------------------------------------------------------------------------

/** Subscribe to native menu clicks (File/Edit/View/Help). Returns the unsubscribe function. */
export async function onMenuAction(fn: (id: string) => void): Promise<() => void> {
    if (!isDesktop) return () => {};
    const { listen } = await import('@tauri-apps/api/event');
    return listen<string>('menu-action', (e) => fn(e.payload));
}

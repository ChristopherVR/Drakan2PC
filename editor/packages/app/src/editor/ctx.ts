import type { AssetClass, LevelDocument } from '@drakan/editor-core';
import type { LevelView } from '../level.ts';
import type { LevelLoadResult } from '../rpc.ts';

/** What the editor panels need from the host (`levelui.ts`). */
export interface Ctx {
    view(): LevelView;
    doc(): LevelDocument;
    catalog(): AssetClass[];
    /** The disc level the document was loaded from (null for a world dump or a saved document). */
    src(): LevelLoadResult | null;
    toast(msg: string, kind?: 'info' | 'error' | 'ok'): void;
    /** Select objects and frame them. */
    locate(ids: number[]): void;
    /** Add a new instance of a catalog class at `pos` (game units; the viewport centre ground point when omitted). */
    placeClass(cls: AssetClass, pos?: [number, number, number]): void;
    /** Run an edit over objects as one undo step. */
    edit(label: string, ids: readonly number[], fn: (o: import('@drakan/editor-core').LevelObject) => void): void;
}

/** A dock panel: its element and a refresh hook for the events it cares about. */
export interface Panel {
    el: HTMLElement;
    /** Selection changed. */
    onSelection?(): void;
    /** The document changed (command run, undone or redone). */
    onChange?(): void;
    /** Hidden/locked sets or filters changed. */
    onVisibility?(): void;
    /** A new level was loaded. */
    onLoad?(): void;
}

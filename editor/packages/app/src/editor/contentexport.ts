// "Export > Engine-neutral package": the open level as a Drakan Content Format package (editor-core/src/content), saved
// through the platform's download / save-as path. Nothing is written into the repository or the game folder.

import {
    buildContentPackage,
    buildContentScene,
    levelSlug,
    validateContent,
    zipContentPackage,
    type AssetClass,
    type DcfScene,
    type LevelDocument,
    type SchemaProvider,
} from '@drakan/editor-core';
import { loadLevelStory, odbStemForDependency, type DiscProfile, type LevelStory } from '@drakan/formats';
import type { LevelUiHost } from '../levelui.ts';
import type { Api, LevelLoadResult } from '../rpc.ts';
import { downloadFile } from '../platform.ts';

export interface ContentExportHost {
    call: LevelUiHost['call'];
    doc(): LevelDocument;
    schemas(): SchemaProvider | null;
    catalog(): AssetClass[];
    src(): LevelLoadResult | null;
    profile(): DiscProfile | null;
    toast(msg: string, kind?: 'info' | 'error' | 'ok' | 'warn'): void;
    setStatus(msg: string, bad?: boolean): void;
}

export interface ContentExportResult {
    scene: DcfScene;
    files: Record<string, Uint8Array>;
    zip: Uint8Array;
    name: string;
}

/** `ENGLISH\LEVELS\LEVEL1NA\LEVEL1NA.LVL` -> language and level folder. */
function pathParts(path: string): { language: string; stem: string } {
    const parts = path.split(/[\\/]/).filter(Boolean);
    return { language: parts[0] ?? 'ENGLISH', stem: parts[parts.length - 2] ?? '' };
}

/** Build the package of the open level (the live, possibly edited, document). Throws a plain message when it cannot. */
export async function buildOpenLevelPackage(host: ContentExportHost): Promise<ContentExportResult> {
    const src = host.src();
    const schemas = host.schemas();
    const doc = host.doc();
    if (!src || !schemas || doc.size === 0) throw new Error('Open a level from your disc image first.');
    const { language, stem } = pathParts(src.path);
    host.setStatus('Reading story data...');
    // The story container (ODB with the conversation handlers and dialog lines, SSD sequences, SDB subtitles) is the
    // level's own dependency that is a folder with an .SSD or .SDB: `LEVEL1ON` for the level folder `LEVEL1NA`.
    let story: LevelStory | null = null;
    try {
        const stems = new Set(await host.call('listStoryStems'));
        const deps = src.lvl.header.dependencies.map((d) => odbStemForDependency(d.path.text));
        const storyStem = deps.find((d) => stems.has(d)) ?? (stems.has(stem) ? stem : null);
        if (storyStem) {
            story = await loadLevelStory(
                { read: (p) => host.call('readStoryFile', p).then((b) => b ?? null) },
                language,
                storyStem,
            );
        } else host.toast('This level has no story container; dialogue and sequences are left out.', 'warn');
    } catch (e) {
        host.toast(`Story data skipped: ${e instanceof Error ? e.message : String(e)}`, 'warn');
    }
    let quests: ReturnType<Api['loadStoryCatalog']> | null = null;
    try {
        quests = await host.call('loadStoryCatalog');
    } catch {
        quests = null;
    }
    host.setStatus('Building package...');
    const scene = buildContentScene({
        doc,
        env: schemas,
        catalog: host.catalog(),
        levelName: doc.level || stem,
        stem,
        story,
        quests: quests ? { classes: quests.classes, strings: quests.strings, slot: quests.slot } : null,
        language,
    });
    const errors = validateContent(scene, 5);
    if (errors.length)
        throw new Error(
            `Internal error, the package does not match its schema: ${errors[0]!.path} ${errors[0]!.message}`,
        );
    const profile = host.profile();
    const files = buildContentPackage([scene], {
        source: { ...(profile?.profile ? { profile: profile.profile } : {}), language },
    });
    return { scene, files, zip: zipContentPackage(files), name: `${levelSlug(scene.level.name)}.drakan-content.zip` };
}

export async function exportContentPackage(host: ContentExportHost): Promise<void> {
    try {
        const r = await buildOpenLevelPackage(host);
        const where = await downloadFile(r.name, r.zip as BlobPart, 'application/zip');
        const d = r.scene.diagnostics;
        host.setStatus(
            `Engine-neutral package: ${r.scene.instances.length} instances, ${r.scene.archetypes.length} archetypes, ` +
                `${r.scene.links.length} links, ${r.scene.sequences.length} sequences, ${r.scene.dialogue.lines.length} dialogue lines`,
        );
        host.toast(
            `Exported ${r.name}${where ? ` to ${where}` : ''} (${d.objects} objects)`,
            d.unresolvedClasses || d.danglingLinks ? 'warn' : 'ok',
        );
        // exposed for the browser test harness (no effect otherwise)
        (window as unknown as { __lastContentExport?: unknown }).__lastContentExport = {
            name: r.name,
            diagnostics: d,
        };
    } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        host.setStatus(msg, true);
        host.toast(msg, 'error');
    }
}

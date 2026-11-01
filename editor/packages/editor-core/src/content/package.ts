import { strToU8, zipSync } from 'fflate';
import { GENERATOR, levelSlug } from './build.ts';
import { CONTENT_FORMAT, CONTENT_VERSION, type DcfManifest, type DcfScene } from './format.ts';
import { CONTENT_SCHEMA } from './validate.ts';

export const SCHEMA_PATH = 'schema/drakan-content.schema.json';

export interface PackageOptions {
    /** Disc profile (`pal`) and language the content was read from. */
    source?: { profile?: string; language?: string };
    /** Relative path of an asset exporter's index, when the package is shipped next to one. */
    assetIndex?: string;
}

const README = `Drakan Content Format ${CONTENT_VERSION}
================================
Engine-neutral level and story content exported by the Drakan editor.

manifest.json   what is in the package
scenes/         one <level>.scene.json per level (instances, archetypes, links, triggers, sequences, dialogue, quests)
schema/         the JSON Schema every file validates against

The package holds names, numbers and text only. Meshes, textures and sounds are referenced by id
("RESOURES:128"); a "path" appears on a reference when an asset exporter published that resource.
Reference: docs/reference/content-format.md in the Drakan PC port repository.
`;

const json = (v: unknown): string => JSON.stringify(v, null, 1) + '\n';

/** Files of a package (path -> bytes) for the given scenes. */
export function buildContentPackage(
    scenes: readonly DcfScene[],
    opts: PackageOptions = {},
): Record<string, Uint8Array> {
    const files: Record<string, Uint8Array> = {};
    const manifest: DcfManifest = {
        format: CONTENT_FORMAT,
        version: CONTENT_VERSION,
        kind: 'package',
        generator: GENERATOR,
        ...(opts.source ? { source: opts.source } : {}),
        scenes: [],
        schema: SCHEMA_PATH,
        ...(opts.assetIndex ? { assetIndex: opts.assetIndex } : {}),
    };
    for (const s of scenes) {
        const path = `scenes/${levelSlug(s.level.name)}.scene.json`;
        if (path in files) throw new Error(`two scenes are called "${s.level.name}"`);
        files[path] = strToU8(json(s));
        manifest.scenes.push({ name: s.level.name, path });
    }
    files['manifest.json'] = strToU8(json(manifest));
    files[SCHEMA_PATH] = strToU8(json(CONTENT_SCHEMA));
    files['README.txt'] = strToU8(README);
    return files;
}

export function zipContentPackage(files: Record<string, Uint8Array>): Uint8Array {
    return zipSync(files, { level: 6 });
}

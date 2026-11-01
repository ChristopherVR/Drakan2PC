// Gathers everything the Unreal export needs from the open level through the worker: terrain, static class models,
// and rigs plus clips for the animated classes. UI-free so a harness can drive it.

import type { AssetClass, LevelObject, SchemaProvider } from '@drakan/editor-core';
import { resourceParts } from '@drakan/editor-core';
import type { Api, Method } from '../rpc.ts';
import type { RefService } from '../refs.ts';
import { createAnimSource } from '../editor/animsource.ts';
import type { ModelData, TerrainData } from '../scene/decode.ts';
import type { ExportClass, ExportClip, ExportOptions, LevelExportInput } from './levelglb.ts';
import { ident } from './levelglb.ts';

type Call = <M extends Method>(method: M, ...args: Parameters<Api[M]>) => Promise<ReturnType<Api[M]>>;

export interface CollectHost {
    call: Call;
    refs: RefService;
    schemas: SchemaProvider | null;
    catalog: readonly AssetClass[];
    objects: readonly LevelObject[];
    /** Disc directory of the open level (`LEVELS\LEVEL1NA`). */
    dirPath: string;
    level: string;
    status(msg: string): void;
}

const BATCH = 24;

/** Decoded data of the open level, ready for `buildLevelGltf`. */
export async function collectLevel(
    host: CollectHost,
    opt: Pick<ExportOptions, 'includeTerrain' | 'includeModels' | 'includeSkinned' | 'maxClipsPerClass'>,
): Promise<LevelExportInput> {
    const names = new Map(host.catalog.map((c) => [c.resource, c.name]));
    const classOf = new Map<string, string>();
    for (const o of host.objects) if (o.class && !classOf.has(o.resource)) classOf.set(o.resource, o.class);
    const nameOf = (res: string): string => names.get(res) || classOf.get(res) || `Class${res}`;
    const resources = [...new Set(host.objects.map((o) => o.resource))];

    let terrain: TerrainData | null = null;
    if (opt.includeTerrain) {
        host.status('Reading terrain...');
        terrain = await host.call('loadTerrain', host.dirPath);
    }

    const classes: ExportClass[] = [];
    const done = new Set<string>();

    // Animated classes first: rig, textures and clips come from the reference loaders the animation preview uses.
    if (opt.includeSkinned && host.schemas) {
        const source = createAnimSource(host.refs, () => host.schemas);
        let n = 0;
        for (const res of resources) {
            if (!source.canAnimate(res)) continue;
            host.status(`Reading skeleton ${++n}...`);
            const cr = await source.load(res);
            if (!cr) continue;
            const schema = host.schemas.schemaFor(res);
            const ref = schema ? await host.refs.classModel(schema.db, schema.classId) : null;
            const full = ref ? await host.refs.rig(ref.stem, ref.id) : null;
            const clips: ExportClip[] = [];
            const seen = new Set<string>();
            for (const c of cr.clips) {
                const key = `${c.stem}:${c.id}`;
                if (seen.has(key) || clips.length >= opt.maxClipsPerClass) continue;
                seen.add(key);
                const anim = await source.clip(c);
                if (anim) clips.push({ id: c.id, name: c.name || anim.name, stem: c.stem, anim });
            }
            classes.push({
                resource: res,
                name: ident(nameOf(res)),
                model: cr.model,
                textures: full?.data.textures ?? [],
                rig: cr.rig,
                clips,
            });
            done.add(res);
        }
    }

    // Everything else is a static model.
    if (opt.includeModels) {
        const rest = resources.filter((r) => !done.has(r));
        for (let i = 0; i < rest.length; i += BATCH) {
            const part = rest.slice(i, i + BATCH);
            host.status(`Reading models ${Math.min(i + BATCH, rest.length)}/${rest.length}...`);
            const data: ModelData[] = await host.call(
                'loadModels',
                host.dirPath,
                part.map((r) => resourceParts(r)),
            );
            data.forEach((d, k) => {
                if (!d.model) return;
                const res = part[k]!;
                classes.push({ resource: res, name: ident(nameOf(res)), model: d.model, textures: d.textures });
            });
        }
    }
    return { level: host.level, terrain, classes, objects: host.objects };
}

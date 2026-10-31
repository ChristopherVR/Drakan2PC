import { typeCode, type SchemaProvider } from '@drakan/editor-core';
import type { RefService } from '../refs.ts';
import type { AnimSource, ClassClip, ClassRig } from '../scene/animlayer.ts';
import { classClips, pickIdle } from './animpreview.ts';

/** Does the class have an animation field (type 9, or an array of it)? Only those classes can animate. */
export function hasAnimationField(fields: readonly { type: number }[]): boolean {
    return fields.some((f) => typeCode(f.type) === 9);
}

/** Rigs and clips for the level viewport, from the class schemas and the worker (through `RefService`). */
export function createAnimSource(refs: RefService, schemas: () => SchemaProvider | null): AnimSource {
    const can = new Map<string, boolean>();
    return {
        canAnimate(resource) {
            const p = schemas();
            if (!p) return false;
            let v = can.get(resource);
            if (v === undefined) {
                const s = p.schemaFor(resource);
                v = !!s && hasAnimationField(s.fields);
                can.set(resource, v);
            }
            return v;
        },
        async load(resource): Promise<ClassRig | null> {
            const p = schemas();
            const schema = p?.schemaFor(resource);
            if (!p || !schema) return null;
            const model = await refs.classModel(schema.db, schema.classId);
            if (!model) return null;
            const r = await refs.rig(model.stem, model.id);
            if (!r?.rig || !r.data.model) return null;
            const fromModel: ClassClip[] = r.clips.map((c) => ({
                stem: model.stem,
                id: c.id,
                name: c.name,
                label: '',
            }));
            const names = new Map(fromModel.map((c) => [`${c.stem}:${c.id}`, c.name]));
            const fromClass: ClassClip[] = classClips(p, schema, null).map((c) => ({
                stem: c.stem,
                id: c.id,
                name: names.get(`${c.stem}:${c.id}`) ?? '',
                label: c.label,
            }));
            const seen = new Set(fromClass.map((c) => `${c.stem}:${c.id}`));
            const clips = [...fromClass, ...fromModel.filter((c) => !seen.has(`${c.stem}:${c.id}`))];
            const idle = pickIdle(clips);
            return {
                model: r.data.model,
                lookup: r.lookup,
                rig: r.rig,
                clips,
                idle: idle ? { stem: idle.stem, id: idle.id } : null,
            };
        },
        clip: (ref) => refs.clip(ref.stem, ref.id),
    };
}

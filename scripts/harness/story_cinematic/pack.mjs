// Builds a mod pack for the cinematic harness with the editor's own code (ContentSession, templates, buildStoryPackFiles).
//   node --experimental-transform-types pack.mjs <spec.json> <out-dir>
// Spec (JSON):
//   { "id": "cine.edit", "level": "LEVEL1NA", "story": "LEVEL1ON", "label": "Surdana",
//     "edits": [ { "sequence": 497, "ops": [ {"op":"set","actor":"Camera","type":0,"nth":0,"field":"x","value":1},
//                                            {"op":"time","actor":"Zola Dane","type":3,"nth":0,"time":5},
//                                            {"op":"timescale","actor":"Camera","factor":2.5},
//                                            {"op":"pose","actor":"Camera","nth":0,"pos":[x,y,z],"yaw":deg,"pitch":deg} ] } ],
//     "scene": { "name": "...", "camera": [{"time":0,"pos":[..],"yaw":0,"pitch":0}], "steps": [...], "duration": 8 },
//     "trigger": { "at": [x,y,z], "w": 10, "h": 8, "d": 10, "once": true, "who": "2" } }
// The out dir gets `<id>/` and `mods.ini` (point DRAKAN_MODS at it). Prints a JSON summary on the last line.
import { mkdirSync, writeFileSync, readFileSync, openAsBlob, existsSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../../../editor/packages/');
const pk = (p) => pathToFileURL(resolve(root, p)).href;
const F = await import(pk('formats/src/index.ts'));
const S = await import(pk('formats/src/story/index.ts'));
const C = await import(pk('editor-core/src/index.ts'));
const { ContentSession } = await import(pk('app/src/story/content.ts'));
const { isoStoryFs } = await import(pk('app/src/story/storyfs.ts'));

const BS = String.fromCharCode(92);
function isoPath() {
    if (process.env.DRAKAN_ISO) return process.env.DRAKAN_ISO;
    for (const ini of [resolve(here, '../../../dist/DrakanPC/drakan.ini')]) {
        if (!existsSync(ini)) continue;
        const m = /^iso\s*=\s*(.+)$/m.exec(readFileSync(ini, 'utf8'));
        if (m) return m[1].trim();
    }
    throw new Error('set DRAKAN_ISO');
}
const [specPath, outDir] = process.argv.slice(2);
const spec = JSON.parse(readFileSync(specPath, 'utf8'));
const iso = await F.Iso9660.open(F.blobSource(await openAsBlob(isoPath())));
const target = { profile: 'pal', hash: 'b9f60ba118125a54' };
const sess = new ContentSession({
    fs: () => isoStoryFs(iso, 'ENGLISH'),
    language: () => 'ENGLISH',
    target: () => target,
    status: () => {},
});
await sess.ensure(spec.story);
const summary = { edited: [], scene: null, trigger: null };

// ---- edits of existing sequences (what the Cinematics page does through ContentSession.editSequence) -----------------
const actorOf = (seq, name) => seq.actors.find((a) => a.name.trim().toLowerCase() === name.trim().toLowerCase());
const nthAction = (actor, type, nth) => S.parseActions(actor).filter((a) => type === undefined || a.type === type)[nth ?? 0];
for (const e of spec.edits ?? []) {
    sess.editSequence(e.sequence, (seq) => {
        for (const op of e.ops) {
            const actor = actorOf(seq, op.actor);
            if (!actor) throw new Error(`sequence ${e.sequence}: no actor ${op.actor}`);
            if (op.op === 'timescale') {
                // every action of the actor at factor x its time (a retime of the whole track, order kept)
                for (const a of S.parseActions(actor)) actor.actions = S.setActionTime(actor, a, a.time * op.factor);
                continue;
            }
            const act = nthAction(actor, op.type ?? (op.op === 'pose' ? 0 : undefined), op.nth);
            if (!act) throw new Error(`sequence ${e.sequence}: ${op.actor} has no such action`);
            const set = (f, v) => {
                const cur = S.parseActions(actor).find((a) => a.offset === act.offset);
                actor.actions = S.setActionField(actor, cur, f, v);
            };
            if (op.op === 'set') set(op.field, op.value);
            else if (op.op === 'time') actor.actions = S.setActionTime(actor, act, op.time);
            else if (op.op === 'pose') {
                const [qx, qy, qz, qw] = C.yawPitchQuat(op.yaw, op.pitch);
                for (const [f, v] of Object.entries({ qx, qy, qz, qw, x: op.pos[0], y: op.pos[1], z: op.pos[2] })) set(f, v);
            } else throw new Error(`unknown op ${op.op}`);
        }
    });
    summary.edited.push(e.sequence);
}

// ---- a new cut scene and the trigger volume that plays it (what New cut scene + the logic template do) -----------------
const changes = [];
if (spec.scene) {
    const cameraRef = spec.scene.cameraRef ?? sess.cameraRef();
    const id = sess.addCutscene({
        folder: spec.scene.folder ?? sess.story.folders?.[0]?.id ?? 0,
        name: spec.scene.name,
        cameraRef,
        camera: spec.scene.camera,
        steps: spec.scene.steps ?? [],
        duration: spec.scene.duration,
        camSetup: spec.scene.camSetup,
    });
    summary.scene = { id, cameraRef };
    if (spec.trigger) {
        const dirPath = ['ENGLISH', 'LEVELS', spec.level].join(BS);
        const lvlFile = (await iso.list(dirPath)).find((x) => !x.isDir && /\.LVL$/i.test(x.name));
        const reader = await F.SrscReader.open(F.sliceSource(iso.source, lvlFile.extent * 2048, lvlFile.size));
        const lvl = await F.readLvl(reader);
        const oe = reader.entries.find((x) => x.type === F.LVL_TYPE_OBJECTS);
        const dec = new TextDecoder('latin1');
        const odbReaders = new Map();
        const odbDeps = {};
        for (const d of lvl.header.dependencies) {
            const stem = F.odbStemForDependency(d.path.text);
            if (odbReaders.has(stem)) continue;
            const base = F.resolveDependencyStem(dirPath, d.path.text);
            const f = await iso.find(`${base}.ODB`);
            if (!f || f.isDir) continue;
            odbReaders.set(stem, await F.SrscReader.open(F.sliceSource(iso.source, f.extent * 2048, f.size)));
            const deps = {};
            const dbf = await iso.find(`${base}.DB`);
            if (dbf && !dbf.isDir) {
                const dir = base.slice(0, base.lastIndexOf(BS));
                for (const dep of F.parseDb(dec.decode(await iso.read(dbf))).dependencies)
                    deps[dep.index] = F.odbStemForDependency(F.resolveDependencyStem(dir, dep.path));
            }
            odbDeps[stem] = deps;
        }
        const odbs = await F.buildOdbIndex(odbReaders);
        const { doc } = C.documentFromLvl(lvl, odbs, spec.label ?? spec.level);
        const env = new C.SchemaProvider(lvl.header, odbs, odbDeps);
        const catalog = C.buildCatalog(lvl.header, odbs, doc.objects());
        const stack = new C.CommandStack(doc);
        const t = spec.trigger;
        const r = C.templateById('cutscene-trigger').build(
            { doc, env, catalog, at: t.at, selection: [] },
            { sequence: id, w: t.w ?? 10, h: t.h ?? 8, d: t.d ?? 10, once: t.once ?? true, who: t.who ?? '2' },
        );
        if (C.isError(r)) throw new Error(r.error);
        stack.run(new C.ApplyTemplateCommand(r, 'Template'));
        const { list } = C.applyDocumentToLvl(lvl.list, doc);
        changes.push({ file: lvlFile.name, type: oe.type, id: oe.id, sub: oe.sub, data: F.writeObjectList(list), isNew: false });
        summary.trigger = { notes: r.notes ?? null };
    }
}
changes.push(...(await sess.changes()));
const meta = { id: spec.id, name: spec.id, version: '1.0.0', description: 'cinematic harness pack', target };
const files = C.buildStoryPackFiles(meta, changes);
const dir = `${outDir}/${meta.id}`;
for (const [p, d] of Object.entries(files)) {
    const full = `${dir}/${p}`;
    mkdirSync(full.slice(0, full.lastIndexOf('/')), { recursive: true });
    writeFileSync(full, d);
}
writeFileSync(`${outDir}/mods.ini`, `[mods]\n${meta.id} = on\n`);
summary.files = Object.keys(files);
console.log(JSON.stringify(summary));

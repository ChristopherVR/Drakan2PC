// Authors a puzzle mod pack in Stratos with the editor's own templates (scripts/harness/story_puzzles.py runs it).
// node --experimental-transform-types story_puzzles_author.mjs <kind> <outdir> [json]; env DRAKAN_ISO.
import { mkdirSync, writeFileSync } from 'node:fs';
// Loads a disc level the way the editor worker does (node, no browser). Scratch tool, not part of the repo.
import { openAsBlob } from 'node:fs';
const ROOT = new URL('../../editor/packages/', import.meta.url).href;
export const F = await import(ROOT + 'formats/src/index.ts');
export const C = await import(ROOT + 'editor-core/src/index.ts');
export const ISO = process.env.DRAKAN_ISO;
export const BS = String.fromCharCode(92);
const dec = new TextDecoder('latin1');
export async function openIso() {
    return F.Iso9660.open(F.blobSource(await openAsBlob(ISO)));
}
async function readOdbDeps(fs, stemPath) {
    const out = {};
    const dbf = await fs.find(`${stemPath}.DB`);
    if (!dbf || dbf.isDir) return out;
    const dir = stemPath.slice(0, stemPath.lastIndexOf(BS));
    for (const dep of F.parseDb(dec.decode(await fs.read(dbf))).dependencies)
        out[dep.index] = F.odbStemForDependency(F.resolveDependencyStem(dir, dep.path));
    return out;
}
export async function loadLevel(fs, dirPath) {
    const lvlFile = (await fs.list(dirPath)).find((e) => !e.isDir && /\.LVL$/i.test(e.name));
    const reader = await F.SrscReader.open(F.sliceSource(fs.source, lvlFile.extent * 2048, lvlFile.size));
    const lvl = await F.readLvl(reader);
    const e = reader.entries.find((x) => x.type === F.LVL_TYPE_OBJECTS);
    const odbReaders = new Map();
    const odbDeps = {};
    for (const d of lvl.header.dependencies) {
        const stem = F.odbStemForDependency(d.path.text);
        if (odbReaders.has(stem)) continue;
        const f = await fs.find(`${F.resolveDependencyStem(dirPath, d.path.text)}.ODB`);
        if (!f || f.isDir) continue;
        odbReaders.set(stem, await F.SrscReader.open(F.sliceSource(fs.source, f.extent * 2048, f.size)));
        odbDeps[stem] = await readOdbDeps(fs, F.resolveDependencyStem(dirPath, d.path.text));
    }
    return { path: lvlFile.path, file: lvlFile.name, objects: { type: e.type, id: e.id, sub: e.sub, size: e.size }, lvl, odbs: await F.buildOdbIndex(odbReaders), odbDeps };
}

const kind = process.argv[2]; const out = process.argv[3];
const opt = JSON.parse(process.argv[4] ?? process.env.PUZ_OPT ?? '{}');
const LV = opt.level ?? 'LEVEL8OS';
const fs = await openIso();
const src = await loadLevel(fs, ['ENGLISH', 'LEVELS', LV].join(BS));
const { doc } = C.documentFromLvl(src.lvl, src.odbs, 'x');
const env = new C.SchemaProvider(src.lvl.header, src.odbs, src.odbDeps);
const catalog = C.buildCatalog(src.lvl.header, src.odbs, doc.objects());
const stack = new C.CommandStack(doc);
// Stratos start canyon: Rynn starts at S facing +z on open ground (floor about y 146480).
const S = opt.start ?? [651612, 147580, 961906];
const FLOOR = S[1] - 95;
const at = (dx, dz, y = 0) => [S[0] + dx, FLOOR + y, S[2] + dz];
function place(clsName, pos, fields = {}, yaw = 0) {
  const cls = catalog.find((c) => c.name === clsName);
  if (!cls) throw new Error('no class ' + clsName);
  const o = C.newObjectFromClass(doc, cls, pos, yaw);
  const sch = env.schemaFor(cls.resource);
  for (const [k, v] of Object.entries(fields)) { const f = C.schemaField(sch, k); if (!f) throw new Error('no field ' + k); C.setObjectField(o, f, v); }
  const cmd = new C.AddCommand([o]); stack.run(cmd); return cmd.ids[0];
}
function tpl(id, v, pos) {
  const t = C.templateById(id);
  const r = t.build({ doc, env, catalog, at: pos, selection: [] }, v);
  if (C.isError(r)) throw new Error(r.error);
  console.log(id, r.notes, r.warnings);
  stack.run(new C.ApplyTemplateCommand(r, 'Template: ' + t.title));
  return r;
}
const door = place(
    opt.doorClass ?? 'GF Wood Door01',
    at(-300, opt.doorDz ?? 4500, 0),
    { Locked: opt.locked ?? 0, 'Auto-Open?': opt.autoOpen ?? 0 },
    opt.doorYaw ?? 0,
);
console.log('door', door);
if (kind === 'plate') tpl('pressure-plates', { target: door, count: 1, then: opt.then ?? 'On', axis: 'z', hold: !!opt.hold }, at(0, 1800, -100));
else if (kind === 'counter') tpl('pressure-plates', { target: door, count: 2, then: opt.then ?? 'On', axis: 'z', spacing: 1600 }, at(0, 1800, -100));
else if (kind === 'lever') tpl('lever-door', { door, mode: opt.leverMode ?? 'open' }, at(0, 700, 100));
else if (kind === 'timer') tpl('timer-door', { target: door, delay: 2, then: 'On', w: 3, h: 2, d: 1 }, at(0, 700, 1000));
else if (kind === 'key') {
  tpl('key-lock', { door, code: opt.code ?? 62 }, at(0, 700, 100));
  if (!opt.noKey) place(opt.keyClass ?? 'Prison Key', at(opt.keyDx ?? 0, opt.keyDz ?? 300, opt.keyDy ?? 80), { 'Lock Code': opt.keyCode ?? opt.code ?? 62 });
} else if (kind === 'killall') {
  // two creatures that die when the volume sends Blow Up (no melee needed), a counter they report to, the door
  const src = [...doc.objects()].find((o) => o.class === (opt.enemyClass ?? 'Human Skeleton:Center'));
  const before = new Set([...doc.objects()].map((o) => o.id));
  tpl('ambush', { enemy: src.id, count: 2, radius: 900, w: 4, h: 3, d: 2 }, at(0, opt.killDz ?? 900));
  const fresh = [...doc.objects()].filter((o) => !before.has(o.id));
  const det = fresh.find((o) => o.class === 'Trigger Detector');
  const crea = fresh.filter((o) => o.class === src.class).map((o) => o.id);
  C.setObjectField(det, C.schemaField(env.schemaFor(det.resource), 'Trigger Message'), opt.killMsg ?? 5);
  tpl('kill-all-gate', { enemies: crea, gate: door }, at(0, 2500));
  if (opt.bow) place(opt.bow, at(0, 60, 80)); // a bow to pick up in front of Rynn (the bow kill scenario)
} else if (kind === 'ambush') {
  const src = [...doc.objects()].find((o) => o.class === (opt.enemyClass ?? 'Human Skeleton:Center'));
  if (!src) throw new Error('no enemy of that class in the level');
  tpl('ambush', { enemy: src.id, count: opt.count ?? 3, radius: opt.radius ?? 1200, w: 4, h: 3, d: 2 }, at(0, opt.ambushDz ?? 900, opt.ambushDy ?? 0));
  const det = [...doc.objects()].find((o) => o.id > 11970 && o.class === 'Trigger Detector');
  if (opt.linkDoor) det.links.push(door); // debug: proves the volume fires
  if (opt.wakeMsg !== undefined) { // debug: try another message than On
    const f = C.schemaField(env.schemaFor(det.resource), 'Trigger Message'); C.setObjectField(det, f, opt.wakeMsg);
  }
} else throw new Error('kind ' + kind);
// the stock plate is about 4000 units wide: shrink ours so Rynn does not spawn on it
for (const o of doc.objects()) if (o.id > 11970 && /Pressure Plate/.test(o.class) && o.scale) o.scale = [0.4, 1, 0.4];
const issues = C.validateDocument(doc, { sourceLevel: true }).concat(C.validateLogic(doc, env));
console.log('issues', issues.map((i) => i.message).slice(0, 8));
const { list, report } = C.applyDocumentToLvl(src.lvl.list, doc);
console.log(JSON.stringify(report.diff), report.warnings);
const payload = F.writeObjectList(list);
const id = 'puz.' + kind;
const meta = { id, name: 'Puzzle proof: ' + kind, version: '1.0.0', description: kind, target: { profile: 'pal', hash: 'b9f60ba118125a54' } };
const files = C.buildModFiles(meta, [{ file: src.file, entry: { type: src.objects.type, id: src.objects.id, sub: src.objects.sub }, data: payload }]);
const dir = out + '/' + id;
for (const [p, d] of Object.entries(files)) { const full = dir + '/' + p; mkdirSync(full.slice(0, full.lastIndexOf('/')), { recursive: true }); writeFileSync(full, d); }
writeFileSync(out + '/mods.ini', '[mods]\n' + id + ' = on\n');
for (const o of doc.objects()) if (o.id > 11970) console.log('NEW', o.id, o.class, JSON.stringify(o.links), o.origin ?? ''); 
console.log(JSON.stringify({ door, start: S, level: LV }));

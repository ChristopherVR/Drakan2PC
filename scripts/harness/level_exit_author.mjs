// Authors a mod pack with a NEW level exit placed by the editor's "Level exit to..." template (exit object plus the
// Trigger Detector that fires it), in Surdana. scripts/harness/level_change_walk.py then walks into the new zone.
//   node --experimental-transform-types level_exit_author.mjs <outdir> '{"exitClass":"Inquisitor","at":[735232,152900,768800]}'
// env DRAKAN_ISO. Prints the class used, the new object ids and the exit object's position (the game logs it as
// "exit taken ... at x y z", which tells the new exit from the shipped ones).
import { mkdirSync, writeFileSync, openAsBlob } from 'node:fs';

const ROOT = new URL('../../editor/packages/', import.meta.url).href;
const F = await import(ROOT + 'formats/src/index.ts');
const C = await import(ROOT + 'editor-core/src/index.ts');
const BS = String.fromCharCode(92);
const dec = new TextDecoder('latin1');
const out = process.argv[2];
const opt = JSON.parse(process.argv[3] ?? '{}');
const LV = opt.level ?? 'LEVEL1NA';

const fs = await F.Iso9660.open(F.blobSource(await openAsBlob(process.env.DRAKAN_ISO)));
async function readOdbDeps(stemPath) {
    const o = {};
    const dbf = await fs.find(`${stemPath}.DB`);
    if (!dbf || dbf.isDir) return o;
    const dir = stemPath.slice(0, stemPath.lastIndexOf(BS));
    for (const dep of F.parseDb(dec.decode(await fs.read(dbf))).dependencies)
        o[dep.index] = F.odbStemForDependency(F.resolveDependencyStem(dir, dep.path));
    return o;
}
const dirPath = ['ENGLISH', 'LEVELS', LV].join(BS);
const lvlFile = (await fs.list(dirPath)).find((e) => !e.isDir && /\.LVL$/i.test(e.name));
const reader = await F.SrscReader.open(F.sliceSource(fs.source, lvlFile.extent * 2048, lvlFile.size));
const lvl = await F.readLvl(reader);
const entry = reader.entries.find((x) => x.type === F.LVL_TYPE_OBJECTS);
const odbReaders = new Map();
const odbDeps = {};
for (const d of lvl.header.dependencies) {
    const stem = F.odbStemForDependency(d.path.text);
    if (odbReaders.has(stem)) continue;
    const f = await fs.find(`${F.resolveDependencyStem(dirPath, d.path.text)}.ODB`);
    if (!f || f.isDir) continue;
    odbReaders.set(stem, await F.SrscReader.open(F.sliceSource(fs.source, f.extent * 2048, f.size)));
    odbDeps[stem] = await readOdbDeps(F.resolveDependencyStem(dirPath, d.path.text));
}
const odbs = await F.buildOdbIndex(odbReaders);
const { doc } = C.documentFromLvl(lvl, odbs, 'x');
const env = new C.SchemaProvider(lvl.header, odbs, odbDeps);
const catalog = C.buildCatalog(lvl.header, odbs, doc.objects());
const stack = new C.CommandStack(doc);

const exits = catalog.filter((c) => c.rflClass === 0x37);
console.log('exit classes', exits.map((c) => `${c.name}(${c.instances})`).join(' | '));
const cls = exits.find((c) => new RegExp(opt.exitClass ?? 'Inquisitor', 'i').test(c.name)) ?? exits[0];
console.log('using', cls.name);
const at = opt.at ?? [735232, 152900, 768800];
const t = C.templateById('level-exit');
const r = t.build({ doc, env, catalog, at, selection: [] }, { exit: cls.resource, w: opt.w ?? 6, h: 3, d: opt.d ?? 1 });
if (C.isError(r)) throw new Error(r.error);
console.log(r.notes, r.warnings);
stack.run(new C.ApplyTemplateCommand(r, 'Template: ' + t.title));
for (const o of r.objects) console.log('NEW', o.id, o.class, o.transform.pos.join(' '), JSON.stringify(o.links), 'scale', JSON.stringify(o.scale), JSON.stringify(o.fields));

const { list, report } = C.applyDocumentToLvl(lvl.list, doc);
console.log(JSON.stringify(report.diff), report.warnings);
const payload = F.writeObjectList(list);
const id = 'exit.new';
const meta = { id, name: 'New exit proof', version: '1.0.0', description: 'new level exit', target: { profile: 'pal', hash: 'b9f60ba118125a54' } };
const files = C.buildModFiles(meta, [{ file: lvlFile.name, entry: { type: entry.type, id: entry.id, sub: entry.sub }, data: payload }]);
const dir = out + '/' + id;
for (const [p, d] of Object.entries(files)) {
    const full = dir + '/' + p;
    mkdirSync(full.slice(0, full.lastIndexOf('/')), { recursive: true });
    writeFileSync(full, d);
}
writeFileSync(out + '/mods.ini', '[mods]\n' + id + ' = on\n');
if (opt.dump) {
    const ship = doc.get(12249);
    const mine = r.objects.find((o) => o.class === 'Trigger Detector');
    const names = env.schemaFor(mine.resource).fields.map((f) => f.name);
    for (const n of names) console.log('FIELD', n, '| shipped', JSON.stringify(C.fieldValueOf(ship, env, n)), '| new', JSON.stringify(C.fieldValueOf(mine, env, n)));
}
if (opt.dump) {
    const ship = doc.get(12249);
    const mine = r.objects.find((o) => o.class === 'Trigger Detector');
    const strip = (o) => JSON.stringify({ ...o, fields: undefined, template: undefined, raw: undefined }).slice(0, 600);
    console.log('OBJ ship', strip(ship));
    console.log('OBJ mine', strip(mine));
}

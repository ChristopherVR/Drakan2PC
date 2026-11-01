// Builds the story-dialogue proof pack with the editor's own code (ContentSession + compose logic + LVL writer).
//   cd editor && npx tsx ../scripts/harness/story_dialogue_pack.mts <iso> <outdir> [--base]
// Three edits on the Surdana farmer conversation (placed Conversation Handler #7806, NPC #5102):
//   1. EDIT: subtitle of the farmer's first line (sound 721) rewritten;
//   2. SWAP: Rynn's reply line (class 875) plays sound 33 instead of 153 (its subtitle is then sound 33's text);
//   3. NEW:  a brand new dialogue line (sound 34, new subtitle) inserted first in the handler's Beginning exchange.
// Nothing here stores game data; the pack is written under <outdir> (build/).
import { mkdirSync, writeFileSync, openAsBlob } from 'node:fs';
import * as F from '../../editor/packages/formats/src/index.ts';
import * as C from '../../editor/packages/editor-core/src/index.ts';
import { ContentSession } from '../../editor/packages/app/src/story/content.ts';
import { isoStoryFs } from '../../editor/packages/app/src/story/storyfs.ts';

const [ISO, OUT] = [process.argv[2]!, process.argv[3]!];
const BS = String.fromCharCode(92);
export const TEXTS = {
    edit: 'EDITED SUBTITLE: the editor changed what the farmer says.',
    newLine: 'NEW LINE: the farmer says something nobody wrote.',
};
const HANDLER = 7806;
const fs = await F.Iso9660.open(F.blobSource(await openAsBlob(ISO)));
const dec = new TextDecoder('latin1');
async function readOdbDeps(stemPath: string) {
    const out: Record<number, string> = {};
    const dbf = await fs.find(`${stemPath}.DB`);
    if (!dbf || dbf.isDir) return out;
    const dir = stemPath.slice(0, stemPath.lastIndexOf(BS));
    for (const dep of F.parseDb(dec.decode(await fs.read(dbf))).dependencies)
        out[dep.index] = F.odbStemForDependency(F.resolveDependencyStem(dir, dep.path));
    return out;
}
const dirPath = ['ENGLISH', 'LEVELS', 'LEVEL1NA'].join(BS);
const lvlFile = (await fs.list(dirPath)).find((e) => !e.isDir && /\.LVL$/i.test(e.name))!;
const reader = await F.SrscReader.open(F.sliceSource(fs.source, lvlFile.extent * 2048, lvlFile.size));
const lvl = await F.readLvl(reader);
const oe = reader.entries.find((x) => x.type === F.LVL_TYPE_OBJECTS)!;
const odbReaders = new Map<string, any>();
const odbDeps: any = {};
for (const d of lvl.header.dependencies) {
    const stem = F.odbStemForDependency(d.path.text);
    if (odbReaders.has(stem)) continue;
    const f = await fs.find(`${F.resolveDependencyStem(dirPath, d.path.text)}.ODB`);
    if (!f || f.isDir) continue;
    odbReaders.set(stem, await F.SrscReader.open(F.sliceSource(fs.source, f.extent * 2048, f.size)));
    odbDeps[stem] = await readOdbDeps(F.resolveDependencyStem(dirPath, d.path.text));
}
const odbs = await F.buildOdbIndex(odbReaders);
const { doc } = C.documentFromLvl(lvl, odbs, 'Surdana');
const env = new C.SchemaProvider(lvl.header, odbs, odbDeps);
const stack = new C.CommandStack(doc);

const s = new ContentSession({
    fs: () => isoStoryFs(fs as any, 'ENGLISH'),
    language: () => 'ENGLISH',
    target: () => ({ profile: 'pal' }),
    status: () => {},
});
await s.ensure('LEVEL1ON');
const lines = s.lines();
const farmer = lines.find((l) => l.classId === 876)!;
const rynn = lines.find((l) => l.classId === 875)!;
console.log('farmer line', farmer.name, 'sound', farmer.sounds[0]! & 0xffff, '| rynn line', rynn.name, 'sound', rynn.sounds[0]! & 0xffff);
if (!s.story!.sounds.get(34)) throw new Error('LEVEL1ON has no sound 34');

// 3. NEW line first in the placed handler's Beginning exchange (the placed object overrides the class arrays).
const o = doc.get(HANDLER)!;
const field = env.schemaFor(o.resource)!.fields.find((f) => f.name === 'Beginning Dialog Exchange')!;
const cur = C.effectiveValue(o, field).value as number[];
const newId = s.composeLine(
    { speaker: 'Farmer', text: TEXTS.newLine, soundRef: 34, soundName: '', handlerId: 0, phase: 'beginning' },
    { classOnly: true },
);
const next = [newId, ...cur];
const after = C.cloneObject(o);
C.setObjectField(after, field, next);
stack.run(new C.EditObjectsCommand(new Map([[HANDLER, C.cloneObject(o)]]), new Map([[HANDLER, after]]), 'Add dialogue line'));
console.log('handler Beginning', cur, '->', next);
// 1. EDIT and 2. SWAP
s.setText(farmer.sounds[0]! & 0xffff, TEXTS.edit);
s.setLineSound(875, 0, 33);

const { list } = C.applyDocumentToLvl(lvl.list, doc);
const changes = await s.changes();
changes.push({ file: lvlFile.name, type: oe.type, id: oe.id, sub: oe.sub, data: F.writeObjectList(list), isNew: false });
const meta = {
    id: 'story.dialogue.proof',
    name: 'Story dialogue proof',
    version: '1.0.0',
    description: 'edited subtitle, swapped sound, new dialogue line',
    target: { profile: 'pal', hash: 'b9f60ba118125a54' },
};
const files = C.buildStoryPackFiles(meta as any, changes);
const dir = `${OUT}/${meta.id}`;
for (const [p, d] of Object.entries(files)) {
    const full = `${dir}/${p}`;
    mkdirSync(full.slice(0, full.lastIndexOf('/')), { recursive: true });
    writeFileSync(full, d as Uint8Array);
}
writeFileSync(`${OUT}/mods.ini`, `[mods]\n${meta.id} = on\n`);
console.log('class', newId, Object.keys(files));

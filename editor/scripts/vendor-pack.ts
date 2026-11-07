// Build a mod pack that edits one vendor of a disc level with the editor's own vendor code (stock, quantities, modifiers,
// item prices). Dev tool and the first step of scripts/harness/vendor_shop.py.
//
//   node --experimental-transform-types --no-warnings editor/scripts/vendor-pack.ts --iso <game.iso> --out <mods dir> \
//        [--level LEVEL1NA] [--vendor 1648] [--id vendor.proof] \
//        [--add RESOURES:8569[:unlimited|:<count>]]... [--remove RESOURES:7869]... [--price RESOURES:7869=7]... \
//        [--sale-mod 1.5] [--buy-mod 0.5] [--list] [--new-shop [x,y,z]]
// `--new-shop` adds a shop with its keeper and keeper box like the editor's "Add shop here" (default spot: 1800 units in front
// of Rynn's start in Surdana) and edits that new shop with the other options.
//
// Writes <out>/<id>/ (a pack folder) and <out>/mods.ini enabling it. `--list` prints the level's vendors and stock.
import { mkdirSync, openAsBlob, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import {
    ApplyTemplateCommand,
    CommandStack,
    buildCatalog,
    buildNewShop,
    isError,
    SchemaProvider,
    applyDocumentToLvl,
    overridePath,
    buildStoryPackFiles,
    documentFromLvl,
    editObjects,
    readVendor,
    vendorSchemaOf,
    writeSetting,
    writeStock,
    type StockEntry,
} from '@drakan/editor-core';
import {
    Iso9660,
    SrscReader,
    blobSource,
    buildOdbIndex,
    odbStemForDependency,
    parseDb,
    readLvl,
    resolveDependencyStem,
    sliceSource,
    writeObjectList,
    LVL_TYPE_OBJECTS,
} from '@drakan/formats';
import { VendorPrices } from '../packages/app/src/editor/vendorprices.ts';

const args = process.argv.slice(2);
const opt = new Map<string, string[]>();
for (let i = 0; i < args.length; i++) {
    const a = args[i]!;
    if (!a.startsWith('--')) continue;
    const k = a.slice(2);
    const v = args[i + 1] && !args[i + 1]!.startsWith('--') ? args[++i]! : '';
    opt.set(k, [...(opt.get(k) ?? []), v]);
}
const one = (k: string, d?: string): string =>
    opt.get(k)?.[0] ?? d ?? (console.error(`missing --${k}`), process.exit(2));
const many = (k: string): string[] => opt.get(k) ?? [];

const BS = String.fromCharCode(92);
const dec = new TextDecoder('latin1');
const iso = await Iso9660.open(blobSource(await openAsBlob(one('iso'))));
const levelStem = one('level', 'LEVEL1NA').toUpperCase();
const dirPath = ['ENGLISH', 'LEVELS', levelStem].join(BS);

const lvlFile = (await iso.list(dirPath)).find((e) => !e.isDir && /\.LVL$/i.test(e.name));
if (!lvlFile) throw new Error(`no LVL in ${dirPath}`);
const reader = await SrscReader.open(sliceSource(iso.source, lvlFile.extent * 2048, lvlFile.size));
const lvl = await readLvl(reader);
const objEntry = reader.entries.find((x) => x.type === LVL_TYPE_OBJECTS)!;
const odbReaders = new Map<string, SrscReader>();
const odbDeps: Record<string, Record<number, string>> = {};
for (const d of lvl.header.dependencies) {
    const stem = odbStemForDependency(d.path.text);
    if (odbReaders.has(stem)) continue;
    const full = resolveDependencyStem(dirPath, d.path.text);
    const f = await iso.find(`${full}.ODB`);
    if (!f || f.isDir) continue;
    odbReaders.set(stem, await SrscReader.open(sliceSource(iso.source, f.extent * 2048, f.size)));
    const deps: Record<number, string> = {};
    const dbf = await iso.find(`${full}.DB`);
    if (dbf && !dbf.isDir) {
        const dir = full.slice(0, full.lastIndexOf(BS));
        for (const dep of parseDb(dec.decode(await iso.read(dbf))).dependencies)
            deps[dep.index] = odbStemForDependency(resolveDependencyStem(dir, dep.path));
    }
    odbDeps[stem] = deps;
}
const odbs = await buildOdbIndex(odbReaders);
const { doc } = documentFromLvl(lvl, odbs, levelStem);
const provider = new SchemaProvider(lvl.header, odbs, odbDeps);
const vendors = [...doc.objects()].filter((o) => vendorSchemaOf(o, provider));

const itemName = (stem: string, id: number): string => odbs.get(stem)?.get(id)?.name ?? '?';
if (opt.has('list')) {
    for (const o of vendors) {
        const v = readVendor(o, vendorSchemaOf(o, provider)!, provider);
        console.log(`#${o.id} ${o.class} sale x${v.settings.saleMod} buy x${v.settings.buyMod}`);
        for (const e of v.entries) {
            const price = provider.classNumber(e.stem, e.id, 'Cost to Buy (in Gold)')?.value;
            console.log(`   ${e.mode} x${e.count} ${e.stem}:${e.id} ${itemName(e.stem, e.id)} ${price ?? ''}`);
        }
    }
    process.exit(0);
}

const stack = new CommandStack(doc);
let newShopId: number | undefined;
if (opt.has('new-shop')) {
    const at = (one('new-shop', '446100,171300,344205') || '446100,171300,344205').split(',').map(Number) as [number, number, number];
    const catalog = buildCatalog(lvl.header, odbs, doc.objects());
    const built = buildNewShop({ doc, env: provider, catalog, at, selection: [] });
    if (isError(built)) throw new Error(built.error);
    stack.run(new ApplyTemplateCommand(built, 'Add shop here'));
    newShopId = built.objects[0]!.id;
    console.log('new shop', newShopId, built.objects.map((o) => `${o.class} #${o.id}`).join(', '), built.warnings);
}
const vendorId = Number(one('vendor', String(newShopId ?? vendors[0]?.id)));
const shop = doc.get(vendorId);
const schema = shop && vendorSchemaOf(shop, provider);
if (!shop || !schema) throw new Error(`object ${vendorId} is not a vendor of ${levelStem}`);
let entries: StockEntry[] = readVendor(shop, schema, provider).entries;
const ref = (s: string): { stem: string; id: number } => {
    const [stem, id] = s.split(':');
    return { stem: stem!, id: Number(id) };
};
for (const r of many('remove')) {
    const { stem, id } = ref(r);
    entries = entries.filter((e) => !(e.stem === stem && e.id === id));
}
for (const a of many('add')) {
    const [stem, id, how] = a.split(':');
    const unlimited = how === 'unlimited';
    entries.push({
        stem: stem!,
        id: Number(id),
        mode: unlimited ? 'infinite' : 'limited',
        count: unlimited ? 1 : Math.max(1, Number(how ?? 1)),
    });
}
const cmd = editObjects(doc, [vendorId], 'Edit vendor', (o) => {
    const r = writeStock(o, schema, provider, entries);
    if (r.unreachable.length) throw new Error(`the shop cannot name ${JSON.stringify(r.unreachable)}`);
    if (opt.has('sale-mod')) writeSetting(o, schema, 'saleMod', Number(one('sale-mod')));
    if (opt.has('buy-mod')) writeSetting(o, schema, 'buyMod', Number(one('buy-mod')));
});
if (cmd) stack.run(cmd);
const view = readVendor(doc.get(vendorId)!, schema, provider);
console.log(`vendor #${vendorId} ${shop.class}: ${view.entries.length} stock entries, sale x${view.settings.saleMod}`);
for (const e of view.entries) console.log('  stock', e.mode, e.count, e.stem + ':' + e.id, itemName(e.stem, e.id));

const prices = new VendorPrices({
    readFile: async (p) => {
        const f = await iso.find(`ENGLISH${BS}${p}`);
        return f && !f.isDir ? iso.read(f) : null;
    },
    itemNames: async () => new Map(),
});
for (const p of many('price')) {
    const [k, v] = p.split('=');
    const { stem, id } = ref(k!);
    prices.set(stem, id, Number(v));
}

const target = { profile: 'pal', hash: 'b9f60ba118125a54' };
const meta = {
    id: one('id', 'vendor.proof'),
    name: 'Vendor edit',
    version: '1.0.0',
    description: 'Vendor stock, modifiers and item prices made with the editor vendor code.',
    target,
};
const files = buildStoryPackFiles(meta, await prices.changes());
const { list, report } = applyDocumentToLvl(lvl.list, doc);
console.log('level diff', JSON.stringify(report.diff), report.warnings);
if (cmd || newShopId !== undefined) {
    const data = writeObjectList(list);
    files[overridePath(lvlFile.name, objEntry)] = data;
}
const out = one('out');
const dir = join(out, meta.id);
for (const [p, d] of Object.entries(files)) {
    const full = join(dir, p);
    mkdirSync(dirname(full), { recursive: true });
    writeFileSync(full, d);
}
writeFileSync(join(out, 'mods.ini'), `[mods]\n${meta.id} = on\n`);
console.log('wrote', dir, Object.keys(files).join(', '));

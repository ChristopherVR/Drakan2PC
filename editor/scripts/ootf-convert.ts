// Convert Order of the Flame levels to a PS2 mod pack (zip). Dev tool; the editor UI does the same from the browser.
//
//   node --experimental-transform-types --no-warnings editor/scripts/ootf-convert.ts \
//        --ootf <OotF install dir> --ps2 <dir with the extracted PS2 LEVEL*.LVL and *.ODB> \
//        --template LEVEL1NA --out build/ootf-pack.zip [--id ootf-levels] [--prefix "OotF - "] [--splats <dir with LEVEL1TS.DB and .TXD, default --ps2>] [--campaign 1 [--treasure 1]] [--cover <png>] [--previews <dir of <safe map name>.png>] [--background <png>] [--player <dir with SYSTEM.MOD and .ADB, default <ps2>/../player>] [--hd [--hd-out <textures dir>]] [--ps2-textures 0|1] [--ps2-terrain 0|1] [--ps2-texture-map <json>] [--ps2-models 0|1] [--model-upgrade 0|1] [--authored <dir of authored model .json>] [--enemy-animations drakan2|ootf] [--ps2-model-map <json>] [--subtitles <json>[;<corrected json>]] [--class-sounds 0] [--class-sounds-clear 1] [--class-sound-clears <json>] [--pc-voices 1] [--wartok-idle off] [--iso <disc image>] [--previous <zip>|none] [--save-baseline <zip>] [--list] <pc level paths...>
//
// Level paths are relative to the install dir, e.g. "Multiplayer/Ground/Relic/Relic.lvl". Without paths every level.
//
// Drakan 2 textures (formats ootfps2tex.ts): where the curated table names a Drakan 2 texture for an OotF one (a pine trunk, the
// tropical plant fronds), the disc texture is copied instead of the converted OotF one. --ps2-textures 0 keeps every OotF texture;
// --ps2-texture-map <json> edits the table: a list of { "ootf": "<container>#<id>", "ps2": "<disc TXD path>#<id>" | null } rows
// (null turns a row off). Disc TXDs are read from --ps2-txd <dir>, <ps2>/txd, <ps2> or the hosts dir, else from the disc image
// (--iso, env DRAKAN_ISO, or the iso of dist/DrakanPC/drakan.ini).
// Ground: whole OotF ground families (a surface's variants and transition tiles) draw the closest Drakan 2 ground texture
// (formats DEFAULT_PS2_TERRAIN_FAMILIES); --ps2-terrain 0 keeps the OotF ground. Map rows naming WORLDCON/PROCTEES/PROCTEES.TXD#<id>
// edit ground textures the same way.
//
// Drakan 2 models (formats ootfps2model.ts): Wartoks, orcs and plain weapons use the disc's own models. --ps2-models 0 keeps the
// converter's earlier choices; --enemy-animations ootf retargets the PC clips onto the disc rigs instead of the disc's own clips;
// --ps2-model-map <json> edits the table: a list of { "ootf": "<OotF class>", "ps2": "<Drakan 2 class>" | null } rows.
import { createHash } from 'node:crypto';
import {
    closeSync,
    existsSync,
    fstatSync,
    mkdirSync,
    openSync,
    readFileSync,
    readSync,
    readdirSync,
    rmSync,
    statSync,
    writeFileSync,
} from 'node:fs';
import { spawnSync } from 'node:child_process';
import { join, relative, sep } from 'node:path';
import { strToU8, unzipSync } from 'fflate';
import { buildManifestToml, buildOotfPack, ootfArenaFromPc, previewFileName, zipOotfPack } from '@drakan/editor-core';
import {
    addedLevelObjects,
    chooseSaveBuild,
    classIdPins,
    formatSaveAddedObjects,
    formatSaveRelinkedPowerUps,
    formatSaveReplacedEffects,
    formatSaveResetTogglers,
    packSaveLayout,
    planSaveRevisions,
    readSaveRevisions,
    Iso9660,
    SrscReader,
    SubtitleTable,
    classifySubtitleLines,
    listObjectSounds,
    mergeSubtitleFiles,
    parseSubtitleFile,
    convertOotfLevels,
    chunkTextureLimit,
    findPcLevels,
    setExactSink,
    loadPcLevel,
    ps2BlobFor,
    pcLevelName,
    ps2ModelSwapFromEdits,
    ps2TextureSwapFromEdits,
    type PcFileSource,
    authoredProblem,
    decodePng,
    type RgbaImage,
    type AuthoredModel,
    type Ps2ModelSwapOptions,
    type Ps2TextureSwapOptions,
} from '@drakan/formats';

const args = process.argv.slice(2);
const opt: Record<string, string> = {};
const free: string[] = [];
let list = false;
let hd = false;
for (let i = 0; i < args.length; i++) {
    const a = args[i]!;
    if (a === '--list') list = true;
    else if (a === '--hd') hd = true;
    else if (a.startsWith('--')) opt[a.slice(2)] = args[++i] ?? '';
    else free.push(a);
}
function loadAuthored(dir: string | undefined): AuthoredModel[] | undefined {
    if (!dir) return undefined;
    if (!existsSync(dir)) {
        console.error(`--authored: ${dir} not found`);
        process.exit(2);
    }
    const out: AuthoredModel[] = [];
    for (const f of readdirSync(dir)
        .filter((x) => x.toLowerCase().endsWith('.json'))
        .sort()) {
        const m = JSON.parse(readFileSync(join(dir, f), 'utf-8')) as unknown;
        const why = authoredProblem(m);
        if (why) {
            console.warn(`authored ${f}: skipped (${why})`);
            continue;
        }
        // the model's own textures: PNG files beside it
        const model = m as AuthoredModel;
        const images: Record<string, RgbaImage> = {};
        for (const slot of model.slots) {
            if (slot.texture !== 'image' || !slot.image || images[slot.image]) continue;
            images[slot.image] = decodePng(new Uint8Array(readFileSync(join(dir, slot.image))));
        }
        out.push({ ...model, images });
    }
    return out;
}
const need = (k: string): string => opt[k] ?? (console.error(`missing --${k}`), process.exit(2));

const root = need('ootf');
// --hd: keep the HD source of every texture the converter had to quantise or resize, next to the pack (<out>.hdsrc/<sha1 of the
// record>.rgba: u32 w, u32 h, RGBA), so the HD pack upscales those instead of the 256-colour guest texture. A resized texture (terrain
// enlarged 64 -> 128 px, squared or rounded up) gives the PC pixels before the resize, which can be smaller than the record.
if (hd) {
    const dir = `${need('out')}.hdsrc`;
    rmSync(dir, { recursive: true, force: true });
    mkdirSync(dir, { recursive: true });
    setExactSink((body, img) => {
        const head = new Uint8Array(8);
        new DataView(head.buffer).setUint32(0, img.width, true);
        new DataView(head.buffer).setUint32(4, img.height, true);
        writeFileSync(
            join(dir, `${createHash('sha1').update(body).digest('hex')}.rgba`),
            Buffer.concat([head, img.rgba]),
        );
    });
}
const all: string[] = [];
(function walk(d: string) {
    for (const f of readdirSync(d)) {
        const p = join(d, f);
        statSync(p).isDirectory() ? walk(p) : all.push(relative(root, p).split(sep).join('/'));
    }
})(root);
const byLower = new Map(all.map((p) => [p.toLowerCase(), p]));
const src: PcFileSource = {
    read: async (p) => {
        const r = byLower.get(p.toLowerCase());
        return r ? new Uint8Array(readFileSync(join(root, r))) : null;
    },
    list: async () => all,
};

const levels = free.length ? free : await findPcLevels(src);
if (list) {
    for (const p of levels) console.log(p);
    process.exit(0);
}

const ps2 = need('ps2');
const tplName = need('template').toUpperCase();
const template = await SrscReader.fromBytes(new Uint8Array(readFileSync(join(ps2, `${tplName}.LVL`))));
const odbs = new Map<string, SrscReader>();
for (const f of readdirSync(ps2).filter((x) => /\.ODB$/i.test(x))) {
    odbs.set(f.slice(0, -4).toUpperCase(), await SrscReader.fromBytes(new Uint8Array(readFileSync(join(ps2, f)))));
}
const optReader = async (name: string): Promise<SrscReader | undefined> =>
    existsSync(join(ps2, name)) ? SrscReader.fromBytes(new Uint8Array(readFileSync(join(ps2, name)))) : undefined;
// Other PS2 MOD / ADB containers (NPCS.MOD ...): skinned templates of ODBs that list RESOURES, PS2 rigs and clips of name-mapped creatures.
const optMap = async (ext: string): Promise<Map<string, SrscReader>> => {
    const m = new Map<string, SrscReader>();
    for (const f of readdirSync(ps2).filter((x) => new RegExp(`^(?!RESOURES).*\.${ext}$`, 'i').test(x)))
        m.set(f.slice(0, -4).toUpperCase(), await SrscReader.fromBytes(new Uint8Array(readFileSync(join(ps2, f)))));
    return m;
};
const ssds = new Map<string, SrscReader>();
for (const f of readdirSync(ps2).filter((x) => /\.SSD$/i.test(x))) {
    ssds.set(f.slice(0, -4).toUpperCase(), await SrscReader.fromBytes(new Uint8Array(readFileSync(join(ps2, f)))));
}
// ODB stem -> disc directory (written by scripts/harness/ootf_level_run.py) so the pipeline can patch an ODB's .DB.
const dbFiles = new Map<string, { dir: string; text: string }>();
const pathsFile = join(ps2, 'paths.json');
if (existsSync(pathsFile)) {
    const dirs = JSON.parse(readFileSync(pathsFile, 'utf8')) as Record<string, string>;
    for (const [stem, dir] of Object.entries(dirs)) {
        const f = join(ps2, `${stem}.DB`);
        if (existsSync(f)) dbFiles.set(stem, { dir, text: readFileSync(f, 'latin1') });
    }
}

// Own MOD / TXD of template ODBs that host models of cloned doors / switches / flames (scripts/harness/ootf_level_run.py writes them
// next to the ps2 data dir as <ps2>/../hosts/<STEM>.MOD|TXD, see OotfPs2Context.hostFiles).
const hostFiles = new Map<string, { mod: SrscReader; txd: SrscReader }>();
{
    const hostsDir = opt['hosts'] ?? join(ps2, '..', 'hosts');
    if (existsSync(hostsDir)) {
        for (const f of readdirSync(hostsDir).filter((x) => /\.MOD$/i.test(x))) {
            const stem = f.slice(0, -4).toUpperCase();
            const t = join(hostsDir, `${stem}.TXD`);
            if (!existsSync(t)) continue;
            hostFiles.set(stem, {
                mod: await SrscReader.fromBytes(new Uint8Array(readFileSync(join(hostsDir, f)))),
                txd: await SrscReader.fromBytes(new Uint8Array(readFileSync(t))),
            });
        }
    }
}

// The PS2 player's SYSTEM.MOD / SYSTEM.ADB (scripts/harness/ootf_level_run.py writes them to <ps2>/../player, --player <dir>): the PC
// clips the converted scenes play on Rynn are retargeted onto its rig as new SYSTEM.ADB clips (OotfPs2Context.playerMod). Kept out of
// --ps2 so the player is never name-mapped like a creature.
const playerDir = opt['player'] ?? join(ps2, '..', 'player');
const playerReader = async (name: string): Promise<SrscReader | undefined> =>
    existsSync(join(playerDir, name))
        ? SrscReader.fromBytes(new Uint8Array(readFileSync(join(playerDir, name))))
        : undefined;
const playerMod = await playerReader('SYSTEM.MOD');
const playerAdb = await playerReader('SYSTEM.ADB');
if (!playerMod || !playerAdb)
    console.warn(`warning: no SYSTEM.MOD / SYSTEM.ADB in ${playerDir}: Rynn plays no clips in converted scenes`);

// The template's splat files (LEVEL1TS.DB / .TXD): needed beside every converted LVL (the game crashes without them).
const splatDir = opt['splats'] ?? ps2;
const splatStem = opt['splat-stem'] ?? `${tplName.slice(0, 6)}TS`;
const splats: Record<string, Uint8Array> = {};
for (const ext of ['DB', 'TXD']) {
    const f = join(splatDir, `${splatStem}.${ext}`);
    if (existsSync(f)) splats[ext] = new Uint8Array(readFileSync(f));
}
if (!Object.keys(splats).length) {
    // A pack without the template's splat files never finishes its level load (result -4, a black screen the player reads as a freeze).
    console.error(
        `error: no ${splatStem}.DB/.TXD in ${splatDir} (--splats <dir>); a pack without them cannot load its levels`,
    );
    process.exit(2);
}

// Drakan 2 textures in place of OotF ones (see the header): options and the disc TXD reader.
let ps2Textures: Ps2TextureSwapOptions = { enabled: opt['ps2-textures'] !== '0', terrain: opt['ps2-terrain'] !== '0' };
if (opt['ps2-texture-map'])
    ps2Textures = ps2TextureSwapFromEdits(JSON.parse(readFileSync(opt['ps2-texture-map'], 'utf8')), ps2Textures);
let ps2Models: Ps2ModelSwapOptions = {
    enabled: opt['ps2-models'] !== '0',
    enemyAnimations: opt['enemy-animations'] === 'ootf' ? 'ootf' : 'drakan2',
};
if (opt['ps2-model-map'])
    ps2Models = ps2ModelSwapFromEdits(JSON.parse(readFileSync(opt['ps2-model-map'], 'utf8')), ps2Models);
const isoFile = ((): string | undefined => {
    if (opt['iso']) return opt['iso'];
    if (process.env['DRAKAN_ISO']) return process.env['DRAKAN_ISO'];
    const ini = join(import.meta.dirname, '..', '..', 'dist', 'DrakanPC', 'drakan.ini');
    const m = existsSync(ini) ? /^\s*iso\s*=\s*(.+?)\s*$/m.exec(readFileSync(ini, 'utf8')) : null;
    return m?.[1];
})();
let iso: Promise<Iso9660 | null> | null = null;
const readPs2Txd = async (discPath: string): Promise<Uint8Array | null> => {
    const name = discPath.split('/').pop()!.toUpperCase();
    for (const d of [opt['ps2-txd'], join(ps2, 'txd'), ps2, opt['hosts'] ?? join(ps2, '..', 'hosts')]) {
        const f =
            d &&
            existsSync(d) &&
            readdirSync(d, { withFileTypes: true }).find((e) => e.isFile() && e.name.toUpperCase() === name);
        if (f) return new Uint8Array(readFileSync(join(d, f.name)));
    }
    if (!isoFile || !existsSync(isoFile)) return null;
    iso ??= (async () => {
        const fd = openSync(isoFile, 'r');
        const size = fstatSync(fd).size;
        process.once('exit', () => closeSync(fd));
        return Iso9660.open({
            size,
            read: async (o: number, n: number) => {
                const b = new Uint8Array(Math.max(0, Math.min(n, size - o)));
                readSync(fd, b, 0, b.length, o);
                return b;
            },
        }).catch(() => null);
    })();
    const disc = await iso;
    return disc && (await disc.find(`ENGLISH/${discPath}`)) ? disc.read(`ENGLISH/${discPath}`) : null;
};

const prefix = opt['prefix'] ?? 'OotF - ';
const only = opt['only'] ? new Set(opt['only'].split(',')) : null; // stage switches: --only textures,models,...
// --subtitles <json>: the subtitle lines of the PC voice clips (analysis/scripts/ootf_subtitles.py makes the file on this
// machine from the install's own audio; the PC game has no subtitle text). Without it the pack has no scene subtitles.
// Several files separated by `;`: a later file's lines replace an earlier one's, so a hand-corrected file (the player's or a pack
// author's) listed last wins over a fresh transcription.
// Only dialogue and narration are subtitled: a line whose clip a class or an object plays by itself (an idle mumble, a pain or
// attack list, a waypoint's sound) is a bark and gets no text, unless a file gives the line a kind (`kind` in the line).
const subtitles = await (async () => {
    if (!opt['subtitles']) return undefined;
    const merged = mergeSubtitleFiles(
        opt['subtitles']
            .split(';')
            .filter(Boolean)
            .map((f) => parseSubtitleFile(JSON.parse(readFileSync(f, 'utf8')))),
    );
    for (const l of classifySubtitleLines(merged, await listObjectSounds(src)))
        console.warn(`subtitles: ${l.bank} #${l.id} (${l.name ?? ''}) is a bark, no subtitle: ${l.kindReason}`);
    const table = new SubtitleTable(merged);
    console.warn(`subtitles: ${table.size} line(s) shown, ${table.withheld.length} withheld by their kind`);
    return table;
})();
// Saves across rebuilds (docs/reference/modding.md "Saves and pack builds"): the previous build of the pack (--previous <zip>,
// default the --out file when it exists; --previous none = start afresh) hands its class ids on, so classes the conversion
// adds do not renumber the others, and the new pack keeps its `build` while nothing a save names changed.
const previousPath =
    opt['previous'] === 'none' ? '' : (opt['previous'] ?? (existsSync(need('out')) ? need('out') : ''));
let previous: { build: string; files: Record<string, Uint8Array> } | undefined;
if (previousPath) {
    const files = unzipSync(new Uint8Array(readFileSync(previousPath)), {
        filter: (f) =>
            /^mod\.toml$/i.test(f.name) ||
            /^new\/[^/]+\.ODB\//i.test(f.name) ||
            /^overrides\/[^/]+\.(ODB|LVL)\//i.test(f.name) ||
            /^files\/.*\.(LVL|ODB)$/i.test(f.name),
    });
    const build = /^build\s*=\s*"([^"]+)"/m.exec(new TextDecoder().decode(files['mod.toml'] ?? new Uint8Array()))?.[1];
    if (build) previous = { build, files };
    else console.warn(`warning: --previous ${previousPath} names no build; class ids and the save build start afresh`);
}
const res = await convertOotfLevels(
    src,
    levels,
    {
        template,
        odbs,
        ssds,
        dbFiles,
        hostMod: await optReader('RESOURES.MOD'),
        hostTxd: await optReader('RESOURES.TXD'),
        hostAdb: await optReader('RESOURES.ADB'),
        hostSdb: await optReader('RESOURES.SDB'),
        playerMod,
        playerAdb,
        musicSdb: await optReader('PS2MUSIC.SDB'),
        mods: await optMap('MOD'),
        hostFiles,
        adbs: await optMap('ADB'),
        xdbFiles: readdirSync(ps2).some((f) => /\.XDB$/i.test(f))
            ? new Set(
                  readdirSync(ps2)
                      .filter((f) => /\.XDB$/i.test(f))
                      .map((f) => f.toUpperCase()),
              )
            : undefined,
        splatTxd: splats['TXD'],
        procTxd: existsSync(join(ps2, 'PROCTEES.TXD'))
            ? new Uint8Array(readFileSync(join(ps2, 'PROCTEES.TXD')))
            : undefined,
        readPs2Txd,
    },
    {
        ...(previous ? { classIdPins: classIdPins(previous.files) } : {}),
        name: (pc) => `${prefix}${pcLevelName(pc.header)}`,
        log: (m) => console.warn(m),
        subtitles,
        textures: !only || only.has('textures'),
        terrainInCommon: opt['tex-common'] === '1',
        terrainDiscTextures: opt['tex-disc'] === '1',
        terrain: {
            firstId: opt['tex-first'] ? Number(opt['tex-first']) : undefined,
            maxSize: opt['tex-max'] ? Number(opt['tex-max']) : undefined,
            maxTextures: opt['tex-cap'] ? Number(opt['tex-cap']) : undefined,
        },
        models: !only || only.has('models'),
        classes: !only || only.has('classes'),
        singletons: !only || only.has('singletons'),
        maxNewClasses: opt['max-classes'] ? Number(opt['max-classes']) : undefined,
        skinned: opt['skinned'] !== '0',
        // --model-upgrade 0 (or DRAKAN_MODEL_UPGRADE=off): OotF characters keep their PC mesh, no jaw (formats ootfskinupgrade.ts)
        ...(opt['model-upgrade'] !== undefined ? { modelUpgrade: opt['model-upgrade'] !== '0' } : {}),
        // --authored <dir>: authored replacement meshes (*.json made by analysis/scripts/blender, formats ootfauthored.ts)
        authoredModels: loadAuthored(opt['authored']),
        retarget: opt['retarget'] !== '0',
        preferCreatures: opt['creatures-first'] === '1',
        texts: !only || only.has('texts'),
        music: !only || only.has('music'),
        sounds: !only || only.has('sounds'),
        // --class-sounds 0: converted classes keep their template's sound defaults (no PC class-default sounds)
        classSounds: opt['class-sounds'] !== '0',
        // --class-sounds-clear 1: a PC sound field without a sound empties the Drakan 2 template's sound there
        classSoundsClear: opt['class-sounds-clear'] === '1',
        // --class-sound-clears <json>: the list of OotF creatures whose other-creature template voices are emptied ([] = none)
        ...(opt['class-sound-clears']
            ? { templateVoiceClears: JSON.parse(readFileSync(opt['class-sound-clears'], 'utf8')) }
            : {}),
        // --pc-voices 1: Rynn and Arokh speak with their Order of the Flame voices in the campaign (sounds/swaps.ini)
        pcVoices: opt['pc-voices'] === '1',
        // --wartok-idle off: the Drakan 2 Wartok models' standing and walking grunts play nothing in the campaign
        ...(opt['wartok-idle'] === 'off' ? { wartokIdle: 'off' as const } : {}),
        sequences: !only || only.has('sequences'),
        ps2Textures,
        ps2Models,
    },
);
if (res.ps2Models)
    console.log(
        `drakan 2 models: ${res.ps2Models.used.length} used${res.ps2Models.used.length ? ` (${res.ps2Models.used.map((u) => `${u.ootf} -> ${u.ps2}`).join(', ')})` : ''}`,
    );
if (res.ps2Textures)
    console.log(
        `drakan 2 textures: ${res.ps2Textures.used.length} used${res.ps2Textures.used.length ? ` (${res.ps2Textures.used.map((u) => u.label).join(', ')})` : ''}, ${res.ps2Textures.skipped.length} not used`,
    );
for (const l of res.levels) {
    const R = l.parts.report;
    console.log(
        `${l.pcPath}: ${R.objectsOut}/${R.objectsIn} objects, ${R.unmappedClasses.size} classes unmapped, ${[...(R.omittedClasses?.values() ?? [])].reduce((n, o) => n + o.objects, 0)} objects kept out on purpose, ${R.layers} layers, ${R.cellsMapped} cells textured, ${R.cellsFallback} on fallback`,
    );
}
// --audit <regex>: print the class-plan row (outcome, PS2 class, model state) of every PC class whose name matches.
if (opt['audit']) {
    const re = new RegExp(opt['audit'], 'i');
    for (const r of res.classAudit)
        if (re.test(r.name))
            console.log(
                `audit ${r.name}: rfl ${r.rfl} ${r.kind} x${r.objects} -> ${r.outcome} ${r.template ?? r.ps2Name ?? ''} model=${r.model} ${r.reason ?? ''} ${r.detail ?? ''}`,
            );
}
for (const s of res.stages) if (!s.ok) console.warn(`STAGE FAILED ${s.stage}: ${s.error}`);
if (res.source) console.log(`source: ${res.source.language.language} install (${res.source.language.evidence})`);
if (res.assets) {
    const a = res.assets;
    for (const u of a.authored ?? [])
        console.log(
            `authored ${u.key} ${u.name} (${u.label}): ${u.triangles} triangles, ${u.vertices} vertices${u.dropped ? `, ${u.dropped} dropped` : ''}`,
        );
    for (const u of a.upgraded ?? [])
        console.log(`upgraded ${u.key} ${u.name}: ${u.triangles} triangles${u.jaw ? ', jaw' : ''}`);
    for (const c of a.creatures)
        console.log(
            `creature ${c.key}: ${c.joints} joints, ${c.groups} groups, ${c.anims} clips (${c.animsSkipped} skipped)`,
        );
    console.log(
        `assets: ${a.models.converted} models, ${a.textures.converted} textures (${a.textures.bytes} bytes), ${a.skipped.size} skipped`,
    );
}
if (res.classPlan) {
    console.log(
        `classes: ${res.classPlan.mapped.size} mapped, ${res.classPlan.synthesized.length} synthesized, ${res.classPlan.unsupported.size} unsupported`,
    );
}
// Multiplayer/* levels also get a map definition (spawns from the launch pads), as the editor's import dialog does.
const arenas = new Map<string, NonNullable<ReturnType<typeof ootfArenaFromPc>>>();
for (const l of res.levels) {
    if (!l.arena) continue;
    const a = ootfArenaFromPc(await loadPcLevel(src, l.pcPath), l.title);
    if (a) arenas.set(l.pcPath, a);
    else console.warn(`warning: ${l.pcPath}: no arena (fewer than two launch pads)`);
}
// --previews <dir>: menu pictures of the multiplayer maps (<safe map name>.png, e.g. previews/maps from scripts/harness/map_previews.py)
const previewPng = (name: string | undefined): Uint8Array | undefined => {
    const f = name && opt['previews'] ? join(opt['previews'], previewFileName(name)) : '';
    return f && existsSync(f) ? new Uint8Array(readFileSync(f)) : undefined;
};
const pack = await buildOotfPack(
    {
        id: opt['id'] ?? 'ootf-levels',
        name: 'Order of the Flame levels',
        target: { profile: 'pal' },
        // --campaign: the levels are the campaign in play order (the title menu offers "Drakan: Order of the Flame")
        ...(opt['campaign'] !== undefined
            ? { campaign: 'ootf' as const, campaignName: 'Drakan: Order of the Flame' }
            : {}),
        // Rynn has no spells in Order of the Flame: no Spells page, mana orb or casting while the campaign is played
        ...(opt['campaign'] !== undefined ? { campaignSpells: false } : {}),
        // nor gold and trinkets (no shops): defeated creatures, barrels and chests drop only what the OotF data gives them;
        // --treasure 1 keeps Drakan 2's drops
        ...(opt['campaign'] !== undefined && opt['treasure'] !== '1' ? { campaignTreasure: false } : {}),
        // so the inventory, the Character page and the pause menu leave out the gold amount too (--treasure 1 keeps it)
        ...(opt['campaign'] !== undefined && opt['treasure'] !== '1' ? { campaignGoldCounter: false } : {}),
        // the rendered OotF title theme ("gui", PC Background Music ID 4) plays on the title menu while the campaign is chosen
        ...(opt['campaign'] !== undefined && res.music?.title ? { campaignTitleMusic: res.music.title } : {}),
        // levels of flat picture scenes are shown at the aspect they were framed for (the Ruined Village Book: 4:3)
        ...(res.levelAspect ? { levelAspect: res.levelAspect } : {}),
        // exits that close once taken (the church stair to the Book: a backed-out save offers it again until then)
        ...(res.oneWayExits?.length ? { oneWayExits: res.oneWayExits } : {}),
        // DRAKAN_TERRAIN_CHUNKS=wide: chunks of up to 32 textures, which the runtime must read in its wide layout
        ...(chunkTextureLimit() > 8 ? { terrainChunkTextures: chunkTextureLimit() } : {}),
    },
    template,
    res.levels.map((l) => ({
        title: l.title,
        parts: l.parts,
        splats: l.splats,
        arena: arenas.get(l.pcPath),
        preview: previewPng(arenas.get(l.pcPath)?.name),
    })),
    ps2BlobFor,
    { splats, extraFiles: res.files },
);
// The campaign's own end card: the game serves it in place of the Drakan 2 credits while this campaign is the current game
// (docs/reference/modding.md "Campaign credits"). No names beyond the studio and the game: the PC install carries no credits.
if (opt['campaign'] !== undefined) {
    const nl = String.fromCharCode(13, 10);
    const card = [
        '=Drakan: Order of the Flame',
        "Converted campaign for Drakan: The Ancients' Gates",
        '',
        '=Original game',
        'Drakan: Order of the Flame, Surreal Software, 1999',
        '',
        '=Engine',
        "Drakan: The Ancients' Gates, Surreal Software, 2002",
        '',
        '',
    ].join(nl);
    pack.files['credits/credits.txt'] = new TextEncoder().encode(card);
}
// --cover <png> / --background <png>: the campaign's card cover (previews/campaign-ootf.png) and its menu background
// (previews/background-ootf.png, docs/reference/modding.md "Campaign packs"). The art is the player's own file (never committed);
// a single picture passed as --cover also serves as the background.
if (opt['campaign'] !== undefined) {
    const cover = opt['cover'] ?? opt['background'];
    const background = opt['background'] ?? opt['cover'];
    if (cover && existsSync(cover)) pack.files['previews/campaign-ootf.png'] = new Uint8Array(readFileSync(cover));
    if (background && existsSync(background))
        pack.files['previews/background-ootf.png'] = new Uint8Array(readFileSync(background));
    for (const k of ['cover', 'background'])
        if (opt[k] && !existsSync(opt[k]!)) console.warn(`warning: --${k} ${opt[k]} not found; no picture added`);
}
for (const w of [...res.warnings, ...pack.warnings]) console.warn('warning:', w);
{
    // The pack's build is its save-format id (savelayout.ts): kept from the previous pack while every class id and object id a
    // save can name still means the same, else the hash of the new layout. save_scope: the pack changes no disc class or level.
    const layout = await packSaveLayout(pack.files);
    const previousLayout = previous && (await packSaveLayout(previous.files));
    const choice = chooseSaveBuild(
        layout,
        previous && previousLayout && { build: previous.build, layout: previousLayout },
    );
    // Save revisions (formats saverevisions.ts): what this build changed since the saves of the previous pack were made,
    // so the game upgrades them on load (objects added, spawned effects retired). The history comes from the previous
    // pack's manifest; a previous pack written before revisions is compared with --save-baseline <zip> (the pack the
    // existing saves were made with) when given, else with itself.
    const previousRevisions = readSaveRevisions(
        new TextDecoder().decode(previous?.files['mod.toml'] ?? new Uint8Array()),
    );
    let base = previousLayout;
    if (choice.kept && previous && !previousRevisions.revision && !opt['save-baseline'])
        console.warn(
            'WARNING: the previous pack records no save revisions: objects it already added after the saves were made are not listed for them (pass --save-baseline <the pack those saves were made with>)',
        );
    if (choice.kept && !previousRevisions.revision && opt['save-baseline']) {
        const bz = unzipSync(new Uint8Array(readFileSync(opt['save-baseline'])), {
            filter: (f) => /^files\/.*\.LVL$/i.test(f.name),
        });
        base = await packSaveLayout(bz);
        console.log(`save revisions: added objects counted against --save-baseline ${opt['save-baseline']}`);
    }
    const titleOf = new Map(
        pack.levels.map((l) => [
            `files/english/levels/${l.mangled.toLowerCase()}/${l.mangled.toLowerCase()}.lvl`,
            l.title,
        ]),
    );
    const addedObjects = new Map<string, number[]>();
    for (const [path, ids] of base ? addedLevelObjects(base, layout) : new Map<string, number[]>()) {
        const title = titleOf.get(path);
        if (title) addedObjects.set(title, ids);
        else console.warn(`warning: save revisions: objects added to ${path}, which is not a level of this pack`);
    }
    const { revisions, bumped } = planSaveRevisions(
        previousRevisions,
        choice.kept,
        addedObjects,
        res.saveEffectSwaps ?? [],
        // Dragon relinks (res.saveRelinks) are not written yet: restoring the link gives the upgraded dragon the breath
        // references but he still does not breathe in the next level (journal 2026-11-16/save-state-fix.md), and the
        // relink cannot tell a breathless dragon from one that took a power-up normally.
        [],
        new Map(Object.entries(res.saveTogglers ?? {})),
        new Map(Object.entries(res.saveAddRequires ?? {})),
    );
    pack.files['mod.toml'] = strToU8(
        buildManifestToml({
            ...pack.meta,
            build: choice.build,
            saveScope: 'levels',
            ...(revisions.revision
                ? {
                      saveRevision: revisions.revision,
                      saveAddedObjects: revisions.added.map(formatSaveAddedObjects),
                      saveReplacedEffects: revisions.replaced.map(formatSaveReplacedEffects),
                      saveRelinkedPowerUps: revisions.relinked.map(formatSaveRelinkedPowerUps),
                      saveResetTogglers: revisions.resetTogglers.map(formatSaveResetTogglers),
                  }
                : {}),
        }),
    );
    if (revisions.revision)
        console.log(
            `save revision ${revisions.revision}${bumped ? ' (new)' : ''}: older saves get ${revisions.added.reduce((n, a) => n + a.ids.length, 0)} added object(s), ${revisions.replaced.length} retired effect(s), ${revisions.relinked.length} relinked dragon(s) and ${revisions.resetTogglers.reduce((n, t) => n + t.ids.length, 0)} reset toggler(s) when they load`,
        );
    for (const line of [
        ...revisions.added.map(formatSaveAddedObjects),
        ...revisions.replaced.map(formatSaveReplacedEffects),
        ...revisions.relinked.map(formatSaveRelinkedPowerUps),
        ...revisions.resetTogglers.map(formatSaveResetTogglers),
    ])
        console.log(`  ${line}`);
    const d = choice.diff;
    if (!previous) console.log(`save build ${choice.build} (no previous pack: saves of earlier builds are refused)`);
    else if (choice.kept)
        console.log(
            `save build ${choice.build} kept: saves of the previous pack still load (${d!.added} class/object ids added)`,
        );
    else {
        console.warn(
            `WARNING: saves of the previous pack (build ${previous.build}) will be refused: new save build ${choice.build}. ${d!.changed.length} ids changed meaning, ${d!.removed.length} are gone:`,
        );
        for (const k of [...d!.changed, ...d!.removed.map((r) => `${r}: gone`)].slice(0, 40)) console.warn(`  ${k}`);
    }
}
writeFileSync(need('out'), zipOotfPack(pack));
console.log(`wrote ${opt['out']} (${pack.levels.length} levels, ${Object.keys(pack.files).length} files)`);
// --hd-sources-only 1: only write the lossless sources next to the pack; the caller (scripts/buildtool/extras.py) runs the HD pack.
// --hd [--hd-out dir]: HD pack for the converted textures (Real-ESRGAN, by decoded-RGBA hash) into dist/DrakanPC/textures or <dir>; never committed.
if (hd && opt['hd-sources-only'] === undefined) {
    const hdArgs = [
        '-I',
        'analysis/scripts/ootf_hd_pack.py',
        need('out'),
        ...(opt['hd-out'] ? ['--out', opt['hd-out']] : []),
    ];
    const r = spawnSync('python', hdArgs, { stdio: 'inherit' });
    if (r.status) process.exit(r.status);
}

// Export the voice clips that Order of the Flame sequences play, for the subtitle tool (analysis/scripts/ootf_subtitles.py).
//
//   node --experimental-transform-types --no-warnings editor/scripts/ootf-speech.ts --ootf <OotF install dir> --out <work dir>
//
// Writes <out>/clips/<n>.wav (mono 16 bit) and <out>/manifest.json: one row per clip with its sound bank, id, name, length and
// the scenes that play it, and `played_by`: the classes and objects that play the same clip by themselves (a bark, which the
// subtitle tool marks so it gets no subtitle). The output is the player's own game audio: keep it out of the repository (use a folder under build/).
import { mkdirSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { join, relative, sep } from 'node:path';
import { listObjectSounds, listSequenceSounds, objectSoundUseText, parsePcSounds, pcmWav, type PcFileSource, type PcSoundBank } from '@drakan/formats';

const args = process.argv.slice(2);
const opt: Record<string, string> = {};
for (let i = 0; i < args.length; i++) if (args[i]!.startsWith('--')) opt[args[i]!.slice(2)] = args[++i] ?? '';
const need = (k: string): string => opt[k] ?? (console.error(`missing --${k}`), process.exit(2));
const root = need('ootf');
const out = need('out');
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
        const real = byLower.get(p.toLowerCase());
        return real ? new Uint8Array(readFileSync(join(root, real))) : null;
    },
    list: async () => all,
};
mkdirSync(join(out, 'clips'), { recursive: true });
const banks = new Map<string, PcSoundBank | null>();
const byObjects = new Map((await listObjectSounds(src)).map((s) => [`${s.bank}#${s.id}`, s.uses.map(objectSoundUseText)]));
const rows: unknown[] = [];
let n = 0;
for (const s of await listSequenceSounds(src)) {
    let bank = banks.get(s.bank);
    if (bank === undefined) {
        const bytes = await src.read(`${s.bankPath}.sdb`);
        bank = bytes ? await parsePcSounds(bytes) : null;
        banks.set(s.bank, bank);
    }
    const snd = bank?.sounds.find((x) => x.id === s.id);
    if (!snd || !snd.pcm.length) continue;
    const file = `clips/${String(n++).padStart(4, '0')}.wav`;
    writeFileSync(join(out, file), pcmWav(snd.pcm, snd.rate));
    rows.push({
        bank: s.bank,
        id: s.id,
        name: snd.name,
        duration: Math.round((snd.pcm.length / snd.rate) * 1000) / 1000,
        loop: (snd.flags & 8) !== 0,
        file,
        played_by: byObjects.get(`${s.bank}#${s.id}`) ?? [],
        uses: s.uses.map((u) => ({ ...u, time: Math.round(u.time * 1000) / 1000, duration: Math.round(u.duration * 1000) / 1000 })),
    });
}
writeFileSync(join(out, 'manifest.json'), JSON.stringify({ clips: rows }, null, 1));
console.log(`wrote ${rows.length} clip(s) of ${banks.size} bank(s) to ${out}`);

// `npm run typecheck` for this workspace: the Tauri config and capability files must parse and agree.
import { readFileSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const root = join(dirname(fileURLToPath(import.meta.url)), '..', 'src-tauri');
const conf = JSON.parse(readFileSync(join(root, 'tauri.conf.json'), 'utf8'));
const caps = readdirSync(join(root, 'capabilities')).map((f) =>
    JSON.parse(readFileSync(join(root, 'capabilities', f), 'utf8')),
);
const labels = new Set((conf.app?.windows ?? []).map((w) => w.label ?? 'main'));
for (const c of caps) {
    for (const w of c.windows ?? [])
        if (!labels.has(w)) throw new Error(`capability ${c.identifier}: no window '${w}'`);
    for (const p of c.permissions ?? []) {
        if (typeof p === 'string' && /^(fs|shell|http|opener):/.test(p))
            throw new Error(`capability ${c.identifier}: blanket permission ${p}`);
    }
}
// The standalone save editor overlay must only declare windows the capabilities know.
const save = JSON.parse(readFileSync(join(root, 'tauri.save.conf.json'), 'utf8'));
for (const w of save.app?.windows ?? [])
    if (!caps.some((c) => (c.windows ?? []).includes(w.label ?? 'main')))
        throw new Error(`tauri.save.conf.json: window '${w.label}' has no capability`);
console.log(`tauri config ok (${conf.productName}, ${caps.length} capability file(s))`);

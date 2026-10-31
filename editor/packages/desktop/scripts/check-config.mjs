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
console.log(`tauri config ok (${conf.productName}, ${caps.length} capability file(s))`);

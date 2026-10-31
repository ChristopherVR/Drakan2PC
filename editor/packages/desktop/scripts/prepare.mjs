// Generates src-tauri/icons from app-icon.svg when missing. Icons are build output (binary files are not tracked).
import { existsSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
if (!existsSync(join(root, 'src-tauri', 'icons', 'icon.ico'))) {
    const r = spawnSync('npx', ['tauri', 'icon', 'app-icon.svg', '-o', 'src-tauri/icons'], {
        cwd: root,
        stdio: 'inherit',
        shell: true,
    });
    if (r.status !== 0) process.exit(r.status ?? 1);
}

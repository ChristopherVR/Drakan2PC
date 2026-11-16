import { fileURLToPath } from 'node:url';
import { defineConfig, searchForWorkspaceRoot } from 'vite';

// The OotF converter loads the authored Blender models from the repository's build kit (src/editor/ootfconvert.ts).
const AUTHORED_MODELS = fileURLToPath(new URL('../../../scripts/buildtool/kit/models', import.meta.url));

export default defineConfig({
    base: './',
    worker: { format: 'es' },
    server: { fs: { allow: [searchForWorkspaceRoot(process.cwd()), AUTHORED_MODELS] } },
    build: {
        target: 'es2022',
        chunkSizeWarningLimit: 900,
        // Two applications: the level/mod editor (index.html) and the save game editor (save.html).
        rolldownOptions: { input: { main: 'index.html', save: 'save.html' } },
    },
});

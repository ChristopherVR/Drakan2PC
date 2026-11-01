import { defineConfig } from 'vite';

export default defineConfig({
    base: './',
    worker: { format: 'es' },
    build: {
        target: 'es2022',
        chunkSizeWarningLimit: 900,
        // Two applications: the level/mod editor (index.html) and the save game editor (save.html).
        rolldownOptions: { input: { main: 'index.html', save: 'save.html' } },
    },
});

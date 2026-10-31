// A `StoryFs` over the opened disc (an `Iso9660` on the UI thread: `Iso9660.open(blobSource(file))`), for the dialogue and
// cinematics editors. Paths are language relative, `LEVELS\LEVEL1ON\LEVEL1ON.SSD`.

import type { Iso9660, StoryFsList } from '@drakan/formats';

export function isoStoryFs(iso: Iso9660, language: string): StoryFsList {
    const norm = (p: string): string => `${language}/${p.split(String.fromCharCode(92)).join('/')}`;
    return {
        async read(path) {
            const e = await iso.find(norm(path));
            return e && !e.isDir ? iso.read(e) : null;
        },
        async stems() {
            const out: string[] = [];
            for (const d of await iso.list(`${language}/LEVELS`)) {
                if (!d.isDir) continue;
                const files = await iso.list(`${language}/LEVELS/${d.name}`);
                const names = new Set(files.map((f) => f.name.toUpperCase()));
                if (names.has(`${d.name.toUpperCase()}.SSD`) || names.has(`${d.name.toUpperCase()}.SDB`))
                    out.push(d.name.toUpperCase());
            }
            return out.sort();
        },
    };
}

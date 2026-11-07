// The Dialogue and Cinematics editors. `createContentPanels` builds the session and both panels without registering
// them (the Story area hosts them, `register.ts`); `initContentEditors` registers them as separate dock tabs for hosts
// that do not use the area.

import type { ModMeta } from '@drakan/editor-core';
import type { StoryFsList } from '@drakan/formats';
import { registerPanel } from '../editor/registry.ts';
import type { Panel } from '../editor/ctx.ts';
import { ContentSession, type ContentHost } from './content.ts';
import { createCinematicsPanel, type CinematicsPanel } from './cinematics.ts';
import { createDialoguePanel } from './dialogue.ts';

export interface ContentEditors {
    session: ContentSession;
    dialogue: Panel;
    cinematics: CinematicsPanel;
    /** The disc was opened or closed: forget what was read from the previous one. */
    setDisc(open: boolean, language?: string, keepEdits?: boolean): void;
}

export interface ContentEditorsHost {
    /** Files of the open disc's language tree (null without a disc). */
    fs(): StoryFsList | null;
    target(): ModMeta['target'] | null;
    status(msg: string, bad?: boolean): void;
    play?(stem: string, id: number): Promise<boolean>;
    stop?(): void;
    placed?: ContentHost['placed'];
    showPath?: ContentHost['showPath'];
    converted?: ContentHost['converted'];
    /** Pack id / name used by the export buttons. */
    packId?: string;
    /** Adds the Cut scenes list's "New cut scene" button (`register.ts` places one at the camera target). */
    newCutscene?: () => void;
}

export function createContentPanels(host: ContentEditorsHost): ContentEditors {
    let language = 'ENGLISH';
    const session: ContentSession = new ContentSession({
        fs: () => host.fs(),
        language: () => language,
        target: host.target,
        status: host.status,
        play: host.play,
        stop: host.stop,
        placed: host.placed,
        converted: host.converted,
        showPath: host.showPath,
    });
    const meta = (): ModMeta | null => {
        const target = host.target();
        return target ? { id: host.packId ?? 'story-content', name: 'Story content', version: '1.0.0', target } : null;
    };
    return {
        session,
        dialogue: createDialoguePanel(session, meta),
        cinematics: createCinematicsPanel(session, meta, host.newCutscene),
        setDisc(_open, l, keepEdits) {
            if (l) language = l;
            session.reset(keepEdits);
        },
    };
}

export function initContentEditors(host: ContentEditorsHost): ContentEditors {
    const c = createContentPanels(host);
    registerPanel({ id: 'story-dialogue', title: 'Dialogue', icon: 'edit', panel: c.dialogue });
    registerPanel({ id: 'story-cinematics', title: 'Cinematics', icon: 'history', panel: c.cinematics });
    return c;
}

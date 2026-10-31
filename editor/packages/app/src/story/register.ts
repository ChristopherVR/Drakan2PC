// Registers the story editors as ONE dock tab, "Story", with Strings, Quests, Dialogue, Cinematics and Story pack inside
// (`storyarea.ts`), plus a command per editor. `main.ts` calls `initStoryEditors` once and `setDisc` when a disc opens.
// The panels read the disc lazily through the worker.

import type { DiscProfile } from '@drakan/formats';
import type { ModMeta } from '@drakan/editor-core';
import type { Ctx } from '../editor/ctx.ts';
import { allCommands, registerCommand, registerPanel } from '../editor/registry.ts';
import { createQuestsPanel } from './quests.ts';
import { createStoryPackPanel } from './packpanel.ts';
import { createStringsPanel } from './strings.ts';
import { createContentPanels, type ContentEditors } from './contentregister.ts';
import { createStoryArea, type StoryArea } from './storyarea.ts';
import { createPathLayer } from './pathview.ts';
import { placedStoryObjects } from './placed.ts';
import { notifyExitEdit, setExitEditHost } from './exitbridge.ts';
import { StorySession, type Call } from './session.ts';
import './story.css';

export interface StoryEditors {
    session: StorySession;
    content: ContentEditors;
    area: StoryArea;
    /** A disc was opened (or closed): forget what was read from the previous one. */
    setDisc(profile: DiscProfile | null, language?: string): void;
}

export interface StoryEditorsHost {
    call: Call;
    setStatus: (msg: string, bad?: boolean) => void;
    /** The level editor's context: placed handlers / cut scene players and the viewport for the camera path. */
    ctx?: () => Ctx;
    /** Sound playback for subtitles and cut scene sounds (the RefService's). */
    play?: (stem: string, id: number) => Promise<boolean>;
    stop?: () => void;
}

const AREA_TABS = [
    ['strings', 'Strings'],
    ['quests', 'Quests'],
    ['dialogue', 'Dialogue'],
    ['cinematics', 'Cinematics'],
    ['pack', 'Story pack'],
] as const;

export function initStoryEditors(host: StoryEditorsHost): StoryEditors {
    let profile: DiscProfile | null = null;
    const target = (): ModMeta['target'] | null => {
        if (!profile || (!profile.profile && !profile.hash)) return null;
        return {
            ...(profile.profile ? { profile: profile.profile } : {}),
            ...(profile.hash ? { hash: profile.hash } : {}),
        };
    };
    const session = new StorySession({ call: host.call, target, status: host.setStatus });
    const path = host.ctx ? createPathLayer({ view: () => host.ctx!().view() }) : null;
    const content = createContentPanels({
        fs: () =>
            profile
                ? {
                      read: (p) => host.call('readStoryFile', p).then((b) => b ?? null),
                      stems: () => host.call('listStoryStems'),
                  }
                : null,
        target,
        status: host.setStatus,
        ...(host.play ? { play: host.play } : {}),
        ...(host.stop ? { stop: host.stop } : {}),
        ...(host.ctx
            ? {
                  placed: () => {
                      const c = host.ctx!();
                      return placedStoryObjects(c.doc(), c.schemas());
                  },
              }
            : {}),
        ...(path ? { showPath: (pts) => path.show(pts) } : {}),
    });
    const area = createStoryArea([
        { id: 'strings', title: 'Strings', panel: createStringsPanel(session) },
        { id: 'quests', title: 'Quests', panel: createQuestsPanel(session) },
        { id: 'dialogue', title: 'Dialogue', panel: content.dialogue },
        { id: 'cinematics', title: 'Cinematics', panel: content.cinematics },
        { id: 'pack', title: 'Story pack', panel: createStoryPackPanel(session) },
    ]);
    registerPanel({
        id: 'story',
        title: 'Story',
        icon: 'edit',
        panel: area.panel,
        command: {
            title: 'Show Story editors',
            keys: 'Ctrl+Shift+Y',
            keywords: 'strings quests dialogue subtitles cut scene cinematics conversation journal story',
            hint: 'Edit strings, quests, dialogue and cut scenes and export them as a mod',
        },
    });
    for (const [id, title] of AREA_TABS) {
        registerCommand({
            id: `story.${id}`,
            title: `Story: ${title}`,
            category: 'Panels',
            keywords: 'story',
            run: () => {
                area.select(id);
                allCommands()
                    .find((c) => c.id === 'panel.story')
                    ?.run();
            },
        });
    }
    // Exit objects in the Logic panel write their destination strings through the session.
    setExitEditHost({
        ensure: () => session.ensure().then(() => undefined),
        text: (file, id) => session.str(file, id)?.text[0],
        set: (file, id, text) => session.setExitDestination(file, id, text),
        changed: (file, id) => session.isChanged(file, id),
    });
    session.subscribe(notifyExitEdit);
    return {
        session,
        content,
        area,
        setDisc(p, language) {
            profile = p;
            session.reset();
            content.setDisc(!!p, language);
            path?.show(null);
        },
    };
}

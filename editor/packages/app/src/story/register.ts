// Registers the story editors as ONE dock tab, "Story", with Strings, Quests, Dialogue, Cinematics and Story pack inside
// (`storyarea.ts`), plus a command per editor. `main.ts` calls `initStoryEditors` once and `setDisc` when a disc opens.
// The panels read the disc lazily through the worker.

import type { DiscProfile } from '@drakan/formats';
import { isEmptyStory, type ModMeta, type ProjectStory } from '@drakan/editor-core';
import type { Ctx } from '../editor/ctx.ts';
import { allCommands, registerCommand, registerPanel } from '../editor/registry.ts';
import { createQuestsPanel } from './quests.ts';
import { createStoryPackPanel } from './packpanel.ts';
import { createStringsPanel } from './strings.ts';
import { createContentPanels, type ContentEditors } from './contentregister.ts';
import { createNewCutscenePanel, createNewDialoguePanel, createQuestPlacePanel } from './compose.ts';
import { createStoryArea, type StoryArea } from './storyarea.ts';
import { createConversationPanel } from './conversation.ts';
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
    /** Every story edit as plain JSON for the project file (null when there are none). */
    exportProject(): ProjectStory | null;
    /** Restore the edits from a project file (null clears them). */
    importProject(s: ProjectStory | null): void;
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
    ['new-dialogue', 'New dialogue'],
    ['new-cutscene', 'New cut scene'],
    ['quest-marker', 'Quest marker'],
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
        ...(path ? { showPath: (pts) => path.show(pts), framePath: () => path.frame() } : {}),
    });
    // One workspace, four places: Quests, Conversations, Cut scenes, and Text and export. The old tab ids stay as
    // commands (`story.new-dialogue` ...) and open the right place inside.
    const newDialogue = createNewDialoguePanel(content.session, host.ctx);
    const conversations = createStoryArea(
        [
            {
                id: 'conversation',
                title: 'Conversations',
                panel: createConversationPanel(content.session, () => conversations.select('new-dialogue')),
            },
            { id: 'new-dialogue', title: 'Write a new line', panel: newDialogue },
            { id: 'dialogue', title: 'All lines', panel: content.dialogue },
        ],
        'conversation',
        true,
    );
    const cutscenes = createStoryArea(
        [
            { id: 'cinematics', title: 'Timeline', panel: content.cinematics },
            { id: 'new-cutscene', title: 'Make a cut scene', panel: createNewCutscenePanel(content.session, host.ctx) },
        ],
        'cinematics',
        true,
    );
    const quests = createStoryArea(
        [
            { id: 'quests', title: 'Quests', panel: createQuestsPanel(session, host.ctx) },
            { id: 'quest-marker', title: 'Place a quest marker', panel: createQuestPlacePanel(session, host.ctx) },
        ],
        'quests',
        true,
    );
    const text = createStoryArea(
        [
            { id: 'strings', title: 'Texts', panel: createStringsPanel(session) },
            { id: 'pack', title: 'Export', panel: createStoryPackPanel(session) },
        ],
        'strings',
        true,
    );
    const WHERE: Record<string, [string, StoryArea]> = {
        quests: ['quests', quests],
        'quest-marker': ['quests', quests],
        dialogue: ['conversations', conversations],
        'new-dialogue': ['conversations', conversations],
        cinematics: ['cutscenes', cutscenes],
        'new-cutscene': ['cutscenes', cutscenes],
        strings: ['text', text],
        pack: ['text', text],
    };
    // Showing a tab reads what it needs from the disc, so no page asks for a Load click first.
    const readDisc = (): void => {
        void session.ensure().catch(() => undefined);
        // the story folder of the open level, so the editors start on the level being worked on
        let stem: string | undefined;
        try {
            stem = host
                .ctx?.()
                .src()
                ?.file.replace(/\.LVL$/i, '');
        } catch {
            stem = undefined;
        }
        void content.session.ensure(content.session.story ? undefined : stem).catch(() => undefined);
    };
    const area = createStoryArea([
        { id: 'quests', title: 'Quests', panel: quests.panel, onShow: readDisc },
        { id: 'conversations', title: 'Conversations', panel: conversations.panel, onShow: readDisc },
        { id: 'cutscenes', title: 'Cut scenes', panel: cutscenes.panel, onShow: readDisc },
        { id: 'text', title: 'Text and export', panel: text.panel, onShow: readDisc },
    ]);
    const selectTab = (id: string): void => {
        const w = WHERE[id];
        if (!w) return area.select(id);
        area.select(w[0]);
        w[1].select(id);
    };
    registerPanel({
        id: 'story',
        title: 'Story',
        icon: 'edit',
        panel: area.panel,
        command: {
            title: 'Show Story editors',
            keys: 'Ctrl+Shift+Y',
            keywords:
                'strings quests dialogue subtitles cut scene cinematics conversation journal story new create quest marker',
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
                selectTab(id);
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
        addDestination: (file, fromId, text) => session.addDestinationString(file, fromId, text),
    });
    // Dialogue lines and cut scenes made in the content session ship in the same pack as strings and quests.
    session.addChangeSource({
        changes: () => content.session.changes(),
        subscribe: (fn) => content.session.subscribe(fn),
        count: () => content.session.count(),
    });
    session.subscribe(notifyExitEdit);
    return {
        session,
        content,
        area: { ...area, select: selectTab },
        exportProject() {
            const s: ProjectStory = { ...session.exportState(), content: content.session.exportState() };
            return isEmptyStory(s) ? null : s;
        },
        importProject(s) {
            session.importState(s);
            content.session.importState(s?.content ?? null);
        },
        setDisc(p, language) {
            // Opening a disc after a project's story edits were restored (nothing could be edited without a disc)
            // keeps them; closing or switching discs drops them.
            const keep = !!p && profile === null;
            profile = p;
            session.reset(keep);
            content.setDisc(!!p, language, keep);
            path?.show(null);
        },
    };
}

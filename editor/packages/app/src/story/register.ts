// Registers the story editors as ONE dock tab, "Story", with Strings, Quests, Dialogue, Cinematics and Story pack inside
// (`storyarea.ts`), plus a command per editor. `main.ts` calls `initStoryEditors` once and `setDisc` when a disc opens.
// The panels read the disc lazily through the worker.

import type { DiscProfile } from '@drakan/formats';
import { isEmptyStory, type ModMeta, type ProjectStory } from '@drakan/editor-core';
import type { Ctx } from '../editor/ctx.ts';
import { registerCommand, registerContextItem, registerPanel, runCommand } from '../editor/registry.ts';
import { createQuestsPanel } from './quests.ts';
import { createStoryPackPanel } from './packpanel.ts';
import { createStringsPanel } from './strings.ts';
import { createContentPanels, type ContentEditors } from './contentregister.ts';
import { createNewCutscenePanel, createNewDialoguePanel, createQuestPlacePanel } from './compose.ts';
import { createStoryArea, type StoryArea } from './storyarea.ts';
import { createConversationPanel } from './conversation.ts';
import { createPathLayer } from './pathview.ts';
import { placedStoryObjects } from './placed.ts';
import { cameraPath, sequenceDuration } from '@drakan/formats';
import { fieldValueOf, readVendor, resourceParts, vendorSchemaOf } from '@drakan/editor-core';
import { createStoryView, type StoryView } from './storyview.ts';
import type { StoryFocus, StoryMapSource } from './storymap.ts';
import {
    addQuestArea,
    cameraTarget,
    levelStem,
    makeCutsceneAt,
    openLevelStory,
    questObjects,
    report,
} from './storyactions.ts';
import { SYSTEM_XDB } from './session.ts';
import { notifyExitEdit, setExitEditHost } from './exitbridge.ts';
import { StorySession, type Call } from './session.ts';
import './story.css';

export interface StoryEditors {
    session: StorySession;
    content: ContentEditors;
    /** Cut scenes, shops, quest areas and conversations drawn in the level view (null without a level editor). */
    view: StoryView | null;
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
        ...(host.ctx ? { newCutscene: () => newCutsceneAt(null) } : {}),
    });
    const conversationPanel = createConversationPanel(content.session, () => conversations.select('new-dialogue'));
    const questsPanel = createQuestsPanel(session, host.ctx);
    // One workspace, four places: Quests, Conversations, Cut scenes, and Text and export. The old tab ids stay as
    // commands (`story.new-dialogue` ...) and open the right place inside.
    const newDialogue = createNewDialoguePanel(content.session, host.ctx);
    const conversations = createStoryArea(
        [
            {
                id: 'conversation',
                title: 'Conversations',
                panel: conversationPanel,
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
            {
                id: 'new-cutscene',
                title: 'Build from camera points',
                panel: createNewCutscenePanel(content.session, host.ctx),
            },
        ],
        'cinematics',
        true,
    );
    const quests = createStoryArea(
        [
            { id: 'quests', title: 'Quests', panel: questsPanel },
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
        // the story folder of the open level (per chapter: Surdana's is LEVEL1ON), so the editors start on it
        if (content.session.story) return;
        const c = host.ctx?.();
        void (c ? openLevelStory(c, content.session) : content.session.ensure()).catch(() => undefined);
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
    // ---- the story in the level view ----------------------------------------------------------------------------------
    const storyView = host.ctx ? installStoryView(host.ctx) : null;
    function installStoryView(ctx: () => Ctx): StoryView {
        const source = (): StoryMapSource => {
            const c = ctx();
            const env = c.schemas();
            const sameLevel = !!content.session.story && content.session.stem === levelStem(c, content.session.stems);
            const rows = session.data ? session.questRows() : [];
            let lineSpeakers: Map<number, string> | undefined;
            return {
                sequence: (id) => {
                    const s = sameLevel ? content.session.sequence(id) : null;
                    if (!s) return null;
                    return {
                        id,
                        name: s.name,
                        actors: s.actors.map((a) => ({ name: a.name, ref: a.ref })),
                        camera: cameraPath(s).keys.map((k): [number, number, number] => [k.pos[0], k.pos[1], k.pos[2]]),
                        seconds: sequenceDuration(s),
                    };
                },
                questTitle: (classId) => {
                    const r = rows.find((x) => x.classId === classId);
                    if (!r) return null;
                    const id = r.draft.kind === 'map' ? r.draft.mapTextId || r.draft.journalId : r.draft.journalId;
                    const t = id ? (session.text(SYSTEM_XDB, id, 0).split('\n')[0] ?? '').trim() : '';
                    return t || r.name;
                },
                vendor: (o) => {
                    const schema = env ? vendorSchemaOf(o, env) : null;
                    return schema && env ? { items: readVendor(o, schema, env).entries.length } : null;
                },
                conversation: (o) => {
                    const classId = resourceParts(o.resource).classId;
                    const x = sameLevel ? content.session.story?.handlers.find((y) => y.classId === classId) : null;
                    if (!x || !env) return null;
                    lineSpeakers ??= new Map(content.session.lines().map((l) => [l.classId, l.speaker]));
                    const first = fieldValueOf(o, env, 'Beginning Dialog Exchange');
                    const refs = Array.isArray(first) ? first : [];
                    const speakers = [
                        ...new Set(
                            refs.map((r) => lineSpeakers!.get(r & 0xffff) ?? '').filter((w) => w && !/^rynn$/i.test(w)),
                        ),
                    ].slice(0, 2);
                    return { name: x.name, hasQuest: x.hasQuest, speakers };
                },
            };
        };
        const v = createStoryView({ ctx, source, open: (f) => openFocus(f) });
        let pending = 0;
        const refreshSoon = (): void => {
            if (pending) return;
            pending = requestAnimationFrame(() => {
                pending = 0;
                v.refresh();
            });
        };
        content.session.subscribe(refreshSoon);
        session.subscribe(refreshSoon);
        // The level view's events reach the Story area panel (every registered panel gets them); the overlay follows.
        const p = area.panel;
        const wrap =
            (f: (() => void) | undefined, extra: () => void): (() => void) =>
            () => {
                f?.();
                extra();
            };
        p.onChange = wrap(p.onChange?.bind(p), refreshSoon);
        p.onVisibility = wrap(p.onVisibility?.bind(p), refreshSoon);
        p.onLoad = wrap(p.onLoad?.bind(p), () => {
            refreshSoon();
            readLevelStory(ctx());
        });
        return v;
    }
    /** Read the open level's sequences and the quest titles so the overlay can name and draw them. */
    function readLevelStory(c: Ctx): void {
        if (!profile) return;
        void openLevelStory(c, content.session).catch(() => undefined);
        void session.ensure().catch(() => undefined);
    }
    const showStory = (): void => void runCommand('panel.story');
    function openFocus(f: StoryFocus): void {
        switch (f.kind) {
            case 'sequence':
                selectTab('cinematics');
                showStory();
                content.cinematics.focus(f.id);
                return;
            case 'vendor':
                host.ctx?.().view().select([f.object]);
                runCommand('panel.vendors');
                return;
            case 'quest':
                selectTab('quests');
                showStory();
                void session.ensure().then(
                    () => questsPanel.focus(f.classId),
                    () => undefined,
                );
                return;
            case 'conversation':
                area.select('conversations');
                conversations.select('conversation');
                showStory();
                conversationPanel.focus(f.classId);
                return;
        }
    }
    function newCutsceneAt(at: [number, number, number] | null, actorId?: number): void {
        if (!host.ctx) return;
        const c = host.ctx();
        const actor = actorId !== undefined ? c.doc().get(actorId) : undefined;
        const p = at ?? (actor ? actor.transform.pos : cameraTarget(c));
        const made = makeCutsceneAt(c, content.session, [p[0], p[1], p[2]], actor);
        report(c, made);
        void made.then(
            (r) => {
                storyView?.refresh();
                openFocus({ kind: 'sequence', id: r.sequence });
            },
            () => undefined,
        );
    }
    if (host.ctx) installStoryPlacing(host.ctx);
    function installStoryPlacing(ctx: () => Ctx): void {
        const editable = (): boolean => {
            try {
                const c = ctx();
                return !!c.schemas() && c.doc().size > 0 && c.view().policy.structural;
            } catch {
                return false;
            }
        };
        const noLevel = (): string | null => (editable() ? null : 'Open a level from the disc first');
        const selectedQuest = (): ReturnType<typeof questObjects>[number] | undefined => {
            const c = ctx();
            const q = questObjects(c, [...c.view().selection]);
            return q.length === 1 ? q[0] : undefined;
        };
        const noQuest = (): string | null =>
            noLevel() ?? (selectedQuest() ? null : 'Select one quest first (click its ! badge in the level)');
        const questArea = (kind: 'start' | 'finish', at: [number, number, number] | null): void => {
            const c = ctx();
            const q = selectedQuest();
            if (!q) return;
            try {
                addQuestArea(c, q, at ?? cameraTarget(c), kind);
                c.toast(`Quest ${kind} area added for ${q.class}.`, 'ok');
            } catch (e) {
                c.toast(e instanceof Error ? e.message : String(e), 'error');
            }
        };
        registerContextItem({
            id: 'story.cutscene.here',
            label: 'Add cut scene here',
            icon: 'camera',
            where: 'world',
            unavailable: noLevel,
            run: (t) => newCutsceneAt(t.ground),
        });
        registerContextItem({
            id: 'story.cutscene.object',
            label: 'Add cut scene at this object',
            icon: 'camera',
            where: 'object',
            when: (t) => t.ids.length === 1,
            unavailable: noLevel,
            run: (t) => newCutsceneAt(null, t.ids[0]),
        });
        for (const kind of ['start', 'finish'] as const) {
            registerContextItem({
                id: `story.quest.${kind}-here`,
                label: `Add quest ${kind} area here`,
                icon: 'trigger',
                where: 'world',
                unavailable: noQuest,
                run: (t) => questArea(kind, t.ground),
            });
            registerCommand({
                id: `story.place.quest-${kind}`,
                title: `Add quest ${kind} area`,
                category: 'Story',
                icon: 'trigger',
                keywords: 'quest journal objective volume box',
                hint: `A box that ${kind}s the selected quest when the player walks in, where the camera looks`,
                enabled: () => !noQuest(),
                run: () => questArea(kind, null),
            });
        }
        const noShop = 'Not yet: a new shop needs the character who opens it, and that link is not decoded';
        registerContextItem({
            id: 'story.vendor.here',
            label: 'Add shop here',
            icon: 'item',
            where: 'world',
            unavailable: () => noShop,
            run: () => undefined,
        });
        registerCommand({
            id: 'story.place.cutscene',
            title: 'Add cut scene',
            category: 'Story',
            icon: 'camera',
            keywords: 'cinematic sequence camera new place',
            hint: 'A cut scene player, the box that starts it and a camera move, where the camera looks (or at the selected object)',
            enabled: editable,
            run: () => {
                const sel = [...ctx().view().selection];
                newCutsceneAt(null, sel.length === 1 ? sel[0] : undefined);
            },
        });
        registerCommand({
            id: 'story.place.vendor',
            title: 'Add shop',
            category: 'Story',
            icon: 'item',
            hint: noShop,
            enabled: () => false,
            run: () => undefined,
        });
    }

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
                runCommand('panel.story');
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
        view: storyView,
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
            storyView?.refresh();
            if (p && host.ctx) readLevelStory(host.ctx());
        },
    };
}

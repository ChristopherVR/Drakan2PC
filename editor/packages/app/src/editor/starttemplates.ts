import { WORKFLOWS, type StepFacts, type TaskId, type WorkflowStep } from '@drakan/editor-core';

/**
 * The templates of the start page ("What do you want to make?"), in the spirit of Unreal's Project Browser: each one
 * says what it makes, what you will do, what it needs (nothing, your disc, or a level of your disc), and what its
 * button does first. After it starts, the checklist shows its steps. Pure data and planning: no DOM here.
 */
export type TemplateId =
    | 'campaign'
    | 'build-campaign'
    | 'arena'
    | 'newlevel'
    | 'puzzle'
    | 'cutscene'
    | 'dialogue'
    | 'quest'
    | 'vendor'
    | 'menus'
    | 'story'
    | 'save'
    | 'ootf';

export type TemplateGroup = 'levels' | 'story' | 'other';

export interface StartTemplate {
    id: TemplateId;
    title: string;
    /** One line on the card. */
    line: string;
    /** Two short sentences in the details pane. */
    about: string;
    group: TemplateGroup;
    /** nothing: works without a disc; disc: needs the disc image; level: also needs a level of the disc open. */
    needs: 'nothing' | 'disc' | 'level';
    /** Mod project task it files the work under (null: no project, like a save edit). */
    task: TaskId | null;
    /** The steps of the checklist after it starts. */
    steps: WorkflowStep[];
    /** Primary button once everything it needs is there. */
    button: string;
    /** Heading of the level picker. */
    pickTitle?: string;
    /** Level names (substrings) the picker marks as a good start. */
    suggest?: string[];
    /** Only offered when this command exists (a feature registered by another module). */
    whenCommand?: string;
    /** Name of a new project started from it. */
    projectName?: string;
    /** Where it lands, as a short name with its article ("the Level Editor"). */
    place: string;
    /** Where it lands, as the end of "Next: Surdana opens in ..." (the place and what is shown there). */
    lands: string;
}

export const GROUP_TITLES: Record<TemplateGroup, string> = {
    levels: 'Levels and maps',
    story: 'Story and characters',
    other: 'Saves and imports',
};

const levelOpenStep: WorkflowStep = {
    id: 'open',
    title: 'Open a level',
    text: 'The start page lists the levels of your disc. Ctrl+P and the level name works too.',
    auto: (f) => f.levelOpen,
    action: { label: 'Choose level', command: 'project.chooseLevel' },
};
const playStep: WorkflowStep = {
    id: 'play',
    title: 'Try it in the game',
    text: 'Play (F5) starts the game in this level with your changes. In a browser, export and copy the zip instead.',
    auto: (f) => f.played,
    action: { label: 'Play', command: 'project.play' },
    guide: 'package',
};
const exportStep: WorkflowStep = {
    id: 'export',
    title: 'Export the mod',
    text: 'Writes a mod pack. Your game disc and its files are never changed.',
    auto: (f) => f.exported,
    action: { label: 'Export', command: 'file.export' },
    guide: 'package',
};

export const TEMPLATES: readonly StartTemplate[] = [
    {
        id: 'build-campaign',
        title: 'Make a campaign',
        line: 'Your own title card, linked levels and separate saves.',
        about: 'A step-by-step wizard takes you from a name and cover to an installed campaign. It uses copies, so Drakan stays unchanged.',
        group: 'levels',
        place: 'the campaign wizard',
        lands: 'the campaign wizard',
        needs: 'nothing',
        task: null,
        whenCommand: 'campaign.wizard',
        button: 'Make a campaign',
        steps: [
            { id: 'name', title: 'Name it and choose a cover', text: 'Players see these on Choose your game.' },
            { id: 'levels', title: 'Add copies of levels', text: 'Put the copies in play order.' },
            { id: 'exits', title: 'Link the exits and check the route', text: 'The wizard suggests links and fixes.' },
            {
                id: 'play',
                title: 'Play test the campaign',
                text: 'Choose it from the title screen in the desktop app.',
            },
            exportStep,
        ],
    },
    {
        id: 'campaign',
        title: 'Change a campaign level',
        line: 'Move, add or remove creatures and items in a level of the game.',
        about: 'Pick a level of the game and change what is in it. Your edits become a mod; the disc stays as it is.',
        group: 'levels',
        place: 'the Level Editor',
        lands: 'the Level Editor',
        needs: 'level',
        task: 'campaign',
        steps: WORKFLOWS.campaign.steps,
        button: 'Choose a level',
        pickTitle: 'Which level do you want to change?',
        projectName: 'My campaign mod',
    },
    {
        id: 'arena',
        title: 'Multiplayer arena',
        line: 'Spawn points and pickups on the terrain of a level.',
        about: 'A level of the game becomes a deathmatch or team arena. You place where players appear and what they can pick up.',
        group: 'levels',
        place: 'the Level Editor',
        lands: 'the Level Editor, with World Settings on the right',
        needs: 'level',
        task: 'multiplayer',
        steps: [
            {
                ...WORKFLOWS.multiplayer.steps[0]!,
                title: 'Start the map',
                text: 'World Settings, on the right, shows the level you picked. Give the map a name and press Start new map.',
            },
            ...WORKFLOWS.multiplayer.steps.slice(1),
        ],
        button: 'Choose the terrain',
        pickTitle: 'Which level should the arena be built on?',
        projectName: 'My multiplayer map',
    },
    {
        id: 'newlevel',
        title: 'New level from a copy',
        line: 'Start a new level as a copy of one of the game.',
        about: 'The copy gets its own name and its own place in your mod. The original level is never touched.',
        group: 'levels',
        place: 'the Level Editor',
        lands: 'the Level Editor',
        needs: 'disc',
        task: 'newlevel',
        steps: WORKFLOWS.newlevel.steps.map((s) =>
            s.id === 'edit'
                ? {
                      ...s,
                      text: 'The copy opens by itself. Click an object and press W to move it, or drag a class from the Content Browser into the world.',
                      guide: 'objects',
                  }
                : s,
        ),
        button: 'Choose the level to copy',
        pickTitle: 'Which level do you want to copy?',
        projectName: 'My new level',
    },
    {
        id: 'puzzle',
        title: 'Pressure plate puzzle',
        line: 'Plates on the floor that open a door when all are pressed.',
        about: 'The plates, a counter and the door link are made for you. You pick the door and how many plates.',
        group: 'levels',
        place: 'the Level Editor',
        lands: 'the Level Editor, with the pressure plate setup below the viewport',
        needs: 'level',
        task: 'campaign',
        steps: [
            levelOpenStep,
            {
                id: 'build',
                title: 'Set up the plates',
                text: 'Logic, below the viewport, shows the pressure plate setup. Choose the door and the number of plates, then add it.',
                auto: (f) => f.levelEdits,
                action: { label: 'Show the setup', command: 'add.puzzle' },
                guide: 'triggers',
            },
            playStep,
            exportStep,
        ],
        button: 'Choose a level',
        pickTitle: 'Which level gets the puzzle?',
        projectName: 'My puzzle mod',
    },
    {
        id: 'vendor',
        title: 'Shop and prices',
        line: 'What a shopkeeper sells, how many, and for how much.',
        about: 'Change the stock and prices of a shop. Surdana has a blacksmith, an alchemist and a mage.',
        group: 'levels',
        place: 'Vendors',
        lands: 'the Level Editor, with Vendors open on its first shop',
        needs: 'level',
        task: 'campaign',
        whenCommand: 'panel.vendors',
        steps: [
            levelOpenStep,
            {
                id: 'stock',
                title: 'Change the stock',
                text: 'In Vendors, pick a shop and change quantities and prices. Add an item by typing its name.',
                auto: (f) => f.levelEdits || f.storyChanges > 0,
                action: { label: 'Open Vendors', command: 'panel.vendors' },
                guide: 'vendors',
            },
            playStep,
            exportStep,
        ],
        button: 'Choose a level',
        pickTitle: 'Which level has the shop?',
        suggest: ['Surdana'],
        projectName: 'My shop mod',
    },
    {
        id: 'cutscene',
        title: 'Cut scene',
        line: 'A camera path that plays when the player walks in.',
        about: 'Set a few camera points and what happens at each. A trigger and a player for the scene are placed and linked for you.',
        group: 'story',
        place: 'the Level Editor',
        lands: 'the Level Editor, with Cut scenes below the viewport',
        needs: 'level',
        task: 'campaign',
        steps: [
            levelOpenStep,
            {
                id: 'make',
                title: 'Make the scene',
                text: 'Cut scenes opens below the viewport. Name the scene, add camera points and steps, then create it.',
                auto: (f) => f.storyChanges > 0 || f.levelEdits,
                action: { label: 'Make a cut scene', command: 'add.cutscene' },
                guide: 'story',
            },
            playStep,
            exportStep,
        ],
        button: 'Choose a level',
        pickTitle: 'Which level is the cut scene in?',
        projectName: 'My cut scene',
    },
    {
        id: 'dialogue',
        title: 'Dialogue line',
        line: 'Something a character says, with a subtitle.',
        about: 'Add a line to a conversation of the level: who says it, the subtitle, and a recording from the disc.',
        group: 'story',
        place: 'the Level Editor',
        lands: 'the Level Editor, with Write a new line below the viewport',
        needs: 'level',
        task: 'campaign',
        steps: [
            levelOpenStep,
            {
                id: 'write',
                title: 'Write the line',
                text: 'In Write a new line, pick the conversation, type the speaker and subtitle, choose a sound, and add it.',
                auto: (f) => f.storyChanges > 0 || f.levelEdits,
                action: { label: 'Write a new line', command: 'add.dialogue' },
                guide: 'story',
            },
            playStep,
            exportStep,
        ],
        button: 'Choose a level',
        pickTitle: 'Which level is the conversation in?',
        projectName: 'My dialogue mod',
    },
    {
        id: 'quest',
        title: 'Quest',
        line: 'A title, the quest text and a marker in the world.',
        about: 'Write a quest and place its marker in the level. You choose what starts it and what completes it.',
        group: 'story',
        place: 'the Level Editor',
        lands: 'the Level Editor, with Place a quest marker below the viewport',
        needs: 'level',
        task: 'campaign',
        steps: [
            levelOpenStep,
            {
                id: 'place',
                title: 'Write and place the quest',
                text: 'In Place a quest marker, below the viewport, type the title and text and place the marker.',
                auto: (f) => f.storyChanges > 0 || f.levelEdits,
                action: { label: 'Place a quest marker', command: 'add.quest' },
                guide: 'story',
            },
            playStep,
            exportStep,
        ],
        button: 'Choose a level',
        pickTitle: 'Which level is the quest in?',
        projectName: 'My quest mod',
    },
    {
        id: 'story',
        title: 'Story texts',
        line: 'Change any text, quest, conversation or cut scene of the game.',
        about: 'Browse every piece of text and every quest and conversation on your disc, and change the words.',
        group: 'story',
        place: 'the Story editor',
        lands: 'the Story editor',
        needs: 'disc',
        task: 'story',
        steps: [
            {
                id: 'open',
                title: 'Pick what to change',
                text: 'Story has Quests, Conversations, Cut scenes, and Text and export along the top. Load them from your disc when it asks.',
                action: { label: 'Open Story', command: 'panel.story' },
                guide: 'story',
            },
            ...WORKFLOWS.story.steps.slice(1),
        ],
        button: 'Open the Story editor',
        projectName: 'My story mod',
    },
    {
        id: 'menus',
        title: 'Menu page',
        line: 'Rename, reorder and restyle menu entries.',
        about: 'Change how the menus of the game read and look.',
        group: 'story',
        place: 'the Menus editor',
        lands: 'the Menus editor',
        needs: 'disc',
        task: 'story',
        whenCommand: 'panel.menus',
        steps: [
            {
                id: 'open',
                title: 'Pick a menu page',
                text: 'The Menus editor lists the pages of the game menus.',
                action: { label: 'Open Menus', command: 'panel.menus' },
            },
            {
                id: 'change',
                title: 'Change the entries',
                text: 'Rename, reorder or restyle them. Changed entries are marked.',
            },
            exportStep,
        ],
        button: 'Open the Menus editor',
        projectName: 'My menu mod',
    },
    {
        id: 'save',
        title: 'Edit a save',
        line: 'A character, inventory, quests or the world of a .rsg save.',
        about: 'The Save Editor opens in its own window. Keep a copy of the save before you change it.',
        group: 'other',
        place: 'the Save Editor',
        lands: 'the Save Editor, in its own window',
        needs: 'nothing',
        task: null,
        steps: WORKFLOWS.save.steps,
        button: 'Open the Save Editor',
    },
    {
        id: 'ootf',
        title: 'Order of the Flame level',
        line: 'Convert a level of the older game that you own.',
        about: 'Point the importer at your own Drakan: Order of the Flame install; its levels become a mod for this game.',
        group: 'other',
        place: 'the importer',
        lands: 'the Order of the Flame importer',
        needs: 'disc',
        task: null,
        whenCommand: 'file.importOotf',
        steps: [
            {
                id: 'import',
                title: 'Choose the levels to convert',
                text: 'The importer reads your Order of the Flame install and lists its levels.',
                action: { label: 'Open the importer', command: 'file.importOotf' },
            },
            exportStep,
        ],
        button: 'Open the importer',
    },
];

export const templateById = (id: string): StartTemplate | undefined => TEMPLATES.find((t) => t.id === id);

/** Templates shown now: those whose feature is installed. */
export const availableTemplates = (has: (command: string) => boolean): StartTemplate[] =>
    TEMPLATES.filter((t) => !t.whenCommand || has(t.whenCommand));

export interface StartState {
    disc: boolean;
    levelOpen: boolean;
}

/**
 * What the primary button does, in order: ask for the disc image, show the level picker, then run the template.
 * A copy always picks (the level to copy from); a level template picks only when no level is open.
 */
export function startPlan(t: StartTemplate, s: StartState): ('disc' | 'pick' | 'run')[] {
    const plan: ('disc' | 'pick' | 'run')[] = [];
    if (t.needs !== 'nothing' && !s.disc) plan.push('disc');
    if (t.id === 'newlevel' || (t.needs === 'level' && (!s.disc || !s.levelOpen))) plan.push('pick');
    plan.push('run');
    return plan;
}

/** "Level 1A - Surdana" -> "Surdana" (buttons and lines name the place, not the disc number). */
export function shortLevelName(name: string): string {
    const m = /^Level\s+\S+\s*[-–]\s*(.+)$/.exec(name);
    return m ? m[1]!.trim() : name;
}

/** The level chosen in the picker, before its button is pressed. */
export interface PickedLevel {
    name: string;
}

/** The primary button's label for the current state: exactly what pressing it does. */
export function primaryLabel(t: StartTemplate, s: StartState, picked?: PickedLevel | null): string {
    const plan = startPlan(t, s);
    if (plan[0] === 'disc') return 'Open your disc to start';
    if (plan[0] === 'pick') {
        if (!picked) return t.button;
        const n = shortLevelName(picked.name);
        return t.id === 'newlevel' ? `Copy ${n}` : `Open ${n}`;
    }
    if (t.needs === 'level') return 'Start in the open level';
    return t.button;
}

/** One line under the button, starting with "Next:": what happens when it is pressed. */
export function primaryNote(t: StartTemplate, s: StartState, picked?: PickedLevel | null): string {
    const plan = startPlan(t, s);
    if (plan[0] === 'disc')
        return plan.includes('pick')
            ? 'Next: you choose your Drakan disc image (.iso), then a level.'
            : 'Next: you choose your Drakan disc image (.iso); the editor then carries on.';
    if (plan[0] === 'pick') {
        if (!picked) return 'Next: you choose a level of your disc.';
        const n = shortLevelName(picked.name);
        return t.id === 'newlevel'
            ? `Next: a copy of ${n} opens in the Level Editor under its own name.`
            : `Next: ${n} opens in ${t.lands}.`;
    }
    if (t.id === 'save') return 'Next: the Save Editor opens in its own window.';
    if (t.needs === 'level') return `Next: works in the level that is open, in ${t.lands}.`;
    return `Next: ${t.lands} opens and a checklist shows each step.`;
}

export interface FlowStep {
    label: string;
    state: 'done' | 'current' | 'todo';
}

/**
 * The steps from the start page into the editor, for the line above the tiles: what you are doing now and what
 * comes after (the template, the disc, the level, then the editor it opens).
 */
export function flowSteps(t: StartTemplate, s: StartState, phase: { picking: boolean }): FlowStep[] {
    const plan = startPlan(t, s);
    const labels: string[] = ['Choose what to make'];
    if (plan.includes('disc')) labels.push('Open your disc');
    if (plan.includes('pick')) labels.push(t.id === 'newlevel' ? 'Choose the level to copy' : 'Choose a level');
    labels.push(`Continue in ${t.place}`);
    const current = phase.picking ? labels.length - 2 : 0;
    return labels.map((label, i) => ({ label, state: i < current ? 'done' : i === current ? 'current' : 'todo' }));
}

/** What the template needs, as a short sentence for the details pane. */
export function needsText(t: StartTemplate): string {
    if (t.id === 'build-campaign') return 'Start with a name; open your disc image when you add levels.';
    if (t.needs === 'nothing') return 'Needs a save file (.rsg). Your disc is optional.';
    if (t.id === 'newlevel') return 'Needs your disc image and a level to copy.';
    if (t.needs === 'level') return 'Needs your disc image and one of its levels.';
    return 'Needs your disc image.';
}

/** Levels the picker marks as a good start for the template. */
export function suggestedLevels<L extends { name: string }>(t: StartTemplate, levels: readonly L[]): L[] {
    const hints = (t.suggest ?? []).map((s) => s.toLowerCase());
    return hints.length ? levels.filter((l) => hints.some((s) => l.name.toLowerCase().includes(s))) : [];
}

/** Whether each step is done: its own test, or a tick the user made. */
export function templateStepStates(t: StartTemplate, f: StepFacts, ticks: readonly string[]): boolean[] {
    return t.steps.map((s) => (s.auto ? s.auto(f) : false) || ticks.includes(s.id));
}

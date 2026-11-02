/**
 * The guided tours of the editor. Tour ids match the start templates (`starttemplates.ts`) where there is one, so the
 * start page can call `startTour(template.id)`; Help guide ids are aliases (`TOUR_ALIASES`). Text is plain: say what
 * the part is for and what to do with it, in a sentence or two. Targets are ids and `data-tour` attributes of the
 * editor, or a resolver that finds a button by its text when the button has neither.
 *
 * Adding a tour: write a `Tour` here (or in a feature's own module) and pass it to `addTours`. See
 * docs/journal/2026-11-02/guided-tours.md.
 */
import { projects } from '../project/store.ts';
import { runCommand } from '../registry.ts';
import { discIsOpen, levelIsOpen, selectedCount } from './events.ts';
import type { Tour, TourStep, TourTarget } from './model.ts';

// ---- finders --------------------------------------------------------------------------------------------------------

const shown = (el: Element): boolean => {
    const r = el.getBoundingClientRect();
    return r.width > 0 && r.height > 0;
};

/** The first visible element matching `sel` whose text (or aria-label / title) contains `text`. */
export function byText(sel: string, text: string | RegExp): TourTarget {
    const test = (s: string): boolean => (typeof text === 'string' ? s.includes(text) : text.test(s));
    return () => {
        for (const el of document.querySelectorAll(sel)) {
            if (!shown(el)) continue;
            const parts = [el.textContent, el.getAttribute('aria-label'), el.getAttribute('title')];
            if (parts.some((p) => !!p && test(p.trim()))) return el;
        }
        return null;
    };
}

/** The closest `up` ancestor of a target (a whole form around its button). */
function around(t: TourTarget, up: string): TourTarget {
    return () => {
        const el = typeof t === 'string' ? document.querySelector(t) : t();
        return el?.closest(up) ?? el ?? null;
    };
}

/** The target is on screen now. */
export function exists(t: TourTarget): boolean {
    try {
        if (typeof t === 'function') return !!t();
        for (const el of document.querySelectorAll(t)) if (shown(el)) return true;
    } catch {
        // bad selector
    }
    return false;
}

const project = () => projects.get();
const hasProject = (): boolean => !!project();
const hasCopy = (): boolean => !!project()?.levels.some((l) => l.copyOf);
const tabActive = (id: string): boolean => !!document.querySelector(`#tab-ed-${id}.active`);

// ---- shared steps ---------------------------------------------------------------------------------------------------

const discStep: TourStep = {
    id: 'disc',
    title: 'Open your disc image',
    text: 'The editor reads your own copy of the game (an .iso file). Nothing is uploaded and the disc is never changed.',
    target: '#open-iso',
    wait: { kind: 'until', test: discIsOpen },
    doing: 'choose your disc image',
    hint: 'Click Open ISO and pick the .iso file, or drop it anywhere on the window.',
    skipIf: discIsOpen,
};

function levelStep(text = 'Pick a level from the list. It takes a few seconds to load.', hint?: string): TourStep {
    return {
        id: 'level',
        title: 'Open a level',
        text,
        target: 'button.ui-dd.levelpick',
        side: 'bottom',
        wait: { kind: 'until', test: levelIsOpen },
        doing: 'open a level',
        hint: hint ?? 'Click the level list in the toolbar and choose a level. Big levels take a little longer.',
        hintAfter: 20000,
        skipIf: levelIsOpen,
    };
}

/** Open the + Add menu, then pick a row of it. */
function quickAddSteps(row: string, title: string, opened: () => boolean, text: string): TourStep[] {
    return [
        {
            id: 'qa',
            title: 'Open the Add menu',
            text: 'Add, at the left of the toolbar, lists everything you can make. Click it.',
            target: '#ed-add',
            wait: { kind: 'until', test: () => !!document.querySelector('.qa-pop .qa-item') },
            doing: 'click Add',
            skipIf: () => !!document.querySelector('.qa-pop .qa-item'),
        },
        {
            id: 'qa-row',
            title,
            text,
            target: byText('.qa-pop .qa-item', row),
            side: 'right',
            // The menu runs its rows directly (no command), so wait for what the row opens.
            wait: { kind: 'until', test: opened },
            doing: `choose ${row}`,
            hint: 'Closed the menu by accident? Press Back and open it again.',
        },
    ];
}

const placeDragStep: TourStep = {
    id: 'drag',
    title: 'Drag something into the level',
    text: 'Pick a group on the left of the Place panel, then drag a row into the 3D view and let go where it should stand.',
    target: '#ed-left',
    also: ['#lv-viewport'],
    side: 'right',
    demo: { kind: 'drag', from: '#ed-left .pl-row.pl-class', to: '#lv-viewport' },
    wait: { kind: 'event', name: 'change' },
    doing: 'place something',
    hint: 'Double-clicking a row puts it in the middle of the view.',
    enter: () => {
        // Bring the Place panel back if it was hidden, and show a group with rows to drag.
        if (!exists('#ed-left .pl-cat')) runCommand('view.left');
        if (!exists('#ed-left .pl-row.pl-class'))
            document.querySelector<HTMLElement>('#ed-left .pl-cat[data-cat="creatures"]')?.click();
    },
};

function playStep(text: string): TourStep {
    return {
        id: 'play',
        title: 'Try it in the game',
        text,
        target: '#ed-play',
        also: ['#ed-export'],
        side: 'bottom',
    };
}

const exportStep: TourStep = {
    id: 'export',
    title: 'Export the mod',
    text: 'Export (Ctrl+E) writes a mod pack with everything you changed. The game loads it on top of your disc.',
    // The toolbar button in the Level Editor; the project menu (which has Export too) in other tabs.
    target: () => document.getElementById(exists('#ed-export') ? 'ed-export' : 'project-chip'),
    side: 'bottom',
};

// ---- the tours ------------------------------------------------------------------------------------------------------

const editorTour: Tour = {
    id: 'editor',
    title: 'Editor tour',
    line: 'The parts of the Level Editor and what each one is for.',
    group: 'basics',
    minutes: 3,
    steps: [
        {
            id: 'hello',
            title: 'The Level Editor',
            text: 'This tour shows the parts of the editor: Place, the 3D view, the Outliner, Details, the Content Browser and Play. Press Next or Enter to go on, Esc to leave.',
            nextLabel: 'Show me',
        },
        discStep,
        levelStep(),
        {
            id: 'place',
            title: 'Place',
            text: 'Everything you can put in a level, in groups like Characters, Creatures, Items and Lights. Search finds anything by name.',
            target: '#ed-left',
            side: 'right',
            enter: () => {
                if (!exists('#ed-left .pl-cat')) runCommand('view.left');
            },
        },
        {
            id: 'group',
            title: 'Pick a group',
            text: 'Click Creatures to see the creatures you can place.',
            target: '#ed-left .pl-cat[data-cat="creatures"]',
            side: 'right',
            wait: { kind: 'click' },
            doing: 'click Creatures',
            skipIf: () => !document.querySelector('#ed-left .pl-cat[data-cat="creatures"]'),
        },
        placeDragStep,
        {
            id: 'viewport',
            title: 'The 3D view',
            text: 'Your level as the game shows it. Hold the right mouse button and move to look around, and use W A S D to fly.',
            target: '#lv-viewport',
            side: 'right',
            pad: 0,
        },
        {
            id: 'select',
            title: 'Select an object',
            text: 'Click any object in the 3D view. W moves it, E turns it, R scales it; F brings the camera to it.',
            target: '#lv-viewport',
            pad: 0,
            wait: { kind: 'event', name: 'selection', test: (n) => typeof n === 'number' && n > 0 },
            doing: 'click an object',
            skipIf: () => selectedCount() > 0,
        },
        {
            id: 'outliner',
            title: 'Outliner',
            text: 'Every object in the level, in groups. Click a row to select it; the search box finds objects by name or number.',
            target: '#ed-outliner',
            side: 'left',
        },
        {
            id: 'details',
            title: 'Details',
            text: 'The settings of what you selected: health, speed, model, sounds. Changed values are marked and can be reset.',
            target: '#ed-details',
            side: 'left',
            enter: () => {
                const tab = document.querySelector<HTMLElement>('#ed-details .side-tab');
                if (tab && !tab.classList.contains('active')) tab.click();
            },
        },
        {
            id: 'content',
            title: 'Content Browser',
            text: 'Every class on your disc, with pictures. Drag one into the world, or star it to keep it under Favourites.',
            target: '#ed-bottom',
            side: 'top',
            enter: () => runCommand('panel.show.assets'),
        },
        playStep(
            'Play starts the game in this level with your changes (desktop app). In a browser, Export builds a mod pack to copy into the game folder.',
        ),
        {
            id: 'help',
            title: 'Help is always here',
            text: 'Help has a guide for each task, and Ctrl+P finds any command, object or level by name.',
            target: '#help-btn',
            side: 'bottom',
        },
    ],
};

const campaignTour: Tour = {
    id: 'campaign',
    title: 'Change a campaign level',
    line: 'Select, move and add things in a level of the game.',
    group: 'templates',
    minutes: 4,
    steps: [
        discStep,
        levelStep('Pick the level you want to change. Your edits become a mod; the disc stays as it is.'),
        {
            id: 'find',
            title: 'Find an object',
            text: 'The Outliner lists everything in the level. Click a row, or click an object in the 3D view.',
            target: '#ed-outliner',
            also: ['#lv-viewport'],
            side: 'left',
            wait: { kind: 'event', name: 'selection', test: (n) => typeof n === 'number' && n > 0 },
            doing: 'select an object',
            skipIf: () => selectedCount() > 0,
        },
        {
            id: 'move',
            title: 'Move it',
            text: 'Choose Move (or press W), then drag one of the coloured arrows in the 3D view.',
            target: '#ed-tool-move',
            also: ['#lv-viewport'],
            side: 'bottom',
            wait: { kind: 'event', name: 'change' },
            doing: 'move the object',
            hint: 'Drag an arrow of the gizmo on the selected object. Ctrl+Z undoes it.',
        },
        {
            id: 'fields',
            title: 'Change how it behaves',
            text: 'Details shows its settings. Change a number, for example its health, and press Enter.',
            target: '#ed-details',
            side: 'left',
            wait: { kind: 'event', name: 'change' },
            doing: 'change a value',
            hint: 'Not every object has numbers to change; try a creature or a chest.',
        },
        placeDragStep,
        {
            id: 'undo',
            title: 'Undo and the history',
            text: 'Ctrl+Z undoes, Ctrl+Y redoes. Edit > Undo History lists every step and jumps back to one.',
            target: '#ed-undo',
            also: ['#ed-redo'],
            side: 'bottom',
        },
        playStep(
            'Play starts the game in this level with your edits (desktop app). In a browser, Export builds the mod pack instead.',
        ),
    ],
};

const newLevelTour: Tour = {
    id: 'newlevel',
    title: 'Level copy',
    line: 'Start a new level as a copy of one of the game.',
    group: 'templates',
    minutes: 3,
    steps: [
        discStep,
        {
            id: 'settings',
            title: 'Open Project Settings',
            text: 'Your mod project keeps its levels in Project Settings. Click the project button, then Open Project Settings.',
            target: '#project-chip',
            side: 'bottom',
            wait: { kind: 'until', test: () => tabActive('project') },
            doing: 'open Project Settings',
            skipIf: () => tabActive('project'),
        },
        {
            id: 'create',
            title: 'Start a project',
            text: 'Give the mod a name and create it. It holds your levels and every change you make.',
            target: around('#pj-create', 'section, form, .pjx-start, div'),
            also: ['#pj-new-name'],
            wait: { kind: 'until', test: hasProject },
            doing: 'create the project',
            skipIf: hasProject,
        },
        {
            id: 'copy',
            title: 'Add a copy',
            text: 'Open the arrow next to Add, choose "Add a copy of a level", and pick the level to start from.',
            target: '#pj-add-more',
            also: ['#pj-add'],
            side: 'bottom',
            wait: { kind: 'until', test: () => !!document.querySelector('#pj-copy-title') || hasCopy() },
            doing: 'pick a level to copy',
            skipIf: hasCopy,
        },
        {
            id: 'title',
            title: 'Name the new level',
            text: 'Type a title for your level and press Add copy. The original level is never touched.',
            target: around('#pj-copy', '.ed-pop'),
            side: 'right',
            wait: { kind: 'until', test: hasCopy },
            doing: 'add the copy',
            skipIf: hasCopy,
        },
        {
            id: 'levels',
            title: 'Your levels',
            text: 'The copy is now in the level list. Open it from its row to edit it like any other level; exits still lead where the original did.',
            target: byText('#ed-doc h2', 'Levels'),
            side: 'bottom',
            pad: 10,
        },
        exportStep,
    ],
};

const puzzleTour: Tour = {
    id: 'puzzle',
    title: 'Pressure plate puzzle',
    line: 'Plates on the floor that open a door when all are pressed.',
    group: 'templates',
    minutes: 3,
    steps: [
        discStep,
        levelStep('Pick a level with a door. The plates, a counter and the link to the door are made for you.'),
        ...quickAddSteps(
            'Puzzle',
            'Choose Puzzle',
            () => exists('.lg-form'),
            'Puzzle (plates open a door) opens Logic below the 3D view with the pressure plate setup.',
        ),
        {
            id: 'setup',
            title: 'Choose the door',
            text: 'Pick the door that opens and how many plates there are. The plates are laid out in a row in front of it.',
            target: '.lg-form',
            side: 'top',
        },
        {
            id: 'create',
            title: 'Make the puzzle',
            text: 'Press Create. The plates, the counter and the link to the door appear in the level and in Logic.',
            target: byText('.lg-form button', /^Create$/),
            also: ['.lg-form'],
            side: 'top',
            wait: { kind: 'event', name: 'change' },
            doing: 'press Create',
            hint: 'If Create is greyed out, this level has no plate to copy; try another level.',
        },
        {
            id: 'graph',
            title: 'See how it is wired',
            text: 'Logic shows each object as a card and each link as a line: the plates count down, the counter opens the door.',
            target: '#ed-bottom',
            side: 'top',
        },
        playStep('Play the level and step on every plate. In a browser, Export the mod pack and copy it to the game.'),
    ],
};

const cutsceneTour: Tour = {
    id: 'cutscene',
    title: 'Make a cut scene',
    line: 'A camera path that plays when the player walks in.',
    group: 'templates',
    minutes: 4,
    steps: [
        discStep,
        levelStep(),
        ...quickAddSteps(
            'Cut scene',
            'Choose Cut scene',
            () => exists('input[aria-label="Cut scene name"]'),
            'Cut scene opens Story below the 3D view, ready to build a scene from camera points.',
        ),
        {
            id: 'name',
            title: 'Name the scene',
            text: 'Type a name for the cut scene. The player sees it nowhere; it is for you.',
            target: 'input[aria-label="Cut scene name"]',
            side: 'top',
        },
        {
            id: 'points',
            title: 'Add camera points',
            text: 'Fly the 3D view to where the camera should be and press this button. Add a few; the camera moves from one to the next.',
            target: byText('#ed-bottom button', 'Add a point at the viewport camera'),
            also: ['#lv-viewport'],
            side: 'top',
            wait: { kind: 'click' },
            doing: 'add a camera point',
        },
        {
            id: 'steps',
            title: 'Add steps',
            text: 'Steps are what happens during the scene: a line of dialogue, a fade, a wait. They are optional.',
            target: byText('#ed-bottom button', /^Add step$/),
            side: 'top',
        },
        {
            id: 'create',
            title: 'Create the scene',
            text: 'Create cut scene places the scene and a trigger that starts it, and links them for you.',
            target: byText('#ed-bottom button', 'Create cut scene'),
            side: 'top',
            wait: { kind: 'click' },
            doing: 'press Create cut scene',
        },
        playStep('Play the level and walk into the trigger to see your scene. In a browser, Export the mod pack.'),
    ],
};

const vendorTour: Tour = {
    id: 'vendor',
    title: 'Shop and prices',
    line: 'What a shopkeeper sells, how many, and for how much.',
    group: 'templates',
    minutes: 3,
    steps: [
        discStep,
        levelStep(
            'Pick a level with a shop. Surdana (Level 1A) has a blacksmith, an alchemist and a mage.',
            'Choose "Level 1A - Surdana" in the level list.',
        ),
        ...quickAddSteps(
            'Vendor',
            'Choose Vendor',
            () => exists('.panel.vendors'),
            'Vendor opens the shops of this level in their own tab.',
        ),
        {
            id: 'shop',
            title: 'Pick a shop',
            text: 'The list at the top picks the shop. Below it, Sale price x is what this shop charges compared to the item price.',
            target: '.panel.vendors .panel-tools',
            also: ['.panel.vendors .vendor-settings'],
            side: 'bottom',
        },
        {
            id: 'stock',
            title: 'Change the stock',
            text: 'Each row is an item: whether it runs out, how many there are, and the item price. Change a number and press Enter.',
            target: '.panel.vendors .vendor-table',
            side: 'top',
            // Item prices live outside the level, so wait for the field itself rather than a level change.
            wait: { kind: 'event', name: 'target-change' },
            doing: 'change a quantity or price',
            hint: 'The Pays column is what the player pays after the shop modifier.',
        },
        {
            id: 'add',
            title: 'Add an item',
            text: 'Type part of an item name here and click it to put it on sale.',
            target: '.panel.vendors .vendor-add',
            side: 'top',
        },
        exportStep,
    ],
};

const buildCampaignTour: Tour = {
    id: 'build-campaign',
    title: 'Build a campaign',
    line: 'List your levels in order and see how their exits connect.',
    group: 'templates',
    minutes: 4,
    steps: [
        discStep,
        {
            id: 'settings',
            title: 'Open Project Settings',
            text: 'The project button at the top shows your mod. Click it, then Open Project Settings.',
            target: '#project-chip',
            side: 'bottom',
            wait: { kind: 'until', test: () => tabActive('project') },
            doing: 'open Project Settings',
            skipIf: () => tabActive('project'),
        },
        {
            id: 'create',
            title: 'Start a project',
            text: 'Give the mod a name and create it.',
            target: around('#pj-create', 'section, form, div'),
            also: ['#pj-new-name'],
            wait: { kind: 'until', test: hasProject },
            doing: 'create the project',
            skipIf: hasProject,
        },
        {
            id: 'details',
            title: 'Mod details',
            text: 'The name, author and version players see. The id is used for the mod folder.',
            target: around('#pj-name', 'section, .pjx-pane'),
            side: 'right',
        },
        {
            id: 'add',
            title: 'Add levels',
            text: 'Add puts a level of the disc in your campaign. Add two or three; the arrow next to it adds a copy instead.',
            target: '#pj-add',
            also: ['#pj-add-more'],
            side: 'bottom',
            wait: { kind: 'until', test: () => (project()?.levels.length ?? 0) >= 2 },
            doing: 'add two levels',
            skipIf: () => (project()?.levels.length ?? 0) >= 2,
        },
        {
            id: 'map',
            title: 'Show the Campaign map',
            text: 'Map draws each level as a card and each exit as an arrow to where it leads.',
            target: '#pj-view-map',
            side: 'bottom',
            wait: { kind: 'until', test: () => !!document.querySelector('#ed-doc .cm') },
            doing: 'click Map',
            skipIf: () => !!document.querySelector('#ed-doc .cm'),
        },
        {
            id: 'cards',
            title: 'Read the map',
            text: 'Click a card to open that level, drag cards to lay out the route. An orange arrow is an exit that needs fixing; click it to change where it leads.',
            target: '#ed-doc .cm',
            side: 'left',
            pad: 0,
        },
        {
            id: 'checks',
            title: 'Checks',
            text: 'Problems with the campaign show here: a missing level, an exit with no way back. Click one to go to it.',
            target: '#pj-checks',
            side: 'left',
        },
        {
            id: 'export',
            title: 'Export the campaign',
            text: 'One mod pack holds every level you changed and every copy.',
            target: '#pj-export',
            side: 'top',
        },
    ],
};

const arenaTour: Tour = {
    id: 'arena',
    title: 'Multiplayer arena',
    line: 'Spawn points and pickups on the terrain of a level.',
    group: 'templates',
    minutes: 4,
    steps: [
        discStep,
        levelStep('Pick the level whose terrain the arena is built on.'),
        ...quickAddSteps(
            'Arena',
            'Choose Arena',
            () => exists(byText('#ed-details .side-tab.active', 'World Settings')),
            'Arena (PvP map) opens World Settings with the multiplayer map form.',
        ),
        {
            id: 'start',
            title: 'Start the map',
            text: 'Give the map a name and press Start new map. The level stays as it is; the arena is a map of its own.',
            target: '#ed-details',
            side: 'left',
            wait: {
                kind: 'until',
                test: () => !exists(byText('#ed-details button', 'Start new map')),
            },
            doing: 'press Start new map',
            hint: 'Start new map is under the map name in World Settings.',
        },
        {
            id: 'spawns',
            title: 'Place spawn points',
            text: 'World Settings now lists the map. Add spawn points where players appear, and pickups they can collect, then drag them into place in the 3D view.',
            target: '#ed-details',
            also: ['#lv-viewport'],
            side: 'left',
        },
        {
            id: 'validate',
            title: 'Check the map',
            text: 'Validation, at the bottom, lists what the map still needs, like too few spawn points.',
            target: byText('#ed-bottom .dock-tab', 'Validation'),
            side: 'top',
        },
        exportStep,
    ],
};

const saveTour: Tour = {
    id: 'save',
    title: 'Edit a save',
    line: 'Open the Save Editor for a character, inventory or quests.',
    group: 'templates',
    minutes: 2,
    steps: [
        {
            id: 'tools',
            title: 'Open the Tools menu',
            text: 'Saves are edited in the Save Editor, an application of its own. It is under Tools.',
            target: '#menu-tools',
            side: 'bottom',
            wait: { kind: 'until', test: () => !!document.querySelector('.ui-menu') },
            doing: 'click Tools',
        },
        {
            id: 'open',
            title: 'Open the Save Editor',
            text: 'Click Save Editor. It opens in a window (or tab) of its own and the tour carries on there.',
            target: byText('.ui-menu .ui-menu-item', 'Save Editor'),
            side: 'right',
            wait: { kind: 'command', ids: ['mode.save'] },
            doing: 'click Save Editor',
            hint: 'Closed the menu? Press Back.',
            enter: () => setPendingWindowTour('save-window'),
        },
        {
            id: 'there',
            title: 'Carry on in the Save Editor',
            text: 'The Save Editor shows the tour from its first step. Keep a copy of your save before you change it.',
        },
    ],
};

/** The tour of the Save Editor window (save.html). */
export const saveWindowTour: Tour = {
    id: 'save-window',
    title: 'Save Editor',
    line: 'Open a save and change the character, inventory or quests.',
    group: 'templates',
    minutes: 3,
    steps: [
        {
            id: 'hello',
            title: 'The Save Editor',
            text: 'It reads a save file of the game (.rsg) and writes your changes back. Keep a copy of the original first.',
        },
        {
            id: 'open',
            title: 'Open a save',
            text: 'Open save picks one file. Open folder lists every save of a memory card folder on the left.',
            target: '#sv-open',
            also: ['#sv-folder'],
            side: 'bottom',
            wait: { kind: 'until', test: saveIsOpen },
            doing: 'open a save',
            skipIf: saveIsOpen,
            hint: 'You can also drop a .rsg file onto the window.',
            hintAfter: 20000,
        },
        {
            id: 'sections',
            title: 'What you can change',
            text: 'Each part of the save has a page: the character, the inventory, the quests and the world.',
            target: '.sv-tabs',
            side: 'bottom',
        },
        {
            id: 'edit',
            title: 'Change something',
            text: 'Change a value on this page, for example the gold or the health. Changed values are marked and Undo takes them back.',
            target: '.sv-tabs',
            side: 'bottom',
            wait: { kind: 'until', test: () => !document.querySelector('#sv-dirty')?.hasAttribute('hidden') },
            doing: 'change a value',
            skipIf: () => !document.querySelector('#sv-dirty')?.hasAttribute('hidden'),
        },
        {
            id: 'write',
            title: 'Save your changes',
            text: 'Save writes the changes into the save file (the desktop app keeps a backup). In a browser it downloads the changed file; Export saves a copy elsewhere.',
            target: '#sv-save',
            also: ['#sv-export'],
            side: 'bottom',
        },
    ],
};

function saveIsOpen(): boolean {
    const t = document.querySelector('#sv-title')?.textContent ?? '';
    return t.length > 0 && t !== 'No save open';
}

// ---- a tour that continues in another window ------------------------------------------------------------------------

const PENDING_KEY = 'drakan-editor:tour-next-window';

/** Ask the next editor window that opens (the Save Editor) to start a tour. Kept for two minutes. */
export function setPendingWindowTour(id: string): void {
    try {
        localStorage.setItem(PENDING_KEY, JSON.stringify({ id, at: Date.now() }));
    } catch {
        // storage blocked: the other window just does not start the tour
    }
}

/** The tour this window was asked to start, once (cleared on read). */
export function takePendingWindowTour(): string | null {
    try {
        const raw = localStorage.getItem(PENDING_KEY);
        localStorage.removeItem(PENDING_KEY);
        const v = raw ? (JSON.parse(raw) as { id?: unknown; at?: unknown }) : null;
        if (v && typeof v.id === 'string' && typeof v.at === 'number' && Date.now() - v.at < 120000) return v.id;
    } catch {
        // malformed or blocked
    }
    return null;
}

export const EDITOR_TOURS: readonly Tour[] = [
    editorTour,
    campaignTour,
    newLevelTour,
    puzzleTour,
    cutsceneTour,
    vendorTour,
    buildCampaignTour,
    arenaTour,
    saveTour,
];

/** Help guide ids (editor-core guides.ts) and other names that start a tour. */
export const TOUR_ALIASES: Record<string, string> = {
    start: 'editor',
    objects: 'campaign',
    triggers: 'puzzle',
    vendors: 'vendor',
    multiplayer: 'arena',
    story: 'cutscene',
    // Start template ids that differ from the tour id.
    level: 'newlevel',
};

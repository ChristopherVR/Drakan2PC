import { button, emptyState, h, icon } from './dom.ts';

/** The tabs of Save mode, in navigation order. */
export const TABS = [
    ['character', 'Character', 'user'],
    ['items', 'Inventory', 'bag'],
    ['story', 'Story', 'scroll'],
    ['world', 'World', 'map'],
    ['transfer', 'Transfer', 'swap'],
    ['compare', 'Compare', 'compare'],
    ['repair', 'Repair', 'wrench'],
] as const;

export type SubTab = (typeof TABS)[number][0];

/**
 * Builds the whole Save mode skeleton inside `<main id="save-main">` (index.html only provides the empty mount point).
 * Layout, after AbioticEditor: app bar (brand, active folder, open buttons) over a three-column workspace (save files,
 * the editing area with its header and tab strip, a right dock with the pending-changes tray and the 3D view) and a
 * status strip. Every element other code needs keeps a stable `sv-*` id.
 */
export function buildShell(root: HTMLElement, fsa: boolean): void {
    root.className = 'sv';
    root.replaceChildren(
        h(
            'header',
            { class: 'sv-appbar' },
            h(
                'div',
                { class: 'sv-brand' },
                h('span', { class: 'sv-brand-mark' }, icon('save', 18)),
                h('div', null, h('b', null, 'Save editor'), h('small', null, 'Drakan: The Ancients’ Gates')),
            ),
            h(
                'div',
                { class: 'sv-folder' },
                h('small', null, 'Active save folder'),
                h('span', { id: 'sv-folder-name', class: 'sv-folder-name' }, 'No folder open'),
            ),
            h('span', { class: 'sv-spacer' }),
            button('Open save', {
                id: 'sv-open',
                icon: 'file',
                title: 'Open one .rsg save file',
            }),
            button('Open folder', {
                id: 'sv-folder',
                icon: 'folder',
                kind: 'primary',
                title: 'Pick the memory card folder to list every slot',
            }),
            h('input', { id: 'sv-file', type: 'file', accept: '.rsg', hidden: true }),
            h('input', { id: 'sv-dir', type: 'file', webkitdirectory: true, hidden: true }),
        ),
        h(
            'div',
            { class: 'sv-work' },
            // ---- left: save files
            h(
                'aside',
                { class: 'sv-side' },
                h(
                    'div',
                    { class: 'sv-side-head' },
                    h('h2', null, 'Save files', h('small', { id: 'sv-count' })),
                    h(
                        'label',
                        { class: 'sv-search' },
                        icon('search', 14),
                        h('input', {
                            id: 'sv-list-q',
                            type: 'search',
                            placeholder: 'Search saves',
                            'aria-label': 'Search saves',
                        }),
                    ),
                ),
                h(
                    'ul',
                    { id: 'sv-list', class: 'sv-list' },
                    h(
                        'li',
                        { class: 'sv-list-empty' },
                        emptyState(
                            'folder',
                            'No saves listed yet',
                            'Open the memory card folder to list every slot with its screenshot, or open a single .rsg file.',
                        ),
                    ),
                ),
            ),
            // ---- centre: header, tabs, panes
            h(
                'section',
                { class: 'sv-centre' },
                h(
                    'header',
                    { class: 'sv-head' },
                    h(
                        'div',
                        { class: 'sv-title' },
                        h(
                            'div',
                            { class: 'sv-title-row' },
                            h('h2', { id: 'sv-title' }, 'No save open'),
                            h('span', { id: 'sv-dirty', class: 'sv-chip warn', hidden: true }, 'Unsaved changes'),
                        ),
                        h('p', { id: 'sv-subtitle' }, 'Pick a save on the left.'),
                    ),
                    h(
                        'div',
                        { class: 'sv-head-actions' },
                        button('', { id: 'sv-undo', icon: 'undo', title: 'Undo', disabled: true, kind: 'icon' }),
                        button('', { id: 'sv-redo', icon: 'redo', title: 'Redo', disabled: true, kind: 'icon' }),
                        button('Reload', {
                            id: 'sv-reload',
                            icon: 'reload',
                            title: 'Read the file again',
                            disabled: true,
                        }),
                        button('Revert', {
                            id: 'sv-revert',
                            icon: 'revert',
                            title: 'Throw away every change that is not saved yet',
                            disabled: true,
                        }),
                        button('Export', {
                            id: 'sv-export',
                            icon: 'download',
                            title: 'Download the edited .rsg',
                            disabled: true,
                        }),
                        button('Save', { id: 'sv-save', icon: 'save', kind: 'primary', disabled: true }),
                        h(
                            'button',
                            {
                                id: 'sv-dock-toggle',
                                class: 'sv-btn icon',
                                type: 'button',
                                title: 'Hide the pending-changes and 3D panel',
                                'aria-pressed': 'true',
                            },
                            icon('dock'),
                            h('span', { id: 'sv-dock-badge', class: 'sv-dock-badge', hidden: true }, '0'),
                        ),
                    ),
                ),
                h(
                    'nav',
                    { class: 'sv-tabs', role: 'tablist', 'aria-label': 'Save sections' },
                    ...TABS.map(([k, label, ico]) =>
                        h(
                            'button',
                            {
                                id: `sv-sub-${k}`,
                                class: k === 'character' ? 'sv-tab active' : 'sv-tab',
                                role: 'tab',
                                type: 'button',
                            },
                            icon(ico, 15),
                            h('span', null, label),
                        ),
                    ),
                ),
                h(
                    'div',
                    { class: 'sv-panes' },
                    ...TABS.map(([k]) =>
                        k === 'world'
                            ? h(
                                  'div',
                                  { id: 'sv-world', class: 'sv-pane sv-worldpane', hidden: true },
                                  h(
                                      'div',
                                      { class: 'sv-worldlist' },
                                      h(
                                          'div',
                                          { class: 'sv-filters' },
                                          h(
                                              'label',
                                              { class: 'sv-search grow' },
                                              icon('search', 14),
                                              h('input', {
                                                  id: 'sv-q',
                                                  type: 'search',
                                                  placeholder: 'Search records',
                                              }),
                                          ),
                                          h(
                                              'label',
                                              { class: 'sv-check' },
                                              h('input', { id: 'sv-posonly', type: 'checkbox' }),
                                              ' Positioned only',
                                          ),
                                      ),
                                      h('small', { id: 'sv-wcount', class: 'sv-dim sv-wcount' }),
                                      h('div', { id: 'sv-groups', class: 'sv-groups' }),
                                  ),
                                  h(
                                      'div',
                                      { id: 'sv-worldside', class: 'sv-worldside' },
                                      h('div', { id: 'sv-details' }),
                                  ),
                              )
                            : h('div', { id: `sv-${k}`, class: 'sv-pane', hidden: k !== 'character' }),
                    ),
                ),
            ),
            // ---- right: pending changes tray and 3D
            h(
                'aside',
                { class: 'sv-dock' },
                h(
                    'section',
                    { class: 'sv-tray' },
                    h(
                        'header',
                        { class: 'sv-dock-head' },
                        h('h2', null, 'Pending changes', h('span', { id: 'sv-pending', class: 'sv-count' }, '0')),
                        button('Revert all', {
                            id: 'sv-revert-all',
                            kind: 'ghost sm',
                            title: 'Throw away every change that is not saved yet',
                            disabled: true,
                        }),
                    ),
                    h('div', { id: 'sv-edits-list', class: 'sv-edits' }),
                    h(
                        'footer',
                        { class: 'sv-tray-foot' },
                        button('Save changes', { id: 'sv-save2', icon: 'save', kind: 'primary block', disabled: true }),
                        h('p', { id: 'sv-save-note' }),
                        h('div', { id: 'sv-backups', class: 'sv-backups' }),
                    ),
                ),
                h(
                    'section',
                    { class: 'sv-3d' },
                    h(
                        'header',
                        { class: 'sv-dock-head' },
                        h('h2', null, '3D view'),
                        button('Show player', {
                            id: 'sv-frame-player',
                            icon: 'cube',
                            kind: 'ghost sm',
                            title: 'Select Rynn and frame the camera on her',
                        }),
                        button('Frame all', { id: 'sv-frame-all', kind: 'ghost sm', title: 'Frame every record' }),
                    ),
                    h('div', { id: 'sv-view', class: 'sv-view' }),
                ),
            ),
        ),
        h(
            'footer',
            { class: 'sv-foot' },
            h('span', { class: 'sv-dot' }),
            h('span', { id: 'sv-foot-msg' }, 'Ready.'),
            h('span', { class: 'sv-spacer' }),
            h(
                'span',
                { class: 'sv-foot-note' },
                fsa
                    ? 'Save writes the file in place and keeps a backup beside it.'
                    : 'This browser cannot write files in place: Save downloads the edited .rsg instead.',
            ),
        ),
        h('div', { id: 'sv-toast', class: 'sv-toast', role: 'status', hidden: true }),
    );
}

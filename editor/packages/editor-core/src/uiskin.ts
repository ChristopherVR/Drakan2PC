import { strToU8 } from 'fflate';
import { buildModFiles, type ModMeta } from './mod.ts';

/**
 * UI skin: the data file that configures the game's menus and HUD (`ui/<name>.ini` in a mod pack, `ui.ini` next to the
 * exe). Mirrors runtime/gameui/include/gameui/UiSkin.h (docs/reference/modding.md, "Menus and HUD"); the defaults
 * below equal the compiled-in ones and configs/ui/default.ini (a test checks that).
 */
export interface UiSkin {
    /** Theme colours by name (`accent`, `panel`, ...), `#RRGGBB` or `#RRGGBBAA`, upper case. `hud_accent` is [hud] accent. */
    colors: Record<string, string>;
    /** Sizes and layout numbers by name (`size_body`, `title_row_h`, ...). */
    numbers: Record<string, number>;
    /** Row order per page; ids left out are hidden. */
    rows: Record<string, string[]>;
    /** Strings by key (`title.row.new_game`). */
    strings: Record<string, string>;
}

export const emptySkin = (): UiSkin => ({ colors: {}, numbers: {}, rows: {}, strings: {} });

export interface SkinKey {
    key: string;
    def: string;
}

const k = (key: string, def: string): SkinKey => ({ key, def });

export const THEME_COLORS: readonly SkinKey[] = [
    k('accent', '#E07B2E'),
    k('backdrop', '#090604CD'),
    k('backdrop_edge', '#000000EB'),
    k('panel', '#150E0AE8'),
    k('panel_raised', '#261911EE'),
    k('inset', '#1F0B05F0'),
    k('brass', '#C99B52'),
    k('brass_dim', '#76542A'),
    k('text', '#E5D9BF'),
    k('text_muted', '#A38F72'),
    k('text_disabled', '#635646'),
    k('heading', '#EFD398'),
    k('ember', '#E07B2E'),
    k('ember_glow', '#D6301A3A'),
    k('select', '#D62216'),
    k('grid_line', '#643C0C'),
    k('grid_light', '#9C6715'),
    k('gilt', '#E2B65C'),
    k('ivory', '#DFD6BC'),
    k('good', '#8FCB6B'),
    k('bad', '#D8584A'),
    k('hp', '#B8322E'),
    k('mana', '#4A7FD8'),
    k('xp', '#C9A44C'),
    k('parchment', '#D9C9A3F5'),
    k('ink', '#3A2A1A'),
];
export const HUD_ACCENT_DEFAULT = '#E2B65C';
export const THEME_SIZES: readonly SkinKey[] = [
    k('size_title', '44'),
    k('size_heading', '32'),
    k('size_body', '24'),
    k('size_small', '19'),
    k('size_numeric', '26'),
];
export const LAYOUT_KEYS: readonly SkinKey[] = [
    k('title_column_w', '520'),
    k('title_row_h', '56'),
    k('title_row_step', '64'),
    k('title_centre_y', '650'),
    k('pause_menu_x', '96'),
    k('pause_menu_w', '560'),
    k('pause_panel_y', '160'),
    k('pause_row_h', '88'),
    k('pause_row_step', '104'),
];

export interface RowDef {
    id: string;
    label: string;
    /** Only shown in some situations (Continue needs a save, Multiplayer is an option of the host). */
    conditional?: boolean;
}
export interface PageDef {
    id: string;
    title: string;
    rows: RowDef[];
}

const r = (id: string, label: string, conditional = false): RowDef => ({
    id,
    label,
    ...(conditional ? { conditional } : {}),
});

/** Pages whose entries a skin may reorder or hide, with the default order and labels. */
export const PAGES: readonly PageDef[] = [
    {
        id: 'title.main',
        title: 'Main menu',
        rows: [
            r('continue', 'Continue', true),
            r('load_game', 'Load Game'),
            r('new_game', 'New Game'),
            r('multiplayer', 'Multiplayer', true),
            r('options', 'Options'),
            r('credits', 'Credits'),
            r('pc_settings', 'PC Settings'),
            r('quit', 'Quit'),
        ],
    },
    {
        id: 'pause.root',
        title: 'Pause menu',
        rows: [
            r('resume', 'Resume'),
            r('save', 'Save Game'),
            r('load', 'Load Game'),
            r('options', 'Options'),
            r('quit', 'Quit'),
        ],
    },
    {
        id: 'pause.options',
        title: 'Options page',
        rows: [
            r('controller', 'Controller'),
            r('audio', 'Audio'),
            r('pc_settings', 'PC Settings'),
            r('defaults', 'Default Settings'),
            r('back', 'Back'),
        ],
    },
    {
        id: 'pause.audio',
        title: 'Audio page',
        rows: [
            r('master_volume', 'Master Volume'),
            r('music_volume', 'Music Volume'),
            r('subtitles', 'Show Subtitles'),
            r('stereo', 'Stereo Sound'),
            r('back', 'Back'),
        ],
    },
    {
        id: 'pause.controller',
        title: 'Controller page',
        rows: [
            r('scheme', 'Control Scheme'),
            r('vibration', 'Vibration'),
            r('reverse_pitch', 'Reverse Pitch'),
            r('key_bindings', 'Key Bindings'),
            r('back', 'Back'),
        ],
    },
];

/** Prefix of a page's row label keys. */
export const labelKey = (pageId: string, rowId: string): string =>
    `${pageId.startsWith('title.') ? 'title' : 'pause'}.row.${rowId}`;

/** Row descriptions of the main menu (the line under the column). */
export const TITLE_DESCRIPTIONS: Record<string, string> = {
    load_game: 'Load a saved game.',
    new_game: 'Start a new adventure.',
    options: 'Controller and audio options.',
    credits: 'The people who made the game.',
    pc_settings: 'Display, graphics, controls and more.',
    quit: 'Close the game.',
    multiplayer: 'Host or join a match over the network.',
};

const PAGE_IDS = new Set(PAGES.map((p) => p.id));
const COLOR_KEYS = new Set([...THEME_COLORS.map((c) => c.key)]);
const SIZE_KEYS = new Set(THEME_SIZES.map((c) => c.key));
const LAYOUT_SET = new Set(LAYOUT_KEYS.map((c) => c.key));

export function parseColor(s: string): string | null {
    const m = /^#?([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.exec(s.trim());
    return m ? `#${m[1]!.toUpperCase()}` : null;
}

/** Parse INI text on top of `into` (later keys win). Problems are returned, not thrown. */
export function parseUiSkin(text: string, into: UiSkin = emptySkin()): { skin: UiSkin; warnings: string[] } {
    const warnings: string[] = [];
    let section = '';
    const lines = text.split('\n');
    lines.forEach((raw, i) => {
        const line = raw.trim();
        const warn = (m: string): number => warnings.push(`line ${i + 1}: ${m}`);
        if (!line || line[0] === ';' || line[0] === '#' || line.startsWith('//')) return;
        if (line[0] === '[') {
            const e = line.indexOf(']');
            if (e < 0) {
                warn('unterminated section header');
                section = '?';
                return;
            }
            section = line.slice(1, e).trim().toLowerCase();
            if (!['theme', 'hud', 'layout', 'rows', 'strings'].includes(section)) {
                warn(`unknown section [${section}]`);
                section = '?';
            }
            return;
        }
        const eq = line.indexOf('=');
        if (eq < 0) {
            warn('expected key = value');
            return;
        }
        const key = line.slice(0, eq).trim();
        const val = line.slice(eq + 1).trim();
        if (!section) {
            warn('key outside a section');
            return;
        }
        if (section === '?') return;
        if (section === 'strings') {
            if (key) into.strings[key] = val.replace(/\\(n|\\)/g, (_m, c: string) => (c === 'n' ? '\n' : '\\'));
        } else if (section === 'rows') {
            if (!PAGE_IDS.has(key)) warn(`unknown row page '${key}'`);
            else
                into.rows[key] = val
                    .split(',')
                    .map((x) => x.trim())
                    .filter(Boolean);
        } else if (section === 'layout') {
            const n = Number(val);
            if (!LAYOUT_SET.has(key)) warn(`unknown layout key '${key}'`);
            else if (!Number.isFinite(n) || n < 0 || n > 4000) warn(`bad number for ${key}`);
            else into.numbers[key] = n;
        } else {
            const hud = section === 'hud';
            if (!hud && SIZE_KEYS.has(key)) {
                const n = Number(val);
                if (!Number.isFinite(n) || n < 4 || n > 400) warn(`bad size for ${key}`);
                else into.numbers[key] = n;
                return;
            }
            const c = parseColor(val);
            if (!c) warn(`bad colour '${val}' for ${key}`);
            else if (hud && key === 'accent') into.colors['hud_accent'] = c;
            else if (!hud && COLOR_KEYS.has(key)) into.colors[key] = c;
            else warn(`unknown key '${key}' in [${section}]`);
        }
    });
    return { skin: into, warnings };
}

/** INI text, only what differs from the defaults when `onlyChanges` (the default: a mod ships just its edits). */
export function serializeUiSkin(skin: UiSkin, onlyChanges = true): string {
    const out: string[] = [];
    const theme: string[] = [];
    for (const c of THEME_COLORS) {
        const v = skin.colors[c.key];
        if (v && (!onlyChanges || v !== c.def)) theme.push(`${c.key} = ${v}`);
    }
    for (const s of THEME_SIZES) {
        const v = skin.numbers[s.key];
        if (v !== undefined && (!onlyChanges || String(v) !== s.def)) theme.push(`${s.key} = ${v}`);
    }
    if (theme.length) out.push('[theme]', ...theme, '');
    const hud = skin.colors['hud_accent'];
    if (hud && (!onlyChanges || hud !== HUD_ACCENT_DEFAULT)) out.push('[hud]', `accent = ${hud}`, '');
    const layout: string[] = [];
    for (const l of LAYOUT_KEYS) {
        const v = skin.numbers[l.key];
        if (v !== undefined && (!onlyChanges || String(v) !== l.def)) layout.push(`${l.key} = ${v}`);
    }
    if (layout.length) out.push('[layout]', ...layout, '');
    const rows: string[] = [];
    for (const p of PAGES) {
        const v = skin.rows[p.id];
        if (!v) continue;
        if (onlyChanges && v.join(',') === p.rows.map((x) => x.id).join(',')) continue;
        rows.push(`${p.id} = ${v.join(', ')}`);
    }
    if (rows.length) out.push('[rows]', ...rows, '');
    const strings: string[] = [];
    for (const [key, v] of Object.entries(skin.strings).sort(([a], [b]) => (a < b ? -1 : 1))) {
        if (onlyChanges && v === defaultString(key)) continue;
        strings.push(`${key} = ${v.replace(/\\/g, '\\\\').replace(/\n/g, '\\n')}`);
    }
    if (strings.length) out.push('[strings]', ...strings, '');
    return out.join('\n');
}

/** The compiled-in text of a string key, or undefined for an unknown key. */
export function defaultString(key: string): string | undefined {
    for (const p of PAGES) for (const row of p.rows) if (labelKey(p.id, row.id) === key) return row.label;
    const d = /^title\.desc\.(.+)$/.exec(key);
    if (d) return TITLE_DESCRIPTIONS[d[1]!];
    if (key === 'title.quit.title') return 'Quit the game?';
    if (key === 'title.quit.body') return 'Drakan closes and returns to the desktop.';
    return undefined;
}

export const isEmptySkin = (s: UiSkin): boolean => serializeUiSkin(s).trim() === '';

/** The entries of a page in the order the skin gives (ids left out are hidden). */
export function pageRows(skin: UiSkin, page: PageDef): { row: RowDef; shown: boolean }[] {
    const listed = skin.rows[page.id];
    if (!listed) return page.rows.map((row) => ({ row, shown: true }));
    const out: { row: RowDef; shown: boolean }[] = [];
    const seen = new Set<string>();
    for (const id of listed) {
        const row = page.rows.find((x) => x.id === id);
        if (row && !seen.has(id)) {
            out.push({ row, shown: true });
            seen.add(id);
        }
    }
    for (const row of page.rows) if (!seen.has(row.id)) out.push({ row, shown: false });
    return out;
}

/** What the skin says is wrong or pointless, for the editor's checklist. */
export function validateUiSkin(skin: UiSkin): string[] {
    const issues: string[] = [];
    for (const p of PAGES) {
        const listed = skin.rows[p.id];
        if (!listed) continue;
        if (!listed.some((id) => p.rows.some((x) => x.id === id)))
            issues.push(`${p.title}: no known entry is listed, the game falls back to the default order.`);
        if (p.id === 'title.main' && !listed.includes('quit'))
            issues.push('Main menu: Quit is hidden; players can only leave with Alt+F4.');
        if (p.id !== 'title.main' && p.id !== 'pause.root' && !listed.includes('back'))
            issues.push(`${p.title}: Back is hidden; the page can only be left with the Back key.`);
        if (p.id === 'pause.root' && !listed.includes('resume'))
            issues.push('Pause menu: Resume is hidden; Esc still resumes.');
    }
    return issues;
}

/** Mod pack with only the skin: `mod.toml` and `ui/menus.ini`. */
export function buildUiSkinPackFiles(skin: UiSkin, meta: ModMeta): Record<string, Uint8Array> {
    const files = buildModFiles(meta, []);
    files['ui/menus.ini'] = strToU8(serializeUiSkin(skin));
    return files;
}

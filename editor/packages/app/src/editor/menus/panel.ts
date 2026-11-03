// Menus panel: edit the game's menu strings, entry order, theme, layout numbers and HUD accent (a UI skin,
// docs/reference/modding.md "Menus and HUD"), see them in a 2D mock preview and export them as a mod pack.
import {
    HUD_ACCENT_DEFAULT,
    LAYOUT_KEYS,
    PAGES,
    THEME_COLORS,
    THEME_SIZES,
    TITLE_DESCRIPTIONS,
    buildUiSkinPackFiles,
    defaultString,
    emptySkin,
    isEmptySkin,
    labelKey,
    pageRows,
    parseUiSkin,
    serializeUiSkin,
    validateUiSkin,
    zipModFiles,
    type ModMeta,
    type PageDef,
    type UiSkin,
} from '@drakan/editor-core';
import { writePackToModsFolder } from '../../modio.ts';
import type { Panel } from '../ctx.ts';
import { fill, h, icon } from '../dom.ts';
import { projects } from '../project/store.ts';
import { notifyMenusChanged } from './bridge.ts';

export interface MenusHost {
    target(): ModMeta['target'] | null;
    download(name: string, data: BlobPart, type: string): void;
    toast(msg: string, kind?: 'info' | 'error' | 'ok' | 'warn'): void;
    setStatus(msg: string, error?: boolean): void;
}

export interface MenusPanel extends Panel {
    skin(): UiSkin;
    /** True when the skin differs from the defaults (the project export includes it then). */
    dirty(): boolean;
    /** The skin as pack files for the project export. */
    packFiles(meta: ModMeta): Record<string, Uint8Array>;
}

const STORE = 'drakan.menus.skin.v1';
type Section = { kind: 'page'; page: PageDef } | { kind: 'theme' } | { kind: 'hud' } | { kind: 'layout' };

const cssColor = (c: string): string => {
    const m = /^#([0-9A-F]{6})([0-9A-F]{2})?$/i.exec(c);
    if (!m) return '#f0f';
    const n = parseInt(m[1]!, 16);
    const a = m[2] ? parseInt(m[2], 16) / 255 : 1;
    return `rgba(${(n >> 16) & 255},${(n >> 8) & 255},${n & 255},${a.toFixed(3)})`;
};

export function createMenusPanel(host: MenusHost): MenusPanel {
    let skin: UiSkin = emptySkin();
    try {
        const raw = localStorage.getItem(STORE);
        if (raw) skin = parseUiSkin(raw).skin;
    } catch {
        // no storage: start from the defaults
    }
    const sections: { id: string; title: string; ico: string; s: Section }[] = [
        ...PAGES.map((p) => ({ id: p.id, title: p.title, ico: 'list', s: { kind: 'page', page: p } as Section })),
        { id: 'theme', title: 'Theme', ico: 'sun', s: { kind: 'theme' } },
        { id: 'hud', title: 'HUD', ico: 'effect', s: { kind: 'hud' } },
        { id: 'layout', title: 'Layout', ico: 'grid', s: { kind: 'layout' } },
    ];
    let cur = sections[0]!;

    const nav = h('div', { class: 'mn-nav' });
    const form = h('div', { class: 'mn-form' });
    const stage = h('div', { class: 'mn-stage' });
    const preview = h('div', { class: 'mn-preview' }, stage);
    const issues = h('div', { class: 'mn-issues' });
    const report = h('pre', { class: 'mod-report' });
    const idIn = h('input', { type: 'text', value: 'my.menus', spellcheck: 'false', 'aria-label': 'Mod id' });
    const nameIn = h('input', { type: 'text', value: 'My menus', 'aria-label': 'Mod name' });
    const authIn = h('input', { type: 'text', value: '', 'aria-label': 'Author' });

    const colorOf = (key: string): string =>
        skin.colors[key] ??
        (key === 'hud_accent' ? HUD_ACCENT_DEFAULT : (THEME_COLORS.find((c) => c.key === key)?.def ?? '#FF00FF'));
    // The accent drives ember (and its glow) unless ember itself is set.
    const emberOf = (): string => skin.colors['ember'] ?? skin.colors['accent'] ?? colorOf('ember');
    const numOf = (key: string): number =>
        skin.numbers[key] ?? Number([...THEME_SIZES, ...LAYOUT_KEYS].find((x) => x.key === key)?.def ?? 0);
    const textOf = (key: string): string => skin.strings[key] ?? defaultString(key) ?? '';

    function changed(): void {
        try {
            localStorage.setItem(STORE, serializeUiSkin(skin));
        } catch {
            // ignore
        }
        renderNav();
        renderPreview();
        const list = validateUiSkin(skin);
        fill(issues, ...list.map((m) => h('div', { class: 'mn-issue' }, icon('warn', 13), m)));
        notifyMenusChanged();
    }

    function setString(key: string, v: string): void {
        if (v === (defaultString(key) ?? '')) delete skin.strings[key];
        else skin.strings[key] = v;
        changed();
    }

    function colorField(key: string, def: string): HTMLElement {
        const cur0 = colorOf(key);
        const text = h('input', { type: 'text', class: 'mn-hex', value: cur0, spellcheck: 'false', 'aria-label': key });
        const pick = h('input', { type: 'color', value: cur0.slice(0, 7), 'aria-label': `${key} picker` });
        const set = (v: string): void => {
            if (v.toUpperCase() === def.toUpperCase()) delete skin.colors[key];
            else skin.colors[key] = v.toUpperCase();
            changed();
        };
        text.addEventListener('change', () => {
            const m = /^#?([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.exec(text.value.trim());
            if (!m) {
                text.value = colorOf(key);
                return;
            }
            const v = `#${m[1]!.toUpperCase()}`;
            text.value = v;
            pick.value = v.slice(0, 7);
            set(v);
        });
        pick.addEventListener('input', () => {
            const alpha = colorOf(key).slice(7);
            const v = pick.value.toUpperCase() + alpha;
            text.value = v;
            set(v);
        });
        const reset = h(
            'button',
            {
                class: 'btn mn-reset',
                type: 'button',
                title: 'Back to the default',
                onclick: () => {
                    delete skin.colors[key];
                    renderForm();
                    changed();
                },
            },
            '↺',
        );
        return h('div', { class: 'mn-field' }, h('label', {}, key.replace(/_/g, ' ')), pick, text, reset);
    }

    function numberField(key: string, def: string, min: number, max: number): HTMLElement {
        const input = h('input', { type: 'number', value: numOf(key), min, max, 'aria-label': key });
        input.addEventListener('change', () => {
            const n = Number(input.value);
            if (!Number.isFinite(n)) return;
            if (String(n) === def) delete skin.numbers[key];
            else skin.numbers[key] = Math.min(max, Math.max(min, n));
            changed();
        });
        return h(
            'div',
            { class: 'mn-field' },
            h('label', {}, key.replace(/_/g, ' ')),
            input,
            h('span', { class: 'dim small' }, `default ${def}`),
        );
    }

    function renderForm(): void {
        const s = cur.s;
        if (s.kind === 'page') {
            const page = s.page;
            const list = h('div', { class: 'mn-rows' });
            const draw = (): void => {
                const rows = pageRows(skin, page);
                fill(
                    list,
                    ...rows.map(({ row, shown }, i) => {
                        const key = labelKey(page.id, row.id);
                        const label = h('input', { type: 'text', value: textOf(key), 'aria-label': `${row.id} label` });
                        label.addEventListener('change', () => setString(key, label.value));
                        const move = (d: number): void => {
                            const ids = rows.filter((x) => x.shown).map((x) => x.row.id);
                            const at = ids.indexOf(row.id);
                            const to = at + d;
                            if (at < 0 || to < 0 || to >= ids.length) return;
                            ids.splice(to, 0, ...ids.splice(at, 1));
                            skin.rows[page.id] = ids;
                            changed();
                            draw();
                        };
                        const show = h('input', { type: 'checkbox', 'aria-label': `show ${row.id}` });
                        (show as HTMLInputElement).checked = shown;
                        show.addEventListener('change', () => {
                            const ids = rows.filter((x) => x.shown).map((x) => x.row.id);
                            const next = show.checked ? [...ids, row.id] : ids.filter((x) => x !== row.id);
                            skin.rows[page.id] = next;
                            changed();
                            draw();
                        });
                        const desc =
                            page.id === 'title.main' && TITLE_DESCRIPTIONS[row.id] !== undefined
                                ? (() => {
                                      const dk = `title.desc.${row.id}`;
                                      const d = h('input', {
                                          type: 'text',
                                          class: 'mn-desc',
                                          value: textOf(dk),
                                          'aria-label': `${row.id} description`,
                                      });
                                      d.addEventListener('change', () => setString(dk, d.value));
                                      return d;
                                  })()
                                : null;
                        return h(
                            'div',
                            { class: `mn-row${shown ? '' : ' off'}` },
                            show,
                            h(
                                'button',
                                {
                                    class: 'btn',
                                    type: 'button',
                                    title: 'Move up',
                                    disabled: !shown || i === 0,
                                    onclick: () => move(-1),
                                },
                                '▲',
                            ),
                            h(
                                'button',
                                { class: 'btn', type: 'button', title: 'Move down', onclick: () => move(1) },
                                '▼',
                            ),
                            h('code', {}, row.id + (row.conditional ? ' *' : '')),
                            label,
                            desc,
                        );
                    }),
                );
            };
            draw();
            const extra =
                page.id === 'title.main'
                    ? h(
                          'div',
                          { class: 'mn-sub' },
                          h('div', { class: 'lbl' }, 'Quit card'),
                          ...(['title.quit.title', 'title.quit.body'] as const).map((k) => {
                              const i = h('input', { type: 'text', value: textOf(k), 'aria-label': k });
                              i.addEventListener('change', () => setString(k, i.value));
                              return h('div', { class: 'mn-field' }, h('label', {}, k.replace('title.quit.', '')), i);
                          }),
                      )
                    : null;
            fill(
                form,
                h('div', { class: 'lbl' }, `${page.title}: entries in order (unticked are hidden)`),
                list,
                h(
                    'div',
                    { class: 'dim small' },
                    '* shown only when the game has a use for it (a save for Continue, the multiplayer option).',
                ),
                extra,
            );
        } else if (s.kind === 'theme') {
            fill(
                form,
                h('div', { class: 'lbl' }, 'Colours (#RRGGBB or #RRGGBBAA)'),
                ...THEME_COLORS.map((c) => colorField(c.key, c.def)),
                h('div', { class: 'lbl' }, 'Text sizes (design units on 1920 x 1080)'),
                ...THEME_SIZES.map((c) => numberField(c.key, c.def, 8, 120)),
            );
        } else if (s.kind === 'hud') {
            fill(
                form,
                h('div', { class: 'lbl' }, 'HUD accent'),
                colorField('hud_accent', HUD_ACCENT_DEFAULT),
                h(
                    'div',
                    { class: 'dim small' },
                    'Respawn countdown, chat names, the results highlight and the Co-op banner.',
                ),
            );
        } else {
            fill(
                form,
                h('div', { class: 'lbl' }, 'Menu layout (design units on 1920 x 1080)'),
                ...LAYOUT_KEYS.map((c) => numberField(c.key, c.def, 0, 2000)),
            );
        }
    }

    function renderNav(): void {
        fill(
            nav,
            ...sections.map((sec) => {
                const mine =
                    sec.s.kind === 'page'
                        ? Boolean(skin.rows[sec.s.page.id]) ||
                          sec.s.page.rows.some((r) => skin.strings[labelKey(sec.id, r.id)] !== undefined)
                        : sec.s.kind === 'theme'
                          ? Object.keys(skin.colors).some((k) => k !== 'hud_accent') ||
                            Object.keys(skin.numbers).some((k) => k.startsWith('size_'))
                          : sec.s.kind === 'hud'
                            ? skin.colors['hud_accent'] !== undefined
                            : Object.keys(skin.numbers).some((k) => !k.startsWith('size_'));
                return h(
                    'button',
                    {
                        type: 'button',
                        class: `mn-nav-item${sec === cur ? ' active' : ''}`,
                        onclick: () => {
                            cur = sec;
                            renderNav();
                            renderForm();
                            renderPreview();
                        },
                    },
                    icon(sec.ico, 14),
                    sec.title,
                    mine ? h('span', { class: 'mn-dot', title: 'Changed' }) : null,
                );
            }),
        );
    }

    // ---- 2D mock preview: the 1920x1080 canvas scaled into the panel -------------------------------------------------
    const PW = 560;
    stage.style.transform = `scale(${PW / 1920})`;
    preview.style.width = `${PW}px`;
    preview.style.height = `${(PW * 1080) / 1920}px`;
    function box(x: number, y: number, w: number, hh: number, bg: string, border?: string, extra = ''): HTMLElement {
        const el = h('div', { class: 'mn-box' });
        el.style.cssText = `left:${x}px;top:${y}px;width:${w}px;height:${hh}px;background:${bg};${border ? `border:3px solid ${border};` : ''}${extra}`;
        return el;
    }
    function label(
        text: string,
        x: number,
        y: number,
        w: number,
        hh: number,
        size: number,
        color: string,
        align = 'center',
    ): HTMLElement {
        const el = h('div', { class: 'mn-text' }, text);
        el.style.cssText = `left:${x}px;top:${y}px;width:${w}px;height:${hh}px;line-height:${hh}px;font-size:${size}px;color:${color};text-align:${align}`;
        return el;
    }
    function renderPreview(): void {
        const items: HTMLElement[] = [box(0, 0, 1920, 1080, '#0b0806')];
        items.push(box(0, 0, 1920, 1080, cssColor(colorOf('backdrop'))));
        const panel = cssColor(colorOf('panel'));
        const text = cssColor(colorOf('text'));
        const muted = cssColor(colorOf('text_muted'));
        const ember = cssColor(emberOf());
        const brass = cssColor(colorOf('brass'));
        const bodyPx = numOf('size_body');
        const s = cur.s;
        const pageId = s.kind === 'page' ? s.page.id : s.kind === 'hud' ? 'hud' : 'title.main';
        const isTitle = pageId === 'title.main' || (s.kind !== 'page' && s.kind !== 'hud');
        if (pageId === 'hud') {
            const acc = cssColor(colorOf('hud_accent'));
            items.push(label('RESPAWNING IN', 480, 500, 960, 30, 18, brass));
            items.push(label('3', 480, 526, 960, 120, 96, acc));
            items.push(label('Rynn: ready when you are', 76, 880, 800, 40, 28, text, 'left'));
            items.push(label('Rynn:', 76, 880, 90, 40, 28, acc, 'left'));
            items.push(box(1500, 40, 340, 60, panel, brass));
            items.push(label('Results highlight', 1500, 40, 340, 60, 22, text));
            items.push(box(1500, 40, 340, 60, 'transparent', acc, 'border-width:4px'));
        } else if (isTitle) {
            const page = PAGES[0]!;
            const shown = pageRows(skin, page).filter((x) => x.shown);
            const n = Math.max(shown.length, 1);
            const rowH = numOf('title_row_h');
            const step = numOf('title_row_step');
            const w = numOf('title_column_w');
            const hh = n * step - (step - rowH) + 48 + 64;
            const x = (1920 - w) / 2;
            const y = numOf('title_centre_y') - hh / 2;
            items.push(box(x, y, w, hh, panel, cssColor(colorOf('brass_dim'))));
            shown.forEach(({ row }, i) => {
                const ry = y + 24 + i * step;
                const foc = i === 0;
                items.push(
                    box(
                        x + 24,
                        ry,
                        w - 48,
                        rowH,
                        foc ? cssColor(colorOf('panel_raised')) : cssColor(colorOf('inset')),
                        foc ? ember : cssColor(colorOf('brass_dim')),
                    ),
                );
                items.push(
                    label(
                        textOf(labelKey(page.id, row.id)),
                        x + 24,
                        ry,
                        w - 48,
                        rowH,
                        bodyPx + 4,
                        foc ? cssColor(colorOf('heading')) : text,
                    ),
                );
            });
            const first = shown[0]?.row.id;
            if (first)
                items.push(
                    label(
                        textOf(`title.desc.${first}`),
                        x + 24,
                        y + hh - 24 - 40,
                        w - 48,
                        40,
                        numOf('size_small'),
                        muted,
                    ),
                );
        } else if (s.kind === 'page') {
            const page = s.page;
            const shown = pageRows(skin, page).filter((x) => x.shown);
            const mx = numOf('pause_menu_x');
            const mw = numOf('pause_menu_w');
            const py = numOf('pause_panel_y');
            const step = numOf('pause_row_step');
            const rh = numOf('pause_row_h');
            items.push(box(mx, py, mw, 660, panel, cssColor(colorOf('brass_dim'))));
            items.push(box(1920 - 96 - 560, py, 560, 660, panel, cssColor(colorOf('brass_dim'))));
            items.push(
                label('Surdana', 1920 - 96 - 560 + 28, py + 24, 504, 50, 32, cssColor(colorOf('heading')), 'left'),
            );
            items.push(label(page.title, 480, 24, 960, 60, numOf('size_title'), cssColor(colorOf('heading'))));
            shown.forEach(({ row }, i) => {
                const ry = py + 36 + i * step;
                const foc = i === 0;
                items.push(
                    box(
                        mx + 28,
                        ry,
                        mw - 56,
                        rh,
                        foc ? cssColor(colorOf('panel_raised')) : cssColor(colorOf('inset')),
                        foc ? ember : cssColor(colorOf('brass_dim')),
                    ),
                );
                items.push(
                    label(
                        textOf(labelKey(page.id, row.id)),
                        mx + 28 + 24,
                        ry,
                        mw - 56 - 48,
                        rh,
                        numOf('size_heading'),
                        foc ? cssColor(colorOf('heading')) : text,
                        'left',
                    ),
                );
            });
        }
        fill(stage, ...items);
    }

    // ---- pack export -------------------------------------------------------------------------------------------------
    const metaNow = (): ModMeta => ({
        id: idIn.value.trim(),
        name: nameIn.value.trim() || 'My menus',
        version: '1.0.0',
        ...(authIn.value.trim() ? { author: authIn.value.trim() } : {}),
        description: 'Menu and HUD skin made with the Drakan editor.',
        target: host.target() ?? { profile: '*' },
    });
    async function exportPack(mode: 'zip' | 'folder'): Promise<void> {
        try {
            if (isEmptySkin(skin)) throw new Error('Nothing to export: every value is still the default.');
            const meta = metaNow();
            const files = buildUiSkinPackFiles(skin, meta);
            const lines = Object.entries(files).map(([p, b]) => `${p}  ${b.length} bytes`);
            if (mode === 'zip') {
                host.download(`${meta.id}.zip`, zipModFiles(files) as BlobPart, 'application/zip');
                lines.push(`Downloaded ${meta.id}.zip. Put it in the game's mods folder and turn it on.`);
            } else {
                const where = await writePackToModsFolder({ meta, files });
                if (where === null) return;
                lines.push(`Written to ${where} and turned on.`);
            }
            report.textContent = lines.join('\n');
            host.toast(`Exported ${meta.id}`, 'ok');
        } catch (e) {
            report.textContent = e instanceof Error ? e.message : String(e);
            host.toast(report.textContent, 'error');
        }
    }
    const importBtn = h('input', {
        type: 'file',
        accept: '.ini,text/plain',
        class: 'mn-file',
        'aria-label': 'Import skin ini',
    });
    importBtn.addEventListener('change', () => {
        const f = importBtn.files?.[0];
        if (!f) return;
        void f.text().then((t) => {
            const r = parseUiSkin(t, emptySkin());
            skin = r.skin;
            report.textContent = r.warnings.length ? r.warnings.join('\n') : `Loaded ${f.name}`;
            renderForm();
            changed();
        });
    });

    // A campaign project: these menus belong to the campaign (the game shows them only while it is chosen).
    const campaignNote = h('div', { class: 'mn-campaign', id: 'mn-campaign', role: 'note' });
    const paintCampaign = (): void => {
        const c = projects.get()?.campaign;
        campaignNote.hidden = !c?.enabled;
        if (c?.enabled)
            campaignNote.textContent = `Part of the campaign "${c.title || c.id}": the project export ships these menus with it, and the game shows them only while "${c.title || c.id}" is chosen on its title card.`;
    };
    projects.on(paintCampaign);
    paintCampaign();

    const root = h(
        'div',
        { class: 'panel menus' },
        campaignNote,
        h(
            'div',
            { class: 'mn-main' },
            nav,
            h('div', { class: 'mn-center' }, form, issues),
            h(
                'div',
                { class: 'mn-side' },
                preview,
                h(
                    'div',
                    { class: 'dim small' },
                    'Mock preview: layout and colours of the game screen (the game draws the real fonts and art).',
                ),
            ),
        ),
        h(
            'div',
            { class: 'mn-export mod-form' },
            h('label', { class: 'mf' }, h('span', {}, 'Id'), idIn),
            h('label', { class: 'mf' }, h('span', {}, 'Name'), nameIn),
            h('label', { class: 'mf' }, h('span', {}, 'Author'), authIn),
            h(
                'div',
                { class: 'btnrow' },
                h(
                    'button',
                    { class: 'btn primary', type: 'button', onclick: () => void exportPack('zip') },
                    'Export zip',
                ),
                h(
                    'button',
                    {
                        class: 'btn',
                        type: 'button',
                        title: "Write into the game's mods folder and turn it on",
                        onclick: () => void exportPack('folder'),
                    },
                    'Write to mods folder',
                ),
                h(
                    'button',
                    {
                        class: 'btn',
                        type: 'button',
                        title: 'Copy the skin as ui.ini text',
                        onclick: () =>
                            void navigator.clipboard
                                ?.writeText(serializeUiSkin(skin))
                                .then(() => host.toast('Copied ui.ini text', 'ok')),
                    },
                    'Copy INI',
                ),
                h('label', { class: 'btn' }, 'Import INI', importBtn),
                h(
                    'button',
                    {
                        class: 'btn',
                        type: 'button',
                        onclick: () => {
                            skin = emptySkin();
                            renderForm();
                            changed();
                            report.textContent = 'Reset to the defaults.';
                        },
                    },
                    'Reset all',
                ),
            ),
            report,
        ),
    );
    renderForm();
    changed();
    return {
        el: root,
        skin: () => skin,
        dirty: () => !isEmptySkin(skin),
        packFiles: (meta) => buildUiSkinPackFiles(skin, meta),
    };
}

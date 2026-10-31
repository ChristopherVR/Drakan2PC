import { MOD_ID_PATTERN, slugId, type ModProject } from '@drakan/editor-core';
import { isDesktop } from '../../platform.ts';
import { h, icon } from '../dom.ts';
import { projects } from './store.ts';
import type { ProjectPack } from './export.ts';

/**
 * The one Export: it builds the mod pack from the project (every project level with changes, copies, story edits) and
 * puts it where the user wants. Defaults: into the project folder's `mod/` (desktop, project on disk), else a save
 * dialog (desktop) or a download (browser); a checkbox also installs it into the game's mods folder.
 */
export interface ExportHost {
    /** The project, created on first use, with the open edited level added to it. */
    ensureProject(): ModProject;
    build(p: ModProject): Promise<ProjectPack>;
    /** Problems that make an export pointless (the Project tab's errors). */
    blockers(): string[];
    zip(pack: ProjectPack): Uint8Array;
    /** Desktop project folder: write `mod/<id>.zip` there; null when the project has no folder. */
    writeToProjectFolder(rel: string, bytes: Uint8Array): Promise<string | null>;
    /** Download (browser) or a "save as" dialog (desktop). Resolves to the path or null. */
    saveZip(name: string, bytes: Uint8Array): Promise<string | null>;
    installAvailable(): boolean;
    /** Write the pack into the game's mods folder and enable it. */
    install(pack: ProjectPack): Promise<string | null>;
    folderDir(): string | null;
    /** Save the project too (so what was exported matches the project file). */
    saveProject(): Promise<void>;
    toast(msg: string, kind?: 'info' | 'error' | 'ok' | 'warn'): void;
    setStatus(msg: string, error?: boolean): void;
    /** Desktop with a game folder already chosen: the install box starts ticked. */
    installDefault(): boolean;
}

export function openExportDialog(host: ExportHost): void {
    document.getElementById('ex-dlg')?.remove();
    const p0 = host.ensureProject();
    const dlg = h('dialog', { class: 'pj-dlg', id: 'ex-dlg', 'aria-label': 'Export mod' });
    document.body.append(dlg);
    dlg.addEventListener('close', () => dlg.remove());

    const field = (id: string, value: string, on: (v: string) => void): HTMLInputElement => {
        const el = h('input', { type: 'text', id, spellcheck: 'false' });
        el.value = value;
        el.addEventListener('input', () => on(el.value));
        return el;
    };
    const wrap = (label: string, el: HTMLElement): HTMLElement =>
        h('label', { class: 'pj-f' }, h('span', {}, label), el);

    let p = p0;
    const sync = (patch: Partial<ModProject>): void => {
        projects.update((q) => ({ ...q, ...patch }));
        p = projects.get() ?? p;
        schedule();
    };
    const nameEl = field('ex-name', p.name, (v) => {
        const prev = slugId(p.name);
        sync({ name: v, ...(p.id === prev || !p.id ? { id: slugId(v) } : {}) });
        idEl.value = p.id;
        paintId();
    });
    const idEl = field('ex-id', p.id, (v) => {
        sync({ id: v.trim() });
        paintId();
    });
    const verEl = field('ex-version', p.version, (v) => sync({ version: v.trim() }));
    const authEl = field('ex-author', p.author, (v) => sync({ author: v }));
    const paintId = (): void => {
        idEl.classList.toggle('bad', !MOD_ID_PATTERN.test(p.id));
        paintDest();
        goBtn.disabled = busy || !MOD_ID_PATTERN.test(p.id) || host.blockers().length > 0 || !!buildError;
    };

    const summary = h(
        'div',
        { class: 'ex-summary', id: 'ex-summary', role: 'status' },
        'Checking what the pack holds...',
    );
    const dest = h('div', { class: 'ex-dest', id: 'ex-dest' });
    const install = h('input', { type: 'checkbox', id: 'ex-install' });
    install.checked = host.installAvailable() && host.installDefault();
    const installRow = h(
        'label',
        { class: 'ex-check' },
        install,
        h('span', {}, "Also install it into the game's mods folder and turn it on"),
    );
    installRow.hidden = !host.installAvailable();
    const result = h('pre', { class: 'ex-result', id: 'ex-result', hidden: true });

    const folder = host.folderDir();
    const paintDest = (): void => {
        const id = p.id || 'my-mod';
        dest.replaceChildren(
            h('span', { class: 'ex-lbl' }, 'Goes to'),
            h(
                'code',
                {},
                folder
                    ? `${folder}\\mod\\${id}.zip`
                    : `${id}.zip (${isDesktop ? 'you choose where' : 'your downloads'})`,
            ),
        );
    };

    let busy = false;
    let buildError = '';
    let last: ProjectPack | null = null;
    let timer = 0;
    function schedule(): void {
        window.clearTimeout(timer);
        timer = window.setTimeout(() => void preview(), 250);
    }
    async function preview(): Promise<void> {
        buildError = '';
        try {
            last = await host.build(projects.get() ?? p);
            const files = Object.keys(last.files).length;
            summary.replaceChildren(
                h('strong', {}, `${files} file${files === 1 ? '' : 's'} in the pack`),
                h(
                    'ul',
                    {},
                    ...[...last.lines, ...last.warnings.map((w) => `Warning: ${w}`)].map((l) =>
                        h('li', { class: l.startsWith('Warning') ? 'warn' : '' }, l),
                    ),
                ),
            );
        } catch (e) {
            last = null;
            buildError = e instanceof Error ? e.message : String(e);
            summary.replaceChildren(h('span', { class: 'bad' }, buildError));
        }
        const bl = host.blockers();
        if (bl.length) summary.append(h('p', { class: 'bad' }, `Fix first: ${bl.slice(0, 3).join(' ')}`));
        paintId();
    }

    const cancel = h(
        'button',
        { class: 'ui-btn', type: 'button', id: 'ex-cancel', onclick: () => dlg.close() },
        'Cancel',
    );
    const goBtn = h('button', { class: 'ui-btn primary', type: 'button', id: 'ex-go' }, icon('download', 14), 'Export');
    goBtn.addEventListener('click', () => {
        void run().catch((e: unknown) => {
            const msg = e instanceof Error ? e.message : String(e);
            result.hidden = false;
            result.textContent = msg;
            host.setStatus(msg, true);
            host.toast(msg, 'error');
            busy = false;
            paintId();
        });
    });

    async function run(): Promise<void> {
        busy = true;
        paintId();
        const cur = projects.get() ?? p;
        const pack = await host.build(cur);
        const zip = host.zip(pack);
        const text = [...pack.lines, ...pack.warnings.map((w) => `Warning: ${w}`)];
        let where: string | null;
        if (folder) {
            where = await host.writeToProjectFolder(`mod/${pack.meta.id}.zip`, zip);
        } else {
            where = await host.saveZip(`${pack.meta.id}.zip`, zip);
            if (where === null && isDesktop) {
                busy = false;
                paintId();
                return; // the save dialog was cancelled
            }
        }
        text.push(`${pack.meta.id}.zip (${Object.keys(pack.files).length} files): ${where ?? 'downloaded'}`);
        let place = where ?? `${pack.meta.id}.zip (downloads)`;
        if (install.checked && host.installAvailable()) {
            const inst = await host.install(pack);
            if (inst) {
                text.push(`Installed into ${inst} and enabled in mods.ini.`);
                place += `; installed in ${inst}`;
            }
        }
        await host.saveProject();
        projects.session.exported = true;
        projects.session.lastExport = { where: place, at: Date.now() };
        projects.notify();
        result.hidden = false;
        result.textContent = text.join('\n');
        host.setStatus(`Exported ${pack.meta.id}.zip.`);
        host.toast(`Exported ${pack.meta.id}.zip`, 'ok');
        busy = false;
        cancel.textContent = 'Close';
        paintId();
    }

    dlg.append(
        h('div', { class: 'pj-dlg-head' }, 'Export mod'),
        h(
            'div',
            { class: 'pj-dlg-body' },
            h(
                'p',
                { class: 'pj-note' },
                'The pack holds every level of the project that has changes, its level copies and the story edits.',
            ),
            h('div', { class: 'pj-row2' }, wrap('Name', nameEl), wrap('Id', idEl)),
            h('div', { class: 'pj-row2' }, wrap('Version', verEl), wrap('Author', authEl)),
            summary,
            dest,
            installRow,
            result,
        ),
        h('div', { class: 'pj-dlg-foot' }, cancel, goBtn),
    );
    dlg.showModal();
    goBtn.disabled = true;
    paintDest();
    void preview();
    nameEl.select();
}

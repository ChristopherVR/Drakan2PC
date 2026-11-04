import {
    RemoveAddedItemCommand,
    ReplaceItemCommand,
    SetFieldCommand,
    SaveDocument,
    TransformCommand,
    type PendingEdit,
    type Transform,
} from '@drakan/editor-core';
import {
    FIELDS,
    KNOWN_CLASSES,
    SaveFieldError,
    TYPE_NAMES,
    TYPE_RECORD,
    classHex,
    isRynnClass,
    diffQuests,
    diffSaves,
    formatFieldValue,
    locateField,
    parseFieldValue,
    parseSave,
    recordLabel,
    resolveField,
    summarizeSave,
    type FieldValue,
    type QuestDiffRow,
    type RecordInfo,
    type SaveDiff,
    type SaveFile,
    nextBackupName,
} from '@drakan/formats';
import { LevelView } from './level.ts';
import './save/save.css';
import './save/drakan.css';
import {
    badge,
    button,
    card,
    emptyState,
    fileChanged,
    h,
    hex,
    hexDump,
    icon,
    shotCanvas,
    statTile,
} from './save/dom.ts';
import { TABS, buildShell, type SubTab } from './save/shell.ts';
import { drawMap, groupColor, mapBounds, pickMap, type MapBounds, type MapPoint } from './save/map.ts';
import type { ClassInfo } from './save/inventory.ts';
import type { StoryData } from './save/storyart.ts';
import { listQuestsSafe, questCompare } from './save/journal.ts';
import { renderItems, renderRepair, renderStory, renderTransfer, type PanelCtx } from './save/panels.ts';
import {
    canWriteRef,
    download,
    hasFsa,
    pickSaveFile,
    pickSaveFolder,
    refFromFile,
    refsFromFileList,
    saveWithBackup,
    listBackups,
    backupChain,
    statRef,
    type SaveRef,
} from './saveio.ts';
import { isDesktop } from './platform.ts';
import { changeRow, listRow } from './shared/components.ts';

const $ = <T extends HTMLElement>(id: string): T => {
    const el = document.getElementById(id);
    if (!el) throw new Error(`missing #${id}`);
    return el as T;
};

export interface SaveUiHost {
    setStatus(msg: string, error?: boolean): void;
    /** Game name and model render of an item class (`0x31bc9`), when a level with that class is open in Level mode. */
    classInfo?(resource: string, thumbnail?: boolean): ClassInfo | null;
    /** Subscribe to "item names or pictures arrived" so the Items tab can redraw. */
    onItemArt?(fn: () => void): void;
    /** Quest classes and journal text of the open disc. */
    story?(): StoryData | null;
    /** Subscribe to "the story catalog loaded or was reset" so the Story tab can redraw. */
    onStory?(fn: () => void): void;
}

interface Open {
    ref: SaveRef;
    doc: SaveDocument;
    /** The file as read (the "original" for compare), kept for revert-free diffs. */
    original: SaveFile;
}

/** The Save tab: open, character panel, world list, 3D view, pending edits, SAVE/EXPORT, compare. */
export function initSaveUi(host: SaveUiHost): { openFile(f: File): Promise<void> } {
    const fsa = hasFsa();
    buildShell($('save-main'), fsa);
    let view: LevelView | null = null;
    let cur: Open | null = null;
    let discovered: SaveRef[] = [];
    let subtab: SubTab = 'character';
    let diff: { a: string; b: string; result: SaveDiff; quests: QuestDiffRow[] } | null = null;
    let selectedEntry: number | null = null;
    let hexLimit = 512;
    let busy = false;

    let artFrame = 0;
    host.onItemArt?.(() => {
        if (artFrame || !cur || subtab !== 'items') return;
        artFrame = requestAnimationFrame(() => {
            artFrame = 0;
            if (cur && subtab === 'items') renderExtra();
        });
    });

    host.onStory?.(() => {
        if (cur && (subtab === 'story' || subtab === 'compare')) renderExtra();
    });

    let toastTimer = 0;
    /** Status line (header and footer). `toast` also pops a short message over the workspace. */
    function status(msg: string, error?: boolean, toast = false): void {
        host.setStatus(msg, error);
        const foot = $('sv-foot-msg');
        foot.textContent = msg;
        foot.classList.toggle('bad', !!error);
        $('save-main').classList.toggle('sv-error', !!error);
        if (!toast && !error) return;
        const t = $('sv-toast');
        t.textContent = msg;
        t.className = `sv-toast${error ? ' bad' : ' ok'}`;
        t.hidden = false;
        window.clearTimeout(toastTimer);
        toastTimer = window.setTimeout(() => (t.hidden = true), error ? 7000 : 4000);
    }

    const err = (e: unknown): void => status(e instanceof Error ? e.message : String(e), true);

    // ---- 3D ----------------------------------------------------------------------------------------------------

    function getView(): LevelView {
        if (view) return view;
        view = new LevelView($('sv-view'), {
            onSelection: (ids) => onViewSelection(ids),
            onChange: () => refresh(),
        });
        view.policy = {
            structural: false,
            translateOnly: true,
            movable: (id) => cur?.doc.playerObject === id,
        };
        view.setActive(true);
        return view;
    }

    function onViewSelection(ids: number[]): void {
        if (!cur) return;
        const first = ids.length === 1 ? cur.doc.entryOfObject.get(ids[0]!) : undefined;
        if (first === undefined || first === selectedEntry) return;
        showDetails(first, false);
        if (!$('sv-groups').querySelector(`[data-index="${first}"]`)) renderWorldList();
    }

    function showIn3d(entry: number): void {
        if (!cur) return;
        const id = cur.doc.objectOfEntry.get(entry);
        if (id === undefined) return;
        const v = getView();
        v.select([id]);
        v.frameSelection();
        status(
            id === cur.doc.playerObject
                ? 'Player marker selected: drag the gizmo to move it; the saved position changes when you SAVE.'
                : 'Record selected in 3D (read only; only the player marker can be moved).',
        );
    }

    // ---- open --------------------------------------------------------------------------------------------------

    async function openRef(ref: SaveRef): Promise<void> {
        if (cur && cur.doc.pendingEdits().length && !confirm('Discard the pending edits of the open save?')) return;
        status(`Opening ${ref.name}...`);
        try {
            const bytes = await ref.read();
            const doc = SaveDocument.open(bytes);
            cur = { ref, doc, original: doc.save };
            selectedEntry = null;
            hexLimit = 512;
            diff = null;
            getView().setDocument(doc);
            if (lastBackup?.file !== ref.name) lastBackup = null;
            backupsSeen = [];
            renderAll();
            void refreshBackups();
            markActiveInList();
            status(
                `${ref.name}: ${doc.save.entries.length} records, ${doc.size} positioned, level "${doc.level || 'unknown'}".`,
            );
        } catch (e) {
            err(e);
        }
    }

    async function openFile(f: File): Promise<void> {
        discovered = discovered.filter((r) => r.folder !== '' || r.name !== f.name);
        if (!discovered.length) $('sv-folder-name').textContent = f.name;
        await openRef(refFromFile(f));
    }

    $('sv-open').onclick = () => {
        if (!fsa) {
            $<HTMLInputElement>('sv-file').click();
            return;
        }
        pickSaveFile()
            .then((r) => (r ? openRef(r) : undefined))
            .catch(err);
    };
    $('sv-folder').onclick = () => {
        if (!fsa) {
            $<HTMLInputElement>('sv-dir').click();
            return;
        }
        pickSaveFolder()
            .then((r) => (r ? setDiscovered(r.root, r.refs) : undefined))
            .catch(err);
    };
    $<HTMLInputElement>('sv-file').onchange = (e) => {
        const i = e.target as HTMLInputElement;
        const f = i.files?.[0];
        i.value = '';
        if (f) openFile(f).catch(err);
    };
    $<HTMLInputElement>('sv-dir').onchange = (e) => {
        const i = e.target as HTMLInputElement;
        const found = refsFromFileList(i.files ?? []);
        i.value = '';
        setDiscovered(found.root, found.refs).catch(err);
    };

    // ---- save list ---------------------------------------------------------------------------------------------

    const listEl = $('sv-list');
    const listQ = $<HTMLInputElement>('sv-list-q');
    listQ.oninput = () => filterList();
    let listRows: { ref: SaveRef; li: HTMLElement; hay: string }[] = [];

    function filterList(): void {
        const q = listQ.value.trim().toLowerCase();
        let shown = 0;
        for (const r of listRows) {
            const hit = !q || r.hay.includes(q);
            r.li.hidden = !hit;
            if (hit) shown++;
        }
        listEl.querySelector('.sv-list-none')?.remove();
        if (listRows.length && !shown) {
            listEl.append(
                h(
                    'li',
                    { class: 'sv-list-none' },
                    emptyState(
                        'search',
                        'No match',
                        `No save matches "${listQ.value}". Clear the search to see all ${listRows.length}.`,
                    ),
                ),
            );
        }
    }

    async function setDiscovered(root: string, refs: SaveRef[]): Promise<void> {
        discovered = refs;
        $('sv-count').textContent = refs.length ? String(refs.length) : '';
        $('sv-folder-name').textContent = root || 'Opened file';
        $('sv-folder-name').title = root;
        renderCompareChoices();
        listRows = [];
        if (!refs.length) {
            listEl.replaceChildren(
                h(
                    'li',
                    { class: 'sv-list-empty' },
                    emptyState(
                        'folder',
                        'No saves found',
                        `No .rsg files under "${root}". Pick the memory card folder.`,
                    ),
                ),
            );
            return;
        }
        const rows = refs.map((ref) => {
            const meta = h(
                'div',
                { class: 'sv-slot-meta' },
                h('b', null, ref.name.replace(/\.rsg$/i, '')),
                h('small', null, 'Reading...'),
            );
            const thumb = h('div', { class: 'sv-slot-thumb' }, icon('file', 18));
            const li = listRow(
                {
                    cls: 'sv-slot',
                    title: `${ref.folder}/${ref.name}`,
                    key: `${ref.folder}/${ref.name}`,
                    onActivate: () => void openRef(ref),
                },
                thumb,
                meta,
            );
            const row = { ref, li, thumb, meta, hay: `${ref.folder} ${ref.name}`.toLowerCase() };
            listRows.push(row);
            return row;
        });
        listEl.replaceChildren(...rows.map((r) => r.li));
        status(`Found ${refs.length} saves in "${root}". Reading previews...`);
        for (const r of rows) {
            try {
                const sum = summarizeSave(await r.ref.read());
                if (sum.screenshot) r.thumb.replaceChildren(shotCanvas(sum.screenshot, 'sv-shot'));
                const slot = r.ref.folder.replace(/^BESCES-\d+/i, '').replace(/^Slot/i, 'Slot ') || 'File';
                r.hay += ` ${sum.levelName} ${slot}`.toLowerCase();
                r.meta.replaceChildren(
                    h('b', null, sum.levelName || r.ref.name.replace(/\.rsg$/i, '')),
                    h('small', null, [slot, fileChanged(r.ref.modified)].filter(Boolean).join(' \u00b7 ')),
                    h(
                        'div',
                        { class: 'sv-slot-tags' },
                        h('span', { class: 'sv-tag' }, `${(r.ref.size / 1024).toFixed(0)} KB`),
                        h('span', { class: 'sv-tag' }, `${sum.entryCount} records`),
                    ),
                );
            } catch (e) {
                r.meta.replaceChildren(
                    h('b', null, r.ref.name),
                    h('small', { class: 'error' }, e instanceof Error ? e.message : 'unreadable'),
                );
            }
        }
        status(`Found ${refs.length} saves in "${root}". Select one to open it.`);
        filterList();
        markActiveInList();
    }

    function markActiveInList(): void {
        const key = cur ? `${cur.ref.folder}/${cur.ref.name}` : '';
        for (const li of listEl.querySelectorAll<HTMLElement>('li.sv-slot')) {
            li.classList.toggle('sel', li.dataset.key === key);
            li.setAttribute('aria-current', li.dataset.key === key ? 'true' : 'false');
        }
    }

    // ---- toolbar -----------------------------------------------------------------------------------------------

    const btn = (id: string): HTMLButtonElement => $<HTMLButtonElement>(id);
    btn('sv-undo').onclick = () => view?.undo();
    btn('sv-redo').onclick = () => view?.redo();
    btn('sv-export').onclick = () => {
        if (!cur) return;
        try {
            const { bytes } = cur.doc.build();
            const name = cur.ref.name;
            void download(name, bytes).then((where) => {
                if (isDesktop && !where) return; // save dialog cancelled
                status(
                    `Exported ${where ?? name} (${bytes.length} bytes). Copy it over your save, keeping a backup.`,
                    false,
                    true,
                );
            }, err);
        } catch (e) {
            err(e);
        }
    };
    btn('sv-save').onclick = () => void saveInPlace();
    btn('sv-save2').onclick = () => void saveInPlace();
    btn('sv-reload').onclick = () => {
        if (cur) void openRef(cur.ref);
    };
    const revertAll = (): void => {
        if (!cur || !view) return;
        const n = cur.doc.pendingEdits().length;
        if (!n || !confirm(`Throw away ${n} pending change${n === 1 ? '' : 's'}?`)) return;
        while (view.stack.canUndo) view.undo();
        status('Reverted every pending change.', false, true);
    };
    btn('sv-revert').onclick = revertAll;
    btn('sv-revert-all').onclick = revertAll;
    btn('sv-frame-player').onclick = () => {
        if (cur?.doc.playerObject != null) showIn3d(cur.doc.entryOfObject.get(cur.doc.playerObject)!);
        else status('This save has no player record with a position.', true);
    };
    btn('sv-frame-all').onclick = () => view?.frameAll();
    const DOCK_KEY = 'drakan.save.dockHidden';
    const setDock = (hidden: boolean, remember: boolean): void => {
        document.querySelector('#save-main .sv-work')?.classList.toggle('dock-hidden', hidden);
        const t = btn('sv-dock-toggle');
        t.setAttribute('aria-pressed', String(!hidden));
        t.title = hidden ? 'Show the pending-changes and 3D panel' : 'Hide the pending-changes and 3D panel';
        if (remember) {
            try {
                localStorage.setItem(DOCK_KEY, hidden ? '1' : '0');
            } catch {
                /* storage can be unavailable */
            }
        }
        if (!hidden) requestAnimationFrame(() => window.dispatchEvent(new Event('resize')));
    };
    let dockStored = false;
    try {
        dockStored = localStorage.getItem(DOCK_KEY) === '1';
    } catch {
        /* storage can be unavailable */
    }
    setDock(dockStored, false);
    btn('sv-dock-toggle').onclick = () =>
        setDock(!document.querySelector('#save-main .sv-work')?.classList.contains('dock-hidden'), true);

    // ---- backups: what exists beside the open file, what the next save will write ------------------------------
    let backupsSeen: string[] = [];
    let lastBackup: { file: string; backup: string } | null = null;
    let backupsListed = false;

    async function refreshBackups(): Promise<void> {
        const ref = cur?.ref;
        let list: string[] | null = null;
        try {
            list = ref ? await listBackups(ref) : null;
        } catch {
            list = null;
        }
        if (cur?.ref !== ref && cur) return;
        if (list) backupsSeen = list;
        backupsListed = list !== null;
        renderBackups();
    }

    function renderBackups(): void {
        const listed = backupsListed;
        const box = $('sv-backups');
        if (!cur || !canWriteRef(cur.ref)) {
            box.replaceChildren();
            box.hidden = true;
            return;
        }
        box.hidden = false;
        const name = cur.ref.name;
        const known = backupChain(new Set([...backupsSeen, ...(lastBackup ? [lastBackup.backup] : [])]), name);
        const next = nextBackupName(known, name);
        const chips = known.map((b, i) =>
            h(
                'span',
                { class: `sv-bak${b === lastBackup?.backup ? ' new' : ''}`, title: i === 0 ? 'The oldest backup' : '' },
                b.slice(name.length),
            ),
        );
        box.replaceChildren(
            h('h3', null, icon('save', 12), 'Backups'),
            h(
                'p',
                null,
                `Before each save the file on disk is copied to ${name}.bak. A later save never overwrites a backup: it ` +
                    'writes .bak.1, then .bak.2 and so on. The highest number is the newest backup.',
            ),
            known.length
                ? h(
                      'div',
                      { class: 'sv-bak-row' },
                      h('span', { class: 'sv-dim' }, 'Oldest'),
                      ...chips,
                      h('span', { class: 'sv-dim' }, 'newest'),
                  )
                : h(
                      'p',
                      { class: 'sv-dim' },
                      listed ? 'No backups beside this file yet.' : 'No backup written in this session yet.',
                  ),
            h('p', { class: 'sv-bak-next' }, `The next save writes ${next}.`),
            lastBackup && lastBackup.file === name
                ? h('p', { class: 'sv-bak-last' }, `Last save: the previous file is now ${lastBackup.backup}.`)
                : '',
        );
    }

    async function saveInPlace(): Promise<void> {
        if (!cur || busy) return;
        busy = true;
        try {
            const { bytes } = cur.doc.build();
            if (!canWriteRef(cur.ref)) {
                // No File System Access here: hand the edited file over as a download.
                const name = cur.ref.name;
                const where = await download(name, bytes);
                if (isDesktop && !where) return;
                status(
                    `Downloaded ${where ?? name}. This browser cannot write in place: copy it over your save and keep the original as a backup.`,
                    false,
                    true,
                );
                return;
            }
            const backup = await saveWithBackup(cur.ref, bytes);
            const ref = cur.ref;
            const file = await statRef(ref);
            cur = null; // reopen from what is now on disk
            await openRef({ ...ref, size: file.size, modified: file.modified, read: async () => bytes });
            lastBackup = { file: ref.name, backup };
            backupsSeen = [...backupsSeen, backup];
            void refreshBackups();
            status(
                `Saved ${ref.name}; the previous file is kept as ${backup} (backups are never overwritten).`,
                false,
                true,
            );
        } catch (e) {
            if (e instanceof DOMException && e.name === 'AbortError') status('Save cancelled; nothing was written.');
            else err(e);
        } finally {
            busy = false;
        }
    }

    window.addEventListener('beforeunload', (e) => {
        if (cur && cur.doc.pendingEdits().length) e.preventDefault();
    });

    // ---- sub tabs ----------------------------------------------------------------------------------------------

    const panes = Object.fromEntries(TABS.map(([k]) => [k, $(`sv-${k}`)])) as Record<SubTab, HTMLElement>;
    function showSub(t: SubTab): void {
        subtab = t;
        for (const k of Object.keys(panes) as SubTab[]) {
            panes[k].hidden = k !== t;
            $(`sv-sub-${k}`).classList.toggle('active', k === t);
            $(`sv-sub-${k}`).setAttribute('aria-selected', String(k === t));
        }
        renderExtra();
        if (cur && t === 'world') renderWorldList();
    }
    for (const k of Object.keys(panes) as SubTab[]) $(`sv-sub-${k}`).onclick = () => showSub(k);

    function panelCtx(): PanelCtx {
        const doc = cur!.doc;
        return {
            doc,
            run: (c) => getView().run(c),
            setPlayerPos: (v) => {
                if (doc.playerObject === null) return;
                for (let i = 0; i < 3; i++) getView().setTransformField(doc.playerObject, 'pos', i, v[i]!);
            },
            err,
            status: (m, e) => status(m, e),
            showRecord: (entry, in3d) => {
                showSub('world');
                showDetails(entry, in3d);
                renderWorldList();
            },
            listed: () => discovered,
            changed: () => refresh(),
            rerender: () => renderExtra(),
            classInfo: host.classInfo,
            story: host.story,
        };
    }

    /** Render the pane of the current extra tab (they are rebuilt on demand, not kept in sync). */
    function renderExtra(): void {
        const render =
            subtab === 'items'
                ? renderItems
                : subtab === 'story'
                  ? renderStory
                  : subtab === 'repair'
                    ? renderRepair
                    : subtab === 'transfer'
                      ? (c: PanelCtx) => renderTransfer(c, renderExtra)
                      : null;
        if (!render) return;
        const catalogSearch = document.activeElement?.id === 'sv-catalog-search';
        panes[subtab].replaceChildren(cur ? render(panelCtx()) : noSave());
        if (catalogSearch) document.getElementById('sv-catalog-search')?.focus({ preventScroll: true });
    }

    function noSave(): HTMLElement {
        return emptyState(
            'folder',
            'Open a save to start',
            'Pick a slot from the list on the left, or use Open folder to list every slot on the memory card. Nothing is uploaded; files are read in your browser.',
            button('Open folder', { icon: 'folder', kind: 'primary', onclick: () => $('sv-folder').click() }),
            button('Open save', { icon: 'file', onclick: () => $('sv-open').click() }),
        );
    }

    // ---- refresh -----------------------------------------------------------------------------------------------

    function refresh(): void {
        const pending = cur?.doc.pendingEdits() ?? [];
        const v = view;
        const scroll = panes[subtab].scrollTop;
        btn('sv-undo').disabled = !v?.stack.canUndo;
        btn('sv-redo').disabled = !v?.stack.canRedo;
        btn('sv-undo').title = v?.stack.undoLabel ? `Undo: ${v.stack.undoLabel}` : 'Undo';
        btn('sv-redo').title = v?.stack.redoLabel ? `Redo: ${v.stack.redoLabel}` : 'Redo';
        const canWrite = canWriteRef(cur?.ref);
        btn('sv-export').disabled = !cur;
        btn('sv-reload').disabled = !cur;
        btn('sv-revert').disabled = !pending.length;
        btn('sv-revert-all').disabled = !pending.length;
        for (const id of ['sv-save', 'sv-save2']) {
            btn(id).disabled = !cur || pending.length === 0;
            btn(id).title = !cur
                ? 'Open a save first'
                : !canWrite
                  ? 'This save was opened without write access (no File System Access API in this browser, or the file was dropped). Save downloads the edited file instead.'
                  : 'Writes the file in place; the previous contents are kept as a .bak next to it (never overwritten: .bak, .bak.1, ...)';
        }
        $('sv-pending').textContent = String(pending.length);
        $('sv-pending').classList.toggle('on', pending.length > 0);
        $('sv-dirty').hidden = !pending.length;
        $('sv-dock-badge').textContent = String(pending.length);
        $('sv-dock-badge').hidden = !pending.length;
        $('sv-save-note').textContent = !cur
            ? 'Open a save to edit it.'
            : canWrite
              ? `Save writes ${cur.ref.name} in place and keeps the old file as a backup beside it.`
              : 'Save downloads the edited .rsg (this browser cannot write in place). Copy it over your save and keep the original as a backup.';
        $('sv-title').textContent = cur ? cur.doc.level || cur.ref.name : 'No save open';
        $('sv-subtitle').textContent = cur
            ? `${cur.ref.name} · ${(cur.ref.size / 1024).toFixed(1)} KB · ${cur.doc.save.entries.length} records`
            : 'Pick a save on the left.';
        renderBackups();
        renderCharacter();
        renderEdits(pending);
        renderExtra();
        panes[subtab].scrollTop = scroll;
    }

    function renderAll(): void {
        refresh();
        renderWorld();
        renderCompare();
    }

    // ---- character ---------------------------------------------------------------------------------------------

    /** Only doubtful fields carry a badge; "high" is the normal case and would repeat on every row. */
    function confidenceBadge(name: string): HTMLElement | null {
        const def = resolveField(name).def;
        const unconfirmed = cur?.doc.probe(name).status === 'unconfirmed';
        if (unconfirmed) {
            return badge('unconfirmed', 'bad', 'This save may not support this field. ' + def.note);
        }
        return def.confidence === 'high' ? null : badge(def.confidence, def.confidence, def.note);
    }

    function lockControl(name: string): HTMLElement | null {
        const doc = cur!.doc;
        const p = doc.probe(name);
        if (p.status === 'missing') return h('small', { class: 'sv-dim', title: p.reason }, 'not present in this save');
        if (p.status === 'ok') return null;
        const cb = h('input', {
            type: 'checkbox',
            checked: doc.unlocked.has(name),
            onchange: () => {
                if (cb.checked) doc.unlocked.add(name);
                else if (!doc.fieldValues.has(name)) doc.unlocked.delete(name);
                else {
                    cb.checked = true;
                    status('Undo the pending edit of this field before locking it again.', true);
                }
                renderCharacter();
            },
        });
        return h('label', { class: 'unlock', title: p.reason }, cb, ' unlock editing (unconfirmed layout)');
    }

    /** Commit a typed or dragged value of a numeric field through the document (one undoable command). */
    function commitNumber(name: string, raw: string, input: HTMLInputElement): void {
        const doc = cur!.doc;
        try {
            const v = parseFieldValue(resolveField(name).def, raw);
            const problem = doc.checkEdit(name, v);
            if (problem) throw new SaveFieldError(problem);
            getView().run(new SetFieldCommand(name, v));
        } catch (e) {
            err(e);
            input.value = String(doc.value(name) ?? '');
        }
    }

    /**
     * One editable stat: label with confidence badge, an optional slider (value over `max`), the number box and a
     * "was N" note while it differs from the file. `compact` drops the label row (used inside the skills table).
     */
    function statField(
        name: string,
        label: string,
        opts: { max?: number; step?: number; slider?: boolean; hint?: string; compact?: boolean } = {},
    ): HTMLElement {
        const doc = cur!.doc;
        const p = doc.probe(name);
        const value = doc.value(name);
        const missing = p.status === 'missing' || value === undefined;
        const input = h('input', {
            class: 'sv-num',
            type: 'number',
            step: opts.step ?? 1,
            min: 0,
            value: missing ? '' : String(value),
            disabled: !doc.editable(name),
            'data-field': name,
            'aria-label': label,
            onchange: () => commitNumber(name, input.value, input),
        });
        const orig = doc.originalValue(name);
        const changed = !missing && orig !== undefined && value !== orig;
        const max = opts.max;
        let meter: HTMLElement | null = null;
        if (max !== undefined && !missing) {
            const pct = (n: number): string => `${Math.max(0, Math.min(100, (n / Math.max(max, 1)) * 100))}%`;
            if (opts.slider) {
                const range = h('input', {
                    class: 'sv-range',
                    type: 'range',
                    min: 0,
                    max,
                    step: opts.step ?? 1,
                    value: String(value),
                    disabled: !doc.editable(name),
                    'aria-label': `${label} slider`,
                    style: `--p:${pct(Number(value))}`,
                    oninput: () => {
                        input.value = range.value;
                        range.style.setProperty('--p', pct(Number(range.value)));
                    },
                    onchange: () => commitNumber(name, range.value, input),
                });
                meter = range;
            } else {
                meter = h(
                    'div',
                    { class: 'sv-meter', title: `${value} of ${max}` },
                    h('i', { style: `width:${pct(Number(value))}` }),
                );
            }
        }
        return h(
            'div',
            { class: `sv-field${changed ? ' changed' : ''}${opts.compact ? ' compact' : ''}` },
            opts.compact
                ? null
                : h(
                      'div',
                      { class: 'sv-field-head' },
                      h('label', { title: opts.hint }, label),
                      confidenceBadge(name),
                      lockControl(name),
                  ),
            h(
                'div',
                { class: 'sv-field-body' },
                meter,
                input,
                changed ? h('small', { class: 'sv-was', title: 'Value in the file as opened' }, `was ${orig}`) : null,
            ),
            opts.hint && !opts.compact ? h('small', { class: 'sv-hint' }, opts.hint) : null,
        );
    }

    function positionField(): HTMLElement {
        const doc = cur!.doc;
        const p = doc.probe('pos');
        const head = h(
            'div',
            { class: 'sv-field-head' },
            h('label', null, 'Rynn’s position'),
            confidenceBadge('pos'),
            lockControl('pos'),
        );
        if (p.status === 'missing' || doc.playerObject === null) {
            return h(
                'div',
                { class: 'sv-field' },
                head,
                h('small', { class: 'sv-hint' }, p.status === 'missing' ? p.reason : 'This save has no player marker.'),
            );
        }
        const v = doc.value('pos') as number[];
        const o = doc.originalValue('pos') as number[];
        const inputs = ['x', 'y', 'z'].map((axis, i) => {
            const input = h('input', {
                class: 'sv-num',
                type: 'number',
                step: 10,
                value: String(Number(v[i]!.toPrecision(9))),
                'data-field': `pos.${axis}`,
                'aria-label': `Position ${axis}`,
                onchange: () => {
                    const n = Number(input.value);
                    if (!Number.isFinite(n) || input.value.trim() === '') {
                        err(new Error('coordinates must be finite numbers'));
                        input.value = String(v[i]);
                        return;
                    }
                    getView().setTransformField(doc.playerObject!, 'pos', i, n);
                },
            });
            return h('label', { class: `sv-axis ${axis}` }, h('span', null, axis.toUpperCase()), input);
        });
        const changed = v.some((x, i) => Math.fround(x) !== Math.fround(o[i]!));
        return h(
            'div',
            { class: `sv-field${changed ? ' changed' : ''}` },
            head,
            h('div', { class: 'sv-axes' }, ...inputs),
            h(
                'small',
                { class: 'sv-hint' },
                'Game units. Drag the green player marker in the 3D view to move her.',
                changed
                    ? h(
                          'span',
                          { class: 'sv-was' },
                          ` was ${formatFieldValue(FIELDS.pos!, o as [number, number, number])}`,
                      )
                    : null,
            ),
            button('Show in 3D', {
                icon: 'cube',
                kind: 'sm',
                onclick: () => $('sv-frame-player').click(),
                title: 'Select the player marker and frame it',
            }),
        );
    }

    /** A bar for a value that is shown but not edited here (mana), or the hero health bar. */
    function vitalBar(label: string, now: number | null, max: number | null, kind: string, note?: string): HTMLElement {
        const pct = now === null || max === null ? 0 : Math.max(0, Math.min(100, (now / Math.max(max, 1)) * 100));
        return h(
            'div',
            { class: `sv-vital ${kind}`, title: now === null || max === null ? '' : `${now} of ${max}` },
            h(
                'div',
                { class: 'sv-vital-head' },
                h('span', null, label),
                h('b', null, now === null ? 'n/a' : now.toLocaleString()),
                h('small', null, max === null ? '' : `/ ${max.toLocaleString()}`),
            ),
            h('div', { class: 'sv-meter lg' }, h('i', { style: `width:${pct}%` })),
            note ? h('small', { class: 'sv-hint' }, note) : null,
        );
    }

    function renderCharacter(): void {
        const pane = panes.character;
        if (!cur) {
            pane.replaceChildren(noSave());
            return;
        }
        const { doc, ref } = cur;
        const sum = (() => {
            try {
                return summarizeSave(doc.bytes);
            } catch {
                return null;
            }
        })();
        const num = (n: string): number | null => {
            const v = doc.value(n);
            return typeof v === 'number' ? v : null;
        };
        const hp = num('stat.hp');
        const maxhp = num('stat.maxhp');
        const mana = num('stat.mana');
        const maxmana = num('stat.maxmana');
        const skills = doc.skills;
        const skillTotal = skills ? skills.reduce((n, r) => n + Number(doc.value(`skill.${r.id}`) ?? r.level), 0) : 0;
        const skillCap = skills ? skills.reduce((n, r) => n + Number(doc.value(`skill.${r.id}.max`) ?? r.max), 0) : 0;

        // ---- hero: portrait (the save's own screenshot), identity, vitals
        const heal = button('Heal to full', {
            icon: 'plus',
            kind: 'sm',
            disabled: !doc.editable('stat.hp') || maxhp === null || hp === maxhp,
            title: 'Set health to max health',
            onclick: () => maxhp !== null && getView().run(new SetFieldCommand('stat.hp', maxhp)),
        });
        const hero = h(
            'section',
            { class: 'sv-hero' },
            h(
                'div',
                { class: 'sv-portrait' },
                sum?.screenshot
                    ? shotCanvas(sum.screenshot, 'sv-shot')
                    : h('div', { class: 'sv-shot none' }, 'No screenshot'),
            ),
            h(
                'div',
                { class: 'sv-hero-main' },
                h(
                    'div',
                    { class: 'sv-hero-id' },
                    h('h3', null, doc.level || ref.name),
                    h(
                        'div',
                        { class: 'sv-tags' },
                        ref.folder ? h('span', { class: 'sv-tag' }, ref.folder.replace(/^BESCES-\d+/i, '')) : null,
                        h('span', { class: 'sv-tag' }, `${(ref.size / 1024).toFixed(1)} KB`),
                        ref.modified ? h('span', { class: 'sv-tag' }, fileChanged(ref.modified)) : null,
                        h('span', { class: 'sv-tag' }, `${doc.save.entries.length} records`),
                    ),
                    sum?.levelPath ? h('small', { class: 'sv-faint' }, sum.levelPath) : null,
                ),
                doc.hasStats
                    ? h(
                          'div',
                          { class: 'sv-vitals' },
                          h(
                              'div',
                              { class: 'sv-vital-wrap' },
                              statField('stat.hp', 'Health', { max: maxhp ?? undefined, slider: true }),
                              heal,
                          ),
                          vitalBar('Mana', mana, maxmana, 'mana', 'Read only.'),
                      )
                    : null,
            ),
        );

        if (!doc.hasStats) {
            pane.replaceChildren(
                hero,
                h(
                    'div',
                    { class: 'sv-note warn' },
                    icon('warn', 16),
                    h(
                        'div',
                        null,
                        h('b', null, 'No stats block found'),
                        h(
                            'p',
                            null,
                            'This save has no recognisable copy of the game stats, so health, gold, XP and level cannot be shown or edited reliably. The unconfirmed candidates are under Advanced below.',
                        ),
                    ),
                ),
                advancedCard(),
            );
            return;
        }

        const stats = card(
            'Stats',
            {
                hint: 'Level and XP are separate values. Keep them consistent.',
            },
            h(
                'div',
                { class: 'sv-fgrid' },
                statField('stat.gold', 'Gold'),
                statField('stat.xp', 'Experience'),
                statField('stat.level', 'Level'),
                statField('stat.skillpoints', 'Skill points', { hint: 'Unspent points.' }),
                statField('stat.maxhp', 'Max health'),
            ),
        );

        const skillRows = skills
            ? skills.map((s) => {
                  const max = Number(doc.value(`skill.${s.id}.max`));
                  return h(
                      'div',
                      { class: 'sv-skill' },
                      h('span', { class: 'sv-skill-name' }, s.name),
                      statField(`skill.${s.id}`, `${s.name} level`, { max, slider: true, compact: true }),
                      h('span', { class: 'sv-skill-of' }, 'of'),
                      statField(`skill.${s.id}.max`, `${s.name} max`, { compact: true }),
                  );
              })
            : [h('p', { class: 'sv-dim' }, 'No recognisable skill table in this save.')];
        const skillCard = card(
            'Skills',
            {
                hint: skills ? `${skillTotal} of ${skillCap} levels earned` : undefined,
            },
            ...skillRows,
        );

        const place = card('Position', { hint: 'Where Rynn stands when the save loads.' }, positionField());
        pane.replaceChildren(
            hero,
            h(
                'div',
                { class: 'sv-cols' },
                h('div', { class: 'sv-col' }, stats, place),
                h('div', { class: 'sv-col' }, skillCard),
            ),
            advancedCard(),
        );
    }

    function advancedCard(): HTMLElement {
        return h(
            'details',
            { class: 'sv-adv' },
            h('summary', null, icon('chevron', 14), 'Advanced: older raw candidates'),
            h(
                'p',
                { class: 'sv-hint' },
                'Older values read from other records. They may have no visible effect in game.',
            ),
            h(
                'div',
                { class: 'sv-fgrid' },
                statField('hp', 'Health (Rynn record)'),
                statField('gold', 'Gold (gold record)'),
            ),
        );
    }

    // ---- pending edits -----------------------------------------------------------------------------------------

    function revert(e: PendingEdit): void {
        if (!cur || !view) return;
        const doc = cur.doc;
        if (e.name === 'pos') {
            const id = doc.playerObject!;
            const before = new Map<number, Transform>([[id, structuredClone(doc.get(id)!.transform)]]);
            const t = structuredClone(doc.get(id)!.transform);
            t.pos = [...(e.before as [number, number, number])];
            view.run(new TransformCommand(before, new Map([[id, t]])));
        } else if (e.name.startsWith('add.')) view.run(new RemoveAddedItemCommand(Number(e.name.slice(4))));
        else if (e.name.startsWith('item.')) view.run(new ReplaceItemCommand(Number(e.name.slice(5)), undefined));
        else view.run(new SetFieldCommand(e.name, undefined));
    }

    function renderEdits(pending: PendingEdit[]): void {
        const box = $('sv-edits-list');
        if (!cur) {
            box.replaceChildren(
                emptyState('save', 'Nothing open', 'Changes you make are listed here until you save them.'),
            );
            return;
        }
        if (!pending.length) {
            box.replaceChildren(
                emptyState(
                    'check',
                    'No changes yet',
                    'Edit a stat, an item or a quest and it is listed here. Nothing touches the file until you save.',
                ),
            );
            return;
        }
        const doc = cur.doc;
        // One shared change row (shared/components.ts) with the save editor's classes, icons and badges.
        box.replaceChildren(
            ...pending.map((e) =>
                changeRow({
                    prefix: 'sv-edit',
                    label: e.label,
                    before: e.text?.before ?? doc.describe(e.name, e.before),
                    after: e.text?.after ?? doc.describe(e.name, e.after),
                    tag: e.unconfirmed ? 'unconfirmed' : e.confidence,
                    tone: '',
                    arrow: () => icon('chevron', 12),
                    badge: (t) => badge(t, e.unconfirmed ? 'bad' : e.confidence),
                    button: (title, run) => button('', { icon: 'revert', kind: 'icon sm', title, onclick: run }),
                    onRevert: () => revert(e),
                    revertTitle: `Revert ${e.label} (undoable)`,
                }),
            ),
        );
    }

    // ---- world -------------------------------------------------------------------------------------------------

    const worldQ = $<HTMLInputElement>('sv-q');
    const worldPos = $<HTMLInputElement>('sv-posonly');
    worldQ.oninput = () => renderWorldList();
    worldPos.onchange = () => renderWorldList();

    function groupKey(r: RecordInfo): string {
        return r.type === TYPE_RECORD && r.classId !== undefined ? `c${r.classId}` : `t${r.type}`;
    }
    function groupTitle(r: RecordInfo): string {
        if (r.type === TYPE_RECORD && r.classId !== undefined) {
            return KNOWN_CLASSES[r.classId] ?? `Object ${classHex(r.classId)}`;
        }
        return TYPE_NAMES[r.type] ?? `Record 0x${hex(r.type, 3)}`;
    }

    function renderWorld(): void {
        renderWorldList();
        $('sv-details').replaceChildren(
            cur ? emptyState('map', 'Select a record', 'Click a row in the list or a dot on the map.') : noSave(),
        );
    }

    // A top-down map above the lists: every positioned record as a dot, the filtered ones emphasised.
    const mapCanvas = h('canvas', { id: 'sv-map', class: 'sv-worldmap', width: 720, height: 300 });
    const mapNote = h('small', { class: 'sv-hint' });
    $('sv-worldside').insertBefore(
        card('Map', { hint: 'Top-down view.' }, mapCanvas, mapNote),
        $('sv-worldside').firstChild,
    );
    let mapPoints: MapPoint[] = [];
    let mapB: MapBounds = mapBounds([]);
    mapCanvas.onclick = (e) => {
        const entry = pickMap(mapCanvas, mapPoints, mapB, e);
        if (entry !== null) showDetails(entry, false);
    };

    function pointOf(entry: number): MapPoint | null {
        return mapPoints.find((p) => p.entry === entry) ?? null;
    }

    let lastMap: { rows: RecordInfo[]; filtered: boolean } = { rows: [], filtered: false };

    function drawWorldMap(rows: RecordInfo[], filtered: boolean): void {
        if (!cur) return;
        lastMap = { rows, filtered };
        const doc = cur.doc;
        mapPoints = doc.records.flatMap((r) =>
            r.pos ? [{ x: r.pos[0], z: r.pos[2], entry: r.index, group: groupKey(r) }] : [],
        );
        mapB = mapBounds(mapPoints);
        const keep = new Set(rows.map((r) => r.index));
        const player = doc.records.find((r) => isRynnClass(r.classId) && r.pos);
        const arokh = doc.records.find((r) => r.resource === 0xa60 && r.pos);
        drawMap(mapCanvas, mapPoints, mapB, {
            highlight: filtered ? mapPoints.filter((p) => keep.has(p.entry)) : undefined,
            selected: selectedEntry,
            player: player ? pointOf(player.index) : null,
            arokh: arokh ? pointOf(arokh.index) : null,
            dot: 3,
        });
        mapNote.textContent = `${mapPoints.length} positioned records. Green ring: Rynn. Gold ring: Arokh. Click a dot to inspect it.`;
    }

    function renderWorldList(): void {
        const box = $('sv-groups');
        if (!cur) {
            box.replaceChildren();
            $('sv-wcount').textContent = '';
            mapCanvas.getContext('2d')?.clearRect(0, 0, mapCanvas.width, mapCanvas.height);
            mapNote.textContent = '';
            return;
        }
        const q = worldQ.value.trim().toLowerCase();
        const onlyPos = worldPos.checked;
        const groups = new Map<string, { title: string; rows: RecordInfo[]; total: number }>();
        for (const r of cur.doc.records) {
            const key = groupKey(r);
            let g = groups.get(key);
            if (!g) groups.set(key, (g = { title: groupTitle(r), rows: [], total: 0 }));
            g.total++;
            if (onlyPos && !r.pos) continue;
            const hay =
                `${g.title} #${r.index} id ${r.id} type ${hex(r.type, 3)} 0x${hex(r.type, 3)} ${r.classId !== undefined ? hex(r.classId, 8) : ''}`.toLowerCase();
            if (q && !hay.includes(q)) continue;
            g.rows.push(r);
        }
        const shown = [...groups.values()].filter((g) => g.rows.length).sort((a, b) => b.rows.length - a.rows.length);
        drawWorldMap(
            shown.flatMap((g) => g.rows),
            q !== '' || onlyPos,
        );
        $('sv-wcount').textContent =
            `${shown.reduce((n, g) => n + g.rows.length, 0)} of ${cur.doc.records.length} records, ${shown.length} groups`;
        const openAll = q !== '' && shown.length <= 5;
        box.replaceChildren(
            ...shown.map((g) => {
                const d = h(
                    'details',
                    null,
                    h(
                        'summary',
                        null,
                        groupThumb(g.rows),
                        h('span', { class: 'gname' }, g.title),
                        h('small', null, g.rows.length === g.total ? String(g.total) : `${g.rows.length} / ${g.total}`),
                    ),
                );
                let filled = false;
                const fill = (): void => {
                    if (filled) return;
                    filled = true;
                    const list = h('div', { class: 'rows' });
                    for (const r of g.rows) list.append(recordRow(r));
                    d.append(list);
                };
                d.ontoggle = () => {
                    if (d.open) fill();
                };
                if (openAll || (selectedEntry !== null && g.rows.some((r) => r.index === selectedEntry))) {
                    d.open = true;
                    fill();
                }
                return d;
            }),
        );
    }

    /** A small picture of where a group's records are in the level. */
    function groupThumb(rows: RecordInfo[]): HTMLElement {
        const pts = rows.flatMap((r) =>
            r.pos ? [{ x: r.pos[0], z: r.pos[2], entry: r.index, group: groupKey(r) }] : [],
        );
        if (!pts.length) return h('span', { class: 'gthumb none', title: 'no positions' });
        const c = h('canvas', { class: 'gthumb', width: 72, height: 44 });
        drawMap(c, mapPoints, mapB, { highlight: pts, mono: groupColor(pts[0]!.group), dot: 2 });
        return c;
    }

    function recordRow(r: RecordInfo): HTMLElement {
        const row = h(
            'div',
            { class: `sv-rec${r.index === selectedEntry ? ' sel' : ''}`, onclick: () => showDetails(r.index, false) },
            h('span', null, `#${r.index}`),
            h('span', { title: recordLabel(r) }, recordLabel(r) || `id ${r.id}${r.sub ? `/${r.sub}` : ''}`),
            h('span', null, `${r.size} B`),
            h('span', { class: 'sv-dim' }, r.pos ? r.pos.map((x) => Math.round(x)).join(', ') : ''),
            r.pos
                ? h(
                      'button',
                      {
                          title: 'Show in 3D',
                          onclick: (e) => {
                              e.stopPropagation();
                              showDetails(r.index, true);
                          },
                      },
                      '3D',
                  )
                : h('span', null),
        );
        row.dataset.index = String(r.index);
        return row;
    }

    function showDetails(index: number, in3d: boolean): void {
        if (!cur) return;
        selectedEntry = index;
        drawWorldMap(lastMap.rows, lastMap.filtered);
        for (const el of $('sv-groups').querySelectorAll<HTMLElement>('.sv-rec'))
            el.classList.toggle('sel', el.dataset.index === String(index));
        const doc = cur.doc;
        const e = doc.save.entries[index]!;
        const rec = doc.records[index]!;
        const rows: [string, string][] = [
            [
                'entry',
                `#${index}  type 0x${hex(e.type, 3)} (${TYPE_NAMES[e.type] ?? 'unknown'})  id ${e.id}  sub ${e.sub}`,
            ],
            ['size', `${e.data.length} B inflated, ${e.raw.length} B stored (zlib)`],
        ];
        const known: [string, string][] = [];
        if (e.type === TYPE_RECORD && e.data.length >= 24) {
            const dv = new DataView(e.data.buffer, e.data.byteOffset, e.data.byteLength);
            known.push(['+0x00 u32', `0x${hex(dv.getUint32(0, true), 8)} (unknown)`]);
            known.push(['+0x04 u32', `0x${hex(dv.getUint32(4, true), 8)} (unknown)`]);
            known.push([
                '+0x08 class',
                `${classHex(dv.getUint32(8, true))} ${KNOWN_CLASSES[dv.getUint32(8, true)] ?? ''}`,
            ]);
            known.push([
                '+0x0c pos',
                rec.pos ? rec.pos.map((x) => Number(x.toPrecision(9))).join(', ') : 'not a plausible position',
            ]);
            for (const name of ['hp', 'gold']) {
                const loc = locateField(doc.save, name);
                const p = doc.probe(name);
                if (loc?.entryIndex === index && p.status !== 'missing') {
                    const conf = p.status === 'ok' ? FIELDS[name]!.confidence : 'unconfirmed';
                    known.push([
                        `+0x${hex(loc.offset, 3)} ${name}`,
                        `${formatFieldValue(FIELDS[name]!, p.value)} (${conf})`,
                    ]);
                }
            }
            const you = doc.records.find((x) => isRynnClass(x.classId) && x.pos);
            if (rec.pos && you?.pos) {
                const d = Math.hypot(rec.pos[0] - you.pos[0], rec.pos[1] - you.pos[1], rec.pos[2] - you.pos[2]);
                known.push(['from Rynn', `${Math.round(d).toLocaleString()} units`]);
            }
            known.push([`+0x18..${hex(e.data.length - 1, 3)}`, 'unknown (hex below)']);
        } else if (e.type === 0x400 || (e.type === 0x401 && e.id === 891)) {
            known.push(['text', new TextDecoder('latin1').decode(e.data.subarray(2)).replace(/\0+$/, '')]);
        } else if (e.type === 0x401 && e.id === 893 && doc.skills) {
            known.push(['skill table', doc.skills.map((s) => `${s.name} ${s.level}/${s.max}`).join(', ')]);
        }
        const kv = (list: [string, string][]): HTMLElement => {
            const t = h('table', { class: 'sv-kv' });
            for (const [k, v] of list) {
                const tr = t.insertRow();
                tr.insertCell().textContent = k;
                tr.insertCell().textContent = v;
            }
            return t;
        };
        const friendly = known.filter(([k]) => !k.startsWith('+0x'));
        const table = kv([...rows, ...known.filter(([k]) => k.startsWith('+0x'))]);
        const pre = h('pre', { class: 'sv-hex' }, hexDump(e.data, 0, hexLimit));
        const more =
            e.data.length > hexLimit
                ? h(
                      'button',
                      {
                          onclick: () => {
                              hexLimit += 4096;
                              showDetails(index, false);
                          },
                      },
                      `Show ${Math.min(4096, e.data.length - hexLimit)} more bytes`,
                  )
                : null;
        $('sv-details').replaceChildren(
            h(
                'div',
                { class: 'sv-dhead' },
                h('b', null, recordLabel(rec) || groupTitle(rec)),
                rec.pos
                    ? h('button', { onclick: () => showDetails(index, true) }, 'Show in 3D')
                    : h('small', { class: 'sv-dim' }, 'no position'),
            ),
            ...(friendly.length ? [kv(friendly)] : []),
            h('details', { class: 'adv' }, h('summary', null, 'Raw data'), table, pre, ...(more ? [more] : [])),
        );
        if (in3d) showIn3d(index);
        else if (view && cur.doc.objectOfEntry.has(index)) {
            const id = cur.doc.objectOfEntry.get(index)!;
            if (!view.selection.has(id) || view.selection.size !== 1) view.select([id]);
        }
    }

    // ---- compare -----------------------------------------------------------------------------------------------

    const cmpSelect = h('select', {
        id: 'sv-cmp-select',
        class: 'sv-select',
        'aria-label': 'Compare with a listed save',
    });
    function renderCompareChoices(): void {
        cmpSelect.replaceChildren(
            new Option(discovered.length ? 'Compare with a listed save...' : 'No folder opened', ''),
            ...discovered.map(
                (r, i) =>
                    new Option(`${r.folder ? `${r.folder.replace(/^BESCES-\d+/i, '')} · ` : ''}${r.name}`, String(i)),
            ),
        );
        cmpSelect.disabled = !discovered.length;
    }
    cmpSelect.onchange = () => {
        const r = discovered[Number(cmpSelect.value)];
        cmpSelect.value = '';
        if (r) runCompare(r.name, r.read).catch(err);
    };

    async function runCompare(label: string, read: () => Promise<Uint8Array>): Promise<void> {
        if (!cur) return status('Open a save first.', true);
        const b = parseSave(await read());
        const a = cur.doc.pendingEdits().length ? cur.doc.build().save : cur.original;
        diff = {
            a: `${cur.ref.name}${cur.doc.pendingEdits().length ? ' (with pending edits)' : ''}`,
            b: label,
            result: diffSaves(a, b),
            quests: diffQuests(listQuestsSafe(a), listQuestsSafe(b)),
        };
        renderCompare();
        showSub('compare');
        status(
            `Compared with ${label}: ${diff.result.counts.changed} changed, ${diff.result.counts.unchanged} unchanged entries.`,
        );
    }

    function compareWithOriginal(): void {
        if (!cur) return status('Open a save first.', true);
        try {
            const built = cur.doc.build().save;
            diff = {
                a: `${cur.ref.name} (with pending edits)`,
                b: `${cur.ref.name} (as opened)`,
                result: diffSaves(built, cur.original),
                quests: diffQuests(listQuestsSafe(built), listQuestsSafe(cur.original)),
            };
            renderCompare();
            showSub('compare');
        } catch (e) {
            err(e);
        }
    }

    function renderCompare(): void {
        const pane = panes.compare;
        const fileIn = h('input', {
            type: 'file',
            accept: '.rsg',
            hidden: true,
            onchange: (e: Event) => {
                const i = e.target as HTMLInputElement;
                const f = i.files?.[0];
                i.value = '';
                if (f) runCompare(f.name, async () => new Uint8Array(await f.arrayBuffer())).catch(err);
            },
        });
        const toolbar = h(
            'div',
            { class: 'sv-toolbar' },
            button('Compare with file', { icon: 'file', disabled: !cur, onclick: () => fileIn.click() }),
            cmpSelect,
            button('Pending vs as opened', {
                icon: 'compare',
                disabled: !cur,
                title: 'Review what Save would change',
                onclick: compareWithOriginal,
            }),
            fileIn,
        );
        if (!diff) {
            pane.replaceChildren(
                toolbar,
                cur
                    ? emptyState(
                          'compare',
                          'Nothing compared yet',
                          'Compare the open save, pending changes included, with another .rsg. You get the field table first, then every record as changed or unchanged.',
                      )
                    : noSave(),
            );
            return;
        }
        const d = diff.result;
        const ft = h(
            'table',
            { class: 'sv-grid' },
            h('thead', null, h('tr', null, ...['field', 'confidence', 'A', 'B'].map((x) => h('th', null, x)))),
        );
        const fb = h('tbody');
        const fmt = (n: string, v: FieldValue | null): string =>
            v === null ? '-' : formatFieldValue(resolveField(n).def, v);
        for (const f of d.fields) {
            if (f.a === null && f.b === null) continue;
            fb.append(
                h(
                    'tr',
                    { class: f.changed ? 'chg' : '' },
                    h('td', null, f.label),
                    h('td', null, badge(f.confidence, f.confidence)),
                    h('td', null, fmt(f.name, f.a)),
                    h('td', null, fmt(f.name, f.b)),
                ),
            );
        }
        ft.append(fb);
        const changedOnly = h('input', { type: 'checkbox', checked: true, onchange: () => fillEntries() });
        const et = h(
            'table',
            { class: 'sv-grid' },
            h(
                'thead',
                null,
                h(
                    'tr',
                    null,
                    ...['status', 'type', 'id', 'sub', 'size A', 'size B', 'bytes'].map((x) => h('th', null, x)),
                ),
            ),
        );
        const eb = h('tbody');
        et.append(eb);
        const fillEntries = (): void => {
            const rows = d.entries.filter((e) => !changedOnly.checked || e.status !== 'unchanged');
            eb.replaceChildren(
                ...rows
                    .slice(0, 2000)
                    .map((e) =>
                        h(
                            'tr',
                            { class: e.status === 'unchanged' ? '' : 'chg' },
                            h(
                                'td',
                                null,
                                e.status === 'unchanged' && e.recompressed ? 'unchanged (recompressed)' : e.status,
                            ),
                            h('td', null, `0x${hex(e.type, 3)}`),
                            h('td', null, String(e.id)),
                            h('td', null, String(e.sub)),
                            h('td', null, e.indexA < 0 ? '-' : String(e.sizeA)),
                            h('td', null, e.indexB < 0 ? '-' : String(e.sizeB)),
                            h('td', null, e.bytesChanged >= 0 ? String(e.bytesChanged) : ''),
                        ),
                    ),
            );
        };
        fillEntries();
        pane.replaceChildren(
            toolbar,
            h(
                'div',
                { class: 'sv-cmphead' },
                h('span', { class: 'sv-ab a' }, 'A'),
                h('span', null, diff.a),
                h('span', { class: 'sv-ab b' }, 'B'),
                h('span', null, diff.b),
            ),
            card('Fields', {}, ft),
            questCompare(diff.quests, host.story?.() ?? null),
            card(
                'Records',
                {
                    hint:
                        `${d.counts.changed} changed, ${d.counts.unchanged} unchanged` +
                        (d.counts.onlyA || d.counts.onlyB
                            ? `, ${d.counts.onlyA} only in A, ${d.counts.onlyB} only in B`
                            : ''),
                    tools: h('label', { class: 'sv-check' }, changedOnly, ' Changed only'),
                },
                et,
            ),
        );
    }

    renderCompareChoices();
    renderAll();
    return { openFile };
}

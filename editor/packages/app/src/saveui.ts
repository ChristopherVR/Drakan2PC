import { SetFieldCommand, SaveDocument, TransformCommand, type PendingEdit, type Transform } from '@drakan/editor-core';
import {
    FIELDS,
    KNOWN_CLASSES,
    SaveFieldError,
    TYPE_NAMES,
    TYPE_RECORD,
    classHex,
    diffSaves,
    formatFieldValue,
    locateField,
    parseFieldValue,
    parseSave,
    recordLabel,
    resolveField,
    summarizeSave,
    type FieldValue,
    type RecordInfo,
    type SaveDiff,
    type SaveFile,
} from '@drakan/formats';
import { LevelView } from './level.ts';
import './save/save.css';
import { badge, fmtDate, h, hex, hexDump, shotCanvas, statTile } from './save/dom.ts';
import { drawMap, groupColor, mapBounds, pickMap, type MapBounds, type MapPoint } from './save/map.ts';
import type { ClassInfo } from './save/inventory.ts';
import { renderItems, renderRepair, renderStory, renderTransfer, type PanelCtx } from './save/panels.ts';
import {
    download,
    hasFsa,
    pickSaveFile,
    pickSaveFolder,
    refFromFile,
    refsFromFileList,
    saveWithBackup,
    type SaveRef,
} from './saveio.ts';

const $ = <T extends HTMLElement>(id: string): T => {
    const el = document.getElementById(id);
    if (!el) throw new Error(`missing #${id}`);
    return el as T;
};

export interface SaveUiHost {
    setStatus(msg: string, error?: boolean): void;
    /** Game name and model render of an item class (`0x31bc9`), when a level with that class is open in Level mode. */
    classInfo?(resource: string): ClassInfo | null;
}

type SubTab = 'character' | 'world' | 'items' | 'story' | 'transfer' | 'compare' | 'repair';

interface Open {
    ref: SaveRef;
    doc: SaveDocument;
    /** The file as read (the "original" for compare), kept for revert-free diffs. */
    original: SaveFile;
}

/** The Save tab: open, character panel, world list, 3D view, pending edits, SAVE/EXPORT, compare. */
export function initSaveUi(host: SaveUiHost): { openFile(f: File): Promise<void> } {
    const fsa = hasFsa();
    let view: LevelView | null = null;
    let cur: Open | null = null;
    let discovered: SaveRef[] = [];
    let subtab: SubTab = 'character';
    let diff: { a: string; b: string; result: SaveDiff } | null = null;
    let selectedEntry: number | null = null;
    let hexLimit = 512;
    let busy = false;

    const err = (e: unknown): void => host.setStatus(e instanceof Error ? e.message : String(e), true);

    // ---- 3D ----------------------------------------------------------------------------------------------------

    function getView(): LevelView {
        if (view) return view;
        view = new LevelView($('sv-viewport'), {
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
        host.setStatus(
            id === cur.doc.playerObject
                ? 'Player marker selected: drag the gizmo to move it; the saved position changes when you SAVE.'
                : 'Record selected in 3D (read only; only the player marker can be moved).',
        );
    }

    // ---- open --------------------------------------------------------------------------------------------------

    async function openRef(ref: SaveRef): Promise<void> {
        if (cur && cur.doc.pendingEdits().length && !confirm('Discard the pending edits of the open save?')) return;
        host.setStatus(`Opening ${ref.name}...`);
        try {
            const bytes = await ref.read();
            const doc = SaveDocument.open(bytes);
            cur = { ref, doc, original: doc.save };
            selectedEntry = null;
            hexLimit = 512;
            diff = null;
            getView().setDocument(doc);
            renderAll();
            markActiveInList();
            host.setStatus(
                `${ref.name}: ${doc.save.entries.length} records, ${doc.size} positioned, level "${doc.level || 'unknown'}".`,
            );
        } catch (e) {
            err(e);
        }
    }

    async function openFile(f: File): Promise<void> {
        discovered = discovered.filter((r) => r.folder !== '' || r.name !== f.name);
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
    async function setDiscovered(root: string, refs: SaveRef[]): Promise<void> {
        discovered = refs;
        $('sv-count').textContent = refs.length ? `${refs.length} in ${root}` : '';
        renderCompareChoices();
        if (!refs.length) {
            listEl.replaceChildren(
                h('li', { class: 'empty' }, `No .rsg files found under "${root}". Pick the memcard folder (mc0).`),
            );
            return;
        }
        const rows = refs.map((ref) => {
            const meta = h(
                'div',
                { class: 'meta' },
                h('b', null, ref.name.replace(/\.rsg$/i, '')),
                h('small', null, '...'),
            );
            const thumb = h('div', { class: 'thumb' });
            const li = h(
                'li',
                {
                    onclick: () => void openRef(ref),
                    title: `${ref.folder}/${ref.name}`,
                },
                thumb,
                meta,
            );
            li.dataset.key = `${ref.folder}/${ref.name}`;
            return { ref, li, thumb, meta };
        });
        listEl.replaceChildren(...rows.map((r) => r.li));
        host.setStatus(`Found ${refs.length} saves in "${root}". Reading previews...`);
        for (const r of rows) {
            try {
                const sum = summarizeSave(await r.ref.read());
                if (sum.screenshot) r.thumb.append(shotCanvas(sum.screenshot, 'shot'));
                const slot = r.ref.folder.replace(/^BESCES-\d+/i, '') || 'file';
                r.meta.replaceChildren(
                    h('b', null, sum.levelName || r.ref.name.replace(/\.rsg$/i, '')),
                    h('small', null, `${slot} · ${fmtDate(r.ref.modified)}`),
                    h('small', null, `${sum.entryCount} records`),
                );
            } catch (e) {
                r.meta.replaceChildren(
                    h('b', null, r.ref.name),
                    h('small', { class: 'error' }, e instanceof Error ? e.message : 'unreadable'),
                );
            }
        }
        host.setStatus(`Found ${refs.length} saves in "${root}". Click one to open it.`);
        markActiveInList();
    }

    function markActiveInList(): void {
        const key = cur ? `${cur.ref.folder}/${cur.ref.name}` : '';
        for (const li of listEl.querySelectorAll<HTMLElement>('li')) li.classList.toggle('sel', li.dataset.key === key);
    }

    // ---- toolbar -----------------------------------------------------------------------------------------------

    const btn = (id: string): HTMLButtonElement => $<HTMLButtonElement>(id);
    btn('sv-undo').onclick = () => view?.undo();
    btn('sv-redo').onclick = () => view?.redo();
    btn('sv-export').onclick = () => {
        if (!cur) return;
        try {
            const { bytes } = cur.doc.build();
            download(cur.ref.name, bytes);
            host.setStatus(
                `Exported ${cur.ref.name} (${bytes.length} bytes). Copy it over your save, keeping a backup.`,
            );
        } catch (e) {
            err(e);
        }
    };
    btn('sv-save').onclick = () => void saveInPlace();
    btn('sv-frame-player').onclick = () => {
        if (cur?.doc.playerObject != null) showIn3d(cur.doc.entryOfObject.get(cur.doc.playerObject)!);
        else host.setStatus('This save has no player record with a position.', true);
    };
    btn('sv-frame-all').onclick = () => view?.frameAll();

    async function saveInPlace(): Promise<void> {
        if (!cur || busy) return;
        busy = true;
        try {
            const { bytes } = cur.doc.build();
            const backup = await saveWithBackup(cur.ref, bytes);
            const ref = cur.ref;
            const file = await ref.handle!.getFile();
            cur = null; // reopen from what is now on disk
            await openRef({ ...ref, size: file.size, modified: file.lastModified, read: async () => bytes });
            host.setStatus(`Saved ${ref.name}; the previous file is kept as ${backup}.`);
        } catch (e) {
            if (e instanceof DOMException && e.name === 'AbortError')
                host.setStatus('Save cancelled; nothing was written.');
            else err(e);
        } finally {
            busy = false;
        }
    }

    window.addEventListener('beforeunload', (e) => {
        if (cur && cur.doc.pendingEdits().length) e.preventDefault();
    });

    // ---- sub tabs ----------------------------------------------------------------------------------------------

    // The tabs after World are created here so index.html stays as the shell agents left it.
    const nav = $('sv-sub-compare').parentElement!;
    const centre = $('sv-compare').parentElement!;
    const extraTabs: [SubTab, string][] = [
        ['items', 'Items'],
        ['story', 'Story'],
        ['transfer', 'Transfer'],
        ['repair', 'Repair'],
    ];
    for (const [k, label] of extraTabs) {
        const b = h('button', { id: `sv-sub-${k}` }, label);
        if (k === 'repair') nav.append(b);
        else nav.insertBefore(b, $('sv-sub-compare'));
        centre.append(h('div', { id: `sv-${k}`, class: 'subpane', hidden: true }));
    }
    const panes: Record<SubTab, HTMLElement> = {
        character: $('sv-character'),
        world: $('sv-world'),
        items: $('sv-items'),
        story: $('sv-story'),
        transfer: $('sv-transfer'),
        compare: $('sv-compare'),
        repair: $('sv-repair'),
    };
    function showSub(t: SubTab): void {
        subtab = t;
        for (const k of Object.keys(panes) as SubTab[]) {
            panes[k].hidden = k !== t;
            $(`sv-sub-${k}`).classList.toggle('active', k === t);
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
            status: (m, e) => host.setStatus(m, e),
            showRecord: (entry, in3d) => {
                showSub('world');
                showDetails(entry, in3d);
                renderWorldList();
            },
            listed: () => discovered,
            changed: () => refresh(),
            rerender: () => renderExtra(),
            classInfo: host.classInfo,
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
        panes[subtab].replaceChildren(
            cur
                ? render(panelCtx())
                : h(
                      'div',
                      { class: 'welcome' },
                      h('h3', null, 'Open a save'),
                      h('p', null, 'Open a save to use this tab.'),
                  ),
        );
    }

    // ---- refresh -----------------------------------------------------------------------------------------------

    function refresh(): void {
        const pending = cur?.doc.pendingEdits() ?? [];
        const v = view;
        btn('sv-undo').disabled = !v?.stack.canUndo;
        btn('sv-redo').disabled = !v?.stack.canRedo;
        btn('sv-undo').title = v?.stack.undoLabel ? `Undo: ${v.stack.undoLabel}` : '';
        btn('sv-redo').title = v?.stack.redoLabel ? `Redo: ${v.stack.redoLabel}` : '';
        btn('sv-export').disabled = !cur;
        btn('sv-save').disabled = !cur || !cur.ref.handle || pending.length === 0;
        btn('sv-save').title = !cur?.ref.handle
            ? 'This save was opened without write access (your browser has no File System Access API, or the file was dropped). Use EXPORT.'
            : 'Writes the file in place; the previous contents are kept as a .bak next to it';
        $('sv-pending').textContent = cur ? (pending.length ? `${pending.length} pending` : 'no pending edits') : '';
        renderCharacter();
        renderEdits(pending);
        renderExtra();
    }

    function renderAll(): void {
        refresh();
        renderWorld();
        renderCompare();
    }

    // ---- character ---------------------------------------------------------------------------------------------

    function confidenceBadge(name: string): HTMLElement {
        const def = resolveField(name).def;
        const unconfirmed = cur?.doc.probe(name).status === 'unconfirmed';
        if (unconfirmed) {
            return badge(
                'unconfirmed',
                'bad',
                'This save does not match the layout the field was verified on. ' + def.note,
            );
        }
        return badge(def.confidence, def.confidence, def.note);
    }

    function lockControl(name: string): HTMLElement | null {
        const doc = cur!.doc;
        const p = doc.probe(name);
        if (p.status === 'missing') return h('small', { class: 'dim', title: p.reason }, 'not present in this save');
        if (p.status === 'ok') return null;
        const cb = h('input', {
            type: 'checkbox',
            checked: doc.unlocked.has(name),
            onchange: () => {
                if (cb.checked) doc.unlocked.add(name);
                else if (!doc.fieldValues.has(name)) doc.unlocked.delete(name);
                else {
                    cb.checked = true;
                    host.setStatus('Undo the pending edit of this field before locking it again.', true);
                }
                renderCharacter();
            },
        });
        return h('label', { class: 'unlock', title: p.reason }, cb, ' unlock editing (unconfirmed layout)');
    }

    function numberField(name: string, label: string, opts: { barMax?: number; step?: number } = {}): HTMLElement {
        const doc = cur!.doc;
        const p = doc.probe(name);
        const value = doc.value(name);
        const missing = p.status === 'missing' || value === undefined;
        const input = h('input', {
            type: 'number',
            step: opts.step ?? 1,
            min: 0,
            value: missing ? '' : String(value),
            disabled: !doc.editable(name),
            'data-field': name,
            onchange: () => {
                try {
                    const v = parseFieldValue(resolveField(name).def, input.value);
                    const problem = doc.checkEdit(name, v);
                    if (problem) throw new SaveFieldError(problem);
                    getView().run(new SetFieldCommand(name, v));
                } catch (e) {
                    err(e);
                    input.value = String(doc.value(name) ?? '');
                }
            },
        });
        const orig = doc.originalValue(name);
        const changed = !missing && orig !== undefined && value !== orig;
        const max = opts.barMax;
        const bar =
            max !== undefined && !missing
                ? h(
                      'div',
                      { class: 'bar', title: `${value} of ${max}` },
                      h('i', { style: `width:${Math.min(100, (Number(value) / Math.max(max, 1)) * 100)}%` }),
                  )
                : null;
        return h(
            'div',
            { class: `field${changed ? ' changed' : ''}` },
            h('div', { class: 'fhead' }, h('b', null, label), confidenceBadge(name), lockControl(name)),
            h('div', { class: 'frow' }, input, changed ? h('small', { class: 'dim' }, `was ${orig}`) : null),
            bar,
        );
    }

    function positionField(): HTMLElement {
        const doc = cur!.doc;
        const p = doc.probe('pos');
        const head = h('div', { class: 'fhead' }, h('b', null, 'Position'), confidenceBadge('pos'), lockControl('pos'));
        if (p.status === 'missing' || doc.playerObject === null) {
            return h(
                'div',
                { class: 'field' },
                head,
                h('small', { class: 'dim' }, p.status === 'missing' ? p.reason : 'no player marker'),
            );
        }
        const v = doc.value('pos') as number[];
        const o = doc.originalValue('pos') as number[];
        const inputs = ['x', 'y', 'z'].map((axis, i) => {
            const input = h('input', {
                type: 'number',
                step: 10,
                value: String(Number(v[i]!.toPrecision(9))),
                'data-field': `pos.${axis}`,
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
            return h('label', { class: 'axis' }, axis, input);
        });
        const changed = v.some((x, i) => Math.fround(x) !== Math.fround(o[i]!));
        return h(
            'div',
            { class: `field${changed ? ' changed' : ''}` },
            head,
            h('div', { class: 'frow' }, ...inputs),
            h('small', { class: 'dim' }, 'Game units. Drag the green player marker in the 3D view to move it.'),
            changed
                ? h('small', { class: 'dim' }, `was ${formatFieldValue(FIELDS.pos!, o as [number, number, number])}`)
                : null,
        );
    }

    function renderCharacter(): void {
        const pane = panes.character;
        if (!cur) {
            pane.replaceChildren(
                h(
                    'div',
                    { class: 'welcome' },
                    h('h3', null, 'Open a save'),
                    h(
                        'p',
                        null,
                        'Open a single .rsg, or a memcard folder (mc0) to list every slot with its screenshot. Nothing is uploaded; files are read locally. Edits stay pending until SAVE or EXPORT.',
                    ),
                    h(
                        'p',
                        { class: 'dim' },
                        fsa
                            ? 'SAVE writes the file in place and keeps a .bak next to it.'
                            : 'This browser cannot write files in place: use EXPORT to download the edited .rsg.',
                    ),
                ),
            );
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
        const card = h(
            'div',
            { class: 'card' },
            sum?.screenshot
                ? shotCanvas(sum.screenshot, 'shot big')
                : h('div', { class: 'shot big none' }, 'no screenshot'),
            h(
                'div',
                { class: 'meta' },
                h('h3', null, doc.level || ref.name),
                h('small', null, ref.name),
                h(
                    'small',
                    null,
                    `${ref.folder ? `${ref.folder} · ` : ''}${(ref.size / 1024).toFixed(1)} KB · ${fmtDate(ref.modified)}`,
                ),
                h('small', null, `${doc.save.entries.length} records, ${doc.size} positioned`),
                sum?.levelPath ? h('small', { class: 'dim' }, sum.levelPath) : null,
            ),
        );
        const skills = doc.skills;
        const skillBlock = skills
            ? skills.map((s) =>
                  h(
                      'div',
                      { class: 'skill' },
                      h('b', null, s.name),
                      h(
                          'div',
                          { class: 'srow' },
                          numberField(`skill.${s.id}`, 'level', { barMax: doc.value(`skill.${s.id}.max`) as number }),
                          numberField(`skill.${s.id}.max`, 'max'),
                      ),
                  ),
              )
            : [h('small', { class: 'dim' }, 'No recognisable skill table in this save.')];
        const num = (n: string): number | null => {
            const v = doc.value(n);
            return typeof v === 'number' ? v : null;
        };
        const hp = num('stat.hp');
        const maxhp = num('stat.maxhp');
        const mana = num('stat.mana');
        const maxmana = num('stat.maxmana');
        const skillTotal = skills ? skills.reduce((n, r) => n + Number(doc.value(`skill.${r.id}`) ?? r.level), 0) : 0;
        const skillCap = skills ? skills.reduce((n, r) => n + Number(doc.value(`skill.${r.id}.max`) ?? r.max), 0) : 0;
        const vital = (label: string, cur: number | null, max: number | null, kind: string): HTMLElement =>
            h(
                'div',
                { class: `vital ${kind}` },
                h(
                    'div',
                    { class: 'vhead' },
                    h('small', null, label),
                    h('b', null, cur === null ? 'n/a' : cur.toLocaleString()),
                ),
                h(
                    'div',
                    { class: 'vbar', title: cur === null || max === null ? '' : `${cur} of ${max}` },
                    h('i', {
                        style: `width:${cur === null || max === null ? 0 : Math.max(0, Math.min(100, (cur / Math.max(max, 1)) * 100))}%`,
                    }),
                ),
                h('small', { class: 'dim' }, max === null ? '' : `of ${max.toLocaleString()}`),
            );
        const stats = doc.hasStats
            ? [
                  h('div', { class: 'vitals' }, vital('Health', hp, maxhp, 'hp'), vital('Mana', mana, maxmana, 'mana')),
                  h(
                      'div',
                      { class: 'tiles' },
                      statTile('Gold', (num('stat.gold') ?? 0).toLocaleString()),
                      statTile('Level', String(num('stat.level') ?? '-')),
                      statTile('Experience', (num('stat.xp') ?? 0).toLocaleString()),
                      statTile('Skill points', String(num('stat.skillpoints') ?? '-'), { note: 'unspent' }),
                      statTile('Skill levels', skills ? `${skillTotal} / ${skillCap}` : 'n/a', {
                          bar: skills ? [skillTotal, skillCap] : undefined,
                      }),
                  ),
                  h('h4', null, 'Edit'),
                  h(
                      'div',
                      { class: 'fgrid' },
                      numberField('stat.hp', 'Health', { barMax: maxhp ?? undefined }),
                      numberField('stat.maxhp', 'Max health'),
                      numberField('stat.gold', 'Gold'),
                      numberField('stat.xp', 'Experience'),
                      numberField('stat.level', 'Level'),
                      numberField('stat.skillpoints', 'Skill points'),
                  ),
                  h(
                      'small',
                      { class: 'dim' },
                      'Mana is shown read only: it was read in 58 saves but never changed in game. Level and XP are stored ' +
                          'separately; the game does not recompute one from the other, so keep them sensible.',
                  ),
              ]
            : [
                  h(
                      'div',
                      { class: 'note-box' },
                      h('b', null, 'No stats block found. '),
                      'This save has no recognisable copy of the game stats, so health, gold, XP and level cannot be shown or ' +
                          'edited reliably. The unconfirmed candidates are under Advanced / raw below.',
                  ),
              ];
        const legacy = h(
            'details',
            { class: 'adv' },
            h('summary', null, 'Advanced / raw'),
            h(
                'p',
                { class: 'dim' },
                'Older candidates read from other records. The game applies the stats block above, not these, so editing them ' +
                    'probably has no visible effect.',
            ),
            h(
                'div',
                { class: 'fgrid' },
                numberField('hp', 'Health (Rynn record)'),
                numberField('gold', 'Gold (gold record)'),
            ),
        );
        pane.replaceChildren(
            card,
            ...stats,
            h('h4', null, 'Position'),
            positionField(),
            h('h4', null, 'Skills'),
            ...skillBlock,
            legacy,
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
        } else view.run(new SetFieldCommand(e.name, undefined));
    }

    function renderEdits(pending: PendingEdit[]): void {
        const box = $('sv-edits-list');
        if (!cur) {
            box.replaceChildren(h('small', { class: 'dim' }, 'No save open.'));
            return;
        }
        if (!pending.length) {
            box.replaceChildren(
                h('small', { class: 'dim' }, 'No pending edits. Changes are listed here until you SAVE or EXPORT.'),
            );
            return;
        }
        const doc = cur.doc;
        box.replaceChildren(
            ...pending.map((e) =>
                h(
                    'div',
                    { class: 'edit' },
                    h('b', null, e.label),
                    badge(e.unconfirmed ? 'unconfirmed' : e.confidence, e.unconfirmed ? 'bad' : e.confidence),
                    h('span', { class: 'chg' }, `${doc.describe(e.name, e.before)} → ${doc.describe(e.name, e.after)}`),
                    h('button', { onclick: () => revert(e), title: 'Revert this edit (undoable)' }, 'Revert'),
                ),
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
            return `${KNOWN_CLASSES[r.classId] ?? 'Object class'} ${classHex(r.classId)}`;
        }
        return `${TYPE_NAMES[r.type] ?? 'Type'} 0x${hex(r.type, 3)}`;
    }

    function renderWorld(): void {
        renderWorldList();
        $('sv-details').replaceChildren(h('small', { class: 'dim' }, cur ? 'Select a record.' : 'No save open.'));
    }

    // A top-down map above the lists: every positioned record as a dot, the filtered ones emphasised.
    const mapCanvas = h('canvas', { id: 'sv-map', class: 'worldmap', width: 720, height: 300 });
    const mapNote = h('small', { class: 'dim' });
    $('sv-world').insertBefore(h('div', { class: 'mapbox' }, mapCanvas, mapNote), $('sv-world').firstChild);
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
        const player = doc.records.find((r) => r.classId === 0x17081069 && r.pos);
        const arokh = doc.records.find((r) => r.resource === 0xa60 && r.pos);
        drawMap(mapCanvas, mapPoints, mapB, {
            highlight: filtered ? mapPoints.filter((p) => keep.has(p.entry)) : undefined,
            selected: selectedEntry,
            player: player ? pointOf(player.index) : null,
            arokh: arokh ? pointOf(arokh.index) : null,
            dot: 3,
        });
        mapNote.textContent = `${mapPoints.length} positioned records, top-down (x across, z down). Green ring: Rynn. Gold ring: Arokh. Click a dot to inspect it.`;
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
            { class: `rec${r.index === selectedEntry ? ' sel' : ''}`, onclick: () => showDetails(r.index, false) },
            h('span', null, `#${r.index}`),
            h('span', { title: recordLabel(r) }, recordLabel(r) || `id ${r.id}${r.sub ? `/${r.sub}` : ''}`),
            h('span', null, `${r.size} B`),
            h('span', { class: 'dim' }, r.pos ? r.pos.map((x) => Math.round(x)).join(', ') : ''),
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
        for (const el of $('sv-groups').querySelectorAll<HTMLElement>('.rec'))
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
            const you = doc.records.find((x) => x.classId === 0x17081069 && x.pos);
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
        const table = h('table', { class: 'kv' });
        for (const [k, v] of [...rows, ...known]) {
            const tr = table.insertRow();
            tr.insertCell().textContent = k;
            tr.insertCell().textContent = v;
        }
        const pre = h('pre', { class: 'hexview' }, hexDump(e.data, 0, hexLimit));
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
                { class: 'dhead' },
                h('b', null, recordLabel(rec) || groupTitle(rec)),
                rec.pos
                    ? h('button', { onclick: () => showDetails(index, true) }, 'Show in 3D')
                    : h('small', { class: 'dim' }, 'no position'),
            ),
            table,
            pre,
            ...(more ? [more] : []),
        );
        if (in3d) showIn3d(index);
        else if (view && cur.doc.objectOfEntry.has(index)) {
            const id = cur.doc.objectOfEntry.get(index)!;
            if (!view.selection.has(id) || view.selection.size !== 1) view.select([id]);
        }
    }

    // ---- compare -----------------------------------------------------------------------------------------------

    const cmpSelect = h('select', { id: 'sv-cmp-select' });
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
        if (!cur) return host.setStatus('Open a save first.', true);
        const b = parseSave(await read());
        const a = cur.doc.pendingEdits().length ? cur.doc.build().save : cur.original;
        diff = {
            a: `${cur.ref.name}${cur.doc.pendingEdits().length ? ' (with pending edits)' : ''}`,
            b: label,
            result: diffSaves(a, b),
        };
        renderCompare();
        showSub('compare');
        host.setStatus(
            `Compared with ${label}: ${diff.result.counts.changed} changed, ${diff.result.counts.unchanged} unchanged entries.`,
        );
    }

    function compareWithOriginal(): void {
        if (!cur) return host.setStatus('Open a save first.', true);
        try {
            const built = cur.doc.build().save;
            diff = {
                a: `${cur.ref.name} (with pending edits)`,
                b: `${cur.ref.name} (as opened)`,
                result: diffSaves(built, cur.original),
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
            { class: 'toolbar' },
            h('button', { onclick: () => fileIn.click(), disabled: !cur }, 'Compare with file...'),
            cmpSelect,
            h(
                'button',
                { onclick: compareWithOriginal, disabled: !cur, title: 'Review what SAVE would change' },
                'Pending vs as opened',
            ),
            fileIn,
        );
        if (!diff) {
            pane.replaceChildren(
                toolbar,
                h(
                    'div',
                    { class: 'welcome' },
                    h(
                        'p',
                        null,
                        'Compare the open save (including pending edits) with another .rsg: the field table first, then every record as changed or unchanged.',
                    ),
                ),
            );
            return;
        }
        const d = diff.result;
        const ft = h(
            'table',
            { class: 'grid' },
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
            { class: 'grid' },
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
            h('div', { class: 'cmphead' }, h('b', null, 'A'), ` ${diff.a}  `, h('b', null, 'B'), ` ${diff.b}`),
            h('h4', null, 'Fields'),
            ft,
            h(
                'h4',
                null,
                `Records: ${d.counts.changed} changed, ${d.counts.unchanged} unchanged` +
                    (d.counts.onlyA || d.counts.onlyB
                        ? `, ${d.counts.onlyA} only in A, ${d.counts.onlyB} only in B`
                        : ''),
                h('label', { class: 'check' }, changedOnly, ' changed only'),
            ),
            et,
        );
    }

    renderCompareChoices();
    renderAll();
    return { openFile };
}

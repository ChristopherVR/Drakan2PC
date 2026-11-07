// "Make a campaign": a step-by-step wizard over the project's campaign settings, from a title to a pack installed in the
// game. Steps: name and cover, the levels (copies of disc levels, in order), linking the exits ("Link levels in order"
// gives each exit its own destination string, the Logic panel's "Only this exit"), the campaign checks with one-click
// fixes, play tests, export and install. It edits the same project as Project Settings > Campaign (campaign.ts).
// The pure part (steps, link plans, fixes) is wizardplan.ts. Format: docs/reference/modding.md "Campaign packs".
import { trapFocus } from '../focustrap.ts';
import {
    COVER_PALETTES,
    CAMPAIGN_DESCRIPTION_MAX,
    LevelDocument,
    SchemaProvider,
    campaignSlug,
    documentFromLvl,
    newCampaign,
    newProject,
    renderCoverPng,
    resourceParts,
    validateCampaign,
    withExitOverrides,
    type CampaignCover,
    type ExitIndex,
    type ModProject,
    type ProjectCampaign,
    type ProjectIssue,
    type ProjectLevel,
} from '@drakan/editor-core';
import { exitDestinationText } from '@drakan/formats';
import type { ExitLevelData } from '../../logicdata.ts';
import type { LevelInfo, LevelLoadResult } from '../../rpc.ts';
import type { LevelUiHost } from '../../levelui.ts';
import { isDesktop } from '../../platform.ts';
import { exitEditHost } from '../../story/exitbridge.ts';
import { autosaveKey, loadAutosave, saveAutosave } from '../autosave.ts';
import type { Ctx } from '../ctx.ts';
import { h, icon, store } from '../dom.ts';
import { registerCommand, runCommand } from '../registry.ts';
import { projectExitOverrides, type CampaignStrings } from './campaign.ts';
import { createCampaignMap } from './campaignmap.ts';
import { renameProject } from './checks.ts';
import type { ProjectPack } from './export.ts';
import { projects } from './store.ts';
import {
    WIZARD_STEPS,
    autoLinkable,
    copyTitleProblem,
    defaultCopyTitle,
    fixFor,
    issueLine,
    linkPlans,
    placeName,
    stepsDone,
    copyProjectLevel,
    type LinkData,
    type LinkPlan,
    type WizardFix,
    type WizardStepId,
} from './wizardplan.ts';
import './campaignwizard.css';

export interface WizardHost {
    ctx: Ctx;
    call: LevelUiHost['call'];
    /** Levels of the open disc (empty without a disc). */
    levels(): LevelInfo[];
    /** Open a project level in the Level Editor (a copy opens its source under the copy's title). */
    openLevel(l: ProjectLevel): Promise<void>;
    /** The edited document of a project level (the open one, else its autosave), null when unedited. */
    levelDoc(title: string): Promise<LevelDocument | null>;
    /** Persist the live document before moving its edits to a level copy. */
    flush(): Promise<void>;
    /** Create a project through the normal project I/O (including saving the previous folder). */
    newProject(): Promise<boolean>;
    strings: CampaignStrings;
    buildPack(p: ModProject, progress?: (msg: string) => void): Promise<ProjectPack>;
    zip(pack: ProjectPack): Uint8Array;
    /** Download (browser) or a "save as" dialog (desktop). */
    saveZip(name: string, bytes: Uint8Array): Promise<string | null>;
    installAvailable(): boolean;
    /** Write the pack into the game's mods folder and enable it; where it went, null when cancelled. */
    install(pack: ProjectPack): Promise<string | null>;
    /** Desktop: play the project from one of its levels. */
    playLevel(title: string): Promise<void>;
    /** Desktop: install the pack and start the game at its title screen ("Choose your game"). */
    playCampaign(pack: ProjectPack): Promise<string | null>;
    /** Desktop: start the game without changing anything. */
    startGame(): Promise<string | null>;
    openDisc(): void;
}

export interface CampaignWizard {
    open(step?: WizardStepId): void;
    close(): void;
    go(step: WizardStepId): void;
    isOpen(): boolean;
    step(): WizardStepId;
    resetDisc(): void;
    /** Put the "Make a campaign step by step" link into the Campaign pane of Project Settings (campaign.ts). */
    attachLink(pane: HTMLElement): HTMLElement;
}

const STEP_KEY = 'campaign-wizard-step';
const BACK_KEY = 'campaign-wizard-back';
const norm = (s: string): string => s.toLowerCase().replace(/[^a-z0-9]/g, '');
const plural = (n: number, one: string, many = `${one}s`): string => `${n} ${n === 1 ? one : many}`;

let instance: CampaignWizard | null = null;
/** The wizard, once installed (the tour and the start page use it). */
export const campaignWizard = (): CampaignWizard | null => instance;

/** A picture of the user's own, scaled and cropped to 960 x 540, as base64 PNG. */
async function pictureToCover(f: File): Promise<string> {
    const bmp = await createImageBitmap(f);
    const c = document.createElement('canvas');
    c.width = 960;
    c.height = 540;
    const g = c.getContext('2d')!;
    const s = Math.max(960 / bmp.width, 540 / bmp.height);
    g.drawImage(bmp, (960 - bmp.width * s) / 2, (540 - bmp.height * s) / 2, bmp.width * s, bmp.height * s);
    const blob = await new Promise<Blob>((res, rej) =>
        c.toBlob((b) => (b ? res(b) : rej(new Error('no PNG'))), 'image/png'),
    );
    const bytes = new Uint8Array(await blob.arrayBuffer());
    let bin = '';
    for (let i = 0; i < bytes.length; i += 0x8000) bin += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
    return btoa(bin);
}

const sev = (s: ProjectIssue['severity']): SVGElement => {
    const el = icon(s === 'error' ? 'clear' : s === 'warning' ? 'warn' : 'info', 14);
    el.classList.add('cwz-sev', s);
    return el;
};

export function installCampaignWizard(host: WizardHost): CampaignWizard {
    let step: WizardStepId = (WIZARD_STEPS.find((s) => s.id === store.get(STEP_KEY))?.id ?? 'about') as WizardStepId;
    let shown = false;
    let previousFocus: HTMLElement | null = null;
    let generation = 0;
    /** Exits per disc level (the source of each project level), read once per disc. */
    const exitCache = new Map<string, Promise<ExitLevelData | null>>();
    let discSig = '';
    let linkData: LinkData | null = null;
    let exitsState: 'none' | 'loading' | 'done' | 'failed' = 'none';
    let issues: ProjectIssue[] | null = null;
    let busy = '';
    let picked: string | null = null;
    let copyTitle = '';
    let addQuery = '';
    let linkBack = true;
    let lastShip: { where: string; installed: boolean } | null = null;
    let packLines: string[] | null = null;
    let packError = '';
    let previewKey = '';
    let previewUrl = '';
    const srcCache = new Map<string, Promise<LevelLoadResult>>();

    // ---- frame ------------------------------------------------------------------------------------------------------

    const rail = h('ol', { class: 'cwz-rail', 'aria-label': 'Steps' });
    const body = h('div', { class: 'cwz-body', id: 'cwz-body' });
    const status = h('span', { class: 'cwz-status', role: 'status', id: 'cwz-status' });
    const backBtn = h('button', { type: 'button', class: 'ui-btn', id: 'cwz-back', onclick: () => move(-1) }, 'Back');
    const nextBtn = h('button', { type: 'button', class: 'ui-btn primary', id: 'cwz-next', onclick: () => move(1) });
    const titleEl = h('h1', { class: 'cwz-title', id: 'cwz-heading' });
    const panel = h(
        'section',
        {
            class: 'cwz',
            role: 'dialog',
            'aria-modal': 'true',
            'aria-labelledby': 'cwz-heading',
            id: 'cwz',
            tabindex: -1,
        },
        h(
            'header',
            { class: 'cwz-head' },
            titleEl,
            h('span', { class: 'spacer' }),
            h(
                'button',
                {
                    type: 'button',
                    class: 'ui-tool',
                    id: 'cwz-close',
                    title: 'Close the wizard (your work stays in the project)',
                    'aria-label': 'Close the wizard',
                    onclick: () => close(),
                },
                icon('x', 14),
            ),
        ),
        h('div', { class: 'cwz-main' }, h('nav', { class: 'cwz-nav', 'aria-label': 'Campaign steps' }, rail), body),
        h('footer', { class: 'cwz-foot' }, backBtn, status, nextBtn),
    );
    const root = h('div', { class: 'cwz-backdrop', id: 'cwz-root', hidden: true }, panel);
    trapFocus(root);
    root.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && !document.querySelector('.tour-card')) {
            e.stopPropagation();
            close();
        }
        if (e.key === 'Tab' && !document.querySelector('.tour-card')) {
            const items = [
                ...panel.querySelectorAll<HTMLElement>(
                    'button:not(:disabled), input:not(:disabled), textarea, select:not(:disabled), [tabindex="0"]',
                ),
            ].filter((el) => el.getClientRects().length > 0);
            const first = items[0];
            const last = items.at(-1);
            if (e.shiftKey && (document.activeElement === first || document.activeElement === panel)) {
                e.preventDefault();
                last?.focus();
            } else if (!e.shiftKey && document.activeElement === last) {
                e.preventDefault();
                first?.focus();
            }
        }
    });
    const resume = h(
        'button',
        { type: 'button', class: 'cwz-resume', id: 'cwz-resume', hidden: true, onclick: () => open() },
        icon('layers', 14),
        h('span', {}, 'Back to the campaign wizard'),
    );
    document.body.append(root, resume);
    // The opener often loses the focus just before the wizard opens (its button is redrawn): remember the last control
    // outside the wizard that had it, so closing returns there.
    let lastOutside: HTMLElement | null = null;
    document.addEventListener(
        'focusin',
        (e) => {
            if (e.target instanceof HTMLElement && !root.contains(e.target) && !resume.contains(e.target))
                lastOutside = e.target;
        },
        true,
    );

    const project = (): ModProject | null => projects.get();
    const campaign = (): ProjectCampaign | null => (project()?.campaign?.enabled ? project()!.campaign! : null);
    const say = (msg: string, kind: 'info' | 'error' | 'ok' | 'warn' = 'info'): void => host.ctx.toast(msg, kind);
    const fail = (e: unknown): void => {
        busy = '';
        say(e instanceof Error ? e.message : String(e), 'error');
        render();
    };
    const update = (patch: Partial<ProjectCampaign>): void =>
        projects.update((q) => (q.campaign ? { ...q, campaign: { ...q.campaign, ...patch } } : q));
    const discLevels = (): LevelInfo[] => host.levels().filter((l) => l.present);
    const discDir = (title: string): string | null =>
        discLevels().find((l) => norm(l.name) === norm(title))?.dirPath ?? null;

    /** A project that is a campaign: the open one (turned into a campaign) or a new one. */
    function ensureCampaign(): ModProject {
        let p = project();
        if (!p) {
            p = newProject('My campaign');
            p = { ...p, campaign: { ...newCampaign(p), title: '' } };
            projects.set(p);
        } else if (!p.campaign?.enabled) {
            p = { ...p, campaign: p.campaign ? { ...p.campaign, enabled: true } : newCampaign(p) };
            projects.set(p);
        }
        return p;
    }

    // ---- exits ------------------------------------------------------------------------------------------------------

    function exitsOfDisc(name: string): Promise<ExitLevelData | null> {
        let pr = exitCache.get(norm(name));
        if (!pr) {
            pr = host
                .call('exitIndex', name)
                .then((d) => d.levels.find((l) => norm(l.name) === norm(name)) ?? d.levels[0] ?? null);
            exitCache.set(norm(name), pr);
            pr.catch(() => exitCache.delete(norm(name)));
        }
        return pr;
    }

    function loadSrc(dir: string): Promise<LevelLoadResult> {
        let pr = srcCache.get(dir);
        if (!pr) {
            pr = host.call('loadLevel', dir);
            srcCache.set(dir, pr);
            pr.catch(() => srcCache.delete(dir));
        }
        return pr;
    }

    let refreshing: Promise<void> | null = null;
    let again = false;
    /** Read the exits of the project's levels, apply the project's own exit edits and run the campaign checks. */
    function refresh(): Promise<void> {
        if (refreshing) {
            again = true;
            return refreshing;
        }
        refreshing = (async () => {
            do {
                again = false;
                await refreshOnce();
            } while (again);
        })().finally(() => (refreshing = null));
        return refreshing;
    }

    async function refreshOnce(): Promise<void> {
        const p = project();
        const gen = generation;
        const sig = discLevels()
            .map((l) => l.dirPath)
            .join('|');
        if (sig !== discSig) {
            discSig = sig;
            exitCache.clear();
            srcCache.clear();
        }
        if (!p || !discLevels().length) {
            linkData = null;
            exitsState = 'none';
            issues = p ? validateCampaign(p, { discLevels: [], exits: null }) : null;
            render();
            return;
        }
        if (!linkData) {
            exitsState = 'loading';
            render();
        }
        try {
            const srcs = [...new Set(p.levels.map((l) => l.copyOf ?? l.title))].filter((s) => discDir(s));
            const data = (await Promise.all(srcs.map(exitsOfDisc))).filter((d): d is ExitLevelData => !!d);
            await host.strings.ensure().catch(() => undefined);
            if (gen !== generation || p !== project()) {
                again = true;
                return;
            }
            const cur = p;
            const disc: ExitIndex = { levels: new Map(data.map((d) => [d.name, d.exits])) };
            const over = await projectExitOverrides(cur, { levels: data }, host.levelDoc, host.strings);
            linkData = { disc, current: withExitOverrides(cur, disc, over) };
            exitsState = 'done';
            issues = validateCampaign(cur, { discLevels: discLevels().map((l) => l.name), exits: linkData.current });
        } catch (e) {
            if (gen !== generation) return;
            linkData = null;
            issues = null;
            exitsState = 'failed';
            busy = '';
            say(`Could not read the exits: ${e instanceof Error ? e.message : String(e)}`, 'error');
        }
        render();
    }

    /**
     * Point exits of a project level at `dest` (a level title), each with a destination string of its own ("Only this
     * exit"), so the disc level and its other exits keep theirs. The open level is edited as an undo step; another
     * level gets the edit in its stored document (its autosave), which the export and Play read.
     */
    async function retarget(levelTitle: string, exitIds: readonly number[], dest: string): Promise<number> {
        const p = project();
        const l = p?.levels.find((x) => x.title === levelTitle);
        if (!p || !l || !exitIds.length) return 0;
        if (!l.copyOf) throw new Error(`"${l.title}" is an original Drakan level: make it a copy first.`);
        const ed = exitEditHost();
        if (!ed) throw new Error('The level strings are not ready yet. Try again in a moment.');
        await ed.ensure();
        const data = await exitsOfDisc(l.copyOf);
        const dir = discDir(l.copyOf);
        if (!data || !dir) throw new Error(`"${l.copyOf}" is not on the open disc.`);
        const isOpen = !!host.ctx.src() && norm(host.ctx.doc().level) === norm(l.title);
        let doc: LevelDocument;
        let schemas: SchemaProvider | null;
        if (isOpen) {
            doc = host.ctx.doc();
            schemas = host.ctx.schemas();
        } else {
            const r = await loadSrc(dir);
            schemas = new SchemaProvider(r.lvl.header, r.odbs, r.odbDeps);
            doc = (await host.levelDoc(l.title)) ?? documentFromLvl(r.lvl, r.odbs, l.title).doc;
        }
        const fresh = new Map<string, number>();
        const edits: { id: number; value: number; type: number; name: string }[] = [];
        let n = 0;
        for (const id of exitIds) {
            const e = data.exits.find((x) => x.id === id);
            const o = doc.get(id);
            const parts = o && resourceParts(o.resource);
            const cls = parts && data.classes[`${parts.dbIndex}:${parts.classId}`];
            if (!e || !o || !cls?.destRef) continue;
            const { file, id: classStr } = cls.destRef;
            const own = o.fields?.find((f) => f.fieldIndex === 0 && typeof f.value === 'number');
            if (own) {
                const text = ed.text(file, own.value as number);
                if (text !== undefined) {
                    if (ed.set(file, own.value as number, exitDestinationText(text, dest))) n++;
                    continue;
                }
                // A browser reload can restore a level autosave without the unsaved story strings. Recreate the
                // destination from the source class instead of silently claiming an orphaned string was retargeted.
            }
            const key = `${file}:${classStr}`;
            let sid = fresh.get(key);
            if (sid === undefined) {
                const text = ed.text(file, classStr);
                if (text === undefined) continue;
                const made = ed.addDestination(file, classStr, exitDestinationText(text, dest));
                if (made === null) continue;
                sid = made;
                fresh.set(key, sid);
            }
            const f0 = schemas?.schemaFor(o.resource)?.fields.find((f) => f.index === 0);
            if (!f0) continue;
            edits.push({ id, value: sid, type: f0.type, name: f0.name });
            n++;
        }
        if (edits.length) {
            const setField = (o: NonNullable<ReturnType<LevelDocument['get']>>): void => {
                const e = edits.find((x) => x.id === o.id);
                if (!e) return;
                const rest = (o.fields ?? []).filter((f) => f.fieldIndex !== 0);
                o.fields = [...rest, { slot: rest.length, fieldIndex: 0, type: e.type, name: e.name, value: e.value }];
            };
            if (isOpen) {
                host.ctx.edit(
                    `Link ${plural(edits.length, 'exit')} to ${dest}`,
                    edits.map((e) => e.id),
                    setField,
                );
            } else {
                for (const e of edits) setField(doc.get(e.id)!);
                const saved = await saveAutosave({
                    key: autosaveKey(l.title),
                    level: l.title,
                    savedAt: Date.now(),
                    doc: doc.toJSON(),
                    bookmarks: (await loadAutosave(autosaveKey(l.title)))?.bookmarks ?? [],
                });
                if (!saved)
                    throw new Error('The browser could not save the exit edits. Free some storage and try again.');
                // Opening the level later restores the stored edits without asking.
                projects.autoRestore.add(l.title);
                projects.edited();
            }
        }
        return n;
    }

    async function linkPlan(plan: LinkPlan, exitIds?: readonly number[]): Promise<number> {
        const forward = exitIds ?? (plan.state === 'linked' ? [] : plan.forward.map((e) => e.id));
        let n = await retarget(plan.from.title, forward, plan.to.title);
        if (linkBack && plan.back.length && plan.to.copyOf)
            n += await retarget(
                plan.to.title,
                plan.back.map((e) => e.id),
                plan.from.title,
            );
        return n;
    }

    async function linkInOrder(): Promise<void> {
        if (!linkData) await refresh();
        const p = project();
        if (!p || !linkData) return;
        const todo = autoLinkable(linkPlans(p, linkData));
        busy = 'Linking the exits...';
        render();
        let n = 0;
        try {
            for (const plan of todo) n += await linkPlan(plan, plan.state === 'linked' ? [] : undefined);
        } catch (e) {
            fail(e);
            return;
        }
        busy = '';
        await refresh();
        const left = linkPlans(project()!, linkData).filter((l) => l.state !== 'linked').length;
        say(
            n
                ? `Linked ${plural(n, 'exit')}.${left ? ` ${plural(left, 'level')} still need an exit chosen by hand.` : ''}`
                : left
                  ? 'Nothing could be linked by itself: choose an exit for each level below.'
                  : 'Every level already leads to the next one.',
            n ? 'ok' : 'info',
        );
    }

    // ---- fixes ------------------------------------------------------------------------------------------------------

    async function copyInstead(title: string): Promise<void> {
        const p = project();
        const l = p?.levels.find((x) => x.title === title);
        if (!p || !l || l.copyOf) return;
        const taken = [...p.levels.map((x) => x.title), ...discLevels().map((x) => x.name)];
        const t = defaultCopyTitle(l.title, taken);
        // Flush and prefer the live document: the timed autosave may not contain the latest stroke or object edit.
        await host.flush();
        const live = await host.levelDoc(l.title);
        const rec = await loadAutosave(autosaveKey(l.title));
        const wasOpen = !!host.ctx.src() && norm(host.ctx.doc().level) === norm(l.title);
        if (live || rec) {
            const doc = LevelDocument.fromJSON(live ? live.toJSON() : rec!.doc);
            doc.level = t;
            const saved = await saveAutosave({
                key: autosaveKey(t),
                level: t,
                doc: doc.toJSON(),
                savedAt: Date.now(),
                bookmarks: rec?.bookmarks ?? [],
            });
            if (!saved)
                throw new Error('The browser could not save the copy. The original level and its edits are unchanged.');
            projects.autoRestore.add(t);
        }
        projects.update((q) => copyProjectLevel(q, title, t));
        // Keep the source autosave as a recovery copy; it is no longer a level of this project or part of its export.
        if (wasOpen) await host.openLevel({ title: t, copyOf: title });
        projects.edited();
        say(`${title} is now the copy "${t}". Drakan's own level stays as it was.`, 'ok');
    }

    async function runFix(f: WizardFix): Promise<void> {
        const c = campaign();
        switch (f.kind) {
            case 'title':
                go('about');
                setTimeout(() => document.getElementById('cwz-title')?.focus(), 0);
                return;
            case 'id':
                update({ id: f.value, ...(c && c.saves === c.id ? { saves: f.value } : {}) });
                return;
            case 'saves':
                update({ saves: f.value });
                return;
            case 'start':
                update({ start: f.level });
                return;
            case 'copy-instead':
                await copyInstead(f.level);
                return;
            case 'cover':
                update({ cover: newCover() });
                return;
            case 'goto':
                go(f.step);
                return;
            case 'link': {
                const plan = linkData
                    ? linkPlans(project()!, linkData).find((l) => l.from.title === f.from && l.to.title === f.to)
                    : null;
                if (plan) await linkPlan(plan);
                await refresh();
                return;
            }
            case 'retarget':
                await retarget(f.level, [f.exitId], f.to);
                await refresh();
                return;
        }
    }

    async function fixAll(): Promise<void> {
        const p = project();
        if (!p || !issues) return;
        busy = 'Fixing...';
        render();
        try {
            for (let round = 0; round < 3; round++) {
                const fixes = (issues ?? [])
                    .map((i) => fixFor(project()!, i, linkData))
                    .filter((f): f is WizardFix => !!f && f.kind !== 'goto' && f.kind !== 'title');
                if (!fixes.length) break;
                const seen = new Set<string>();
                for (const f of fixes) {
                    const k = JSON.stringify(f);
                    if (seen.has(k)) continue;
                    seen.add(k);
                    await runFix(f);
                }
                await refresh();
            }
        } catch (e) {
            fail(e);
            return;
        }
        busy = '';
        render();
    }

    const newCover = (palette?: string, dragon = true): CampaignCover => ({
        kind: 'generated',
        palette: palette ?? COVER_PALETTES[0]!.id,
        seed: Math.floor(Math.random() * 0x7fffffff),
        dragon,
    });

    // ---- steps ------------------------------------------------------------------------------------------------------

    const field = (label: string, id: string, input: HTMLElement, hint?: string | null, bad?: string | null) =>
        h(
            'div',
            { class: 'cwz-field' },
            h('label', { for: id }, label),
            input,
            bad || hint ? h('div', { class: `cwz-hint${bad ? ' bad' : ''}` }, bad ?? hint ?? '') : null,
        );

    function needDisc(what: string): HTMLElement {
        return h(
            'div',
            { class: 'cwz-empty' },
            icon('open', 22),
            h('strong', {}, 'Open your disc image first'),
            h('p', {}, `The editor reads your own copy of the game to ${what}. The disc itself is never changed.`),
            h(
                'button',
                { type: 'button', class: 'ui-btn primary', id: 'cwz-open-disc', onclick: () => host.openDisc() },
                'Open your disc image...',
            ),
        );
    }

    function coverPreview(c: ProjectCampaign): HTMLElement {
        const key = JSON.stringify(c.cover);
        if (c.cover && key !== previewKey) {
            previewKey = key;
            if (previewUrl) URL.revokeObjectURL(previewUrl);
            const cover = c.cover;
            const bytes =
                cover.kind === 'generated'
                    ? renderCoverPng({ ...cover, width: 480, height: 270 })
                    : Uint8Array.from(atob(cover.png), (ch) => ch.charCodeAt(0));
            previewUrl = URL.createObjectURL(new Blob([bytes as BlobPart], { type: 'image/png' }));
        }
        return h(
            'figure',
            { class: 'cwz-card', id: 'cwz-cover' },
            c.cover
                ? h('img', { src: previewUrl, alt: '', class: 'cwz-card-img' })
                : h('div', { class: 'cwz-card-img none' }),
            h(
                'figcaption',
                { class: 'cwz-card-text' },
                h('span', { class: 'cwz-card-title' }, c.title.trim() || 'Your campaign'),
                h('span', { class: 'cwz-card-sub' }, 'A Drakan campaign'),
                c.description.trim() ? h('span', { class: 'cwz-card-desc' }, c.description.trim()) : null,
            ),
        );
    }

    function aboutStep(): HTMLElement {
        const p = project();
        if (p && !p.campaign?.enabled) {
            return h(
                'div',
                { class: 'cwz-choice' },
                h('p', {}, `Your project "${p.name}" is not a campaign yet.`),
                h(
                    'div',
                    { class: 'cwz-row' },
                    h(
                        'button',
                        {
                            type: 'button',
                            class: 'ui-btn primary',
                            id: 'cwz-make-this',
                            onclick: () => {
                                ensureCampaign();
                                render();
                            },
                        },
                        `Make "${p.name}" a campaign`,
                    ),
                    h(
                        'button',
                        {
                            type: 'button',
                            class: 'ui-btn',
                            id: 'cwz-new-project',
                            title: 'Your current project stays saved where it is',
                            onclick: () =>
                                void host
                                    .newProject()
                                    .then((made) => {
                                        if (made) ensureCampaign();
                                        render();
                                    })
                                    .catch(fail),
                        },
                        'Start a new project for it',
                    ),
                ),
                p.levels.length
                    ? h(
                          'p',
                          { class: 'cwz-hint' },
                          `Its ${plural(p.levels.length, 'level')} become the campaign's levels.`,
                      )
                    : null,
            );
        }
        const c = ensureCampaign().campaign!;
        const title = h('input', {
            type: 'text',
            class: 'ui-input',
            id: 'cwz-title',
            spellcheck: 'true',
            placeholder: 'The Lost Valley',
            maxlength: 60,
        });
        title.value = c.title;
        title.addEventListener('input', () => {
            const v = title.value;
            projects.update((q) => {
                const cc = q.campaign!;
                const slugWas = campaignSlug(cc.title || q.name);
                const followId = !cc.id || cc.id === slugWas;
                const id = followId ? campaignSlug(v || q.name) : cc.id;
                const named = v.trim() ? renameProject(q, v.trim()) : q;
                return {
                    ...named,
                    campaign: { ...cc, title: v, id, saves: cc.saves === cc.id ? id : cc.saves },
                };
            });
        });
        const desc = h('textarea', {
            class: 'ui-input',
            id: 'cwz-desc',
            rows: 2,
            spellcheck: 'true',
            placeholder: 'Rynn wakes in a valley no map shows.',
        });
        desc.value = c.description;
        desc.addEventListener('input', () => {
            const v = desc.value.replace(/[\r\n]+/g, ' ');
            projects.update((q) => ({ ...q, description: v, campaign: { ...q.campaign!, description: v } }));
        });
        const gen = c.cover?.kind === 'generated' ? c.cover : null;
        const pal = h(
            'select',
            { class: 'ui-input', id: 'cwz-palette', 'aria-label': 'Colours of the picture' },
            ...COVER_PALETTES.map((x) => h('option', { value: x.id }, x.label)),
        );
        pal.value = gen?.palette ?? COVER_PALETTES[0]!.id;
        pal.addEventListener('change', () =>
            update({ cover: gen ? { ...gen, palette: pal.value } : newCover(pal.value) }),
        );
        const file = h('input', { type: 'file', accept: 'image/png,image/jpeg,image/webp', hidden: true });
        file.addEventListener('change', () => {
            const f = file.files?.[0];
            file.value = '';
            if (!f) return;
            void pictureToCover(f)
                .then((png) => update({ cover: { kind: 'picked', png, name: f.name } }))
                .catch((e: unknown) => say(`Could not read that picture: ${String(e)}`, 'error'));
        });
        return h(
            'div',
            { class: 'cwz-split' },
            h(
                'div',
                { class: 'cwz-form' },
                field('Title', 'cwz-title', title, 'Shown on the "Choose your game" screen.', null),
                field(
                    'Short text',
                    'cwz-desc',
                    desc,
                    `One line under the picture. ${c.description.length} of ${CAMPAIGN_DESCRIPTION_MAX} characters.`,
                    c.description.length > CAMPAIGN_DESCRIPTION_MAX ? 'Too long: the game cuts it.' : null,
                ),
                h(
                    'div',
                    { class: 'cwz-field' },
                    h('label', { for: 'cwz-palette' }, 'Picture'),
                    h(
                        'div',
                        { class: 'cwz-row' },
                        h(
                            'button',
                            {
                                type: 'button',
                                class: 'ui-btn',
                                id: 'cwz-reroll',
                                title: 'Draw another mountain view',
                                onclick: () => update({ cover: newCover(pal.value, gen?.dragon ?? true) }),
                            },
                            'New picture',
                        ),
                        pal,
                        h(
                            'button',
                            {
                                type: 'button',
                                class: 'ui-btn ghost',
                                id: 'cwz-own',
                                title: 'A picture from your own files, scaled to 960 x 540',
                                onclick: () => file.click(),
                            },
                            'Use my own picture...',
                        ),
                        file,
                    ),
                    c.cover?.kind === 'picked'
                        ? h('div', { class: 'cwz-hint' }, `Your picture: ${c.cover.name}`)
                        : null,
                ),
                h(
                    'p',
                    { class: 'cwz-hint' },
                    `Saves of this campaign go to their own folder (mc0_${c.saves || c.id}), apart from Drakan's. Project Settings > Campaign has the id and the save folder.`,
                ),
            ),
            h(
                'div',
                { class: 'cwz-preview' },
                h('div', { class: 'cwz-kicker' }, 'How it looks in the game'),
                coverPreview(c),
            ),
        );
    }

    function levelsStep(): HTMLElement {
        if (!discLevels().length) return needDisc('list its levels');
        const p = ensureCampaign();
        const taken = [...p.levels.map((x) => x.title), ...discLevels().map((x) => x.name)];
        const rows = p.levels.map((l, i) =>
            h(
                'li',
                { class: `cwz-lvl${l.copyOf ? '' : ' disc'}`, 'data-title': l.title },
                h('span', { class: 'cwz-lvl-n' }, String(i + 1)),
                h(
                    'span',
                    { class: 'cwz-lvl-t' },
                    h('strong', {}, l.title),
                    h(
                        'span',
                        { class: 'cwz-lvl-sub' },
                        l.copyOf
                            ? `copy of ${l.copyOf}${i === 0 ? ', New Game starts here' : ''}`
                            : 'an original Drakan level: changing it would change Drakan too',
                    ),
                ),
                l.copyOf
                    ? null
                    : h(
                          'button',
                          {
                              type: 'button',
                              class: 'ui-btn sm',
                              onclick: () => void copyInstead(l.title).catch(fail),
                          },
                          'Use a copy instead',
                      ),
                h(
                    'button',
                    {
                        type: 'button',
                        class: 'ui-tool sm',
                        title: 'Earlier',
                        'aria-label': `Move ${l.title} earlier`,
                        disabled: i === 0,
                        onclick: () => moveLevel(i, -1),
                    },
                    icon('chevron-down', 12),
                ),
                h(
                    'button',
                    {
                        type: 'button',
                        class: 'ui-tool sm flip',
                        title: 'Later',
                        'aria-label': `Move ${l.title} later`,
                        disabled: i === p.levels.length - 1,
                        onclick: () => moveLevel(i, 1),
                    },
                    icon('chevron-down', 12),
                ),
                h(
                    'button',
                    {
                        type: 'button',
                        class: 'ui-tool sm',
                        title: 'Take it out of the campaign',
                        'aria-label': `Remove ${l.title}`,
                        onclick: () => removeLevel(l.title),
                    },
                    icon('trash', 12),
                ),
            ),
        );
        const q = h('input', {
            type: 'search',
            class: 'ui-input',
            id: 'cwz-add-search',
            placeholder: 'Find a level',
            'aria-label': 'Find a level of the disc',
        });
        q.value = addQuery;
        q.addEventListener('input', () => {
            addQuery = q.value;
            paintList();
        });
        const list = h('ul', {
            class: 'cwz-disc',
            id: 'cwz-disc',
            role: 'listbox',
            'aria-label': 'Levels of the disc',
        });
        const paintList = (): void => {
            const s = addQuery.trim().toLowerCase();
            list.replaceChildren(
                ...discLevels()
                    .filter((l) => !s || l.name.toLowerCase().includes(s))
                    .map((l) => {
                        const used = p.levels.filter((x) => norm(x.copyOf ?? '') === norm(l.name)).length;
                        return h(
                            'li',
                            {
                                class: `cwz-disc-row${picked === l.name ? ' on' : ''}`,
                                role: 'option',
                                tabindex: 0,
                                'aria-selected': String(picked === l.name),
                                'data-level': l.name,
                                onclick: () => pick(l.name),
                                ondblclick: () => addCopy(l.name, defaultCopyTitle(l.name, taken)),
                                onkeydown: (e: Event) => {
                                    if ((e as KeyboardEvent).key === 'Enter') pick(l.name);
                                },
                            },
                            h('span', { class: 'cwz-disc-name' }, l.name),
                            used
                                ? h('span', { class: 'cwz-tag' }, used === 1 ? 'In campaign' : `${used} copies`)
                                : null,
                        );
                    }),
            );
        };
        paintList();
        const titleIn = h('input', {
            type: 'text',
            class: 'ui-input',
            id: 'cwz-copy-title',
            spellcheck: 'false',
            disabled: !picked,
            placeholder: 'Pick a level first',
        });
        titleIn.value = picked ? copyTitle : '';
        const bad = picked ? copyTitleProblem(copyTitle, taken) : null;
        titleIn.addEventListener('input', () => {
            copyTitle = titleIn.value;
            const why = copyTitleProblem(copyTitle, taken);
            addBtn.disabled = !!why;
            hint.textContent = why ?? 'The copy gets this name in the game and in your project.';
            hint.classList.toggle('bad', !!why);
        });
        titleIn.addEventListener('keydown', (e) => {
            if (e.key === 'Enter' && picked && !copyTitleProblem(copyTitle, taken)) addCopy(picked, copyTitle);
        });
        const hint = h(
            'div',
            { class: `cwz-hint${bad ? ' bad' : ''}` },
            bad ?? 'The copy gets this name in the game and in your project.',
        );
        const addBtn = h(
            'button',
            {
                type: 'button',
                class: 'ui-btn primary',
                id: 'cwz-add-copy',
                disabled: !picked || !!bad,
                onclick: () => picked && addCopy(picked, copyTitle),
            },
            icon('plus', 13),
            picked ? `Add a copy of ${placeName(picked)}` : 'Add a copy',
        );
        return h(
            'div',
            { class: 'cwz-split levels' },
            h(
                'div',
                { class: 'cwz-col' },
                h('div', { class: 'cwz-kicker' }, `Your campaign, in play order`),
                rows.length
                    ? h('ol', { class: 'cwz-lvls', id: 'cwz-lvls' }, ...rows)
                    : h(
                          'div',
                          { class: 'cwz-none', id: 'cwz-lvls' },
                          'No levels yet. Pick one on the right and add a copy of it.',
                      ),
                h(
                    'aside',
                    { class: 'cwz-why', id: 'cwz-why' },
                    h('strong', {}, 'Why copies?'),
                    h(
                        'p',
                        {},
                        'A campaign is installed next to Drakan, not over it. Each of its levels is a copy of an original Drakan level with a name of its own, so everything you change stays in your campaign and Drakan plays exactly as before.',
                    ),
                ),
            ),
            h(
                'div',
                { class: 'cwz-col' },
                h('div', { class: 'cwz-kicker' }, 'Add a level'),
                q,
                list,
                h(
                    'div',
                    { class: 'cwz-field' },
                    h('label', { for: 'cwz-copy-title' }, 'Name of the copy'),
                    titleIn,
                    hint,
                ),
                h('div', { class: 'cwz-row' }, addBtn),
            ),
        );
    }

    function pick(name: string): void {
        const p = project();
        picked = name;
        copyTitle = defaultCopyTitle(name, [
            ...(p?.levels.map((x) => x.title) ?? []),
            ...discLevels().map((x) => x.name),
        ]);
        render();
        document.getElementById('cwz-copy-title')?.focus();
    }

    function addCopy(source: string, title: string): void {
        const p = ensureCampaign();
        const t = title.trim();
        const why = copyTitleProblem(t, [...p.levels.map((x) => x.title), ...discLevels().map((x) => x.name)]);
        if (why) return say(why, 'warn');
        projects.update((q) => {
            const levels = [...q.levels, { title: t, copyOf: source }];
            const c = q.campaign!;
            return {
                ...q,
                levels,
                start: q.start ?? t,
                campaign: { ...c, start: c.start && levels.some((l) => l.title === c.start) ? c.start : t },
            };
        });
        picked = null;
        copyTitle = '';
        say(`Added "${t}", a copy of ${source}.`, 'ok');
    }

    function moveLevel(i: number, d: -1 | 1): void {
        projects.update((q) => {
            const levels = q.levels.slice();
            const j = i + d;
            if (j < 0 || j >= levels.length) return q;
            [levels[i], levels[j]] = [levels[j]!, levels[i]!];
            // New Game opens the first level of the list.
            const first = levels.find((l) => l.copyOf)?.title ?? null;
            return { ...q, levels, ...(q.campaign ? { campaign: { ...q.campaign, start: first } } : {}) };
        });
    }

    function removeLevel(title: string): void {
        projects.update((q) => {
            const levels = q.levels.filter((l) => l.title !== title);
            const first = levels.find((l) => l.copyOf)?.title ?? null;
            const c = q.campaign;
            return {
                ...q,
                levels,
                start: q.start === title ? null : q.start,
                ...(c ? { campaign: { ...c, start: c.start === title || !c.start ? first : c.start } } : {}),
            };
        });
    }

    const map = createCampaignMap({
        openLevel: (l) => {
            minimise();
            void host.openLevel(l).catch(fail);
        },
        openExit: (l, id) => {
            minimise();
            void host
                .openLevel(l)
                .then(() => {
                    host.ctx.locate([id]);
                    runCommand('panel.logic');
                })
                .catch(fail);
        },
        play: isDesktop ? (t) => void host.playLevel(t).catch(fail) : null,
        setLayout: (layout) => projects.update((q) => ({ ...q, mapLayout: layout })),
    });

    function planRow(plan: LinkPlan): HTMLElement {
        const label = h(
            'span',
            { class: 'cwz-link-route' },
            h('strong', {}, plan.from.title),
            icon('chevron', 12),
            h('strong', {}, plan.to.title),
        );
        let state: HTMLElement;
        let action: HTMLElement | null = null;
        switch (plan.state) {
            case 'linked':
                state = h(
                    'span',
                    { class: 'cwz-chip ok' },
                    icon('check', 12),
                    `Linked: ${plan.forward.map((e) => `"${e.name}"`).join(', ')}`,
                );
                break;
            case 'suggested':
                state = h(
                    'span',
                    { class: 'cwz-chip todo' },
                    `${plural(plan.forward.length, 'exit')} of ${placeName(plan.from.copyOf!)} lead to ${placeName(plan.to.copyOf ?? plan.to.title)} in Drakan`,
                );
                action = h('button', { type: 'button', class: 'ui-btn sm', onclick: () => void runLink(plan) }, 'Link');
                break;
            case 'manual': {
                const sel = h(
                    'select',
                    {
                        class: 'ui-input cwz-exit-sel',
                        'aria-label': `Exit of ${plan.from.title} that leads to ${plan.to.title}`,
                    },
                    h('option', { value: '' }, 'Choose the exit...'),
                    ...plan.all.map((e) =>
                        h('option', { value: String(e.id) }, `${e.name} (now to ${placeName(e.dest ?? 'nowhere')})`),
                    ),
                );
                state = h('span', { class: 'cwz-chip warn' }, 'No exit leads there in Drakan: choose one');
                action = h(
                    'span',
                    { class: 'cwz-row' },
                    sel,
                    h(
                        'button',
                        {
                            type: 'button',
                            class: 'ui-btn sm',
                            onclick: () => {
                                const id = Number(sel.value);
                                if (!id) return say('Choose the exit first.', 'warn');
                                void runLink(plan, [id]);
                            },
                        },
                        'Link',
                    ),
                );
                break;
            }
            case 'disc':
                state = h('span', { class: 'cwz-chip warn' }, `${plan.from.title} is an original Drakan level`);
                action = h(
                    'button',
                    {
                        type: 'button',
                        class: 'ui-btn sm',
                        onclick: () => void copyInstead(plan.from.title).catch(fail),
                    },
                    'Use a copy instead',
                );
                break;
            default:
                state = h(
                    'span',
                    { class: 'cwz-chip' },
                    exitsState === 'loading' ? 'Reading the exits...' : 'Not read yet',
                );
        }
        return h('li', { class: `cwz-link ${plan.state}` }, label, state, h('span', { class: 'spacer' }), action);
    }

    async function runLink(plan: LinkPlan, ids?: number[]): Promise<void> {
        busy = 'Linking...';
        render();
        try {
            const n = await linkPlan(plan, ids);
            busy = '';
            await refresh();
            say(`${plan.from.title} now leads to ${plan.to.title} (${plural(n, 'exit')}).`, 'ok');
        } catch (e) {
            fail(e);
        }
    }

    function exitsStep(): HTMLElement {
        if (!discLevels().length) return needDisc('read where the exits lead');
        const p = ensureCampaign();
        if (p.levels.length < 2)
            return h(
                'div',
                { class: 'cwz-empty' },
                h('strong', {}, 'Add at least two levels first'),
                h('p', {}, 'A link goes from one level of your campaign to the next.'),
                h(
                    'button',
                    { type: 'button', class: 'ui-btn primary', onclick: () => go('levels') },
                    'Choose the levels',
                ),
            );
        const plans = linkPlans(p, linkData);
        const todo = autoLinkable(plans).length;
        const back = h('input', { type: 'checkbox', id: 'cwz-link-back' });
        back.checked = linkBack;
        back.addEventListener('change', () => (linkBack = back.checked));
        return h(
            'div',
            { class: 'cwz-stack' },
            h(
                'div',
                { class: 'cwz-row cwz-linkbar' },
                h(
                    'button',
                    {
                        type: 'button',
                        class: 'ui-btn primary',
                        id: 'cwz-link-order',
                        disabled: !!busy || exitsState !== 'done',
                        title: 'Point the exits that lead to the next level in Drakan at your copy of it',
                        onclick: () => void linkInOrder(),
                    },
                    icon('link', 13),
                    'Link levels in order',
                ),
                h('label', { class: 'cwz-check', for: 'cwz-link-back' }, back, 'Also link the way back'),
                h(
                    'span',
                    { class: 'cwz-hint' },
                    exitsState === 'loading'
                        ? 'Reading the exits...'
                        : todo
                          ? `${plural(todo, 'link')} can be made for you.`
                          : plans.every((l) => l.state === 'linked')
                            ? 'Every level leads to the next one.'
                            : '',
                ),
            ),
            h('ul', { class: 'cwz-links', id: 'cwz-links' }, ...plans.map(planRow)),
            h(
                'div',
                { class: 'cwz-mapwrap', id: 'cwz-map' },
                map.update({
                    project: p,
                    exits: linkData?.current ?? null,
                    issues: issues ?? [],
                    counts: new Map(),
                    note:
                        exitsState === 'loading'
                            ? 'Reading the exits of every level...'
                            : exitsState === 'failed'
                              ? 'The exits could not be read.'
                              : '',
                }),
            ),
            h(
                'p',
                { class: 'cwz-hint' },
                'To send an exit somewhere else, click its arrow on the map: the level opens with the exit selected in the Logic panel. The wizard waits in the corner of the window.',
            ),
        );
    }

    function checkStep(): HTMLElement {
        const p = ensureCampaign();
        const list = issues ?? [];
        const errors = list.filter((i) => i.severity === 'error').length;
        const warnings = list.filter((i) => i.severity === 'warning').length;
        const fixable = list.filter((i) => {
            const f = fixFor(p, i, linkData);
            return f && f.kind !== 'goto' && f.kind !== 'title';
        }).length;
        const start = campaign()?.start;
        const summary =
            exitsState === 'failed'
                ? h(
                      'div',
                      { class: 'cwz-verdict bad' },
                      'The exits could not be checked. Check again before exporting.',
                  )
                : exitsState === 'loading'
                  ? h('div', { class: 'cwz-verdict' }, 'Walking every exit from the first level...')
                  : !discLevels().length
                    ? h('div', { class: 'cwz-verdict warn' }, 'Open your disc image so the exits can be checked.')
                    : errors
                      ? h(
                            'div',
                            { class: 'cwz-verdict bad', id: 'cwz-verdict' },
                            sev('error'),
                            `${plural(errors, 'problem')} would stop a player. Fix ${errors === 1 ? 'it' : 'them'} before you export.`,
                        )
                      : h(
                            'div',
                            { class: 'cwz-verdict ok', id: 'cwz-verdict' },
                            icon('check-circle', 16),
                            `Ready: a player can walk from ${start ?? 'the first level'} through every level.`,
                        );
        const rows = list.map((i) => {
            const f = fixFor(p, i, linkData);
            return h(
                'li',
                { class: `cwz-issue ${i.severity}` },
                sev(i.severity),
                h('span', { class: 'cwz-issue-text', title: i.message }, issueLine(i)),
                f
                    ? h(
                          'button',
                          {
                              type: 'button',
                              class: `ui-btn sm${i.severity === 'error' ? ' primary' : ''}`,
                              disabled: !!busy,
                              onclick: () => {
                                  busy = 'Fixing...';
                                  render();
                                  void runFix(f)
                                      .then(() => {
                                          busy = '';
                                          return refresh();
                                      })
                                      .catch(fail);
                              },
                          },
                          f.label,
                      )
                    : null,
            );
        });
        return h(
            'div',
            { class: 'cwz-stack' },
            summary,
            h(
                'div',
                { class: 'cwz-row' },
                h(
                    'button',
                    {
                        type: 'button',
                        class: 'ui-btn',
                        id: 'cwz-recheck',
                        disabled: exitsState === 'loading' || !!busy,
                        onclick: () => {
                            linkData = null;
                            exitCache.clear();
                            void refresh();
                        },
                    },
                    'Check again',
                ),
                fixable > 1
                    ? h(
                          'button',
                          {
                              type: 'button',
                              class: 'ui-btn',
                              id: 'cwz-fix-all',
                              disabled: !!busy,
                              onclick: () => void fixAll(),
                          },
                          `Fix ${fixable} for me`,
                      )
                    : null,
                h(
                    'span',
                    { class: 'cwz-hint' },
                    warnings
                        ? `${plural(warnings, 'warning')}: the campaign works, but read them.`
                        : list.length
                          ? ''
                          : 'Nothing to report.',
                ),
            ),
            rows.length ? h('ul', { class: 'cwz-issues', id: 'cwz-issues' }, ...rows) : null,
        );
    }

    async function withPack<T>(fn: (pack: ProjectPack) => Promise<T>, what: string): Promise<T | null> {
        const p = project();
        if (!p) return null;
        busy = what;
        render();
        try {
            await refresh();
            if (exitsState !== 'done' || issues === null || issues.some((i) => i.severity === 'error'))
                throw new Error('Check the campaign and fix its errors before exporting or playing it.');
            if (project() !== p) throw new Error('The project changed while it was being checked. Try again.');
            const pack = await host.buildPack(p, (m) => {
                status.textContent = m;
            });
            packLines = pack.lines;
            packError = '';
            const r = await fn(pack);
            busy = '';
            render();
            return r;
        } catch (e) {
            packError = e instanceof Error ? e.message : String(e);
            fail(e);
            return null;
        }
    }

    function playStep(): HTMLElement {
        const p = ensureCampaign();
        const c = campaign();
        const errs = exitsState !== 'done' || issues === null || issues.some((i) => i.severity === 'error');
        if (!isDesktop)
            return h(
                'div',
                { class: 'cwz-stack' },
                h(
                    'div',
                    { class: 'cwz-empty left' },
                    h('strong', {}, 'Playing starts the game, which needs the desktop app'),
                    h(
                        'p',
                        {},
                        'Use Export and install. If you save a zip, put it in the game’s mods folder and start the game: the title screen offers Enable and restart, then choose your campaign.',
                    ),
                ),
            );
        return h(
            'div',
            { class: 'cwz-stack' },
            h(
                'div',
                { class: 'cwz-playcard' },
                h(
                    'div',
                    {},
                    h('strong', {}, 'From the title screen'),
                    h(
                        'p',
                        {},
                        `The game starts on a fresh test memory card and shows "Choose your game". Pick ${c?.title || 'your campaign'}, then New Game: it opens ${c?.start ?? 'the first level'}.`,
                    ),
                ),
                h(
                    'button',
                    {
                        type: 'button',
                        class: 'ui-btn play',
                        id: 'cwz-play-campaign',
                        disabled: !!busy || errs,
                        title: errs ? 'Fix the problems of the Check step first' : '',
                        onclick: () =>
                            void withPack(async (pack) => {
                                const m = await host.playCampaign(pack);
                                if (m) {
                                    projects.session.played = true;
                                    projects.notify();
                                    say(m, 'ok');
                                }
                            }, 'Building the pack and starting the game...'),
                    },
                    icon('play', 13),
                    'Play the campaign',
                ),
            ),
            h('div', { class: 'cwz-kicker' }, 'One level on its own'),
            h(
                'ul',
                { class: 'cwz-lvls play' },
                ...p.levels.map((l, i) =>
                    h(
                        'li',
                        { class: 'cwz-lvl' },
                        h('span', { class: 'cwz-lvl-n' }, String(i + 1)),
                        h('span', { class: 'cwz-lvl-t' }, h('strong', {}, l.title)),
                        h(
                            'button',
                            {
                                type: 'button',
                                class: 'ui-btn sm',
                                disabled: !!busy,
                                title: `Start the game straight in ${l.title}`,
                                onclick: () => void host.playLevel(l.title).catch(fail),
                            },
                            icon('play', 12),
                            'Play',
                        ),
                    ),
                ),
            ),
        );
    }

    function shipStep(): HTMLElement {
        const c = campaign();
        const p = ensureCampaign();
        const errs = (issues ?? []).filter((i) => i.severity === 'error');
        const unchecked = exitsState !== 'done' || issues === null;
        if (lastShip?.installed) {
            return h(
                'div',
                { class: 'cwz-done', id: 'cwz-done' },
                icon('check-circle', 28),
                h('h2', {}, 'Installed'),
                h(
                    'p',
                    { class: 'cwz-done-line' },
                    `Start the game and choose ${c?.title || 'your campaign'} on the Choose your game screen.`,
                ),
                h('p', { class: 'cwz-hint' }, `The pack is in ${lastShip.where}, turned on in mods.ini.`),
                h(
                    'div',
                    { class: 'cwz-row' },
                    isDesktop
                        ? h(
                              'button',
                              {
                                  type: 'button',
                                  class: 'ui-btn play',
                                  id: 'cwz-start-game',
                                  onclick: () =>
                                      void host
                                          .startGame()
                                          .then((m) => m && say(m, 'ok'))
                                          .catch(fail),
                              },
                              icon('play', 13),
                              'Start the game',
                          )
                        : null,
                    h(
                        'button',
                        { type: 'button', class: 'ui-btn', onclick: () => ((lastShip = null), render()) },
                        'Export again',
                    ),
                    h('button', { type: 'button', class: 'ui-btn ghost', onclick: () => close() }, 'Close the wizard'),
                ),
            );
        }
        const install = (): void =>
            void withPack(async (pack) => {
                const where = await host.install(pack);
                if (!where) return;
                lastShip = { where, installed: true };
                projects.session.exported = true;
                projects.session.lastExport = { where, at: Date.now() };
                projects.notify();
                say(`Installed into ${where}.`, 'ok');
            }, 'Building the pack...');
        const saveZip = (): void =>
            void withPack(async (pack) => {
                const where = await host.saveZip(`${pack.meta.id}.zip`, host.zip(pack));
                if (!where) return;
                lastShip = { where, installed: false };
                projects.session.exported = true;
                projects.session.lastExport = { where, at: Date.now() };
                projects.notify();
                say(`Saved ${where}.`, 'ok');
            }, 'Building the pack...');
        return h(
            'div',
            { class: 'cwz-stack' },
            errs.length
                ? h(
                      'div',
                      { class: 'cwz-verdict bad' },
                      sev('error'),
                      `${plural(errs.length, 'problem')} to fix first.`,
                      h('button', { type: 'button', class: 'ui-btn sm', onclick: () => go('check') }, 'Show them'),
                  )
                : null,
            h(
                'dl',
                { class: 'cwz-facts' },
                h('dt', {}, 'Campaign'),
                h('dd', {}, c?.title || '(no title)'),
                h('dt', {}, 'Levels'),
                h('dd', {}, p.levels.map((l) => l.title).join(', ') || 'none'),
                h('dt', {}, 'New Game opens'),
                h('dd', {}, c?.start ?? 'not chosen'),
                h('dt', {}, 'Mod pack'),
                h('dd', {}, `${p.id} (folder mods/${p.id})`),
            ),
            h(
                'div',
                { class: 'cwz-row' },
                host.installAvailable()
                    ? h(
                          'button',
                          {
                              type: 'button',
                              class: 'ui-btn primary',
                              id: 'cwz-install',
                              disabled: !!busy || unchecked || errs.length > 0,
                              onclick: install,
                          },
                          icon('download', 13),
                          isDesktop ? 'Install into the game' : 'Install into a game folder...',
                      )
                    : null,
                h(
                    'button',
                    {
                        type: 'button',
                        class: `ui-btn${host.installAvailable() ? '' : ' primary'}`,
                        id: 'cwz-zip',
                        disabled: !!busy || unchecked || errs.length > 0,
                        onclick: saveZip,
                    },
                    'Save the pack (.zip)',
                ),
            ),
            h(
                'p',
                { class: 'cwz-hint' },
                isDesktop
                    ? "Install writes the pack into the game's mods folder and turns it on. The zip is for sharing."
                    : host.installAvailable()
                      ? 'Install asks for the game folder (the one with drakan.exe), writes the pack into its mods folder and turns it on.'
                      : 'Save the zip to the game’s mods folder and start the game: the title screen offers Enable and restart (or use the Mods menu).',
            ),
            lastShip && !lastShip.installed
                ? h(
                      'div',
                      { class: 'cwz-verdict ok', id: 'cwz-saved' },
                      icon('check-circle', 16),
                      `Saved ${lastShip.where}. Put the zip in the game’s mods folder and start the game: the title screen offers Enable and restart (or use the Mods menu). Then choose ${c?.title || 'your campaign'} on the Choose your game screen.`,
                  )
                : null,
            packLines && !packError
                ? h('ul', { class: 'cwz-packlines' }, ...packLines.map((l) => h('li', {}, l)))
                : null,
        );
    }

    // ---- frame logic ------------------------------------------------------------------------------------------------

    function paintRail(): void {
        const p = project();
        const done = stepsDone(
            p,
            { issues, played: projects.session.played, exported: projects.session.exported },
            p && linkData ? linkPlans(p, linkData) : [],
        );
        rail.replaceChildren(
            ...WIZARD_STEPS.map((s, i) =>
                h(
                    'li',
                    {
                        class: `cwz-step${s.id === step ? ' on' : ''}${done[s.id] ? ' done' : ''}`,
                        'data-step': s.id,
                    },
                    h(
                        'button',
                        {
                            type: 'button',
                            id: `cwz-step-${s.id}`,
                            'aria-current': s.id === step ? 'step' : undefined,
                            onclick: () => go(s.id),
                        },
                        h(
                            'span',
                            { class: 'cwz-dot' },
                            done[s.id] && s.id !== step ? icon('check', 11) : String(i + 1),
                        ),
                        h('span', { class: 'cwz-step-name' }, s.rail),
                    ),
                ),
            ),
        );
    }

    function render(): void {
        if (!shown) return;
        if (!campaign()) step = 'about';
        body.inert = !!busy;
        const s = WIZARD_STEPS.find((x) => x.id === step)!;
        const i = WIZARD_STEPS.indexOf(s);
        const c = campaign();
        titleEl.textContent = c?.title.trim() ? `Make a campaign: ${c.title.trim()}` : 'Make a campaign';
        const active = document.activeElement as HTMLInputElement | null;
        const focusId = active && body.contains(active) ? active.id : '';
        const hadFocus = !!active && panel.contains(active);
        const pos = focusId && 'selectionStart' in active! ? active!.selectionStart : null;
        const scroll = body.querySelector('.cwz-content')?.scrollTop ?? 0;
        paintRail();
        const content =
            step === 'about'
                ? aboutStep()
                : step === 'levels'
                  ? levelsStep()
                  : step === 'exits'
                    ? exitsStep()
                    : step === 'check'
                      ? checkStep()
                      : step === 'play'
                        ? playStep()
                        : shipStep();
        body.replaceChildren(
            h(
                'div',
                { class: 'cwz-content', 'data-step': step },
                h('div', { class: 'cwz-stepno' }, `Step ${i + 1} of ${WIZARD_STEPS.length}`),
                h('h2', { class: 'cwz-step-title', tabindex: -1 }, s.title),
                h('p', { class: 'cwz-intro' }, s.intro),
                content,
            ),
        );
        const sc = body.querySelector('.cwz-content');
        if (sc) sc.scrollTop = scroll;
        backBtn.disabled = i === 0;
        const next = WIZARD_STEPS[i + 1];
        nextBtn.textContent = next ? `Next: ${next.rail}` : 'Finish';
        nextBtn.disabled = !!busy || !campaign();
        backBtn.disabled = i === 0 || !!busy;
        status.textContent = busy;
        if (focusId) {
            const el = body.querySelector<HTMLInputElement>(`#${focusId}`);
            if (el) {
                el.focus({ preventScroll: true });
                if (pos !== null && pos !== undefined && typeof el.setSelectionRange === 'function') {
                    try {
                        el.setSelectionRange(pos, pos);
                    } catch {
                        // inputs of type search/number have no selection
                    }
                }
            }
        }
        // The control that had the focus was replaced: keep the keyboard inside the wizard, on the step heading.
        if (hadFocus && !panel.contains(document.activeElement)) {
            body.querySelector<HTMLElement>('.cwz-step-title')?.focus({ preventScroll: true });
        }
    }

    function move(d: -1 | 1): void {
        const i = WIZARD_STEPS.findIndex((s) => s.id === step);
        const next = WIZARD_STEPS[i + d];
        if (!next) {
            if (d > 0) close();
            return;
        }
        go(next.id);
    }

    function go(id: WizardStepId): void {
        if (busy) return;
        if (!campaign()) id = 'about';
        step = id;
        store.set(STEP_KEY, id);
        if (id !== 'ship') lastShip = lastShip?.installed ? lastShip : null;
        render();
        if (id !== 'about' && id !== 'levels') void refresh();
        body.querySelector('.cwz-content')?.scrollTo({ top: 0 });
    }

    function open(at?: WizardStepId): void {
        if (!shown) {
            const a = document.activeElement as HTMLElement | null;
            previousFocus = a && a !== document.body ? a : lastOutside;
        }
        shown = true;
        root.hidden = false;
        resume.hidden = true;
        store.set(BACK_KEY, '');
        if (!project()) ensureCampaign();
        if (at) step = at;
        if (!campaign()) step = 'about';
        store.set(STEP_KEY, step);
        render();
        void refresh();
        setTimeout(
            () => panel.querySelector<HTMLElement>('#cwz-title, .cwz-content button, .cwz-content input')?.focus(),
            0,
        );
    }

    function close(): void {
        shown = false;
        root.hidden = true;
        resume.hidden = true;
        store.set(BACK_KEY, '');
        // The opener may have been redrawn while the wizard was open: fall back to the start page button or the toolbar.
        if (previousFocus?.isConnected && previousFocus.getClientRects().length > 0) previousFocus.focus();
        else {
            const shown = (el: HTMLElement): boolean =>
                el.getClientRects().length > 0 && !el.closest('[hidden], [inert]');
            [...document.querySelectorAll<HTMLElement>('main button:not(:disabled), #appbar button')]
                .find(shown)
                ?.focus();
        }
    }

    /** Leave the wizard for the editor (a map click): a button in the corner brings it back. */
    function minimise(): void {
        shown = false;
        root.hidden = true;
        resume.hidden = false;
        store.set(BACK_KEY, '1');
    }

    let timer = 0;
    let previousProject = project();
    projects.on(() => {
        const current = project();
        if (current !== previousProject) {
            previousProject = current;
            generation++;
            linkData = null;
            issues = null;
            exitsState = 'none';
            lastShip = null;
            packLines = null;
        }
        if (!shown) return;
        window.clearTimeout(timer);
        timer = window.setTimeout(() => {
            render();
            if (step !== 'about') void refresh();
        }, 150);
    });
    let editTimer = 0;
    projects.onEdit(() => {
        if (!shown || step === 'about' || step === 'levels') return;
        window.clearTimeout(editTimer);
        editTimer = window.setTimeout(() => void refresh(), 400);
    });
    if (store.get(BACK_KEY) === '1') resume.hidden = false;

    registerCommand({
        id: 'campaign.wizard',
        title: 'Make a campaign (step by step)',
        category: 'File',
        icon: 'layers',
        keywords: 'campaign wizard new levels exits title card choose your game export install',
        hint: 'Name a campaign, choose its levels, link the exits, check, play and install it',
        run: () => open(),
    });

    function attachLink(pane: HTMLElement): HTMLElement {
        if (!pane.querySelector('#cwz-link')) {
            const link = h(
                'div',
                { class: 'cwz-panelink', id: 'cwz-link' },
                h('span', {}, 'New to campaigns? The wizard takes you through every step, from the title to the game.'),
                h(
                    'button',
                    { type: 'button', class: 'ui-btn sm primary', id: 'cwz-open', onclick: () => open() },
                    'Make a campaign step by step',
                ),
            );
            // After the header: the pane redraws only its header's and body's children, so the link stays.
            pane.insertBefore(link, pane.children[1] ?? null);
        }
        return pane;
    }

    instance = {
        open,
        close,
        go: (id) => {
            if (!shown) open(id);
            else go(id);
        },
        isOpen: () => shown,
        step: () => step,
        resetDisc: () => {
            generation++;
            exitCache.clear();
            srcCache.clear();
            linkData = null;
            issues = null;
            exitsState = 'none';
            if (shown) void refresh();
        },
        attachLink,
    };
    return instance;
}

/** For the tour: the level copies of the project (exits need at least two). */
export const campaignLevelCount = (): number => projects.get()?.levels.filter((l) => l.copyOf).length ?? 0;

/** For the tour: every level of the campaign leads to the next one. */
export function campaignLinked(): boolean {
    return !!document.querySelector('#cwz-links') && !document.querySelector('#cwz-links .cwz-link:not(.linked)');
}

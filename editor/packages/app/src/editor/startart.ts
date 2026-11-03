import type { TemplateId } from './starttemplates.ts';

/**
 * Preview pictures of the start page templates: small top-down sketches drawn as SVG (no game assets). Colours come
 * from classes in `startpage.css` (ln: accent line, wm: warm marker, dm: faint ground, fl: soft fill), so both themes
 * work. Static strings only.
 */
const ART: Record<TemplateId, string> = {
    'build-campaign':
        '<path class="ln dash" d="M32 30 C80 0 80 100 128 70"/>' +
        '<rect class="fl" x="12" y="14" width="40" height="30" rx="3"/><rect class="ln" x="12" y="14" width="40" height="30" rx="3"/>' +
        '<rect class="fl" x="108" y="56" width="40" height="30" rx="3"/><rect class="ln" x="108" y="56" width="40" height="30" rx="3"/>' +
        '<circle class="wm" cx="32" cy="30" r="4"/><circle class="wm" cx="128" cy="70" r="4"/>',
    // A level seen from above: terrain contours, a few creatures (warm) and the selected one being moved.
    campaign:
        '<path class="dm" d="M10 78 C40 60 60 88 92 70 S140 52 152 64"/><path class="dm" d="M8 40 C34 26 58 48 86 36 S132 18 154 30"/>' +
        '<rect class="fl" x="62" y="38" width="30" height="24" rx="2"/><path class="ln" d="M77 50 h22 M99 50 l-5 -4 M99 50 l-5 4 M77 50 v-20 M77 30 l-4 5 M77 30 l4 5"/>' +
        '<circle class="wm" cx="34" cy="58" r="3.5"/><circle class="wm" cx="122" cy="40" r="3.5"/><circle class="wm" cx="118" cy="76" r="3.5"/><circle class="ln-solid" cx="77" cy="50" r="4"/>',
    // An arena: bounds, spawn points of two teams, pickups in the middle.
    arena:
        '<rect class="ln dash" x="18" y="12" width="124" height="76" rx="6"/><circle class="dm" cx="80" cy="50" r="18"/>' +
        '<path class="team-a" d="M34 26 l6 10 h-12z M34 64 l6 10 h-12z"/><path class="team-b" d="M126 26 l6 10 h-12z M126 64 l6 10 h-12z"/>' +
        '<rect class="wm" x="76" y="46" width="8" height="8" rx="1.5" transform="rotate(45 80 50)"/><rect class="wm" x="57" y="21" width="6" height="6" rx="1"/><rect class="wm" x="97" y="73" width="6" height="6" rx="1"/>',
    // Two level outlines: the original and the copy, with an arrow.
    newlevel:
        '<path class="dm" d="M14 22 h48 v20 h-14 v36 h-34z"/><path class="fl" d="M92 22 h48 v20 h-14 v36 h-34z"/><path class="ln" d="M92 22 h48 v20 h-14 v36 h-34z"/>' +
        '<path class="ln" d="M66 50 h18 M84 50 l-5 -4 M84 50 l-5 4"/><circle class="wm" cx="110" cy="64" r="3"/>',
    // Four plates wired to a counter, which opens a door.
    puzzle:
        '<rect class="wm" x="16" y="22" width="16" height="12" rx="2"/><rect class="wm" x="16" y="66" width="16" height="12" rx="2"/><rect class="fl" x="44" y="22" width="16" height="12" rx="2"/><rect class="fl" x="44" y="66" width="16" height="12" rx="2"/>' +
        '<path class="ln thin" d="M32 28 C70 28 70 50 86 50 M32 72 C70 72 70 50 86 50 M60 28 C76 28 76 50 86 50 M60 72 C76 72 76 50 86 50"/>' +
        '<circle class="ln-solid" cx="92" cy="50" r="7"/><path class="ln" d="M100 50 h16"/><path class="ln" d="M120 28 h22 v44 h-22z M131 28 v44"/>',
    // A camera travelling along a dotted path, three keys.
    cutscene:
        '<path class="ln dash" d="M18 76 C44 30 92 86 140 26"/><circle class="wm" cx="18" cy="76" r="3.5"/><circle class="wm" cx="76" cy="58" r="3.5"/><circle class="wm" cx="140" cy="26" r="3.5"/>' +
        '<rect class="fl" x="62" y="20" width="22" height="15" rx="2"/><rect class="ln" x="62" y="20" width="22" height="15" rx="2"/><path class="ln" d="M84 24 l9 -4 v15 l-9 -4"/>' +
        '<path class="dm" d="M10 90 h140"/>',
    // Two speech bubbles and a subtitle bar.
    dialogue:
        '<path class="fl" d="M18 18 h66 a6 6 0 0 1 6 6 v20 a6 6 0 0 1 -6 6 h-46 l-10 9 v-9 h-10 a6 6 0 0 1 -6 -6 v-20 a6 6 0 0 1 6 -6z"/><path class="ln" d="M18 18 h66 a6 6 0 0 1 6 6 v20 a6 6 0 0 1 -6 6 h-46 l-10 9 v-9 h-10 a6 6 0 0 1 -6 -6 v-20 a6 6 0 0 1 6 -6z"/>' +
        '<path class="dm" d="M24 30 h52 M24 39 h36"/><path class="wm-line" d="M100 36 h40 a5 5 0 0 1 5 5 v12 a5 5 0 0 1 -5 5 h-6 v8 l-9 -8 h-25 a5 5 0 0 1 -5 -5 v-12 a5 5 0 0 1 5 -5z"/>' +
        '<rect class="dm-fill" x="30" y="78" width="100" height="10" rx="2"/>',
    // A parchment with a marker on a route.
    quest:
        '<path class="dm" d="M14 70 C40 50 70 80 96 58 S128 40 146 46"/><path class="ln dash" d="M30 64 C52 56 70 70 96 58"/>' +
        '<path class="wm" d="M96 58 c-9 -11 -10 -15 -10 -20 a10 10 0 0 1 20 0 c0 5 -1 9 -10 20z"/><circle class="bg-dot" cx="96" cy="38" r="3.5"/>' +
        '<rect class="fl" x="18" y="14" width="44" height="30" rx="2"/><path class="dm" d="M24 24 h32 M24 32 h22"/>',
    // A counter with goods and coins.
    vendor:
        '<path class="ln" d="M16 70 h128 M22 70 v16 M138 70 v16"/><path class="dm" d="M26 30 l10 -10 h88 l10 10z"/>' +
        '<rect class="fl" x="34" y="44" width="16" height="26" rx="2"/><path class="ln thin" d="M62 70 l12 -30 l12 30"/><circle class="ln-solid" cx="108" cy="58" r="10"/>' +
        '<circle class="wm" cx="130" cy="62" r="5"/><circle class="wm" cx="124" cy="66" r="5"/><circle class="wm" cx="134" cy="67" r="5"/>',
    // Menu rows, one being dragged to a new place.
    menus:
        '<rect class="fl" x="40" y="12" width="80" height="76" rx="3"/><path class="dm" d="M50 26 h60 M50 42 h60 M50 74 h60"/>' +
        '<rect class="ln" x="56" y="51" width="80" height="13" rx="2"/><path class="wm-line" d="M62 57.5 h40"/><path class="ln thin" d="M144 52 l4 -4 l4 4 M148 48 v18 M144 62 l4 4 l4 -4"/>',
    // An open book.
    story:
        '<path class="fl" d="M80 26 C64 18 40 18 22 24 v54 c18 -6 42 -6 58 2z"/><path class="fl" d="M80 26 C96 18 120 18 138 24 v54 c-18 -6 -42 -6 -58 2z"/>' +
        '<path class="ln" d="M80 26 C64 18 40 18 22 24 v54 c18 -6 42 -6 58 2 c16 -8 40 -8 58 -2 v-54 c-18 -6 -42 -6 -58 2z M80 26 v54"/>' +
        '<path class="dm" d="M32 38 h38 M32 48 h38 M32 58 h26 M90 38 h38 M90 48 h30"/><path class="wm-line" d="M90 58 h38"/>',
    // A memory card with a character stat bar.
    save:
        '<path class="fl" d="M52 14 h44 l12 12 v62 h-56z"/><path class="ln" d="M52 14 h44 l12 12 v62 h-56z"/><path class="dm" d="M60 22 v14 M68 22 v14 M76 22 v14 M84 22 v14"/>' +
        '<rect class="dm-fill" x="60" y="52" width="40" height="5" rx="2"/><rect class="wm" x="60" y="52" width="26" height="5" rx="2"/><rect class="dm-fill" x="60" y="64" width="40" height="5" rx="2"/><rect class="ln-fill" x="60" y="64" width="34" height="5" rx="2"/>',
    // A level of the older game coming in.
    ootf:
        '<path class="dm" d="M14 30 h40 v40 h-40z M22 30 v40 M14 50 h40"/><path class="ln" d="M60 50 h30 M90 50 l-6 -5 M90 50 l-6 5"/>' +
        '<path class="fl" d="M100 30 h46 v40 h-46z"/><path class="ln" d="M100 30 h46 v40 h-46z"/><circle class="wm" cx="123" cy="50" r="4"/>',
};

export function templateArt(id: TemplateId): SVGSVGElement {
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('viewBox', '0 0 160 100');
    svg.setAttribute('aria-hidden', 'true');
    svg.setAttribute('class', 'sp-art');
    svg.innerHTML = ART[id];
    return svg;
}

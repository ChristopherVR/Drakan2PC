// Drakan: subtitles of voice clips a mod pack brings (the converted Order of the Flame scenes).
//
// The engine shows the text of a sound descriptor only for an actor with a speech controller (the player and the
// talking NPC classes); a scene's camera or narrator plays its clips without one, and the PC game the campaign comes
// from has no subtitle text at all. A pack therefore stores the text of each clip as timed pages in the clip's sound
// descriptor (`[[<seconds>]]text...`, settings/Subtitles.h parseClipCues) and the runtime shows them:
//   * trigger: the sound manager's per-instance gain update (0x1e5470) of an instance that has taken an SPU2 voice
//     and whose descriptor text starts with a page mark; each instance (by its handle) starts its pages once;
//   * display: the narration subtitle band (drakan_subtitles, HudSlot::Subtitles) on the SPU2 audio clock, so the
//     text follows the voice and freezes with it;
//   * end: the pages end early when the instance stops (it no longer holds this play's handle with a voice: a skipped
//     scene, a dropped sound), and at every level load and save restore (lifecycle events), which also forget the
//     handles already seen;
//   * the engine's own bar is not given marked text (SetSubtitle 0x4149a0 returns at once for it), so a line spoken
//     by the player is not drawn twice.
// Disc text has no page marks and is untouched. PAL addresses; not installed for other regions.
#pragma once

class PS2Runtime;

namespace drakan_clip_subtitles {
void install(PS2Runtime& runtime);
} // namespace drakan_clip_subtitles

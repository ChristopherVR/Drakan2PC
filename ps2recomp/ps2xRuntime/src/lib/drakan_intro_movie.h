// Drakan port: the Order of the Flame opening as a pre-rendered movie.
//
// The PC game has no intro movie file: the opening ("Psygnosis Presents", the owl, Intro 1 / 2 / 3) is the
// engine-rendered level "OotF 00 - Intro". Rendered live it can stutter and run at the wrong speed on a busy machine,
// so the player can render it once into <exe dir>/movies/ootf_intro.mkv (analysis/scripts/ootf_intro_movie.py drives
// the game in the render mode below) and this module plays that file instead:
//
//   * playback: when the Intro level loads from a New Game and the file exists, the movie covers the output
//     (HudSlot::Movie) with its own sound track mixed into the audio output, while the live level, which does the
//     hand-over to Ruined Village, keeps running unseen with its sound silenced. Esc / Start / Cross skip: the movie
//     fades to black and the live level's exit is fired (its own hand-over is a 44 s timer; scene skips do not end
//     it). The cover stays until the movie is over and the next level is loading, then the game sound returns.
//     Without the file nothing changes (the live level plays).
//   * render mode (env DRAKAN_INTRO_RENDER=<folder>): fixed 1/60 s engine time step, every completed frame of the Intro
//     level piped to ffmpeg, the audio position of each frame logged, the game quits at the level change. See
//     docs/reference/modding.md ("Intro movie").
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace gsr {
class Renderer;
}
namespace spu2 {
class AudioOutput;
}

namespace drakan_intro_movie {

// From drakan_settings_host::attachRenderer / detachRenderer (nullptr detaches).
void attach(gsr::Renderer* renderer);
// From drakan_settings_host::setAudio.
void setAudio(spu2::AudioOutput* audio);

// ---- playback (draw side: the HUD layer in drakan_settings_host.cpp, present thread) ----
struct Frame {
    uint32_t width = 0, height = 0;
    uint64_t serial = 0;
    std::vector<uint8_t> rgba; // tightly packed
};
// True while the movie (or the black cover after it) is on screen.
bool coverActive();
// The picture due now (null = black) and the cover's alpha 0..1 (fades in and out).
std::shared_ptr<const Frame> currentFrame(float& alpha);

// ---- input (EE thread, from the pad read hook) ----
// `pad` is the 32 byte sample about to be handed to the guest (buttons active low at [2], [3]); `escDown` is the Escape
// key. Detects a skip request; once the movie is over or skipped it fires the live level's exit.
void padTick(uint8_t* pad, bool escDown);

// ---- render mode ----
bool renderModeActive();
// Engine time step for the next tick in seconds, or 0 when the engine's own measurement stays (not rendering).
double renderTickSeconds();
void renderModeSetAudio(spu2::AudioOutput* audio); // from setAudio
void renderModeAttach(); // from attach(): subscribes the render mode's level hooks when DRAKAN_INTRO_RENDER is set
// Called by the GS bridge after every completed frame (display commit), on the thread that commits it.
void onCompletedFrame(gsr::Renderer& renderer, uint64_t frameNumber);

} // namespace drakan_intro_movie

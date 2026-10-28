// Drakan: subtitles for the intro / outro narration (docs/reference/subtitles.md). The game has no text for them; the
// port ships SRT files (subtitles/intro_<language>.srt, outro_<language>.srt next to the exe) and draws them with the
// settings overlay's HUD layer.
//   * trigger: hle::ResourceService request observer - the first request (offset 0) of the narration soundtrack (SDB
//   type 0x304, sub 14,
//     id 99 = intro, id 100 = outro, file LEVEL0RO.SDB) arms the track; the cues' time zero is the SPU2 key-on of the
//     voice that plays that stream (found by the address of its first upload, onSpuUpload / onVoiceKeyOn), because
//     request -> key-on varies by 0.1 .. 0.6 s with the load time. If no key-on shows up within 4 s the request time is
//     used.
//   * clock: the number of 48 kHz frames the SPU2 has rendered (spu2::AudioOutput::framesRendered), so the subtitles
//   follow the audio exactly
//     and freeze with it (F1 menu, pause, stall)
//   * cue time 0 = the first sample of the track; DRAKAN_SUB_OFFSET_MS (default 0) shifts the cues later (+) / earlier
//   (-)
//   * the game's own "Show Subtitles" option (guest word 0x4fb740, Options > Audio) also switches the narration
//   subtitles
//   * drakan.ini [subtitles] narration = 1, size = auto | 0.5 .. 3.0; env DRAKAN_SUBTITLES=0|1, DRAKAN_SUB_OFFSET_MS
#pragma once
#include <cstdint>
#include <string>

namespace hle {
struct ResourceId;
}
namespace spu2 {
class AudioOutput;
}
namespace dsettings {
class SettingsOverlay;
}

namespace drakan_subtitles {
// language: drakan.ini [general] language, lower case ("english"); rdram: guest memory (the game's subtitle option is
// read from it).
void init(spu2::AudioOutput* audio, uint8_t* rdram, const std::string& language);
void setEnabled(bool on);
void setSize(float size); // 0 = auto
// hle::ResourceService::RequestObserver
void onResourceRequest(const hle::ResourceId& id, uint32_t offset);
// SPU2 side (SpuTap): every RAM upload (called on the resource worker thread, right after the observer for that
// request) and every voice key-on with the voice's start address (`frames` = SPU2 frames rendered at that moment).
bool wantsSpuEvents(); // cheap: true only while a narration track is waiting for its key-on
void onSpuUpload(uint32_t spuAddr, uint32_t bytes);
void onVoiceKeyOn(uint32_t spuAddr, uint64_t frames);
void stop();
// Registers the draw callback on the overlay (render thread).
void installHudLayer(dsettings::SettingsOverlay& overlay);
} // namespace drakan_subtitles

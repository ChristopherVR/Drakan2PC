// Drakan: subtitles of a mod pack's voice clips - see drakan_clip_subtitles.h.
#include "drakan_clip_subtitles.h"

#include "drakan_clip_subtitles_policy.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "drakan_subtitles.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace drakan_clip_subtitles {
namespace {
constexpr uint32_t kSetGain = 0x1e5470u;     // sound instance gain update, a0 = instance (once per tick while it plays)
constexpr uint32_t kSetSubtitle = 0x4149a0u; // SetSubtitle(subtitle object, text, speaker)
// sound instance: +4 handle (unique per play), +0xc sound id, +0x10 descriptor record, +0x78 SPU2 voice (-1: none yet)
// descriptor record: +0 sound id, +0x34 f32 clip length in seconds, +0x50 text
constexpr uint32_t kInstHandle = 4, kInstId = 0xc, kInstRecord = 0x10, kInstVoice = 0x78;
constexpr uint32_t kRecordId = 0, kRecordSeconds = 0x34, kRecordText = 0x50;
constexpr size_t kMaxText = 0x400; // the engine's text slot

PS2Runtime::RecompiledFunction g_setGain = nullptr, g_setSubtitle = nullptr;
detail::StartedHandles g_started;
bool g_log = false;

bool ok(uint32_t address, uint32_t size) {
    address &= 0x1fffffffu;
    return address >= 0x100000u && address + size <= PS2_RAM_SIZE;
}
uint32_t u32(const uint8_t* rdram, uint32_t address) {
    uint32_t v = 0;
    std::memcpy(&v, rdram + (address & 0x1fffffffu), 4);
    return v;
}

void setGainHook(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t inst = getRegU32(c, 4);
    if (ok(inst, 0x80)) {
        const uint32_t handle = u32(rdram, inst + kInstHandle), record = u32(rdram, inst + kInstRecord);
        if (static_cast<int32_t>(u32(rdram, inst + kInstVoice)) >= 0 && ok(record, 0x58) &&
            u32(rdram, record + kRecordId) == u32(rdram, inst + kInstId) && !g_started.seen(handle)) {
            const uint32_t text = u32(rdram, record + kRecordText);
            if (ok(text, 8)) {
                const char* p = reinterpret_cast<const char*>(rdram + (text & 0x1fffffffu));
                const size_t room = std::min<size_t>(kMaxText, PS2_RAM_SIZE - (text & 0x1fffffffu));
                const size_t n = strnlen(p, room);
                if (dsettings::isClipCueText(p, n)) {
                    float seconds = 0.0f;
                    std::memcpy(&seconds, rdram + ((record + kRecordSeconds) & 0x1fffffffu), 4);
                    const auto cues = detail::clipCues(std::string(p, n), seconds);
                    if (g_log)
                        std::fprintf(stderr, "[subtitles] clip %x (%.2f s): %zu page(s), instance %x handle %x\n",
                                     u32(rdram, inst + kInstId), static_cast<double>(seconds), cues.size(), inst,
                                     handle);
                    // The pages follow this play: they end when the instance no longer holds it with a voice (a
                    // skipped scene stops its sounds; a load drops them).
                    if (!cues.empty())
                        drakan_subtitles::startClip(cues, [rdram, inst, handle]() {
                            return u32(rdram, inst + kInstHandle) == handle &&
                                   static_cast<int32_t>(u32(rdram, inst + kInstVoice)) >= 0;
                        });
                }
            }
            // Text or not, this play has been looked at: the gain update runs every tick.
            g_started.add(handle);
        }
    }
    g_setGain(rdram, c, rt);
}

void setSubtitleHook(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t text = getRegU32(c, 5);
    if (c->pc == kSetSubtitle && ok(text, 8)) {
        const char* p = reinterpret_cast<const char*>(rdram + (text & 0x1fffffffu));
        if (dsettings::isClipCueText(p, strnlen(p, 8))) {
            c->pc = getRegU32(c, 31); // the runtime shows this clip's pages itself
            return;
        }
    }
    g_setSubtitle(rdram, c, rt);
}
} // namespace

void install(PS2Runtime& rt) {
    if (const char* off = std::getenv("DRAKAN_CLIP_SUBTITLES"); off && off[0] == '0')
        return;
    if (!drakan_region::build().identityAddresses || !rt.hasFunction(kSetGain) || !rt.hasFunction(kSetSubtitle))
        return;
    g_log = std::getenv("DRAKAN_SUB_LOG") != nullptr;
    g_setGain = rt.lookupFunction(kSetGain);
    g_setSubtitle = rt.lookupFunction(kSetSubtitle);
    if (!g_setGain || !g_setSubtitle)
        return;
    rt.replaceFunction(kSetGain, &setGainHook);
    rt.replaceFunction(kSetSubtitle, &setSubtitleHook);
    // A level load (level change, New Game) or a save restore starts the sound system over: whatever page is up
    // belongs to a sound that is gone, and the new sounds may be given handles seen before the load.
    const auto reset = [](const drakan_lifecycle::Event& e) {
        drakan_subtitles::stopClip(e.kind == drakan_lifecycle::Kind::BeforeSaveRestore ? "a save is loaded"
                                                                                       : "a level is loaded");
        g_started.clear();
    };
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeLevelLoad, reset);
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeSaveRestore, reset);
}
} // namespace drakan_clip_subtitles
